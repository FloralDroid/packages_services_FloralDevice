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

#include <aidl/android/hardware/common/fmq/SynchronizedReadWrite.h>
#include <aidl/floral/device/simulation/GnssSnapshot.h>
#include <aidl/floral/device/simulation/ISimulationState.h>
#include <aidl/floral/device/simulation/SensorDescriptor.h>
#include <aidl/floral/device/simulation/SensorSnapshot.h>
#include <fmq/AidlMessageQueue.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace floral::device::simulation {

class SimulationStateClient final {
  public:
    explicit SimulationStateClient(std::string service_instance = DefaultServiceInstance());
    ~SimulationStateClient();

    SimulationStateClient(const SimulationStateClient&) = delete;
    SimulationStateClient& operator=(const SimulationStateClient&) = delete;

    bool RefreshConnection();
    SimulationConfig GetConfig() const;
    std::vector<ExternalStateRecord> DrainExternalStates();

    bool PublishSensorCatalog(
            const std::vector<aidl::floral::device::simulation::SensorDescriptor>& sensors);
    bool PublishSensorSnapshot(
            const aidl::floral::device::simulation::SensorSnapshot& snapshot);
    bool PublishGnssSnapshot(const aidl::floral::device::simulation::GnssSnapshot& snapshot);

    static std::string DefaultServiceInstance();

  private:
    struct SharedConfig;
    class ConfigListener;

    using Fmq = ::android::AidlMessageQueue<
            int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>;

    void DisconnectLocked();
    std::shared_ptr<aidl::floral::device::simulation::ISimulationState> ServiceSnapshot();

    const std::string service_instance_;
    const std::shared_ptr<SharedConfig> shared_config_;
    const std::shared_ptr<ConfigListener> listener_;
    mutable std::mutex connection_mutex_;
    std::shared_ptr<aidl::floral::device::simulation::ISimulationState> service_;
    std::unique_ptr<Fmq> queue_;
    std::chrono::steady_clock::time_point next_connect_attempt_;
};

}  // namespace floral::device::simulation
