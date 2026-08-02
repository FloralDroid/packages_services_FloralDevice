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

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace floral::stream::audio {

constexpr uint32_t kAudioPacketMagic = 0x46534131;  // "FSA1"
constexpr uint16_t kAudioPacketVersion = 1;
constexpr size_t kAudioPacketHeaderSize = 64;

enum class AudioCodec : uint16_t {
    kOpus = 1,
    kPcmS16Le = 2,
};

enum AudioPacketFlag : uint32_t {
    kAudioPacketStreamStart = 1U << 0,
    kAudioPacketDiscontinuity = 1U << 1,
    kAudioPacketStreamEnd = 1U << 2,
};

struct AudioPacketHeader {
    uint32_t stream_id = 0;
    uint32_t generation = 0;
    uint64_t sequence = 0;
    uint64_t presentation_timestamp_ns = 0;
    uint32_t sample_rate = 48'000;
    uint16_t channel_count = 2;
    AudioCodec codec = AudioCodec::kOpus;
    uint32_t frame_count = 240;
    uint32_t payload_size = 0;
    uint32_t flags = 0;
    uint64_t first_frame_position = 0;
};

using SerializedAudioPacketHeader = std::array<uint8_t, kAudioPacketHeaderSize>;

struct HostAudioPacket {
    AudioPacketHeader header;
    std::vector<uint8_t> payload;
};

bool SerializeAudioPacketHeader(const AudioPacketHeader& header,
                                SerializedAudioPacketHeader* output, std::string* error);
bool ParseAudioPacketHeader(const SerializedAudioPacketHeader& input, AudioPacketHeader* header,
                            std::string* error);

}  // namespace floral::stream::audio
