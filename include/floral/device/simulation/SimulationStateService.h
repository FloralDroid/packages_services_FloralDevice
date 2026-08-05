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

#include <aidl/floral/device/simulation/BnSimulationState.h>
#include <aidl/floral/device/simulation/GnssSnapshot.h>
#include <aidl/floral/device/simulation/ISimulationStateListener.h>
#include <aidl/floral/device/simulation/SensorDescriptor.h>
#include <aidl/floral/device/simulation/SensorSnapshot.h>
#include <aidl/floral/device/simulation/SimulationConfig.h>
#include <fmq/AidlMessageQueue.h>

#include <memory>
#include <mutex>
#include <vector>

namespace floral::device::simulation {

class SimulationController;

using ExternalStateFmq =
        ::android::AidlMessageQueue<int8_t,
                                    aidl::android::hardware::common::fmq::SynchronizedReadWrite>;

class SimulationStateService final : public aidl::floral::device::simulation::BnSimulationState {
  public:
    SimulationStateService();

    ndk::ScopedAStatus getConfig(
            aidl::floral::device::simulation::SimulationConfig* result) override;
    ndk::ScopedAStatus getSensorCatalog(
            std::vector<aidl::floral::device::simulation::SensorDescriptor>* result) override;
    ndk::ScopedAStatus getSensorSnapshot(
            aidl::floral::device::simulation::SensorSnapshot* result) override;
    ndk::ScopedAStatus getGnssSnapshot(
            aidl::floral::device::simulation::GnssSnapshot* result) override;
    ndk::ScopedAStatus openExternalStateStream(
            aidl::android::hardware::common::fmq::MQDescriptor<
                    int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>* result)
            override;
    ndk::ScopedAStatus registerListener(
            const std::shared_ptr<aidl::floral::device::simulation::ISimulationStateListener>&
                    listener) override;
    ndk::ScopedAStatus unregisterListener(
            const std::shared_ptr<aidl::floral::device::simulation::ISimulationStateListener>&
                    listener) override;
    ndk::ScopedAStatus publishSensorCatalog(
            const std::vector<aidl::floral::device::simulation::SensorDescriptor>& sensors)
            override;
    ndk::ScopedAStatus publishSensorSnapshot(
            const aidl::floral::device::simulation::SensorSnapshot& snapshot) override;
    ndk::ScopedAStatus publishGnssSnapshot(
            const aidl::floral::device::simulation::GnssSnapshot& snapshot) override;

  private:
    friend class SimulationController;

    using Config = aidl::floral::device::simulation::SimulationConfig;
    using Listener = aidl::floral::device::simulation::ISimulationStateListener;

    struct ExternalQueue {
        int32_t owner_pid = 0;
        std::shared_ptr<ExternalStateFmq> queue;
        bool pending_discontinuity = false;
    };

    uint64_t MutateConfig(const Config& config);
    uint64_t ResetSensorConfig();
    uint64_t ResetGnssConfig();
    bool PublishExternalState(const ExternalStateRecord& record, std::string* error);

    static bool SameBinder(const std::shared_ptr<Listener>& left,
                           const std::shared_ptr<Listener>& right);
    void RemoveListener(const std::shared_ptr<Listener>& listener);
    void NotifyListeners(const Config& config,
                         const std::vector<std::shared_ptr<Listener>>& listeners);

    mutable std::mutex mutex_;
    Config config_;
    std::vector<std::shared_ptr<Listener>> listeners_;
    std::vector<ExternalQueue> external_queues_;
    std::vector<aidl::floral::device::simulation::SensorDescriptor> sensor_catalog_;
    aidl::floral::device::simulation::SensorSnapshot sensor_snapshot_;
    aidl::floral::device::simulation::GnssSnapshot gnss_snapshot_;
};

}  // namespace floral::device::simulation
