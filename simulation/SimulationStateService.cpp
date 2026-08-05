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

#include "floral/device/simulation/SimulationStateService.h"

#include <android/binder_ibinder.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace floral::device::simulation {
namespace {

constexpr size_t kMaximumExternalStateReaders = 4;
constexpr size_t kMaximumSensorCount = 128;
constexpr size_t kMaximumSensorValues = 16;
constexpr size_t kMaximumSatelliteCount = 64;

bool IsFinite(const std::vector<float>& values) {
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}

}  // namespace

SimulationStateService::SimulationStateService() {
    config_.generation = 1;
    config_.motionSource = 0;
    config_.motionProfile = 0;
    config_.gnssSource = 0;
    config_.gnssEnabled = true;
    config_.targetLightLux = 200.0f;
    config_.targetProximityCm = 5.0f;
    config_.targetPressureHpa = 1013.25f;
    config_.anchorLatitudeDegrees = 35.681236;
    config_.anchorLongitudeDegrees = 139.767125;
    config_.anchorAltitudeMeters = 20.0;
    config_.transitionDurationMs = 1000;
}

ndk::ScopedAStatus SimulationStateService::getConfig(Config* result) {
    if (result == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    std::lock_guard lock(mutex_);
    *result = config_;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SimulationStateService::getSensorCatalog(
        std::vector<aidl::floral::device::simulation::SensorDescriptor>* result) {
    if (result == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    std::lock_guard lock(mutex_);
    *result = sensor_catalog_;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SimulationStateService::getSensorSnapshot(
        aidl::floral::device::simulation::SensorSnapshot* result) {
    if (result == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    std::lock_guard lock(mutex_);
    *result = sensor_snapshot_;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SimulationStateService::getGnssSnapshot(
        aidl::floral::device::simulation::GnssSnapshot* result) {
    if (result == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    std::lock_guard lock(mutex_);
    *result = gnss_snapshot_;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SimulationStateService::openExternalStateStream(
        aidl::android::hardware::common::fmq::MQDescriptor<
                int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>* result) {
    if (result == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    auto queue = std::make_shared<ExternalStateFmq>(
            kExternalStateRecordSize * kExternalStateFmqCapacity, true);
    if (!queue->isValid()) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                EX_ILLEGAL_STATE, "failed to create the external simulation state FMQ");
    }
    {
        std::lock_guard lock(mutex_);
        const int32_t ownerPid = AIBinder_getCallingPid();
        external_queues_.erase(std::remove_if(external_queues_.begin(), external_queues_.end(),
                                              [ownerPid](const ExternalQueue& entry) {
                                                  return entry.owner_pid == ownerPid;
                                              }),
                               external_queues_.end());
        if (external_queues_.size() == kMaximumExternalStateReaders) {
            external_queues_.erase(external_queues_.begin());
        }
        external_queues_.push_back({ownerPid, queue, false});
    }
    *result = queue->dupeDesc();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SimulationStateService::registerListener(
        const std::shared_ptr<Listener>& listener) {
    if (listener == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    Config config;
    {
        std::lock_guard lock(mutex_);
        const auto found = std::find_if(listeners_.begin(), listeners_.end(),
                                        [&listener](const std::shared_ptr<Listener>& candidate) {
                                            return SameBinder(candidate, listener);
                                        });
        if (found == listeners_.end()) {
            listeners_.push_back(listener);
        }
        config = config_;
    }
    if (!listener->onSimulationConfigChanged(config).isOk()) {
        RemoveListener(listener);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SimulationStateService::unregisterListener(
        const std::shared_ptr<Listener>& listener) {
    if (listener == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    RemoveListener(listener);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SimulationStateService::publishSensorCatalog(
        const std::vector<aidl::floral::device::simulation::SensorDescriptor>& sensors) {
    if (sensors.empty() || sensors.size() > kMaximumSensorCount) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                "sensor catalog size is invalid");
    }
    std::unordered_set<int32_t> handles;
    for (const auto& sensor : sensors) {
        if (sensor.handle <= 0 || sensor.type <= 0 || sensor.name.empty() ||
            sensor.name.size() > 128 || sensor.minDelayUs < -1 || sensor.maxDelayUs < 0 ||
            !handles.insert(sensor.handle).second) {
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                    EX_ILLEGAL_ARGUMENT, "sensor catalog contains an invalid descriptor");
        }
    }
    std::lock_guard lock(mutex_);
    sensor_catalog_ = sensors;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SimulationStateService::publishSensorSnapshot(
        const aidl::floral::device::simulation::SensorSnapshot& snapshot) {
    if (snapshot.generation <= 0 || snapshot.timestampNs <= 0 ||
        snapshot.readings.size() > kMaximumSensorCount) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                "sensor snapshot is invalid");
    }
    for (const auto& reading : snapshot.readings) {
        if (reading.handle <= 0 || reading.type <= 0 ||
            reading.values.size() > kMaximumSensorValues || !IsFinite(reading.values) ||
            reading.stepCount < 0) {
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                    EX_ILLEGAL_ARGUMENT, "sensor snapshot contains an invalid reading");
        }
    }
    std::lock_guard lock(mutex_);
    if (snapshot.generation < config_.generation ||
        (sensor_snapshot_.timestampNs > 0 &&
         snapshot.timestampNs <= sensor_snapshot_.timestampNs)) {
        return ndk::ScopedAStatus::ok();
    }
    sensor_snapshot_ = snapshot;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SimulationStateService::publishGnssSnapshot(
        const aidl::floral::device::simulation::GnssSnapshot& snapshot) {
    if (snapshot.generation <= 0 || snapshot.elapsedRealtimeNs <= 0 || snapshot.utcTimeMs <= 0 ||
        snapshot.satellites.size() > kMaximumSatelliteCount ||
        !std::isfinite(snapshot.latitudeDegrees) || !std::isfinite(snapshot.longitudeDegrees) ||
        !std::isfinite(snapshot.altitudeMeters) || !std::isfinite(snapshot.groundSpeedMps) ||
        !std::isfinite(snapshot.bearingDegrees) ||
        !std::isfinite(snapshot.horizontalAccuracyMeters) ||
        !std::isfinite(snapshot.verticalAccuracyMeters) || snapshot.latitudeDegrees < -90.0 ||
        snapshot.latitudeDegrees > 90.0 || snapshot.longitudeDegrees < -180.0 ||
        snapshot.longitudeDegrees > 180.0) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                "GNSS snapshot is invalid");
    }
    std::lock_guard lock(mutex_);
    if (snapshot.generation < config_.generation ||
        (gnss_snapshot_.elapsedRealtimeNs > 0 &&
         snapshot.elapsedRealtimeNs <= gnss_snapshot_.elapsedRealtimeNs)) {
        return ndk::ScopedAStatus::ok();
    }
    gnss_snapshot_ = snapshot;
    return ndk::ScopedAStatus::ok();
}

uint64_t SimulationStateService::MutateConfig(const Config& requested) {
    Config config = requested;
    std::vector<std::shared_ptr<Listener>> listeners;
    {
        std::lock_guard lock(mutex_);
        config.generation = config_.generation == std::numeric_limits<int64_t>::max()
                                    ? 1
                                    : config_.generation + 1;
        config_ = config;
        listeners = listeners_;
    }
    NotifyListeners(config, listeners);
    return static_cast<uint64_t>(config.generation);
}

uint64_t SimulationStateService::ResetSensorConfig() {
    Config config;
    {
        std::lock_guard lock(mutex_);
        config = config_;
    }
    config.motionSource = 0;
    config.motionProfile = 0;
    config.targetLightLux = 200.0f;
    config.targetProximityCm = 5.0f;
    config.targetPressureHpa = 1013.25f;
    config.transitionDurationMs = 1000;
    return MutateConfig(config);
}

uint64_t SimulationStateService::ResetGnssConfig() {
    Config config;
    {
        std::lock_guard lock(mutex_);
        config = config_;
    }
    config.gnssSource = 0;
    config.gnssEnabled = true;
    config.anchorLatitudeDegrees = 35.681236;
    config.anchorLongitudeDegrees = 139.767125;
    config.anchorAltitudeMeters = 20.0;
    config.groundSpeedMps = 0.0f;
    config.bearingDegrees = 0.0f;
    config.transitionDurationMs = 1000;
    return MutateConfig(config);
}

bool SimulationStateService::PublishExternalState(const ExternalStateRecord& record,
                                                  std::string* error) {
    SerializedExternalStateRecord serialized{};
    if (!SerializeExternalStateRecord(record, &serialized, error)) {
        return false;
    }

    std::lock_guard lock(mutex_);
    for (ExternalQueue& entry : external_queues_) {
        const std::shared_ptr<ExternalStateFmq>& queue = entry.queue;
        if (queue == nullptr || !queue->isValid()) {
            continue;
        }
        if (queue->availableToWrite() < serialized.size()) {
            entry.pending_discontinuity = true;
            continue;
        }
        SerializedExternalStateRecord output = serialized;
        if (entry.pending_discontinuity) {
            ExternalStateRecord discontinuous = record;
            discontinuous.flags |= kExternalStateDiscontinuity;
            if (!SerializeExternalStateRecord(discontinuous, &output, error)) {
                return false;
            }
        }
        if (queue->write(output.data(), output.size())) {
            entry.pending_discontinuity = false;
        } else {
            entry.pending_discontinuity = true;
        }
    }
    return true;
}

bool SimulationStateService::SameBinder(const std::shared_ptr<Listener>& left,
                                        const std::shared_ptr<Listener>& right) {
    return left != nullptr && right != nullptr && left->asBinder().get() == right->asBinder().get();
}

void SimulationStateService::RemoveListener(const std::shared_ptr<Listener>& listener) {
    std::lock_guard lock(mutex_);
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [&listener](const std::shared_ptr<Listener>& candidate) {
                                        return SameBinder(candidate, listener);
                                    }),
                     listeners_.end());
}

void SimulationStateService::NotifyListeners(
        const Config& config, const std::vector<std::shared_ptr<Listener>>& listeners) {
    for (const std::shared_ptr<Listener>& listener : listeners) {
        if (listener == nullptr || !listener->onSimulationConfigChanged(config).isOk()) {
            RemoveListener(listener);
        }
    }
}

}  // namespace floral::device::simulation
