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

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace floral::device::simulation {

constexpr uint32_t kExternalStateRecordMagic = 0x46535331;  // "FSS1"
constexpr uint16_t kExternalStateRecordVersion = 1;
constexpr size_t kExternalStateRecordSize = 112;
constexpr size_t kExternalStateFmqCapacity = 256;

enum ExternalStateRecordFlag : uint32_t {
    kExternalStateHasPose = 1U << 0,
    kExternalStateHasGnss = 1U << 1,
    kExternalStateDiscontinuity = 1U << 2,
};

constexpr uint32_t kKnownExternalStateFlags =
        kExternalStateHasPose | kExternalStateHasGnss | kExternalStateDiscontinuity;

struct ExternalStateRecord {
    uint64_t generation = 0;
    uint32_t flags = 0;
    int64_t timestamp_ns = 0;
    float orientation_x = 0.0f;
    float orientation_y = 0.0f;
    float orientation_z = 0.0f;
    float orientation_w = 1.0f;
    float linear_acceleration_x = 0.0f;
    float linear_acceleration_y = 0.0f;
    float linear_acceleration_z = 0.0f;
    float angular_velocity_x = 0.0f;
    float angular_velocity_y = 0.0f;
    float angular_velocity_z = 0.0f;
    double latitude_degrees = 0.0;
    double longitude_degrees = 0.0;
    double altitude_meters = 0.0;
    float ground_speed_mps = 0.0f;
    float bearing_degrees = 0.0f;
    float horizontal_accuracy_meters = 0.0f;
    float vertical_accuracy_meters = 0.0f;
};

using SerializedExternalStateRecord = std::array<int8_t, kExternalStateRecordSize>;

bool SerializeExternalStateRecord(const ExternalStateRecord& record,
                                  SerializedExternalStateRecord* output, std::string* error);

}  // namespace floral::device::simulation
