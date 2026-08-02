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

namespace floral::device::control {

constexpr uint32_t kControlPacketMagic = 0x46484331;  // "FHC1"
constexpr uint16_t kControlPacketVersion = 1;
constexpr size_t kControlPacketHeaderSize = 24;
constexpr uint32_t kMaximumControlPayloadSize = 64 * 1024;

constexpr uint16_t kFhc1ControlPlane = 0x1000;
constexpr uint16_t kControlPlaneMask = 0xf000;
constexpr uint16_t kControlPacketKindMask = 0x0f00;
constexpr uint16_t kControlPacketFlagsMask = 0x00ff;

enum class ControlPacketKind : uint8_t {
    kRequest = 0,
    kResponse = 1,
    kEvent = 2,
    kErrorResponse = 3,
};

constexpr uint16_t MakeFhc1RouteKind(ControlPacketKind kind, uint8_t flags = 0) {
    return static_cast<uint16_t>(kFhc1ControlPlane | (static_cast<uint16_t>(kind) << 8) | flags);
}

constexpr bool IsFhc1RouteKind(uint16_t route_kind, ControlPacketKind kind) {
    return (route_kind & kControlPlaneMask) == kFhc1ControlPlane &&
           (route_kind & kControlPacketKindMask) == (static_cast<uint16_t>(kind) << 8) &&
           (route_kind & kControlPacketFlagsMask) == 0;
}

enum class ControlCommandId : uint16_t {
    kGenericError = 0x0000,
    kReplaceDisplayTopology = 0x0100,
    kSetAudioEncoderConfig = 0x0200,
    kSetVideoEncoderConfig = 0x0300,
};

enum class ControlError : uint32_t {
    kUnsupportedMessage = 1,
    kMalformedRequest = 2,
    kInvalidRequestId = 3,
    kInternalError = 4,
};

struct ControlPacketHeader {
    uint16_t command_id = 0;
    uint16_t route_kind = MakeFhc1RouteKind(ControlPacketKind::kRequest);
    uint32_t request_id = 0;
    uint32_t payload_size = 0;
};

struct ControlRequest {
    ControlPacketHeader header;
    std::vector<uint8_t> payload;
};

struct ControlResponse {
    ControlPacketHeader header;
    std::vector<uint8_t> payload;
    bool refreshes_authority_lease = false;
};

using SerializedControlPacketHeader = std::array<uint8_t, kControlPacketHeaderSize>;

bool SerializeControlPacketHeader(const ControlPacketHeader& header,
                                  SerializedControlPacketHeader* output, std::string* error);
bool ParseControlPacketHeader(const SerializedControlPacketHeader& input,
                              ControlPacketHeader* header, std::string* error);
bool SerializeControlErrorResponse(uint32_t request_id, uint16_t failed_command_id,
                                   ControlError error_code, ControlResponse* response,
                                   std::string* error);

}  // namespace floral::device::control
