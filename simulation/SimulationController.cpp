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

#include "floral/device/simulation/SimulationController.h"

#include "floral/device/simulation/SimulationStateService.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace floral::device::simulation {
namespace {

bool ValidTransition(uint32_t duration_ms) {
    return duration_ms <= 60'000;
}

SimulationUpdate InvalidUpdate(const aidl::floral::device::simulation::SimulationConfig& config) {
    return {SimulationUpdateResult::kInvalid, 0, static_cast<uint64_t>(config.generation)};
}

}  // namespace

SimulationController::SimulationController(std::shared_ptr<SimulationStateService> state_service)
    : state_service_(std::move(state_service)) {}

SimulationUpdate SimulationController::SetMotionConfig(const MotionConfigRequest& request) {
    auto config = GetConfig();
    if (state_service_ == nullptr || request.source > 1 || request.profile > 3 ||
        !ValidTransition(request.transition_duration_ms)) {
        return InvalidUpdate(config);
    }
    if (config.motionSource == static_cast<int32_t>(request.source) &&
        config.motionProfile == static_cast<int32_t>(request.profile) &&
        config.transitionDurationMs == static_cast<int32_t>(request.transition_duration_ms)) {
        return {SimulationUpdateResult::kUnchanged, 0, static_cast<uint64_t>(config.generation)};
    }
    config.motionSource = static_cast<int32_t>(request.source);
    config.motionProfile = static_cast<int32_t>(request.profile);
    config.transitionDurationMs = static_cast<int32_t>(request.transition_duration_ms);
    return {SimulationUpdateResult::kApplied, 0, state_service_->MutateConfig(config)};
}

SimulationUpdate SimulationController::SetEnvironmentConfig(
        const EnvironmentConfigRequest& request) {
    auto config = GetConfig();
    if (state_service_ == nullptr || !std::isfinite(request.light_lux) ||
        !std::isfinite(request.proximity_cm) || !std::isfinite(request.pressure_hpa) ||
        request.light_lux < 0.0f || request.light_lux > 100'000.0f || request.proximity_cm < 0.0f ||
        request.proximity_cm > 5.0f || request.pressure_hpa < 300.0f ||
        request.pressure_hpa > 1100.0f || !ValidTransition(request.transition_duration_ms)) {
        return InvalidUpdate(config);
    }
    if (config.targetLightLux == request.light_lux &&
        config.targetProximityCm == request.proximity_cm &&
        config.targetPressureHpa == request.pressure_hpa &&
        config.transitionDurationMs == static_cast<int32_t>(request.transition_duration_ms)) {
        return {SimulationUpdateResult::kUnchanged, 0, static_cast<uint64_t>(config.generation)};
    }
    config.targetLightLux = request.light_lux;
    config.targetProximityCm = request.proximity_cm;
    config.targetPressureHpa = request.pressure_hpa;
    config.transitionDurationMs = static_cast<int32_t>(request.transition_duration_ms);
    return {SimulationUpdateResult::kApplied, 0, state_service_->MutateConfig(config)};
}

