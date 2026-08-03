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

namespace floral::device::operation {

constexpr uint32_t kOperationPacketMagic = 0x46444f31;  // "FDO1"
constexpr uint16_t kOperationPacketVersion = 1;
constexpr size_t kOperationPacketHeaderSize = 24;
constexpr uint32_t kMaximumOperationPayloadSize = 64 * 1024;

constexpr uint16_t kFdo1OperationPlane = 0x2000;
constexpr uint16_t kOperationPlaneMask = 0xf000;
constexpr uint16_t kOperationPacketKindMask = 0x0f00;
constexpr uint16_t kOperationPacketFlagsMask = 0x00ff;

enum class OperationPacketKind : uint8_t {
    kRequest = 0,
    kResponse = 1,
    kEvent = 2,
    kErrorResponse = 3,
};

constexpr uint16_t MakeFdo1RouteKind(OperationPacketKind kind, uint8_t flags = 0) {
    return static_cast<uint16_t>(kFdo1OperationPlane | (static_cast<uint16_t>(kind) << 8) | flags);
}

constexpr bool IsFdo1RouteKind(uint16_t route_kind, OperationPacketKind kind) {
    return (route_kind & kOperationPlaneMask) == kFdo1OperationPlane &&
           (route_kind & kOperationPacketKindMask) == (static_cast<uint16_t>(kind) << 8) &&
           (route_kind & kOperationPacketFlagsMask) == 0;
}

enum class OperationCode : uint16_t {
    kGenericError = 0x0000,
    kBindInputTarget = 0x0100,
    kUnbindInputTarget = 0x0101,
    kTouch = 0x0200,
};

enum class OperationError : uint32_t {
    kUnsupportedMessage = 1,
    kMalformedMessage = 2,
    kInvalidRequestId = 3,
    kInternalError = 4,
};

struct OperationPacketHeader {
    uint16_t operation_code = 0;
    uint16_t route_kind = MakeFdo1RouteKind(OperationPacketKind::kRequest);
    uint32_t request_id = 0;
    uint32_t payload_size = 0;
};

struct OperationErrorPayload {
    OperationError error = OperationError::kInternalError;
    uint16_t failed_operation_code = 0;
};

using SerializedOperationPacketHeader = std::array<uint8_t, kOperationPacketHeaderSize>;

bool SerializeOperationPacketHeader(const OperationPacketHeader& header,
                                    SerializedOperationPacketHeader* output, std::string* error);
bool ParseOperationPacketHeader(const SerializedOperationPacketHeader& input,
                                OperationPacketHeader* header, std::string* error);
bool SerializeOperationErrorPayload(const OperationErrorPayload& source,
                                    std::vector<uint8_t>* payload, std::string* error);
bool ParseOperationErrorPayload(const std::vector<uint8_t>& payload, OperationErrorPayload* parsed,
                                std::string* error);

}  // namespace floral::device::operation
