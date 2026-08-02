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

#include "floral/device/service/AudioEncoderControlProtocol.h"

namespace floral::device::service {
namespace {

constexpr size_t kStreamIdOffset = 0;
constexpr size_t kFieldsOffset = 4;
constexpr size_t kBitrateOffset = 8;
constexpr size_t kReservedOffset = 12;

constexpr size_t kResultOffset = 0;
constexpr size_t kAppliedFieldsOffset = 4;
constexpr size_t kGenerationOffset = 8;
constexpr size_t kBitrateResponseOffset = 12;
constexpr size_t kCodecOffset = 16;

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

bool IsKnownResult(AudioEncoderConfigResult result) {
    switch (result) {
        case AudioEncoderConfigResult::kApplied:
        case AudioEncoderConfigResult::kInvalidConfig:
        case AudioEncoderConfigResult::kUnsupported:
        case AudioEncoderConfigResult::kUnknownStream:
            return true;
    }
    return false;
}

}  // namespace

bool ParseAudioEncoderConfigRequest(const std::vector<uint8_t>& payload,
                                    AudioEncoderConfigUpdate* update, std::string* error) {
    if (update == nullptr) {
        return SetError(error, "audio encoder update output is null");
    }
    if (payload.size() != kAudioEncoderConfigRequestSize) {
        return SetError(error, "audio encoder request must be exactly 16 bytes");
    }

    const uint8_t* input = payload.data();
    const uint32_t streamId = ReadUint32(input + kStreamIdOffset);
    const uint32_t fields = ReadUint32(input + kFieldsOffset);
    const uint32_t bitrate = ReadUint32(input + kBitrateOffset);
    if (streamId == 0 || fields != kAudioEncoderConfigBitrate || bitrate == 0 ||
        ReadUint32(input + kReservedOffset) != 0) {
        return SetError(error, "audio encoder request contains invalid fields");
    }

    AudioEncoderConfigUpdate parsed;
    parsed.stream_id = streamId;
    parsed.fields = fields;
    parsed.bitrate_bps = bitrate;
    *update = parsed;
    return true;
}

bool SerializeAudioEncoderConfigResponse(const AudioEncoderRuntimeState& state,
                                         AudioEncoderConfigResult result, uint32_t request_id,
                                         control::ControlResponse* response, std::string* error) {
    if (response == nullptr) {
        return SetError(error, "audio encoder response output is null");
    }
    if (request_id == 0 || !IsKnownResult(result) || state.bitrate_bps == 0 ||
        state.codec_id == 0) {
        return SetError(error, "audio encoder response contains invalid state");
    }

    response->payload.assign(kAudioEncoderConfigResponseSize, 0);
    WriteUint32(response->payload.data() + kResultOffset, static_cast<uint32_t>(result));
    WriteUint32(response->payload.data() + kAppliedFieldsOffset, kKnownAudioEncoderConfigFields);
    WriteUint32(response->payload.data() + kGenerationOffset, state.generation);
    WriteUint32(response->payload.data() + kBitrateResponseOffset, state.bitrate_bps);
    WriteUint32(response->payload.data() + kCodecOffset, state.codec_id);
    response->header.command_id =
            static_cast<uint16_t>(control::ControlCommandId::kSetAudioEncoderConfig);
    response->header.route_kind = control::MakeFhc1RouteKind(control::ControlPacketKind::kResponse);
    response->header.request_id = request_id;
    response->header.payload_size = static_cast<uint32_t>(response->payload.size());
    response->refreshes_authority_lease = false;
    return true;
}

}  // namespace floral::device::service
