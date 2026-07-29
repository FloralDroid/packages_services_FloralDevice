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

#define LOG_TAG "floral-stream"

#include "floral/stream/service/VideoFrameConsumerBackend.h"

#include <android-base/logging.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace floral::stream::service {
namespace {

using AidlFrameStatus = aidl::floral::stream::display::FrameStatus;

uint32_t NextGeneration(uint32_t generation) {
    constexpr uint32_t kMaximumGeneration =
            static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
    return generation >= kMaximumGeneration ? 1 : generation + 1;
}

class VideoFrameConsumerBackend final : public display::FrameConsumerBackend {
  public:
    explicit VideoFrameConsumerBackend(VideoFrameConsumerBackendConfig config)
        : config_(std::move(config)), next_reconnect_(std::chrono::steady_clock::now()) {}

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
        const uint64_t frameSubmitTimeNanos =
                request.frame_submit_time_nanos > 0
                        ? static_cast<uint64_t>(request.frame_submit_time_nanos)
                        : 0;
        codec::FrameCopyResult copied = session_->SubmitFrame(
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

        session::VideoStreamSessionConfig sessionConfig = config_.session_config;
        const uint32_t candidateGeneration = NextGeneration(generation_);
        sessionConfig.generation = candidateGeneration;
        std::unique_ptr<session::VideoStreamSession> session =
                session::VideoStreamSession::Create(sessionConfig, std::move(sink), &error);
        if (session == nullptr) {
            LogConnectionFailureLocked(error);
            return;
        }

        generation_ = candidateGeneration;
        session_ = std::move(session);
        buffers_.clear();
        last_connection_error_.clear();
        const VideoGeometry& geometry = sessionConfig.geometry;
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

    void LogConnectionFailureLocked(const std::string& error) {
        if (error == last_connection_error_) {
            return;
        }
        last_connection_error_ = error;
        LOG(INFO) << "video stream is waiting for host socket " << config_.video_socket_path << ": "
                  << error;
    }

    // Immutable runtime configuration and serialized session state.
    const VideoFrameConsumerBackendConfig config_;
    std::mutex mutex_;
    std::unique_ptr<session::VideoStreamSession> session_;
    std::unordered_map<uint64_t, uint64_t> buffers_;
    uint32_t generation_ = 0;
    bool reset_required_ = false;

    // Reconnect pacing keeps the HWC state poll from producing a tight loop.
    std::chrono::steady_clock::time_point next_reconnect_;
    std::string last_connection_error_;
};

}  // namespace

std::shared_ptr<display::FrameConsumerBackend> CreateVideoFrameConsumerBackend(
        VideoFrameConsumerBackendConfig config) {
    if (config.video_socket_path.empty() ||
        config.reconnect_interval <= std::chrono::milliseconds::zero()) {
        LOG(ERROR) << "video frame consumer backend configuration is invalid";
        return nullptr;
    }
    return std::make_shared<VideoFrameConsumerBackend>(std::move(config));
}

}  // namespace floral::stream::service
