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

#include "floral/device/service/VideoEncoderControlProtocol.h"

#include <limits>

namespace floral::device::service {
namespace {

constexpr size_t kDisplayIdOffset = 0;
constexpr size_t kStreamIdOffset = 8;
constexpr size_t kFieldsOffset = 12;
constexpr size_t kBackendOffset = 16;
constexpr size_t kCodecOffset = 20;
constexpr size_t kCodedWidthOffset = 24;
constexpr size_t kCodedHeightOffset = 28;
constexpr size_t kFrameRateOffset = 32;
constexpr size_t kBitrateOffset = 36;
constexpr size_t kIFrameIntervalOffset = 40;
constexpr size_t kReservedOffset = 44;

constexpr size_t kResultOffset = 0;
constexpr size_t kAppliedFieldsOffset = 4;
constexpr size_t kGenerationOffset = 8;
constexpr size_t kPendingOffset = 12;
constexpr size_t kBitrateResponseOffset = 16;
constexpr size_t kFrameRateResponseOffset = 20;
constexpr size_t kCodedWidthResponseOffset = 24;
constexpr size_t kCodedHeightResponseOffset = 28;

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

void WriteUint32(uint8_t* output, uint32_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        output[index] = static_cast<uint8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

uint32_t ReadUint32(const uint8_t* input) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[index];
    }
    return value;
}

uint64_t ReadUint64(const uint8_t* input) {
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[index];
    }
    return value;
}

bool FieldSet(uint32_t fields, uint32_t field) {
    return (fields & field) != 0;
}

bool IsKnownResult(VideoEncoderConfigResult result) {
    switch (result) {
        case VideoEncoderConfigResult::kApplied:
        case VideoEncoderConfigResult::kPending:
        case VideoEncoderConfigResult::kInvalidConfig:
        case VideoEncoderConfigResult::kUnsupported:
        case VideoEncoderConfigResult::kNoActiveStream:
            return true;
    }
    return false;
}

}  // namespace

bool ParseVideoEncoderConfigRequest(const std::vector<uint8_t>& payload,
                                    VideoEncoderConfigUpdate* update, std::string* error) {
    if (update == nullptr) {
        return SetError(error, "video encoder update output is null");
    }
    if (payload.size() != kVideoEncoderConfigRequestSize) {
        return SetError(error, "video encoder request must be exactly 48 bytes");
    }

    const uint8_t* input = payload.data();
    const uint32_t fields = ReadUint32(input + kFieldsOffset);
    if (ReadUint32(input + kReservedOffset) != 0 || fields == 0 ||
        (fields & ~kKnownVideoEncoderConfigFields) != 0) {
        return SetError(error, "video encoder request contains invalid flags or reserved data");
    }
    if (ReadUint32(input + kStreamIdOffset) == 0 || ReadUint64(input + kDisplayIdOffset) == 0) {
        return SetError(error, "video encoder request requires a display and stream id");
    }

    const uint32_t backend = ReadUint32(input + kBackendOffset);
    const uint32_t codecId = ReadUint32(input + kCodecOffset);
    const uint32_t codedWidth = ReadUint32(input + kCodedWidthOffset);
    const uint32_t codedHeight = ReadUint32(input + kCodedHeightOffset);
    const uint32_t frameRate = ReadUint32(input + kFrameRateOffset);
    const uint32_t bitrate = ReadUint32(input + kBitrateOffset);
    const uint32_t iFrameInterval = ReadUint32(input + kIFrameIntervalOffset);

    if ((!FieldSet(fields, kVideoEncoderConfigBackend) && backend != 0) ||
        (!FieldSet(fields, kVideoEncoderConfigCodec) && codecId != 0) ||
        (!FieldSet(fields, kVideoEncoderConfigResolution) &&
         (codedWidth != 0 || codedHeight != 0)) ||
        (!FieldSet(fields, kVideoEncoderConfigFrameRate) && frameRate != 0) ||
        (!FieldSet(fields, kVideoEncoderConfigBitrate) && bitrate != 0) ||
        (!FieldSet(fields, kVideoEncoderConfigIFrameInterval) && iFrameInterval != 0)) {
        return SetError(error, "video encoder fields not selected by the update mask");
    }
    if (FieldSet(fields, kVideoEncoderConfigResolution) && (codedWidth == 0 || codedHeight == 0)) {
        return SetError(error, "video encoder resolution must be non-zero");
    }
    if (FieldSet(fields, kVideoEncoderConfigFrameRate) && frameRate == 0) {
        return SetError(error, "video encoder frame rate must be non-zero");
    }
    if (FieldSet(fields, kVideoEncoderConfigBitrate) && bitrate == 0) {
        return SetError(error, "video encoder bitrate must be non-zero");
    }
    if (FieldSet(fields, kVideoEncoderConfigIFrameInterval) && iFrameInterval == 0) {
        return SetError(error, "video encoder I-frame interval must be non-zero");
    }

    VideoEncoderConfigUpdate parsed;
    parsed.display_id = ReadUint64(input + kDisplayIdOffset);
    parsed.stream_id = ReadUint32(input + kStreamIdOffset);
    parsed.fields = fields;
    parsed.backend = static_cast<floral::stream::codec::EncoderBackendType>(backend);
    parsed.codec_id = codecId;
    parsed.coded_width = codedWidth;
    parsed.coded_height = codedHeight;
    parsed.frame_rate = frameRate;
    parsed.bitrate_bps = bitrate;
    parsed.i_frame_interval_seconds = iFrameInterval;
    *update = parsed;
    return true;
}

bool SerializeVideoEncoderConfigResponse(const VideoEncoderRuntimeState& state,
                                         VideoEncoderConfigResult result, uint32_t request_id,
                                         control::ControlResponse* response, std::string* error) {
    if (response == nullptr) {
        return SetError(error, "video encoder response output is null");
    }
    if (request_id == 0 || !IsKnownResult(result) ||
        state.generation > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return SetError(error, "video encoder response contains invalid state");
    }

    response->payload.assign(kVideoEncoderConfigResponseSize, 0);
    WriteUint32(response->payload.data() + kResultOffset, static_cast<uint32_t>(result));
    WriteUint32(response->payload.data() + kAppliedFieldsOffset, kKnownVideoEncoderConfigFields);
    WriteUint32(response->payload.data() + kGenerationOffset, state.generation);
    WriteUint32(response->payload.data() + kPendingOffset, state.pending ? 1 : 0);
    WriteUint32(response->payload.data() + kBitrateResponseOffset, state.encoder.bitrate_bps);
    WriteUint32(response->payload.data() + kFrameRateResponseOffset, state.encoder.frame_rate);
    WriteUint32(response->payload.data() + kCodedWidthResponseOffset, state.geometry.coded_width);
    WriteUint32(response->payload.data() + kCodedHeightResponseOffset, state.geometry.coded_height);
    response->header.command_id =
            static_cast<uint16_t>(control::ControlCommandId::kSetVideoEncoderConfig);
    response->header.route_kind = control::MakeFhc1RouteKind(control::ControlPacketKind::kResponse);
    response->header.request_id = request_id;
    response->header.payload_size = static_cast<uint32_t>(response->payload.size());
    // FHC1's three-second authority lease currently applies to topology state;
    // video tuning remains in effect until explicitly changed by the host.
    response->refreshes_authority_lease = false;
    return true;
}

}  // namespace floral::device::service
