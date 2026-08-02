/*
 * Copyright 2026 FloralDroid
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "floral-device"

#include "floral/device/service/VideoFrameConsumerBackend.h"

#include <android-base/logging.h>

#include <poll.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace floral::device::service {
namespace stream = ::floral::stream;
namespace session = ::floral::stream::session;
namespace transport = ::floral::stream::transport;
namespace {

using AidlFrameStatus = aidl::floral::device::display::FrameStatus;

uint32_t NextGeneration(uint32_t generation) {
    constexpr uint32_t kMaximumGeneration =
            static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
    return generation >= kMaximumGeneration ? 1 : generation + 1;
}

bool WaitForAcquireFence(android::base::unique_fd* fence, std::string* error) {
    if (fence == nullptr || !fence->ok()) {
        return true;
    }
    pollfd descriptor{};
    descriptor.fd = fence->get();
    descriptor.events = POLLIN;
    int result = 0;
    do {
        result = poll(&descriptor, 1, 5'000);
    } while (result < 0 && errno == EINTR);
    fence->reset();
    if (result > 0) {
        return true;
    }
    if (result == 0) {
        if (error != nullptr) {
            *error = "frame acquire fence did not signal within 5000 ms";
        }
        return false;
    }
    if (error != nullptr) {
        *error = std::string("polling frame acquire fence failed: ") + std::strerror(errno);
    }
    return false;
}

stream::VideoGeometry GeometryForCodedResolution(const stream::VideoGeometry& current,
                                                 uint32_t codedWidth, uint32_t codedHeight) {
    stream::VideoGeometry geometry = current;
    geometry.coded_width = codedWidth;
    geometry.coded_height = codedHeight;
    return geometry;
}

bool IsResolutionWithinBounds(uint32_t width, uint32_t height) {
    return width >= 320 && height >= 320 && width <= 7680 && height <= 4320;
}

class VideoFrameConsumerBackend final : public display::FrameConsumerBackend,
                                        public VideoEncoderControl {
  public:
    explicit VideoFrameConsumerBackend(VideoFrameConsumerBackendConfig config)
        : config_(std::move(config)),
          desired_session_config_(config_.session_config),
          next_reconnect_(std::chrono::steady_clock::now()) {}

    display::DisplayConsumerStreamState GetStreamState(uint64_t displayId) override {
        std::lock_guard lock(mutex_);
        MaintainSessionLocked();

        display::DisplayConsumerStreamState state;
        state.display_id = displayId;
        state.generation = generation_;
        state.accepting_frames = displayId == config_.display_id && session_ != nullptr &&
                                 !reset_required_ && session_->transport_connected();
        return state;
    }

    AidlFrameStatus RegisterBuffer(display::ImportedBufferRegistration registration) override {
        std::lock_guard lock(mutex_);
        const AidlFrameStatus stateStatus =
                ValidateRequestLocked(registration.display_id, registration.generation);
        if (stateStatus != AidlFrameStatus::ACCEPTED) {
            return stateStatus;
        }

        const auto existing = buffers_.find(registration.buffer_id);
        if (existing != buffers_.end()) {
            session_->UnregisterBuffer(existing->second);
            buffers_.erase(existing);
        }

        uint64_t encoderBufferId = 0;
        std::string error;
        if (!session_->RegisterBuffer(registration.buffer.get(), &encoderBufferId, &error)) {
            LOG(ERROR) << "rejecting display buffer " << registration.buffer_id << ": " << error;
            return AidlFrameStatus::UNSUPPORTED_BUFFER;
        }
        buffers_.emplace(registration.buffer_id, encoderBufferId);
        return AidlFrameStatus::ACCEPTED;
    }

    display::ImportedFrameResult SubmitFrame(display::ImportedFrameRequest request) override {
        std::lock_guard lock(mutex_);
        display::ImportedFrameResult result;
        result.status = ValidateRequestLocked(request.display_id, request.generation);
        if (result.status != AidlFrameStatus::ACCEPTED) {
            return result;
        }

        const auto buffer = buffers_.find(request.buffer_id);
        if (buffer == buffers_.end()) {
            result.status = AidlFrameStatus::BUFFER_UNKNOWN;
            return result;
        }

        std::string error;
        if (ShouldDropFrameLocked(request.presentation_time_nanos)) {
            // A dropped frame still owns the source buffer until its acquire
            // fence signals. Returning an empty release fence is then safe.
            if (!WaitForAcquireFence(&request.acquire_fence, &error)) {
                LOG(ERROR) << "failed to retire dropped display frame " << request.source_sequence
                           << ": " << error;
                result.status = AidlFrameStatus::INTERNAL_ERROR;
                return result;
            }
            result.status = AidlFrameStatus::ACCEPTED;
            DrainOutputLocked();
            return result;
        }
        const uint64_t frameSubmitTimeNanos =
                request.frame_submit_time_nanos > 0
                        ? static_cast<uint64_t>(request.frame_submit_time_nanos)
                        : 0;
        stream::codec::FrameCopyResult copied = session_->SubmitFrame(
                buffer->second, std::move(request.acquire_fence), request.presentation_time_nanos,
                frameSubmitTimeNanos, &error);
        if (!copied.success) {
            LOG(ERROR) << "failed to submit display frame " << request.source_sequence << ": "
                       << error;
            result.status = AidlFrameStatus::INTERNAL_ERROR;
            return result;
        }

        result.status = AidlFrameStatus::ACCEPTED;
        result.release_fence = std::move(copied.release_fence);
        DrainOutputLocked();
        return result;
    }

    bool ApplyVideoEncoderConfig(const VideoEncoderConfigUpdate& update,
                                 VideoEncoderRuntimeState* state, VideoEncoderConfigResult* result,
                                 std::string* error) override {
        std::lock_guard lock(mutex_);
        if (state == nullptr || result == nullptr) {
            if (error != nullptr) {
                *error = "video encoder runtime state output is null";
            }
            return false;
        }
        if (update.display_id != config_.display_id ||
            update.stream_id != desired_session_config_.stream_id) {
            FillRuntimeStateLocked(state);
            *result = VideoEncoderConfigResult::kNoActiveStream;
            return true;
        }

        if ((update.fields & ~kKnownVideoEncoderConfigFields) != 0 || update.fields == 0) {
            FillRuntimeStateLocked(state);
            *result = VideoEncoderConfigResult::kInvalidConfig;
            return true;
        }
        if ((update.fields & kVideoEncoderConfigBitrate) != 0 &&
            (update.bitrate_bps < 100'000 || update.bitrate_bps > 100'000'000)) {
            FillRuntimeStateLocked(state);
            *result = VideoEncoderConfigResult::kInvalidConfig;
            return true;
        }
        if ((update.fields & kVideoEncoderConfigFrameRate) != 0 &&
            (update.frame_rate == 0 || update.frame_rate > 60)) {
            FillRuntimeStateLocked(state);
            *result = VideoEncoderConfigResult::kInvalidConfig;
            return true;
        }
        if ((update.fields & kVideoEncoderConfigResolution) != 0 &&
            !IsResolutionWithinBounds(update.coded_width, update.coded_height)) {
            FillRuntimeStateLocked(state);
            *result = VideoEncoderConfigResult::kInvalidConfig;
            return true;
        }
        if ((update.fields & kVideoEncoderConfigIFrameInterval) != 0 &&
            (update.i_frame_interval_seconds == 0 || update.i_frame_interval_seconds > 60)) {
            FillRuntimeStateLocked(state);
            *result = VideoEncoderConfigResult::kInvalidConfig;
            return true;
        }
        if ((update.fields & kVideoEncoderConfigCodec) != 0 && update.codec_id != 1) {
            FillRuntimeStateLocked(state);
            *result = VideoEncoderConfigResult::kUnsupported;
            return true;
        }
        if ((update.fields & kVideoEncoderConfigBackend) != 0 &&
            update.backend != stream::codec::EncoderBackendType::kMediaCodecSoftware &&
            update.backend != stream::codec::EncoderBackendType::kFfmpegVaapi) {
            FillRuntimeStateLocked(state);
            *result = VideoEncoderConfigResult::kUnsupported;
            return true;
        }

        session::VideoStreamSessionConfig candidate = desired_session_config_;
        const uint32_t oldBitrate = candidate.encoder.bitrate_bps;
        bool structuralChange = false;
        if ((update.fields & kVideoEncoderConfigBitrate) != 0) {
            candidate.encoder.bitrate_bps = update.bitrate_bps;
        }
        if ((update.fields & kVideoEncoderConfigFrameRate) != 0) {
            structuralChange =
                    structuralChange || candidate.encoder.frame_rate != update.frame_rate;
            candidate.encoder.frame_rate = update.frame_rate;
        }
        if ((update.fields & kVideoEncoderConfigResolution) != 0) {
            structuralChange = structuralChange ||
                               candidate.geometry.coded_width != update.coded_width ||
                               candidate.geometry.coded_height != update.coded_height;
            candidate.geometry = GeometryForCodedResolution(candidate.geometry, update.coded_width,
                                                            update.coded_height);
            if (!stream::HasMatchingDisplayAspect(candidate.geometry)) {
                FillRuntimeStateLocked(state);
                *result = VideoEncoderConfigResult::kInvalidConfig;
                return true;
            }
            candidate.encoder.width = update.coded_width;
            candidate.encoder.height = update.coded_height;
        }
        if ((update.fields & kVideoEncoderConfigIFrameInterval) != 0) {
            structuralChange = structuralChange || candidate.encoder.i_frame_interval_seconds !=
                                                           update.i_frame_interval_seconds;
            candidate.encoder.i_frame_interval_seconds = update.i_frame_interval_seconds;
        }
        if ((update.fields & kVideoEncoderConfigBackend) != 0) {
            structuralChange = structuralChange || candidate.encoder.backend != update.backend;
            candidate.encoder.backend = update.backend;
        }
        desired_session_config_ = candidate;
        if (structuralChange) {
            if (session_ != nullptr) {
                MarkSessionForResetLocked("video encoder configuration changed");
            }
            FillRuntimeStateLocked(state);
            *result = VideoEncoderConfigResult::kPending;
            return true;
        }

        if ((update.fields & kVideoEncoderConfigBitrate) != 0 &&
            candidate.encoder.bitrate_bps != oldBitrate) {
            if (session_ != nullptr && !reset_required_ && session_->transport_connected()) {
                if (!session_->SetBitrate(candidate.encoder.bitrate_bps, error)) {
                    desired_session_config_.encoder.bitrate_bps = oldBitrate;
                    MarkSessionForResetLocked(error != nullptr && !error->empty()
                                                      ? *error
                                                      : "runtime bitrate update failed");
                    return false;
                }
                FillRuntimeStateLocked(state);
                *result = VideoEncoderConfigResult::kApplied;
                return true;
            }
            FillRuntimeStateLocked(state);
            *result = VideoEncoderConfigResult::kPending;
            return true;
        }

        FillRuntimeStateLocked(state);
        *result = state->pending ? VideoEncoderConfigResult::kPending
                                 : VideoEncoderConfigResult::kApplied;
        return true;
    }

  private:
    AidlFrameStatus ValidateRequestLocked(uint64_t displayId, uint32_t generation) {
        if (displayId != config_.display_id) {
            return AidlFrameStatus::NO_ACTIVE_STREAM;
        }
        if (generation != generation_) {
            return AidlFrameStatus::STALE_GENERATION;
        }
        if (session_ == nullptr || reset_required_ || !session_->transport_connected()) {
            MarkSessionForResetLocked("host video socket disconnected");
            return AidlFrameStatus::NO_ACTIVE_STREAM;
        }
        return AidlFrameStatus::ACCEPTED;
    }

    void MaintainSessionLocked() {
        if (session_ != nullptr && !session_->transport_connected()) {
            MarkSessionForResetLocked("host video socket disconnected");
        }
        if (reset_required_) {
            session_.reset();
            buffers_.clear();
            reset_required_ = false;
            next_reconnect_ = std::chrono::steady_clock::now() + config_.reconnect_interval;
        }
        if (session_ != nullptr || std::chrono::steady_clock::now() < next_reconnect_) {
            return;
        }

        next_reconnect_ = std::chrono::steady_clock::now() + config_.reconnect_interval;
        std::string error;
        std::unique_ptr<transport::HostVideoSink> sink = transport::HostVideoSink::Connect(
                config_.video_socket_path, config_.sink_config, &error);
        if (sink == nullptr) {
            LogConnectionFailureLocked(error);
            return;
        }

        session::VideoStreamSessionConfig sessionConfig = desired_session_config_;
        const uint32_t candidateGeneration = NextGeneration(generation_);
        sessionConfig.generation = candidateGeneration;
        sessionConfig.initial_discontinuity = generation_ != 0;
        std::unique_ptr<session::VideoStreamSession> session =
                session::VideoStreamSession::Create(sessionConfig, std::move(sink), &error);
        if (session == nullptr) {
            LogConnectionFailureLocked(error);
            return;
        }

        generation_ = candidateGeneration;
        session_ = std::move(session);
        buffers_.clear();
        next_frame_presentation_time_nanos_ = 0;
        last_connection_error_.clear();
        const stream::VideoGeometry& geometry = sessionConfig.geometry;
        LOG(INFO) << "video stream generation " << generation_ << " active: logical "
                  << geometry.logical_width << "x" << geometry.logical_height << ", coded "
                  << geometry.coded_width << "x" << geometry.coded_height << ", display rotation "
                  << static_cast<uint32_t>(geometry.display_rotation) << " degrees, "
                  << session_->encoder_config().frame_rate << " via " << session_->codec_name();
    }

    void DrainOutputLocked() {
        const size_t packetLimit = std::max<size_t>(config_.max_output_packets_per_frame, 1);
        for (size_t packet = 0; packet < packetLimit; ++packet) {
            std::string error;
            const session::VideoStreamDrainResult drain = session_->DrainOutput(0, &error);
            if (!drain.success) {
                MarkSessionForResetLocked(error);
                return;
            }
            if (!drain.made_progress || drain.end_of_stream) {
                return;
            }
        }
    }

    void MarkSessionForResetLocked(const std::string& error) {
        if (session_ == nullptr || reset_required_) {
            return;
        }
        reset_required_ = true;
        LOG(WARNING) << "video stream generation " << generation_ << " became inactive: " << error;
    }

    bool ShouldDropFrameLocked(int64_t presentationTimeNanos) {
        if (presentationTimeNanos <= 0 || desired_session_config_.encoder.frame_rate == 0) {
            return false;
        }
        const uint64_t timestamp = static_cast<uint64_t>(presentationTimeNanos);
        const uint64_t frameInterval =
                1'000'000'000ULL / desired_session_config_.encoder.frame_rate;
        if (next_frame_presentation_time_nanos_ == 0 ||
            timestamp + frameInterval < next_frame_presentation_time_nanos_) {
            next_frame_presentation_time_nanos_ = timestamp + frameInterval;
            return false;
        }
        if (timestamp < next_frame_presentation_time_nanos_) {
            return true;
        }
        const uint64_t intervals =
                (timestamp - next_frame_presentation_time_nanos_) / frameInterval + 1;
        if (intervals >
            (std::numeric_limits<uint64_t>::max() - next_frame_presentation_time_nanos_) /
                    frameInterval) {
            next_frame_presentation_time_nanos_ = timestamp + frameInterval;
        } else {
            next_frame_presentation_time_nanos_ += intervals * frameInterval;
        }
        return false;
    }

    void FillRuntimeStateLocked(VideoEncoderRuntimeState* state) const {
        state->generation = generation_;
        state->pending = reset_required_ || session_ == nullptr || !session_->transport_connected();
        state->encoder = desired_session_config_.encoder;
        state->geometry = desired_session_config_.geometry;
        if (session_ != nullptr && !reset_required_ && session_->transport_connected()) {
            state->encoder = session_->encoder_config();
        }
    }

    void LogConnectionFailureLocked(const std::string& error) {
        if (error == last_connection_error_) {
            return;
        }
        last_connection_error_ = error;
        LOG(INFO) << "video stream is waiting for host socket " << config_.video_socket_path << ": "
                  << error;
    }

    // Socket/display identity is immutable; the desired encoder configuration
    // is replaced atomically under mutex_ before a new generation is created.
    const VideoFrameConsumerBackendConfig config_;
    session::VideoStreamSessionConfig desired_session_config_;
    std::mutex mutex_;
    std::unique_ptr<session::VideoStreamSession> session_;
    std::unordered_map<uint64_t, uint64_t> buffers_;
    uint32_t generation_ = 0;
    bool reset_required_ = false;
    uint64_t next_frame_presentation_time_nanos_ = 0;

    // Reconnect pacing keeps the HWC state poll from producing a tight loop.
    std::chrono::steady_clock::time_point next_reconnect_;
    std::string last_connection_error_;
};

}  // namespace

std::shared_ptr<display::FrameConsumerBackend> CreateVideoFrameConsumerBackend(
        VideoFrameConsumerBackendConfig config,
        std::shared_ptr<VideoEncoderControl>* encoderControl) {
    if (encoderControl != nullptr) {
        encoderControl->reset();
    }
    if (config.video_socket_path.empty() ||
        config.reconnect_interval <= std::chrono::milliseconds::zero()) {
        LOG(ERROR) << "video frame consumer backend configuration is invalid";
        return nullptr;
    }
    std::shared_ptr<VideoFrameConsumerBackend> backend =
            std::make_shared<VideoFrameConsumerBackend>(std::move(config));
    if (encoderControl != nullptr) {
        *encoderControl = backend;
    }
    return backend;
}

}  // namespace floral::device::service
