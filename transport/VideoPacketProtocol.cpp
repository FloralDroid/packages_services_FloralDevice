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

#include "floral/stream/transport/VideoPacketProtocol.h"

#include <limits>

namespace floral::stream::transport {
namespace {

constexpr size_t kMagicOffset = 0;
constexpr size_t kVersionOffset = 4;
constexpr size_t kHeaderSizeOffset = 6;
constexpr size_t kStreamIdOffset = 8;
constexpr size_t kGenerationOffset = 12;
constexpr size_t kSequenceOffset = 16;
constexpr size_t kPresentationTimeOffset = 24;
constexpr size_t kFlagsOffset = 32;
constexpr size_t kCodecOffset = 36;
constexpr size_t kCodedWidthOffset = 40;
constexpr size_t kCodedHeightOffset = 44;
constexpr size_t kLogicalWidthOffset = 48;
constexpr size_t kLogicalHeightOffset = 52;
constexpr size_t kPayloadSizeOffset = 56;
constexpr size_t kDisplayRotationOffset = 60;
constexpr size_t kFrameSubmitTimeOffset = 64;
constexpr size_t kReservedOffset = 72;

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

void WriteUint16(SerializedVideoPacketHeader* output, size_t offset, uint16_t value) {
    (*output)[offset] = static_cast<uint8_t>(value >> 8);
    (*output)[offset + 1] = static_cast<uint8_t>(value);
}

void WriteUint32(SerializedVideoPacketHeader* output, size_t offset, uint32_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        (*output)[offset + index] =
                static_cast<uint8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

void WriteUint64(SerializedVideoPacketHeader* output, size_t offset, uint64_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        (*output)[offset + index] =
                static_cast<uint8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

uint16_t ReadUint16(const SerializedVideoPacketHeader& input, size_t offset) {
    return static_cast<uint16_t>((static_cast<uint16_t>(input[offset]) << 8) | input[offset + 1]);
}

uint32_t ReadUint32(const SerializedVideoPacketHeader& input, size_t offset) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[offset + index];
    }
    return value;
}

uint64_t ReadUint64(const SerializedVideoPacketHeader& input, size_t offset) {
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[offset + index];
    }
    return value;
}

bool ValidateHeader(const VideoPacketHeader& header, std::string* error) {
    if (header.presentation_time_us < 0) {
        return SetError(error, "video packet presentation timestamp must not be negative");
    }
    if (header.codec != VideoCodec::kH264) {
        return SetError(error, "video packet codec is unsupported");
    }
    VideoGeometry geometry;
    geometry.logical_width = header.logical_width;
    geometry.logical_height = header.logical_height;
    geometry.coded_width = header.coded_width;
    geometry.coded_height = header.coded_height;
    geometry.display_rotation = header.display_rotation;
    if (!HasMatchingDisplayAspect(geometry)) {
        return SetError(error, "video packet geometry is invalid or changes the display aspect");
    }
    constexpr uint32_t kKnownFlags = kVideoPacketCodecConfig | kVideoPacketKeyFrame |
                                     kVideoPacketEndOfStream | kVideoPacketDiscontinuity;
    if ((header.flags & ~kKnownFlags) != 0) {
        return SetError(error, "video packet contains unknown flags");
    }
    return true;
}

}  // namespace

bool SerializeVideoPacketHeader(const VideoPacketHeader& header,
                                SerializedVideoPacketHeader* output, std::string* error) {
    if (output == nullptr) {
        return SetError(error, "serialized video packet header output is null");
    }
    if (!ValidateHeader(header, error)) {
        return false;
    }

    output->fill(0);
    WriteUint32(output, kMagicOffset, kVideoPacketMagic);
    WriteUint16(output, kVersionOffset, kVideoPacketVersion);
    WriteUint16(output, kHeaderSizeOffset, static_cast<uint16_t>(kVideoPacketHeaderSize));
    WriteUint32(output, kStreamIdOffset, header.stream_id);
    WriteUint32(output, kGenerationOffset, header.generation);
    WriteUint64(output, kSequenceOffset, header.sequence);
    WriteUint64(output, kPresentationTimeOffset,
                static_cast<uint64_t>(header.presentation_time_us));
    WriteUint32(output, kFlagsOffset, header.flags);
    WriteUint32(output, kCodecOffset, static_cast<uint32_t>(header.codec));
    WriteUint32(output, kCodedWidthOffset, header.coded_width);
    WriteUint32(output, kCodedHeightOffset, header.coded_height);
    WriteUint32(output, kLogicalWidthOffset, header.logical_width);
    WriteUint32(output, kLogicalHeightOffset, header.logical_height);
    WriteUint32(output, kPayloadSizeOffset, header.payload_size);
    WriteUint32(output, kDisplayRotationOffset, static_cast<uint32_t>(header.display_rotation));
    WriteUint64(output, kFrameSubmitTimeOffset, header.frame_submit_time_ns);
    WriteUint64(output, kReservedOffset, 0);
    return true;
}

bool ParseVideoPacketHeader(const SerializedVideoPacketHeader& input, VideoPacketHeader* header,
                            std::string* error) {
    if (header == nullptr) {
        return SetError(error, "parsed video packet header output is null");
    }
    if (ReadUint32(input, kMagicOffset) != kVideoPacketMagic) {
        return SetError(error, "video packet magic does not match");
    }
    if (ReadUint16(input, kVersionOffset) != kVideoPacketVersion) {
        return SetError(error, "video packet version is unsupported");
    }
    if (ReadUint16(input, kHeaderSizeOffset) != kVideoPacketHeaderSize) {
        return SetError(error, "video packet header size does not match");
    }
    if (ReadUint64(input, kReservedOffset) != 0) {
        return SetError(error, "video packet reserved field is not zero");
    }

    VideoPacketHeader parsed;
    parsed.stream_id = ReadUint32(input, kStreamIdOffset);
    parsed.generation = ReadUint32(input, kGenerationOffset);
    parsed.sequence = ReadUint64(input, kSequenceOffset);
    const uint64_t presentationTime = ReadUint64(input, kPresentationTimeOffset);
    if (presentationTime > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return SetError(error, "video packet presentation timestamp is out of range");
    }
    parsed.presentation_time_us = static_cast<int64_t>(presentationTime);
    parsed.flags = ReadUint32(input, kFlagsOffset);
    parsed.codec = static_cast<VideoCodec>(ReadUint32(input, kCodecOffset));
    parsed.coded_width = ReadUint32(input, kCodedWidthOffset);
    parsed.coded_height = ReadUint32(input, kCodedHeightOffset);
    parsed.logical_width = ReadUint32(input, kLogicalWidthOffset);
    parsed.logical_height = ReadUint32(input, kLogicalHeightOffset);
    parsed.payload_size = ReadUint32(input, kPayloadSizeOffset);
    parsed.display_rotation =
            static_cast<DisplayRotation>(ReadUint32(input, kDisplayRotationOffset));
    parsed.frame_submit_time_ns = ReadUint64(input, kFrameSubmitTimeOffset);
    if (!ValidateHeader(parsed, error)) {
        return false;
    }
    *header = parsed;
    return true;
}

}  // namespace floral::stream::transport
