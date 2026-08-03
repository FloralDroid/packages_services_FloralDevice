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

#include "floral/device/operation/OperationProtocol.h"

namespace floral::device::operation {
namespace {

constexpr size_t kMagicOffset = 0;
constexpr size_t kVersionOffset = 4;
constexpr size_t kHeaderSizeOffset = 6;
constexpr size_t kOperationCodeOffset = 8;
constexpr size_t kRouteKindOffset = 10;
constexpr size_t kRequestIdOffset = 12;
constexpr size_t kPayloadSizeOffset = 16;
constexpr size_t kReservedOffset = 20;
constexpr size_t kErrorPayloadSize = 8;

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

bool IsKnownPacketKind(uint16_t routeKind) {
    if ((routeKind & kOperationPlaneMask) != kFdo1OperationPlane ||
        (routeKind & kOperationPacketFlagsMask) != 0) {
        return false;
    }
    switch (routeKind & kOperationPacketKindMask) {
        case static_cast<uint16_t>(OperationPacketKind::kRequest) << 8:
        case static_cast<uint16_t>(OperationPacketKind::kResponse) << 8:
        case static_cast<uint16_t>(OperationPacketKind::kEvent) << 8:
        case static_cast<uint16_t>(OperationPacketKind::kErrorResponse) << 8:
            return true;
        default:
            return false;
    }
}

bool HasValidRequestId(uint16_t routeKind, uint32_t requestId) {
    const bool isEvent = IsFdo1RouteKind(routeKind, OperationPacketKind::kEvent);
    return isEvent ? requestId == 0 : requestId != 0;
}

bool IsKnownError(OperationError error) {
    switch (error) {
        case OperationError::kUnsupportedMessage:
        case OperationError::kMalformedMessage:
        case OperationError::kInvalidRequestId:
        case OperationError::kInternalError:
            return true;
    }
    return false;
}

}  // namespace

bool SerializeOperationPacketHeader(const OperationPacketHeader& header,
                                    SerializedOperationPacketHeader* output, std::string* error) {
    if (output == nullptr) {
        return SetError(error, "serialized operation header output is null");
    }
    if (!IsKnownPacketKind(header.route_kind)) {
        return SetError(error, "operation header contains an unsupported route or kind");
    }
    if (!HasValidRequestId(header.route_kind, header.request_id)) {
        return SetError(error, "operation header request id does not match its packet kind");
    }
    if (header.payload_size > kMaximumOperationPayloadSize) {
        return SetError(error, "operation payload exceeds the protocol limit");
    }

    output->fill(0);
    WriteUint32(output->data() + kMagicOffset, kOperationPacketMagic);
    WriteUint16(output->data() + kVersionOffset, kOperationPacketVersion);
    WriteUint16(output->data() + kHeaderSizeOffset,
                static_cast<uint16_t>(kOperationPacketHeaderSize));
    WriteUint16(output->data() + kOperationCodeOffset, header.operation_code);
    WriteUint16(output->data() + kRouteKindOffset, header.route_kind);
    WriteUint32(output->data() + kRequestIdOffset, header.request_id);
    WriteUint32(output->data() + kPayloadSizeOffset, header.payload_size);
    WriteUint32(output->data() + kReservedOffset, 0);
    return true;
}

bool ParseOperationPacketHeader(const SerializedOperationPacketHeader& input,
                                OperationPacketHeader* header, std::string* error) {
    if (header == nullptr) {
        return SetError(error, "parsed operation header output is null");
    }
    if (ReadUint32(input.data() + kMagicOffset) != kOperationPacketMagic) {
        return SetError(error, "operation packet magic does not match");
    }
    if (ReadUint16(input.data() + kVersionOffset) != kOperationPacketVersion) {
        return SetError(error, "operation packet version is unsupported");
    }
    if (ReadUint16(input.data() + kHeaderSizeOffset) != kOperationPacketHeaderSize) {
        return SetError(error, "operation packet header size does not match");
    }
    if (ReadUint32(input.data() + kReservedOffset) != 0) {
        return SetError(error, "operation packet reserved field is not zero");
    }

    OperationPacketHeader parsed;
    parsed.operation_code = ReadUint16(input.data() + kOperationCodeOffset);
    parsed.route_kind = ReadUint16(input.data() + kRouteKindOffset);
    parsed.request_id = ReadUint32(input.data() + kRequestIdOffset);
    parsed.payload_size = ReadUint32(input.data() + kPayloadSizeOffset);
    if (!IsKnownPacketKind(parsed.route_kind)) {
        return SetError(error, "operation header contains an unsupported route or kind");
    }
    if (!HasValidRequestId(parsed.route_kind, parsed.request_id)) {
        return SetError(error, "operation header request id does not match its packet kind");
    }
    if (parsed.payload_size > kMaximumOperationPayloadSize) {
        return SetError(error, "operation payload exceeds the protocol limit");
    }
    *header = parsed;
    return true;
}

bool SerializeOperationErrorPayload(const OperationErrorPayload& source,
                                    std::vector<uint8_t>* payload, std::string* error) {
    if (payload == nullptr) {
        return SetError(error, "operation error payload output is null");
    }
    if (!IsKnownError(source.error)) {
        return SetError(error, "operation error code is unknown");
    }
    payload->assign(kErrorPayloadSize, 0);
    WriteUint32(payload->data(), static_cast<uint32_t>(source.error));
    WriteUint16(payload->data() + 4, source.failed_operation_code);
    return true;
}

bool ParseOperationErrorPayload(const std::vector<uint8_t>& payload, OperationErrorPayload* parsed,
                                std::string* error) {
    if (parsed == nullptr) {
        return SetError(error, "parsed operation error output is null");
    }
    if (payload.size() != kErrorPayloadSize || ReadUint16(payload.data() + 6) != 0) {
        return SetError(error, "operation error payload is invalid");
    }
    OperationErrorPayload result;
    result.error = static_cast<OperationError>(ReadUint32(payload.data()));
    result.failed_operation_code = ReadUint16(payload.data() + 4);
    if (!IsKnownError(result.error)) {
        return SetError(error, "operation error code is unknown");
    }
    *parsed = result;
    return true;
}

}  // namespace floral::device::operation
