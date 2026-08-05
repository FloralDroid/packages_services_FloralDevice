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
#include "floral/device/simulation/ExternalStateRecord.h"
#include "floral/device/simulation/SimulationTypes.h"

#include <aidl/floral/device/simulation/GnssSnapshot.h>
#include <aidl/floral/device/simulation/SensorDescriptor.h>
#include <aidl/floral/device/simulation/SensorSnapshot.h>
#include <aidl/floral/device/simulation/SimulationConfig.h>

#include <cstdint>
#include <string>
#include <vector>

namespace floral::device::simulation {

constexpr uint16_t kSimulationControlVersion = 1;

enum GnssCapabilityFlag : uint64_t {
    kGnssCapabilityLocation = 1ULL << 0,
    kGnssCapabilitySatelliteStatus = 1ULL << 1,
    kGnssCapabilityNmea = 1ULL << 2,
    kGnssCapabilityMultipleConstellations = 1ULL << 3,
    kGnssCapabilityExternalTruthStream = 1ULL << 4,
};

bool ParseMotionConfigRequest(const std::vector<uint8_t>& payload, MotionConfigRequest* request,
                              std::string* error);
bool ParseEnvironmentConfigRequest(const std::vector<uint8_t>& payload,
                                   EnvironmentConfigRequest* request, std::string* error);
bool ParseGnssConfigRequest(const std::vector<uint8_t>& payload, GnssConfigRequest* request,
                            std::string* error);
bool ParseExternalPoseBatch(const std::vector<uint8_t>& payload, int64_t receive_timestamp_ns,
                            std::vector<ExternalStateRecord>* records, std::string* error);
bool ParseExternalGnssBatch(const std::vector<uint8_t>& payload, int64_t receive_timestamp_ns,
                            std::vector<ExternalStateRecord>* records, std::string* error);

bool SerializeSimulationUpdateResponse(control::ControlCommandId command_id, uint32_t request_id,
                                       const SimulationUpdate& update,
                                       control::ControlResponse* response, std::string* error);
bool SerializeSensorConfigResponse(uint32_t request_id,
                                   const aidl::floral::device::simulation::SimulationConfig& config,
                                   control::ControlResponse* response, std::string* error);
bool SerializeSensorCatalogResponse(
        uint32_t request_id, uint64_t generation,
        const std::vector<aidl::floral::device::simulation::SensorDescriptor>& sensors,
        control::ControlResponse* response, std::string* error);
bool SerializeSensorSnapshotResponse(
        uint32_t request_id, const aidl::floral::device::simulation::SensorSnapshot& snapshot,
        control::ControlResponse* response, std::string* error);
bool SerializeGnssConfigResponse(uint32_t request_id,
                                 const aidl::floral::device::simulation::SimulationConfig& config,
                                 control::ControlResponse* response, std::string* error);
bool SerializeGnssCapabilitiesResponse(uint32_t request_id, control::ControlResponse* response,
                                       std::string* error);
bool SerializeGnssSnapshotResponse(uint32_t request_id,
                                   const aidl::floral::device::simulation::GnssSnapshot& snapshot,
                                   control::ControlResponse* response, std::string* error);

}  // namespace floral::device::simulation
