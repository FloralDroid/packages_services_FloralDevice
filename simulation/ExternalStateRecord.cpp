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

#include "floral/device/simulation/ExternalStateRecord.h"

#include <cmath>
#include <cstring>

namespace floral::device::simulation {
namespace {

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

void WriteUint16(int8_t* output, uint16_t value) {
    output[0] = static_cast<int8_t>(value >> 8);
    output[1] = static_cast<int8_t>(value);
}

void WriteUint32(int8_t* output, uint32_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        output[index] = static_cast<int8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

void WriteUint64(int8_t* output, uint64_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        output[index] = static_cast<int8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

void WriteFloat(int8_t* output, float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    WriteUint32(output, bits);
}

void WriteDouble(int8_t* output, double value) {
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    WriteUint64(output, bits);
}

bool IsFinite(const ExternalStateRecord& record) {
    return std::isfinite(record.orientation_x) && std::isfinite(record.orientation_y) &&
           std::isfinite(record.orientation_z) && std::isfinite(record.orientation_w) &&
           std::isfinite(record.linear_acceleration_x) &&
           std::isfinite(record.linear_acceleration_y) &&
           std::isfinite(record.linear_acceleration_z) &&
           std::isfinite(record.angular_velocity_x) && std::isfinite(record.angular_velocity_y) &&
           std::isfinite(record.angular_velocity_z) && std::isfinite(record.latitude_degrees) &&
           std::isfinite(record.longitude_degrees) && std::isfinite(record.altitude_meters) &&
           std::isfinite(record.ground_speed_mps) && std::isfinite(record.bearing_degrees) &&
           std::isfinite(record.horizontal_accuracy_meters) &&
           std::isfinite(record.vertical_accuracy_meters);
}

}  // namespace

bool SerializeExternalStateRecord(const ExternalStateRecord& record,
                                  SerializedExternalStateRecord* output, std::string* error) {
    if (output == nullptr) {
        return SetError(error, "serialized external state output is null");
    }
    if (record.generation == 0 || record.timestamp_ns <= 0 ||
        (record.flags & ~kKnownExternalStateFlags) != 0 ||
        (record.flags & (kExternalStateHasPose | kExternalStateHasGnss)) == 0 ||
        !IsFinite(record)) {
        return SetError(error, "external state record is invalid");
    }
    if ((record.flags & kExternalStateHasGnss) != 0 &&
        (record.latitude_degrees < -90.0 || record.latitude_degrees > 90.0 ||
         record.longitude_degrees < -180.0 || record.longitude_degrees > 180.0 ||
         record.ground_speed_mps < 0.0f || record.bearing_degrees < 0.0f ||
         record.bearing_degrees >= 360.0f || record.horizontal_accuracy_meters < 0.0f ||
         record.vertical_accuracy_meters < 0.0f)) {
        return SetError(error, "external GNSS state is outside its valid range");
    }

    output->fill(0);
    WriteUint32(output->data(), kExternalStateRecordMagic);
    WriteUint16(output->data() + 4, kExternalStateRecordVersion);
    WriteUint16(output->data() + 6, static_cast<uint16_t>(kExternalStateRecordSize));
    WriteUint64(output->data() + 8, record.generation);
    WriteUint32(output->data() + 16, record.flags);
    WriteUint64(output->data() + 24, static_cast<uint64_t>(record.timestamp_ns));
    WriteFloat(output->data() + 32, record.orientation_x);
    WriteFloat(output->data() + 36, record.orientation_y);
    WriteFloat(output->data() + 40, record.orientation_z);
    WriteFloat(output->data() + 44, record.orientation_w);
    WriteFloat(output->data() + 48, record.linear_acceleration_x);
    WriteFloat(output->data() + 52, record.linear_acceleration_y);
    WriteFloat(output->data() + 56, record.linear_acceleration_z);
    WriteFloat(output->data() + 60, record.angular_velocity_x);
    WriteFloat(output->data() + 64, record.angular_velocity_y);
    WriteFloat(output->data() + 68, record.angular_velocity_z);
    WriteDouble(output->data() + 72, record.latitude_degrees);
    WriteDouble(output->data() + 80, record.longitude_degrees);
    WriteDouble(output->data() + 88, record.altitude_meters);
    WriteFloat(output->data() + 96, record.ground_speed_mps);
    WriteFloat(output->data() + 100, record.bearing_degrees);
    WriteFloat(output->data() + 104, record.horizontal_accuracy_meters);
    WriteFloat(output->data() + 108, record.vertical_accuracy_meters);
    return true;
}

}  // namespace floral::device::simulation
