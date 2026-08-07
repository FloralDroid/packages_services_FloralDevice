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

#include "floral/device/power/PowerControlProtocol.h"

#include <cstring>

namespace floral::device::power {
namespace {

constexpr size_t kManualPowerRequestSize = 24;
constexpr size_t kPowerUpdateResponseSize = 16;
constexpr size_t kPowerSnapshotResponseSize = 96;
constexpr size_t kPowerCapabilitiesResponseSize = 24;

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

uint16_t ReadUint16(const uint8_t* input) {
    return static_cast<uint16_t>((static_cast<uint16_t>(input[0]) << 8) | input[1]);
}

uint32_t ReadUint32(const uint8_t* input) {
    return (static_cast<uint32_t>(input[0]) << 24) | (static_cast<uint32_t>(input[1]) << 16) |
           (static_cast<uint32_t>(input[2]) << 8) | static_cast<uint32_t>(input[3]);
}

uint64_t ReadUint64(const uint8_t* input) {
    return (static_cast<uint64_t>(ReadUint32(input)) << 32) | ReadUint32(input + 4);
}

void WriteUint16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value >> 8);
    output[1] = static_cast<uint8_t>(value);
}

void WriteUint32(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value >> 24);
    output[1] = static_cast<uint8_t>(value >> 16);
    output[2] = static_cast<uint8_t>(value >> 8);
    output[3] = static_cast<uint8_t>(value);
}

void WriteUint64(uint8_t* output, uint64_t value) {
    WriteUint32(output, static_cast<uint32_t>(value >> 32));
    WriteUint32(output + 4, static_cast<uint32_t>(value));
}

void WriteFloat(uint8_t* output, float value) {
    static_assert(sizeof(float) == sizeof(uint32_t));
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    WriteUint32(output, bits);
}

bool PrepareResponse(control::ControlCommandId command_id, uint32_t request_id, size_t payload_size,
                     control::ControlResponse* response, std::string* error) {
    if (response == nullptr || payload_size > control::kMaximumControlPayloadSize) {
        return SetError(error, "power response output is invalid");
    }
    response->header.command_id = static_cast<uint16_t>(command_id);
    response->header.route_kind = control::MakeFhc1RouteKind(control::ControlPacketKind::kResponse);
    response->header.request_id = request_id;
    response->header.payload_size = static_cast<uint32_t>(payload_size);
    response->payload.assign(payload_size, 0);
    response->refreshes_authority_lease = false;
    return true;
}

}  // namespace

bool ParseManualPowerRequest(const std::vector<uint8_t>& payload, ManualPowerRequest* request,
                             std::string* error) {
    if (request == nullptr || payload.size() != kManualPowerRequestSize ||
        ReadUint16(payload.data()) != kPowerControlVersion ||
        ReadUint16(payload.data() + 2) != kManualPowerRequestSize ||
        ReadUint64(payload.data() + 16) != 0) {
        return SetError(error, "manual power request version or payload size is invalid");
    }
    const uint32_t mode = ReadUint32(payload.data() + 4);
    const int32_t currentUa = static_cast<int32_t>(ReadUint32(payload.data() + 8));
    const uint32_t leaseDurationMs = ReadUint32(payload.data() + 12);
    if (mode < static_cast<uint32_t>(ManualPowerMode::kCharge) ||
        mode > static_cast<uint32_t>(ManualPowerMode::kIdle) || currentUa < 0 ||
        currentUa > 10'000'000 || leaseDurationMs == 0 ||
        leaseDurationMs > kMaximumPowerControlLeaseMs ||
        (mode == static_cast<uint32_t>(ManualPowerMode::kIdle) && currentUa != 0)) {
        return SetError(error, "manual power request contains an invalid field");
    }
    request->mode = static_cast<ManualPowerMode>(mode);
    request->current_ua = currentUa;
    request->lease_duration_ms = leaseDurationMs;
    return true;
}

