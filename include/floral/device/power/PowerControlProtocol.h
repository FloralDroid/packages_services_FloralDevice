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

#include "floral/device/control/ControlProtocol.h"

#include <aidl/floral/device/power/PowerControlResult.h>
#include <aidl/floral/device/power/PowerSnapshot.h>

#include <cstdint>
#include <string>
#include <vector>

namespace floral::device::power {

constexpr uint16_t kPowerControlVersion = 1;
constexpr int64_t kMaximumPowerControlLeaseMs = 60'000;

enum class ManualPowerMode : uint32_t {
    kCharge = 1,
    kDischarge = 2,
    kIdle = 3,
};

struct ManualPowerRequest {
    ManualPowerMode mode = ManualPowerMode::kIdle;
    int32_t current_ua = 0;
    int64_t lease_duration_ms = 30'000;
};

bool ParseManualPowerRequest(const std::vector<uint8_t>& payload, ManualPowerRequest* request,
                             std::string* error);
bool SerializePowerUpdateResponse(control::ControlCommandId command_id, uint32_t request_id,
                                  const aidl::floral::device::power::PowerControlResult& update,
                                  control::ControlResponse* response, std::string* error);
bool SerializePowerSnapshotResponse(uint32_t request_id,
                                    const aidl::floral::device::power::PowerSnapshot& snapshot,
                                    control::ControlResponse* response, std::string* error);
bool SerializePowerCapabilitiesResponse(uint32_t request_id, control::ControlResponse* response,
                                        std::string* error);

}  // namespace floral::device::power
