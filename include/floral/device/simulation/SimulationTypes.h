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

#include <cstdint>

namespace floral::device::simulation {

enum class SimulationUpdateResult : uint32_t {
    kApplied = 0,
    kUnchanged = 1,
    kInvalid = 2,
    kNotReady = 3,
};

struct MotionConfigRequest {
    uint32_t source = 0;
    uint32_t profile = 0;
    uint32_t transition_duration_ms = 1000;
};

struct EnvironmentConfigRequest {
    float light_lux = 200.0f;
    float proximity_cm = 5.0f;
    float pressure_hpa = 1013.25f;
    uint32_t transition_duration_ms = 1000;
};

struct GnssConfigRequest {
    uint32_t source = 0;
    bool enabled = false;
    double latitude_degrees = 0.0;
    double longitude_degrees = 0.0;
    double altitude_meters = 0.0;
    float ground_speed_mps = 0.0f;
    float bearing_degrees = 0.0f;
    uint32_t transition_duration_ms = 1000;
};

struct SimulationUpdate {
    SimulationUpdateResult result = SimulationUpdateResult::kInvalid;
    uint32_t applied_count = 0;
    uint64_t generation = 0;
};

enum class StateSource : int32_t {
    kAutonomous = 0,
    kExternalStream = 1,
};

enum class MotionProfile : int32_t {
    kStationary = 0,
    kWalking = 1,
    kRunning = 2,
    kVehicle = 3,
};

// This is the validated, implementation-facing representation shared by all
// simulation HALs. The AIDL type remains the external service contract.
struct SimulationConfig {
    uint64_t generation = 1;
    StateSource motion_source = StateSource::kAutonomous;
    StateSource gnss_source = StateSource::kAutonomous;
    MotionProfile motion_profile = MotionProfile::kStationary;
    bool gnss_enabled = true;
    float target_light_lux = 200.0f;
    float target_proximity_cm = 5.0f;
    float target_pressure_hpa = 1013.25f;
    double anchor_latitude_degrees = 35.681236;
    double anchor_longitude_degrees = 139.767125;
    double anchor_altitude_meters = 20.0;
    float ground_speed_mps = 0.0f;
    float bearing_degrees = 0.0f;
    int32_t transition_duration_ms = 1000;
};

}  // namespace floral::device::simulation
