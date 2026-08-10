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

#pragma once

#include "floral/stream/VideoGeometry.h"
#include "floral/stream/codec/EncoderSession.h"
#include "floral/stream/session/VideoOutputController.h"
#include "floral/stream/transport/HostVideoSink.h"

#include <android-base/unique_fd.h>
#include <android/hardware_buffer.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace floral::stream::session {

struct VideoStreamSessionConfig {
    codec::EncoderConfig encoder;
    VideoGeometry geometry;
    uint32_t stream_id = 0;
    uint32_t generation = 0;
    bool initial_discontinuity = false;
    size_t max_pending_frame_timestamps = 512;
};

struct VideoStreamDrainResult {
    bool success = true;
    bool made_progress = false;
    bool end_of_stream = false;
};

// Owns one encoder-to-host stream. The system service supplies the worker and
// calls this object linearly so codec state, timestamps, and transport recovery
// remain ordered without an additional MediaCodec thread.
class VideoStreamSession {
  public:
    static std::unique_ptr<VideoStreamSession> Create(
            const VideoStreamSessionConfig& config, std::unique_ptr<transport::HostVideoSink> sink,
            std::string* error);

    ~VideoStreamSession();

    VideoStreamSession(const VideoStreamSession&) = delete;
    VideoStreamSession& operator=(const VideoStreamSession&) = delete;

    bool RegisterBuffer(AHardwareBuffer* buffer, uint64_t* outBufferId, std::string* error);
    void UnregisterBuffer(uint64_t bufferId);
    codec::FrameCopyResult SubmitFrame(uint64_t bufferId, android::base::unique_fd acquireFence,
                                       int64_t presentationTimeNanos, std::string* error);
    codec::FrameCopyResult SubmitFrame(uint64_t bufferId, android::base::unique_fd acquireFence,
                                       int64_t presentationTimeNanos, uint64_t frameSubmitTimeNanos,
                                       std::string* error);
    codec::FrameCopyResult RepeatLastFrame(int64_t presentationTimeNanos, std::string* error);

    bool SetBitrate(uint32_t bitrateBps, std::string* error);
    bool SignalEndOfInputStream(std::string* error);
    VideoStreamDrainResult DrainOutput(int64_t timeoutUs, std::string* error);
    codec::StaticFrameRepeatMode static_frame_repeat_mode() const;

    const codec::EncoderConfig& encoder_config() const;
    const std::string& codec_name() const;
    VideoOutputState output_state() const;
    const VideoOutputStats& output_stats() const;
    transport::HostVideoSinkStats transport_stats() const;
    bool uses_native_fences() const;
    bool transport_connected() const;
    size_t registered_buffer_count() const;

  private:
    VideoStreamSession(std::unique_ptr<codec::EncoderSession> encoder,
                       std::unique_ptr<transport::HostVideoSink> sink,
                       VideoOutputConfig outputConfig);

    std::unique_ptr<codec::EncoderSession> encoder_;
    std::unique_ptr<transport::HostVideoSink> sink_;
    VideoOutputController output_controller_;
};

}  // namespace floral::stream::session
