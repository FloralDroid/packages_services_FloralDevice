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

#include "floral/device/simulation/ExternalStateRecord.h"
#include "floral/device/simulation/SimulationTypes.h"

#include <aidl/floral/device/simulation/GnssSnapshot.h>
#include <aidl/floral/device/simulation/SensorDescriptor.h>
#include <aidl/floral/device/simulation/SensorSnapshot.h>
#include <aidl/floral/device/simulation/SimulationConfig.h>

#include <memory>
#include <string>
#include <vector>

namespace floral::device::simulation {

class SimulationStateService;

class SimulationController final {
  public:
    explicit SimulationController(std::shared_ptr<SimulationStateService> state_service);

    SimulationUpdate SetMotionConfig(const MotionConfigRequest& request);
    SimulationUpdate SetEnvironmentConfig(const EnvironmentConfigRequest& request);
    SimulationUpdate SetGnssConfig(const GnssConfigRequest& request);
    SimulationUpdate ResetSensors();
    SimulationUpdate ResetGnss();
    SimulationUpdate PublishExternalStates(std::vector<ExternalStateRecord> records,
                                           std::string* error);
    void ClearExternalSources();

    aidl::floral::device::simulation::SimulationConfig GetConfig() const;
    std::vector<aidl::floral::device::simulation::SensorDescriptor> GetSensorCatalog() const;
    aidl::floral::device::simulation::SensorSnapshot GetSensorSnapshot() const;
    aidl::floral::device::simulation::GnssSnapshot GetGnssSnapshot() const;

  private:
    const std::shared_ptr<SimulationStateService> state_service_;
};

}  // namespace floral::device::simulation
