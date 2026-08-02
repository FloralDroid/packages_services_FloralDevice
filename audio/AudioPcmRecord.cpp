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

#include "floral/device/audio/AudioPcmRecord.h"

#include <algorithm>

namespace floral::device::audio {
namespace {

constexpr size_t kMagicOffset = 0;
constexpr size_t kVersionOffset = 4;
constexpr size_t kHeaderSizeOffset = 6;
constexpr size_t kGenerationOffset = 8;
constexpr size_t kFlagsOffset = 12;
constexpr size_t kFirstFramePositionOffset = 16;
constexpr size_t kPresentationTimestampOffset = 24;
constexpr size_t kFrameCountOffset = 32;
constexpr size_t kPayloadSizeOffset = 36;
constexpr size_t kReservedOffset = 40;

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

void WriteUint16(SerializedAudioPcmRecord* output, size_t offset, uint16_t value) {
    (*output)[offset] = static_cast<int8_t>(value >> 8);
    (*output)[offset + 1] = static_cast<int8_t>(value);
}

void WriteUint32(SerializedAudioPcmRecord* output, size_t offset, uint32_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        (*output)[offset + index] = static_cast<int8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

void WriteUint64(SerializedAudioPcmRecord* output, size_t offset, uint64_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        (*output)[offset + index] = static_cast<int8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

uint8_t ByteAt(const SerializedAudioPcmRecord& input, size_t offset) {
    return static_cast<uint8_t>(input[offset]);
}

uint16_t ReadUint16(const SerializedAudioPcmRecord& input, size_t offset) {
    return static_cast<uint16_t>((static_cast<uint16_t>(ByteAt(input, offset)) << 8) |
                                 ByteAt(input, offset + 1));
}

uint32_t ReadUint32(const SerializedAudioPcmRecord& input, size_t offset) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | ByteAt(input, offset + index);
    }
    return value;
}

uint64_t ReadUint64(const SerializedAudioPcmRecord& input, size_t offset) {
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | ByteAt(input, offset + index);
    }
    return value;
}

}  // namespace

bool SerializeAudioPcmRecord(const AudioPcmRecord& record, SerializedAudioPcmRecord* output,
                             std::string* error) {
    constexpr uint32_t kKnownFlags = kAudioPcmRecordStreamStart | kAudioPcmRecordDiscontinuity;
    if (output == nullptr) {
        return SetError(error, "serialized PCM record output is null");
    }
    if (record.stream_generation == 0 || record.frame_count != kAudioPcmFramesPerRecord ||
        (record.flags & ~kKnownFlags) != 0) {
        return SetError(error, "PCM record metadata is invalid");
    }

    output->fill(0);
    WriteUint32(output, kMagicOffset, kAudioPcmRecordMagic);
    WriteUint16(output, kVersionOffset, kAudioPcmRecordVersion);
    WriteUint16(output, kHeaderSizeOffset, kAudioPcmRecordHeaderSize);
    WriteUint32(output, kGenerationOffset, record.stream_generation);
    WriteUint32(output, kFlagsOffset, record.flags);
    WriteUint64(output, kFirstFramePositionOffset, record.first_frame_position);
    WriteUint64(output, kPresentationTimestampOffset, record.presentation_timestamp_ns);
    WriteUint32(output, kFrameCountOffset, record.frame_count);
    WriteUint32(output, kPayloadSizeOffset, kAudioPcmPayloadSize);
    WriteUint64(output, kReservedOffset, 0);
    for (size_t index = 0; index < record.samples.size(); ++index) {
        const uint16_t sample = static_cast<uint16_t>(record.samples[index]);
        (*output)[kAudioPcmRecordHeaderSize + index * 2] = static_cast<int8_t>(sample);
        (*output)[kAudioPcmRecordHeaderSize + index * 2 + 1] = static_cast<int8_t>(sample >> 8);
    }
    return true;
}

bool ParseAudioPcmRecord(const SerializedAudioPcmRecord& input, AudioPcmRecord* record,
                         std::string* error) {
    if (record == nullptr) {
        return SetError(error, "parsed PCM record output is null");
    }
    if (ReadUint32(input, kMagicOffset) != kAudioPcmRecordMagic ||
        ReadUint16(input, kVersionOffset) != kAudioPcmRecordVersion ||
        ReadUint16(input, kHeaderSizeOffset) != kAudioPcmRecordHeaderSize ||
        ReadUint32(input, kPayloadSizeOffset) != kAudioPcmPayloadSize ||
        ReadUint64(input, kReservedOffset) != 0) {
        return SetError(error, "PCM record framing does not match");
    }

    AudioPcmRecord parsed;
    parsed.stream_generation = ReadUint32(input, kGenerationOffset);
    parsed.flags = ReadUint32(input, kFlagsOffset);
    parsed.first_frame_position = ReadUint64(input, kFirstFramePositionOffset);
    parsed.presentation_timestamp_ns = ReadUint64(input, kPresentationTimestampOffset);
    parsed.frame_count = ReadUint32(input, kFrameCountOffset);
    constexpr uint32_t kKnownFlags = kAudioPcmRecordStreamStart | kAudioPcmRecordDiscontinuity;
    if (parsed.stream_generation == 0 || parsed.frame_count != kAudioPcmFramesPerRecord ||
        (parsed.flags & ~kKnownFlags) != 0) {
        return SetError(error, "PCM record metadata is invalid");
    }
    for (size_t index = 0; index < parsed.samples.size(); ++index) {
        const uint16_t sample =
                static_cast<uint16_t>(ByteAt(input, kAudioPcmRecordHeaderSize + index * 2)) |
                (static_cast<uint16_t>(ByteAt(input, kAudioPcmRecordHeaderSize + index * 2 + 1))
                 << 8);
        parsed.samples[index] = static_cast<int16_t>(sample);
    }
    *record = parsed;
    return true;
}

}  // namespace floral::device::audio
