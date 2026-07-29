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

#include <cstdint>
#include <string>
#include <vector>

namespace floral::stream::codec {

inline constexpr uint32_t kEncodedPacketFlagKeyFrame = 1U;
inline constexpr uint32_t kEncodedPacketFlagCodecConfig = 1U << 1;
inline constexpr uint32_t kEncodedPacketFlagEndOfStream = 1U << 2;

enum class EncoderBackendType {
    kMediaCodecSoftware,
    kFfmpegVaapi,
};

struct EncoderConfig {
    EncoderBackendType backend = EncoderBackendType::kMediaCodecSoftware;
    std::string mime = "video/avc";
    std::string va_device_path = "/dev/dri/renderD128";
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t bitrate_bps = 8'000'000;
    uint32_t frame_rate = 60;
    uint32_t i_frame_interval_seconds = 1;
};

struct EncodedPacket {
    std::vector<uint8_t> data;
    int64_t presentation_time_us = 0;
    uint32_t flags = 0;
};

enum class DequeueStatus {
    kPacket,
    kEndOfStream,
    kFormatChanged,
    kTryAgain,
    kError,
};

struct DequeueResult {
    DequeueStatus status = DequeueStatus::kTryAgain;
    EncodedPacket packet;
    int32_t error_code = 0;
};

}  // namespace floral::stream::codec
