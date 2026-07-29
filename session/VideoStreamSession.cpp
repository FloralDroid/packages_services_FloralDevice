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

#include "floral/stream/session/VideoStreamSession.h"

#include <time.h>

#include <utility>

namespace floral::stream::session {
namespace {

uint64_t GetMonotonicTimeNs() {
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 || now.tv_nsec < 0) {
        return 0;
    }
    return static_cast<uint64_t>(now.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(now.tv_nsec);
}

}  // namespace

VideoStreamSession::VideoStreamSession(std::unique_ptr<codec::EncoderSession> encoder,
                                       std::unique_ptr<transport::HostVideoSink> sink,
                                       VideoOutputConfig outputConfig)
    : encoder_(std::move(encoder)),
      sink_(std::move(sink)),
      output_controller_(std::move(outputConfig)) {}

std::unique_ptr<VideoStreamSession> VideoStreamSession::Create(
        const VideoStreamSessionConfig& config, std::unique_ptr<transport::HostVideoSink> sink,
        std::string* error) {
    if (sink == nullptr) {
        if (error != nullptr) {
            *error = "video stream session requires a host sink";
        }
        return nullptr;
    }
    std::unique_ptr<codec::EncoderSession> encoder =
            codec::EncoderSession::Create(config.encoder, config.geometry, error);
    if (encoder == nullptr) {
        return nullptr;
    }

    VideoOutputConfig outputConfig;
    outputConfig.stream_id = config.stream_id;
    outputConfig.generation = config.generation;
    outputConfig.geometry = config.geometry;
    outputConfig.max_pending_frame_timestamps = config.max_pending_frame_timestamps;
    return std::unique_ptr<VideoStreamSession>(
            new VideoStreamSession(std::move(encoder), std::move(sink), std::move(outputConfig)));
}

VideoStreamSession::~VideoStreamSession() = default;

bool VideoStreamSession::RegisterBuffer(AHardwareBuffer* buffer, uint64_t* outBufferId,
                                        std::string* error) {
    return encoder_->RegisterBuffer(buffer, outBufferId, error);
}

void VideoStreamSession::UnregisterBuffer(uint64_t bufferId) {
    encoder_->UnregisterBuffer(bufferId);
}

codec::FrameCopyResult VideoStreamSession::SubmitFrame(uint64_t bufferId,
                                                       android::base::unique_fd acquireFence,
                                                       int64_t presentationTimeNanos,
                                                       std::string* error) {
    return SubmitFrame(bufferId, std::move(acquireFence), presentationTimeNanos,
                       GetMonotonicTimeNs(), error);
}

codec::FrameCopyResult VideoStreamSession::SubmitFrame(uint64_t bufferId,
                                                       android::base::unique_fd acquireFence,
                                                       int64_t presentationTimeNanos,
                                                       uint64_t frameSubmitTimeNanos,
                                                       std::string* error) {
    codec::FrameCopyResult result =
            encoder_->SubmitFrame(bufferId, std::move(acquireFence), presentationTimeNanos, error);
    if (result.success && frameSubmitTimeNanos != 0) {
        output_controller_.RecordFrameSubmission(presentationTimeNanos / 1'000,
                                                 frameSubmitTimeNanos);
    }
    return result;
}

bool VideoStreamSession::SetBitrate(uint32_t bitrateBps, std::string* error) {
    return encoder_->SetBitrate(bitrateBps, error);
}

bool VideoStreamSession::SignalEndOfInputStream(std::string* error) {
    return encoder_->SignalEndOfInputStream(error);
}

VideoStreamDrainResult VideoStreamSession::DrainOutput(int64_t timeoutUs, std::string* error) {
    VideoOutputProcessResult output =
            output_controller_.Process(encoder_->DequeueOutput(timeoutUs), sink_.get(), error);
    VideoStreamDrainResult result;
    result.success = output.success;
    result.made_progress = output.made_progress;
    result.end_of_stream = output.end_of_stream;
    if (output.success && output.request_key_frame && !encoder_->RequestKeyFrame(error)) {
        result.success = false;
    }
    return result;
}

const codec::EncoderConfig& VideoStreamSession::encoder_config() const {
    return encoder_->config();
}

const std::string& VideoStreamSession::codec_name() const {
    return encoder_->codec_name();
}

VideoOutputState VideoStreamSession::output_state() const {
    return output_controller_.state();
}

const VideoOutputStats& VideoStreamSession::output_stats() const {
    return output_controller_.stats();
}

transport::HostVideoSinkStats VideoStreamSession::transport_stats() const {
    return sink_->GetStats();
}

bool VideoStreamSession::uses_native_fences() const {
    return encoder_->uses_native_fences();
}

bool VideoStreamSession::transport_connected() const {
    return sink_->connected();
}

size_t VideoStreamSession::registered_buffer_count() const {
    return encoder_->registered_buffer_count();
}

}  // namespace floral::stream::session
