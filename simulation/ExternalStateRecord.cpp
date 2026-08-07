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

uint16_t ReadUint16(const int8_t* input) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(input);
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}

uint32_t ReadUint32(const int8_t* input) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(input);
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | bytes[index];
    }
    return value;
}

uint64_t ReadUint64(const int8_t* input) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(input);
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | bytes[index];
    }
    return value;
}

float ReadFloat(const int8_t* input) {
    const uint32_t bits = ReadUint32(input);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double ReadDouble(const int8_t* input) {
    const uint64_t bits = ReadUint64(input);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
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

bool ParseExternalStateRecord(const SerializedExternalStateRecord& input,
                              ExternalStateRecord* record, std::string* error) {
    if (record == nullptr) {
        return SetError(error, "parsed external state output is null");
    }
    if (ReadUint32(input.data()) != kExternalStateRecordMagic ||
        ReadUint16(input.data() + 4) != kExternalStateRecordVersion ||
        ReadUint16(input.data() + 6) != kExternalStateRecordSize ||
        ReadUint32(input.data() + 20) != 0) {
        return SetError(error, "external state record header is invalid");
    }

    ExternalStateRecord parsed;
    parsed.generation = ReadUint64(input.data() + 8);
    parsed.flags = ReadUint32(input.data() + 16);
    parsed.timestamp_ns = static_cast<int64_t>(ReadUint64(input.data() + 24));
    parsed.orientation_x = ReadFloat(input.data() + 32);
    parsed.orientation_y = ReadFloat(input.data() + 36);
    parsed.orientation_z = ReadFloat(input.data() + 40);
    parsed.orientation_w = ReadFloat(input.data() + 44);
    parsed.linear_acceleration_x = ReadFloat(input.data() + 48);
    parsed.linear_acceleration_y = ReadFloat(input.data() + 52);
    parsed.linear_acceleration_z = ReadFloat(input.data() + 56);
    parsed.angular_velocity_x = ReadFloat(input.data() + 60);
    parsed.angular_velocity_y = ReadFloat(input.data() + 64);
    parsed.angular_velocity_z = ReadFloat(input.data() + 68);
    parsed.latitude_degrees = ReadDouble(input.data() + 72);
    parsed.longitude_degrees = ReadDouble(input.data() + 80);
    parsed.altitude_meters = ReadDouble(input.data() + 88);
    parsed.ground_speed_mps = ReadFloat(input.data() + 96);
    parsed.bearing_degrees = ReadFloat(input.data() + 100);
    parsed.horizontal_accuracy_meters = ReadFloat(input.data() + 104);
    parsed.vertical_accuracy_meters = ReadFloat(input.data() + 108);
    if (parsed.generation == 0 || parsed.timestamp_ns <= 0 ||
        (parsed.flags & ~kKnownExternalStateFlags) != 0 ||
        (parsed.flags & (kExternalStateHasPose | kExternalStateHasGnss)) == 0 ||
        !IsFinite(parsed)) {
        return SetError(error, "external state record body is invalid");
    }
    if ((parsed.flags & kExternalStateHasGnss) != 0 &&
        (parsed.latitude_degrees < -90.0 || parsed.latitude_degrees > 90.0 ||
         parsed.longitude_degrees < -180.0 || parsed.longitude_degrees > 180.0 ||
         parsed.ground_speed_mps < 0.0f || parsed.bearing_degrees < 0.0f ||
         parsed.bearing_degrees >= 360.0f || parsed.horizontal_accuracy_meters < 0.0f ||
         parsed.vertical_accuracy_meters < 0.0f)) {
        return SetError(error, "external GNSS state is outside its valid range");
    }
    *record = parsed;
    return true;
}

}  // namespace floral::device::simulation