bool SerializePowerUpdateResponse(control::ControlCommandId command_id, uint32_t request_id,
                                  const aidl::floral::device::power::PowerControlResult& update,
                                  control::ControlResponse* response, std::string* error) {
    if (!PrepareResponse(command_id, request_id, kPowerUpdateResponseSize, response, error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kPowerControlVersion);
    WriteUint16(response->payload.data() + 2, kPowerUpdateResponseSize);
    WriteUint32(response->payload.data() + 4, static_cast<uint32_t>(update.result));
    WriteUint64(response->payload.data() + 8, static_cast<uint64_t>(update.generation));
    response->refreshes_authority_lease = update.result == 0 || update.result == 1;
    return true;
}

bool SerializePowerSnapshotResponse(uint32_t request_id,
                                    const aidl::floral::device::power::PowerSnapshot& snapshot,
                                    control::ControlResponse* response, std::string* error) {
    if (!PrepareResponse(control::ControlCommandId::kGetPowerSnapshot, request_id,
                         kPowerSnapshotResponseSize, response, error)) {
        return false;
    }
    const uint32_t flags =
            (snapshot.externallyControlled ? 1U : 0U) | (snapshot.chargerOnline ? 2U : 0U);
    WriteUint16(response->payload.data(), kPowerControlVersion);
    WriteUint16(response->payload.data() + 2, kPowerSnapshotResponseSize);
    WriteUint32(response->payload.data() + 4, flags);
    WriteUint64(response->payload.data() + 8, static_cast<uint64_t>(snapshot.generation));
    WriteUint64(response->payload.data() + 16, static_cast<uint64_t>(snapshot.timestampNs));
    WriteUint32(response->payload.data() + 24, static_cast<uint32_t>(snapshot.controlMode));
    WriteUint32(response->payload.data() + 28, static_cast<uint32_t>(snapshot.batteryStatus));
    WriteUint32(response->payload.data() + 32, static_cast<uint32_t>(snapshot.level));
    WriteUint32(response->payload.data() + 36, static_cast<uint32_t>(snapshot.voltageMv));
    WriteUint32(response->payload.data() + 40, static_cast<uint32_t>(snapshot.currentUa));
    WriteUint32(response->payload.data() + 44, static_cast<uint32_t>(snapshot.currentAverageUa));
    WriteUint32(response->payload.data() + 48, static_cast<uint32_t>(snapshot.chargeCounterUah));
    WriteUint32(response->payload.data() + 52, static_cast<uint32_t>(snapshot.fullChargeUah));
    WriteUint64(response->payload.data() + 56,
                static_cast<uint64_t>(snapshot.chargeTimeToFullSeconds));
    WriteFloat(response->payload.data() + 64, snapshot.batteryTemperatureCelsius);
    WriteFloat(response->payload.data() + 68, snapshot.skinTemperatureCelsius);
    WriteFloat(response->payload.data() + 72, snapshot.cpuTemperatureCelsius);
    WriteFloat(response->payload.data() + 76, snapshot.gpuTemperatureCelsius);
    WriteUint32(response->payload.data() + 80, static_cast<uint32_t>(snapshot.skinThrottling));
    WriteUint32(response->payload.data() + 84, static_cast<uint32_t>(snapshot.cpuThrottling));
    WriteUint32(response->payload.data() + 88, static_cast<uint32_t>(snapshot.gpuThrottling));
    WriteUint32(response->payload.data() + 92, static_cast<uint32_t>(snapshot.batteryThrottling));
    return true;
}

bool SerializePowerCapabilitiesResponse(uint32_t request_id, control::ControlResponse* response,
                                        std::string* error) {
    if (!PrepareResponse(control::ControlCommandId::kGetPowerCapabilities, request_id,
                         kPowerCapabilitiesResponseSize, response, error)) {
        return false;
    }
    constexpr uint64_t kSupportedModes = 0x7;   // Charge, discharge, and idle.
    constexpr uint32_t kCapabilityFlags = 0x7;  // Autonomous, internal entropy, persistence.
    WriteUint16(response->payload.data(), kPowerControlVersion);
    WriteUint16(response->payload.data() + 2, kPowerCapabilitiesResponseSize);
    WriteUint64(response->payload.data() + 8, kSupportedModes);
    WriteUint32(response->payload.data() + 16, static_cast<uint32_t>(kMaximumPowerControlLeaseMs));
    WriteUint32(response->payload.data() + 20, kCapabilityFlags);
    return true;
}

}  // namespace floral::device::power
