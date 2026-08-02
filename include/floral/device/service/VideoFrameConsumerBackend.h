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

#include "floral/device/display/FrameConsumerService.h"
#include "floral/device/service/VideoEncoderControl.h"
#include "floral/stream/session/VideoStreamSession.h"
#include "floral/stream/transport/HostVideoSink.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace floral::device::service {

struct VideoFrameConsumerBackendConfig {
    uint64_t display_id = 1;
    std::string video_socket_path;
    floral::stream::session::VideoStreamSessionConfig session_config;
    floral::stream::transport::HostVideoSinkConfig sink_config;
    std::chrono::milliseconds reconnect_interval{1'000};
    size_t max_output_packets_per_frame = 32;
};

// Creates a reconnecting single-display backend. The returned object also
// implements VideoEncoderControl for FHC1 runtime configuration updates.
std::shared_ptr<display::FrameConsumerBackend> CreateVideoFrameConsumerBackend(
        VideoFrameConsumerBackendConfig config,
        std::shared_ptr<VideoEncoderControl>* encoder_control = nullptr);

}  // namespace floral::device::service
