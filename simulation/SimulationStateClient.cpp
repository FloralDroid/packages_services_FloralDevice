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

#define LOG_TAG "floral-simulation-client"

#include "floral/device/simulation/SimulationStateClient.h"

#include <aidl/floral/device/simulation/BnSimulationStateListener.h>
#include <aidl/floral/device/simulation/SimulationConfig.h>
#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <log/log.h>

#include <utility>

namespace floral::device::simulation {
namespace {

constexpr auto kReconnectInterval = std::chrono::milliseconds(500);

SimulationConfig ConvertConfig(const aidl::floral::device::simulation::SimulationConfig& input) {
    SimulationConfig output;
    output.generation = input.generation > 0 ? static_cast<uint64_t>(input.generation) : 1;
    output.motion_source =
            input.motionSource == 1 ? StateSource::kExternalStream : StateSource::kAutonomous;
    output.gnss_source =
            input.gnssSource == 1 ? StateSource::kExternalStream : StateSource::kAutonomous;
    output.motion_profile = input.motionProfile >= 0 && input.motionProfile <= 3
            ? static_cast<MotionProfile>(input.motionProfile)
            : MotionProfile::kStationary;
    output.gnss_enabled = input.gnssEnabled;
    output.target_light_lux = input.targetLightLux;
    output.target_proximity_cm = input.targetProximityCm;
    output.target_pressure_hpa = input.targetPressureHpa;
    output.anchor_latitude_degrees = input.anchorLatitudeDegrees;
    output.anchor_longitude_degrees = input.anchorLongitudeDegrees;
    output.anchor_altitude_meters = input.anchorAltitudeMeters;
    output.ground_speed_mps = input.groundSpeedMps;
    output.bearing_degrees = input.bearingDegrees;
    output.transition_duration_ms = input.transitionDurationMs;
    return output;
}

}  // namespace

struct SimulationStateClient::SharedConfig {
    mutable std::mutex mutex;
    SimulationConfig config;
};

class SimulationStateClient::ConfigListener final
    : public aidl::floral::device::simulation::BnSimulationStateListener {
  public:
    explicit ConfigListener(std::shared_ptr<SharedConfig> shared_config)
        : shared_config_(std::move(shared_config)) {}

    ndk::ScopedAStatus onSimulationConfigChanged(
            const aidl::floral::device::simulation::SimulationConfig& config) override {
        std::lock_guard lock(shared_config_->mutex);
        shared_config_->config = ConvertConfig(config);
        return ndk::ScopedAStatus::ok();
    }

  private:
    const std::shared_ptr<SharedConfig> shared_config_;
};

SimulationStateClient::SimulationStateClient(std::string service_instance)
    : service_instance_(std::move(service_instance)),
      shared_config_(std::make_shared<SharedConfig>()),
      listener_(ndk::SharedRefBase::make<ConfigListener>(shared_config_)),
      next_connect_attempt_(std::chrono::steady_clock::time_point::min()) {}

SimulationStateClient::~SimulationStateClient() {
    std::shared_ptr<aidl::floral::device::simulation::ISimulationState> service;
    {
        std::lock_guard lock(connection_mutex_);
        service = service_;
        DisconnectLocked();
    }
    if (service != nullptr) {
        (void)service->unregisterListener(listener_);
    }
}

std::string SimulationStateClient::DefaultServiceInstance() {
    return std::string(aidl::floral::device::simulation::ISimulationState::descriptor) + "/default";
}

bool SimulationStateClient::RefreshConnection() {
    std::lock_guard lock(connection_mutex_);
    if (service_ != nullptr && AIBinder_isAlive(service_->asBinder().get())) {
        return true;
    }
    DisconnectLocked();
    const auto now = std::chrono::steady_clock::now();
    if (now < next_connect_attempt_) {
        return false;
    }
    next_connect_attempt_ = now + kReconnectInterval;

    ndk::SpAIBinder binder(AServiceManager_checkService(service_instance_.c_str()));
    if (binder.get() == nullptr) {
        return false;
    }
    auto service = aidl::floral::device::simulation::ISimulationState::fromBinder(binder);
    if (service == nullptr) {
        return false;
    }
    aidl::floral::device::simulation::SimulationConfig aidlConfig;
    if (!service->getConfig(&aidlConfig).isOk()) {
        return false;
    }
    aidl::android::hardware::common::fmq::MQDescriptor<
            int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>
            descriptor;
    if (!service->openExternalStateStream(&descriptor).isOk()) {
        return false;
    }
    auto queue = std::make_unique<Fmq>(descriptor, false);
    if (!queue->isValid()) {
        return false;
    }
    {
        std::lock_guard configLock(shared_config_->mutex);
        shared_config_->config = ConvertConfig(aidlConfig);
    }
    if (!service->registerListener(listener_).isOk()) {
        return false;
    }
    service_ = std::move(service);
    queue_ = std::move(queue);
    ALOGI("Connected to %s", service_instance_.c_str());
    return true;
}

SimulationConfig SimulationStateClient::GetConfig() const {
    std::lock_guard lock(shared_config_->mutex);
    return shared_config_->config;
}

std::vector<ExternalStateRecord> SimulationStateClient::DrainExternalStates() {
    std::lock_guard lock(connection_mutex_);
    std::vector<ExternalStateRecord> result;
    if (queue_ == nullptr || !queue_->isValid()) {
        return result;
    }
    const size_t completeRecords = queue_->availableToRead() / kExternalStateRecordSize;
    result.reserve(completeRecords);
    for (size_t index = 0; index < completeRecords; ++index) {
        SerializedExternalStateRecord serialized{};
        if (!queue_->read(serialized.data(), serialized.size())) {
            break;
        }
        ExternalStateRecord record;
        std::string error;
        if (ParseExternalStateRecord(serialized, &record, &error)) {
            result.push_back(record);
        } else {
            ALOGW("Discarding invalid external state record: %s", error.c_str());
        }
    }
    return result;
}

std::shared_ptr<aidl::floral::device::simulation::ISimulationState>
SimulationStateClient::ServiceSnapshot() {
    if (!RefreshConnection()) {
        return nullptr;
    }
    std::lock_guard lock(connection_mutex_);
    return service_;
}

bool SimulationStateClient::PublishSensorCatalog(
        const std::vector<aidl::floral::device::simulation::SensorDescriptor>& sensors) {
    const auto service = ServiceSnapshot();
    if (service == nullptr || !service->publishSensorCatalog(sensors).isOk()) {
        std::lock_guard lock(connection_mutex_);
        DisconnectLocked();
        return false;
    }
    return true;
}

bool SimulationStateClient::PublishSensorSnapshot(
        const aidl::floral::device::simulation::SensorSnapshot& snapshot) {
    const auto service = ServiceSnapshot();
    if (service == nullptr || !service->publishSensorSnapshot(snapshot).isOk()) {
        std::lock_guard lock(connection_mutex_);
        DisconnectLocked();
        return false;
    }
    return true;
}

bool SimulationStateClient::PublishGnssSnapshot(
        const aidl::floral::device::simulation::GnssSnapshot& snapshot) {
    const auto service = ServiceSnapshot();
    if (service == nullptr || !service->publishGnssSnapshot(snapshot).isOk()) {
        std::lock_guard lock(connection_mutex_);
        DisconnectLocked();
        return false;
    }
    return true;
}

void SimulationStateClient::DisconnectLocked() {
    queue_.reset();
    service_.reset();
}

}  // namespace floral::device::simulation
