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

#include "floral/stream/audio/AudioPacketProtocol.h"

namespace floral::stream::audio {
namespace {

constexpr size_t kMagicOffset = 0;
constexpr size_t kVersionOffset = 4;
constexpr size_t kHeaderSizeOffset = 6;
constexpr size_t kStreamIdOffset = 8;
constexpr size_t kGenerationOffset = 12;
constexpr size_t kSequenceOffset = 16;
constexpr size_t kPresentationTimestampOffset = 24;
constexpr size_t kSampleRateOffset = 32;
constexpr size_t kChannelCountOffset = 36;
constexpr size_t kCodecOffset = 38;
constexpr size_t kFrameCountOffset = 40;
constexpr size_t kPayloadSizeOffset = 44;
constexpr size_t kFlagsOffset = 48;
constexpr size_t kReservedOffset = 52;
constexpr size_t kFirstFramePositionOffset = 56;

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

void WriteUint16(SerializedAudioPacketHeader* output, size_t offset, uint16_t value) {
    (*output)[offset] = static_cast<uint8_t>(value >> 8);
    (*output)[offset + 1] = static_cast<uint8_t>(value);
}

void WriteUint32(SerializedAudioPacketHeader* output, size_t offset, uint32_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        (*output)[offset + index] =
                static_cast<uint8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

void WriteUint64(SerializedAudioPacketHeader* output, size_t offset, uint64_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        (*output)[offset + index] =
                static_cast<uint8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

uint16_t ReadUint16(const SerializedAudioPacketHeader& input, size_t offset) {
    return static_cast<uint16_t>((static_cast<uint16_t>(input[offset]) << 8) | input[offset + 1]);
}

uint32_t ReadUint32(const SerializedAudioPacketHeader& input, size_t offset) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[offset + index];
    }
    return value;
}

uint64_t ReadUint64(const SerializedAudioPacketHeader& input, size_t offset) {
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[offset + index];
    }
    return value;
}

bool ValidateHeader(const AudioPacketHeader& header, std::string* error) {
    constexpr uint32_t kKnownFlags =
            kAudioPacketStreamStart | kAudioPacketDiscontinuity | kAudioPacketStreamEnd;
    if (header.stream_id == 0 || header.generation == 0 || header.sequence == 0) {
        return SetError(error, "audio packet stream identity is invalid");
    }
    if (header.sample_rate != 48'000 || header.channel_count != 2 ||
        header.codec != AudioCodec::kOpus || header.frame_count != 240 ||
        header.payload_size == 0 || header.payload_size > 1275) {
        return SetError(error, "audio packet format is unsupported");
    }
    if ((header.flags & ~kKnownFlags) != 0) {
        return SetError(error, "audio packet contains unknown flags");
    }
    return true;
}

}  // namespace

bool SerializeAudioPacketHeader(const AudioPacketHeader& header,
                                SerializedAudioPacketHeader* output, std::string* error) {
    if (output == nullptr) {
        return SetError(error, "serialized audio packet header output is null");
    }
    if (!ValidateHeader(header, error)) {
        return false;
    }

    output->fill(0);
    WriteUint32(output, kMagicOffset, kAudioPacketMagic);
    WriteUint16(output, kVersionOffset, kAudioPacketVersion);
    WriteUint16(output, kHeaderSizeOffset, kAudioPacketHeaderSize);
    WriteUint32(output, kStreamIdOffset, header.stream_id);
    WriteUint32(output, kGenerationOffset, header.generation);
    WriteUint64(output, kSequenceOffset, header.sequence);
    WriteUint64(output, kPresentationTimestampOffset, header.presentation_timestamp_ns);
    WriteUint32(output, kSampleRateOffset, header.sample_rate);
    WriteUint16(output, kChannelCountOffset, header.channel_count);
    WriteUint16(output, kCodecOffset, static_cast<uint16_t>(header.codec));
    WriteUint32(output, kFrameCountOffset, header.frame_count);
    WriteUint32(output, kPayloadSizeOffset, header.payload_size);
    WriteUint32(output, kFlagsOffset, header.flags);
    WriteUint32(output, kReservedOffset, 0);
    WriteUint64(output, kFirstFramePositionOffset, header.first_frame_position);
    return true;
}

bool ParseAudioPacketHeader(const SerializedAudioPacketHeader& input, AudioPacketHeader* header,
                            std::string* error) {
    if (header == nullptr) {
        return SetError(error, "parsed audio packet header output is null");
    }
    if (ReadUint32(input, kMagicOffset) != kAudioPacketMagic ||
        ReadUint16(input, kVersionOffset) != kAudioPacketVersion ||
        ReadUint16(input, kHeaderSizeOffset) != kAudioPacketHeaderSize ||
        ReadUint32(input, kReservedOffset) != 0) {
        return SetError(error, "audio packet framing does not match");
    }

    AudioPacketHeader parsed;
    parsed.stream_id = ReadUint32(input, kStreamIdOffset);
    parsed.generation = ReadUint32(input, kGenerationOffset);
    parsed.sequence = ReadUint64(input, kSequenceOffset);
    parsed.presentation_timestamp_ns = ReadUint64(input, kPresentationTimestampOffset);
    parsed.sample_rate = ReadUint32(input, kSampleRateOffset);
    parsed.channel_count = ReadUint16(input, kChannelCountOffset);
    parsed.codec = static_cast<AudioCodec>(ReadUint16(input, kCodecOffset));
    parsed.frame_count = ReadUint32(input, kFrameCountOffset);
    parsed.payload_size = ReadUint32(input, kPayloadSizeOffset);
    parsed.flags = ReadUint32(input, kFlagsOffset);
    parsed.first_frame_position = ReadUint64(input, kFirstFramePositionOffset);
    if (!ValidateHeader(parsed, error)) {
        return false;
    }
    *header = parsed;
    return true;
}

}  // namespace floral::stream::audio