SimulationUpdate SimulationController::SetGnssConfig(const GnssConfigRequest& request) {
    auto config = GetConfig();
    if (state_service_ == nullptr || request.source > 1 ||
        !std::isfinite(request.latitude_degrees) || !std::isfinite(request.longitude_degrees) ||
        !std::isfinite(request.altitude_meters) || !std::isfinite(request.ground_speed_mps) ||
        !std::isfinite(request.bearing_degrees) || request.latitude_degrees < -90.0 ||
        request.latitude_degrees > 90.0 || request.longitude_degrees < -180.0 ||
        request.longitude_degrees > 180.0 || request.ground_speed_mps < 0.0f ||
        request.ground_speed_mps > 150.0f || request.bearing_degrees < 0.0f ||
        request.bearing_degrees >= 360.0f || !ValidTransition(request.transition_duration_ms)) {
        return InvalidUpdate(config);
    }
    if (config.gnssSource == static_cast<int32_t>(request.source) &&
        config.gnssEnabled == request.enabled &&
        config.anchorLatitudeDegrees == request.latitude_degrees &&
        config.anchorLongitudeDegrees == request.longitude_degrees &&
        config.anchorAltitudeMeters == request.altitude_meters &&
        config.groundSpeedMps == request.ground_speed_mps &&
        config.bearingDegrees == request.bearing_degrees &&
        config.transitionDurationMs == static_cast<int32_t>(request.transition_duration_ms)) {
        return {SimulationUpdateResult::kUnchanged, 0, static_cast<uint64_t>(config.generation)};
    }
    config.gnssSource = static_cast<int32_t>(request.source);
    config.gnssEnabled = request.enabled;
    config.anchorLatitudeDegrees = request.latitude_degrees;
    config.anchorLongitudeDegrees = request.longitude_degrees;
    config.anchorAltitudeMeters = request.altitude_meters;
    config.groundSpeedMps = request.ground_speed_mps;
    config.bearingDegrees = request.bearing_degrees;
    config.transitionDurationMs = static_cast<int32_t>(request.transition_duration_ms);
    return {SimulationUpdateResult::kApplied, 0, state_service_->MutateConfig(config)};
}

SimulationUpdate SimulationController::ResetSensors() {
    const auto config = GetConfig();
    if (state_service_ == nullptr) {
        return InvalidUpdate(config);
    }
    return {SimulationUpdateResult::kApplied, 0, state_service_->ResetSensorConfig()};
}

SimulationUpdate SimulationController::ResetGnss() {
    const auto config = GetConfig();
    if (state_service_ == nullptr) {
        return InvalidUpdate(config);
    }
    return {SimulationUpdateResult::kApplied, 0, state_service_->ResetGnssConfig()};
}

SimulationUpdate SimulationController::PublishExternalStates(
        std::vector<ExternalStateRecord> records, std::string* error) {
    const auto config = GetConfig();
    if (state_service_ == nullptr) {
        return InvalidUpdate(config);
    }
    uint32_t applied = 0;
    for (ExternalStateRecord& record : records) {
        record.generation = static_cast<uint64_t>(config.generation);
        if (!state_service_->PublishExternalState(record, error)) {
            return {SimulationUpdateResult::kInvalid, applied,
                    static_cast<uint64_t>(config.generation)};
        }
        ++applied;
    }
    return {SimulationUpdateResult::kApplied, applied, static_cast<uint64_t>(config.generation)};
}

void SimulationController::ClearExternalSources() {
    auto config = GetConfig();
    if (state_service_ == nullptr || (config.motionSource == 0 && config.gnssSource == 0)) {
        return;
    }
    config.motionSource = 0;
    config.gnssSource = 0;
    (void)state_service_->MutateConfig(config);
}

aidl::floral::device::simulation::SimulationConfig SimulationController::GetConfig() const {
    aidl::floral::device::simulation::SimulationConfig config;
    if (state_service_ != nullptr) {
        (void)state_service_->getConfig(&config);
    }
    return config;
}

std::vector<aidl::floral::device::simulation::SensorDescriptor>
SimulationController::GetSensorCatalog() const {
    std::vector<aidl::floral::device::simulation::SensorDescriptor> catalog;
    if (state_service_ != nullptr) {
        (void)state_service_->getSensorCatalog(&catalog);
    }
    return catalog;
}

aidl::floral::device::simulation::SensorSnapshot SimulationController::GetSensorSnapshot() const {
    aidl::floral::device::simulation::SensorSnapshot snapshot;
    if (state_service_ != nullptr) {
        (void)state_service_->getSensorSnapshot(&snapshot);
    }
    return snapshot;
}

aidl::floral::device::simulation::GnssSnapshot SimulationController::GetGnssSnapshot() const {
    aidl::floral::device::simulation::GnssSnapshot snapshot;
    if (state_service_ != nullptr) {
        (void)state_service_->getGnssSnapshot(&snapshot);
    }
    return snapshot;
}

}  // namespace floral::device::simulation
