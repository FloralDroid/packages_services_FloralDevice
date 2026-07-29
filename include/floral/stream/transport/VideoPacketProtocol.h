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

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace floral::stream::transport {

constexpr uint32_t kVideoPacketMagic = 0x46535632;  // "FSV2"
constexpr uint16_t kVideoPacketVersion = 2;
constexpr size_t kVideoPacketHeaderSize = 80;

enum class VideoCodec : uint32_t {
    kH264 = 1,
};

enum VideoPacketFlag : uint32_t {
    kVideoPacketCodecConfig = 1u << 0,
    kVideoPacketKeyFrame = 1u << 1,
    kVideoPacketEndOfStream = 1u << 2,
    kVideoPacketDiscontinuity = 1u << 3,
};

struct VideoPacketHeader {
    uint32_t stream_id = 0;
    uint32_t generation = 0;
    uint64_t sequence = 0;
    int64_t presentation_time_us = 0;
    uint32_t flags = 0;
    VideoCodec codec = VideoCodec::kH264;
    uint32_t coded_width = 0;
    uint32_t coded_height = 0;
    uint32_t logical_width = 0;
    uint32_t logical_height = 0;
    DisplayRotation display_rotation = DisplayRotation::k0;
    uint32_t payload_size = 0;
    uint64_t frame_submit_time_ns = 0;
};

struct HostVideoPacket {
    VideoPacketHeader header;
    std::vector<uint8_t> payload;
};

using SerializedVideoPacketHeader = std::array<uint8_t, kVideoPacketHeaderSize>;

bool SerializeVideoPacketHeader(const VideoPacketHeader& header,
                                SerializedVideoPacketHeader* output, std::string* error);
bool ParseVideoPacketHeader(const SerializedVideoPacketHeader& input, VideoPacketHeader* header,
                            std::string* error);

}  // namespace floral::stream::transport
