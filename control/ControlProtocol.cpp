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

#include "floral/device/control/ControlProtocol.h"

namespace floral::device::control {
namespace {

constexpr size_t kMagicOffset = 0;
constexpr size_t kVersionOffset = 4;
constexpr size_t kHeaderSizeOffset = 6;
constexpr size_t kCommandIdOffset = 8;
constexpr size_t kRouteKindOffset = 10;
constexpr size_t kRequestIdOffset = 12;
constexpr size_t kPayloadSizeOffset = 16;
constexpr size_t kReservedOffset = 20;

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

void WriteUint16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value >> 8);
    output[1] = static_cast<uint8_t>(value);
}

void WriteUint32(uint8_t* output, uint32_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        output[index] = static_cast<uint8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

uint16_t ReadUint16(const uint8_t* input) {
    return static_cast<uint16_t>((static_cast<uint16_t>(input[0]) << 8) | input[1]);
}

uint32_t ReadUint32(const uint8_t* input) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[index];
    }
    return value;
}

bool IsValidRouteKind(uint16_t route_kind) {
    if ((route_kind & kControlPlaneMask) != kFhc1ControlPlane ||
        (route_kind & kControlPacketFlagsMask) != 0) {
        return false;
    }
    switch (route_kind & kControlPacketKindMask) {
        case static_cast<uint16_t>(ControlPacketKind::kRequest) << 8:
        case static_cast<uint16_t>(ControlPacketKind::kResponse) << 8:
        case static_cast<uint16_t>(ControlPacketKind::kEvent) << 8:
        case static_cast<uint16_t>(ControlPacketKind::kErrorResponse) << 8:
            return true;
        default:
            return false;
    }
}

}  // namespace

bool SerializeControlPacketHeader(const ControlPacketHeader& header,
                                  SerializedControlPacketHeader* output, std::string* error) {
    if (output == nullptr) {
        return SetError(error, "serialized control header output is null");
    }
    if (!IsValidRouteKind(header.route_kind)) {
        return SetError(error, "control header contains an unsupported route or kind");
    }
    if (header.payload_size > kMaximumControlPayloadSize) {
        return SetError(error, "control payload exceeds the protocol limit");
    }

    output->fill(0);
    WriteUint32(output->data() + kMagicOffset, kControlPacketMagic);
    WriteUint16(output->data() + kVersionOffset, kControlPacketVersion);
    WriteUint16(output->data() + kHeaderSizeOffset,
                static_cast<uint16_t>(kControlPacketHeaderSize));
    WriteUint16(output->data() + kCommandIdOffset, header.command_id);
    WriteUint16(output->data() + kRouteKindOffset, header.route_kind);
    WriteUint32(output->data() + kRequestIdOffset, header.request_id);
    WriteUint32(output->data() + kPayloadSizeOffset, header.payload_size);
    WriteUint32(output->data() + kReservedOffset, 0);
    return true;
}

bool ParseControlPacketHeader(const SerializedControlPacketHeader& input,
                              ControlPacketHeader* header, std::string* error) {
    if (header == nullptr) {
        return SetError(error, "parsed control header output is null");
    }
    if (ReadUint32(input.data() + kMagicOffset) != kControlPacketMagic) {
        return SetError(error, "control packet magic does not match");
    }
    if (ReadUint16(input.data() + kVersionOffset) != kControlPacketVersion) {
        return SetError(error, "control packet version is unsupported");
    }
    if (ReadUint16(input.data() + kHeaderSizeOffset) != kControlPacketHeaderSize) {
        return SetError(error, "control packet header size does not match");
    }
    if (ReadUint32(input.data() + kReservedOffset) != 0) {
        return SetError(error, "control packet reserved field is not zero");
    }

    ControlPacketHeader parsed;
    parsed.command_id = ReadUint16(input.data() + kCommandIdOffset);
    parsed.route_kind = ReadUint16(input.data() + kRouteKindOffset);
    parsed.request_id = ReadUint32(input.data() + kRequestIdOffset);
    parsed.payload_size = ReadUint32(input.data() + kPayloadSizeOffset);
    if (!IsValidRouteKind(parsed.route_kind)) {
        return SetError(error, "control header contains an unsupported route or kind");
    }
    if (parsed.payload_size > kMaximumControlPayloadSize) {
        return SetError(error, "control payload exceeds the protocol limit");
    }
    *header = parsed;
    return true;
}

bool SerializeControlErrorResponse(uint32_t request_id, uint16_t failed_command_id,
                                   ControlError error_code, ControlResponse* response,
                                   std::string* error) {
    if (response == nullptr) {
        return SetError(error, "control error response output is null");
    }

    response->payload.assign(8, 0);
    WriteUint32(response->payload.data(), static_cast<uint32_t>(error_code));
    WriteUint16(response->payload.data() + 4, failed_command_id);
    response->header.command_id = static_cast<uint16_t>(ControlCommandId::kGenericError);
    response->header.route_kind = MakeFhc1RouteKind(ControlPacketKind::kErrorResponse);
    response->header.request_id = request_id;
    response->header.payload_size = static_cast<uint32_t>(response->payload.size());
    response->refreshes_authority_lease = false;
    return true;
}

}  // namespace floral::device::control
