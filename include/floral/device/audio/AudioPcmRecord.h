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

namespace floral::device::audio {

constexpr uint32_t kAudioPcmRecordMagic = 0x46415031;  // "FAP1"
constexpr uint16_t kAudioPcmRecordVersion = 1;
constexpr size_t kAudioPcmRecordHeaderSize = 48;
constexpr uint32_t kAudioPcmSampleRate = 48'000;
constexpr uint32_t kAudioPcmChannelCount = 2;
constexpr uint32_t kAudioPcmSampleFormatS16Le = 1;
constexpr uint32_t kAudioPcmFramesPerRecord = 240;
constexpr size_t kAudioPcmSamplesPerRecord = kAudioPcmFramesPerRecord * kAudioPcmChannelCount;
constexpr size_t kAudioPcmPayloadSize = kAudioPcmSamplesPerRecord * sizeof(int16_t);
constexpr size_t kAudioPcmRecordSize = kAudioPcmRecordHeaderSize + kAudioPcmPayloadSize;
constexpr size_t kAudioPcmFmqPacketCapacity = 8;

enum AudioPcmRecordFlag : uint32_t {
    kAudioPcmRecordStreamStart = 1U << 0,
    kAudioPcmRecordDiscontinuity = 1U << 1,
};

struct AudioPcmRecord {
    uint32_t stream_generation = 0;
    uint32_t flags = 0;
    uint64_t first_frame_position = 0;
    uint64_t presentation_timestamp_ns = 0;
    uint32_t frame_count = kAudioPcmFramesPerRecord;
    std::array<int16_t, kAudioPcmSamplesPerRecord> samples{};
};

using SerializedAudioPcmRecord = std::array<int8_t, kAudioPcmRecordSize>;

bool SerializeAudioPcmRecord(const AudioPcmRecord& record, SerializedAudioPcmRecord* output,
                             std::string* error);
bool ParseAudioPcmRecord(const SerializedAudioPcmRecord& input, AudioPcmRecord* record,
                         std::string* error);

}  // namespace floral::device::audio
