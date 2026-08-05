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

#include "floral/device/simulation/SimulationControlProtocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace floral::device::simulation {
namespace {

constexpr size_t kMotionConfigRequestSize = 24;
constexpr size_t kEnvironmentConfigRequestSize = 24;
constexpr size_t kGnssConfigRequestSize = 56;
constexpr size_t kBatchHeaderSize = 8;
constexpr size_t kExternalPoseInputRecordSize = 56;
constexpr size_t kExternalGnssInputRecordSize = 56;
constexpr uint16_t kMaximumExternalBatchRecords = 512;
constexpr uint64_t kMaximumPoseAgeNs = 2'000'000'000;
constexpr uint64_t kMaximumGnssAgeNs = 10'000'000'000;

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

uint16_t ReadUint16(const uint8_t* input) {
    return static_cast<uint16_t>((static_cast<uint16_t>(input[0]) << 8) | input[1]);
}

uint32_t ReadUint32(const uint8_t* input) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[index];
    }
    return value;
}

uint64_t ReadUint64(const uint8_t* input) {
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[index];
    }
    return value;
}

float ReadFloat(const uint8_t* input) {
    const uint32_t bits = ReadUint32(input);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double ReadDouble(const uint8_t* input) {
    const uint64_t bits = ReadUint64(input);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void WriteUint16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value >> 8);
    output[1] = static_cast<uint8_t>(value);
}

void WriteUint32(uint8_t* output, uint32_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        output[index] = static_cast<uint8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

void WriteUint64(uint8_t* output, uint64_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        output[index] = static_cast<uint8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

void WriteFloat(uint8_t* output, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    WriteUint32(output, bits);
}

void WriteDouble(uint8_t* output, double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    WriteUint64(output, bits);
}

bool HasVersionedSize(const std::vector<uint8_t>& payload, size_t expected_size,
                      std::string* error) {
    if (payload.size() != expected_size || payload.size() < 4 ||
        ReadUint16(payload.data()) != kSimulationControlVersion ||
        ReadUint16(payload.data() + 2) != expected_size) {
        return SetError(error, "simulation request version or payload size is invalid");
    }
    return true;
}

bool PrepareResponse(control::ControlCommandId command_id, uint32_t request_id, size_t payload_size,
                     control::ControlResponse* response, std::string* error) {
    if (response == nullptr || payload_size > control::kMaximumControlPayloadSize) {
        return SetError(error, "simulation response is null or exceeds the protocol limit");
    }
    response->payload.assign(payload_size, 0);
    response->header.command_id = static_cast<uint16_t>(command_id);
    response->header.route_kind = control::MakeFhc1RouteKind(control::ControlPacketKind::kResponse);
    response->header.request_id = request_id;
    response->header.payload_size = static_cast<uint32_t>(payload_size);
    response->refreshes_authority_lease = false;
    return true;
}

bool ParseBatchHeader(const std::vector<uint8_t>& payload, size_t expected_record_size,
                      uint16_t* count, std::string* error) {
    if (payload.size() < kBatchHeaderSize ||
        ReadUint16(payload.data()) != kSimulationControlVersion ||
        ReadUint16(payload.data() + 2) != expected_record_size ||
        ReadUint16(payload.data() + 6) != 0) {
        return SetError(error, "external state batch header is invalid");
    }
    const uint16_t parsedCount = ReadUint16(payload.data() + 4);
    if (parsedCount == 0 || parsedCount > kMaximumExternalBatchRecords ||
        payload.size() != kBatchHeaderSize + parsedCount * expected_record_size) {
        return SetError(error, "external state batch count or size is invalid");
    }
    *count = parsedCount;
    return true;
}

bool IsValidPose(const ExternalStateRecord& record) {
    const float orientationNormSquared = record.orientation_x * record.orientation_x +
                                         record.orientation_y * record.orientation_y +
                                         record.orientation_z * record.orientation_z +
                                         record.orientation_w * record.orientation_w;
    return std::isfinite(orientationNormSquared) && orientationNormSquared > 1.0e-12f &&
           std::isfinite(record.linear_acceleration_x) &&
           std::isfinite(record.linear_acceleration_y) &&
           std::isfinite(record.linear_acceleration_z) &&
           std::isfinite(record.angular_velocity_x) && std::isfinite(record.angular_velocity_y) &&
           std::isfinite(record.angular_velocity_z);
}

bool IsValidGnss(const ExternalStateRecord& record) {
    return std::isfinite(record.latitude_degrees) && record.latitude_degrees >= -90.0 &&
           record.latitude_degrees <= 90.0 && std::isfinite(record.longitude_degrees) &&
           record.longitude_degrees >= -180.0 && record.longitude_degrees <= 180.0 &&
           std::isfinite(record.altitude_meters) && std::isfinite(record.ground_speed_mps) &&
           record.ground_speed_mps >= 0.0f && std::isfinite(record.bearing_degrees) &&
           record.bearing_degrees >= 0.0f && record.bearing_degrees < 360.0f &&
           std::isfinite(record.horizontal_accuracy_meters) &&
           record.horizontal_accuracy_meters >= 0.0f &&
           std::isfinite(record.vertical_accuracy_meters) &&
           record.vertical_accuracy_meters >= 0.0f;
}

}  // namespace

bool ParseMotionConfigRequest(const std::vector<uint8_t>& payload, MotionConfigRequest* request,
                              std::string* error) {
    if (request == nullptr || !HasVersionedSize(payload, kMotionConfigRequestSize, error) ||
        ReadUint64(payload.data() + 16) != 0) {
        return SetError(error, "motion configuration request is invalid");
    }
    request->source = ReadUint32(payload.data() + 4);
    request->profile = ReadUint32(payload.data() + 8);
    request->transition_duration_ms = ReadUint32(payload.data() + 12);
    return true;
}

bool ParseEnvironmentConfigRequest(const std::vector<uint8_t>& payload,
                                   EnvironmentConfigRequest* request, std::string* error) {
    if (request == nullptr || !HasVersionedSize(payload, kEnvironmentConfigRequestSize, error) ||
        ReadUint32(payload.data() + 20) != 0) {
        return SetError(error, "environment configuration request is invalid");
    }
    request->light_lux = ReadFloat(payload.data() + 4);
    request->proximity_cm = ReadFloat(payload.data() + 8);
    request->pressure_hpa = ReadFloat(payload.data() + 12);
    request->transition_duration_ms = ReadUint32(payload.data() + 16);
    return true;
}

bool ParseGnssConfigRequest(const std::vector<uint8_t>& payload, GnssConfigRequest* request,
                            std::string* error) {
    if (request == nullptr || !HasVersionedSize(payload, kGnssConfigRequestSize, error) ||
        ReadUint32(payload.data() + 12) != 0 || ReadUint32(payload.data() + 52) != 0) {
        return SetError(error, "GNSS configuration request is invalid");
    }
    request->source = ReadUint32(payload.data() + 4);
    const uint32_t enabled = ReadUint32(payload.data() + 8);
    if (enabled > 1) {
        return SetError(error, "GNSS enabled field is invalid");
    }
    request->enabled = enabled != 0;
    request->latitude_degrees = ReadDouble(payload.data() + 16);
    request->longitude_degrees = ReadDouble(payload.data() + 24);
    request->altitude_meters = ReadDouble(payload.data() + 32);
    request->ground_speed_mps = ReadFloat(payload.data() + 40);
    request->bearing_degrees = ReadFloat(payload.data() + 44);
    request->transition_duration_ms = ReadUint32(payload.data() + 48);
    return true;
}

bool ParseExternalPoseBatch(const std::vector<uint8_t>& payload, int64_t receive_timestamp_ns,
                            std::vector<ExternalStateRecord>* records, std::string* error) {
    uint16_t count = 0;
    if (records == nullptr || receive_timestamp_ns <= 0 ||
        !ParseBatchHeader(payload, kExternalPoseInputRecordSize, &count, error)) {
        return SetError(error, "external pose batch is invalid");
    }
    std::vector<ExternalStateRecord> parsed;
    parsed.reserve(count);
    for (uint16_t index = 0; index < count; ++index) {
        const uint8_t* input =
                payload.data() + kBatchHeaderSize + index * kExternalPoseInputRecordSize;
        const uint64_t ageNs = ReadUint64(input);
        const uint32_t flags = ReadUint32(input + 8);
        if (ageNs > kMaximumPoseAgeNs || (flags & ~1U) != 0 || ReadUint32(input + 12) != 0 ||
            ageNs >= static_cast<uint64_t>(receive_timestamp_ns)) {
            return SetError(error, "external pose record header is invalid");
        }
        ExternalStateRecord record;
        record.flags = kExternalStateHasPose;
        if ((flags & 1U) != 0) {
            record.flags |= kExternalStateDiscontinuity;
        }
        record.timestamp_ns = receive_timestamp_ns - static_cast<int64_t>(ageNs);
        record.orientation_x = ReadFloat(input + 16);
        record.orientation_y = ReadFloat(input + 20);
        record.orientation_z = ReadFloat(input + 24);
        record.orientation_w = ReadFloat(input + 28);
        record.linear_acceleration_x = ReadFloat(input + 32);
        record.linear_acceleration_y = ReadFloat(input + 36);
        record.linear_acceleration_z = ReadFloat(input + 40);
        record.angular_velocity_x = ReadFloat(input + 44);
        record.angular_velocity_y = ReadFloat(input + 48);
        record.angular_velocity_z = ReadFloat(input + 52);
        if (!IsValidPose(record)) {
            return SetError(error, "external pose record contains invalid values");
        }
        parsed.push_back(record);
    }
    *records = std::move(parsed);
    return true;
}

bool ParseExternalGnssBatch(const std::vector<uint8_t>& payload, int64_t receive_timestamp_ns,
                            std::vector<ExternalStateRecord>* records, std::string* error) {
    uint16_t count = 0;
    if (records == nullptr || receive_timestamp_ns <= 0 ||
        !ParseBatchHeader(payload, kExternalGnssInputRecordSize, &count, error)) {
        return SetError(error, "external GNSS batch is invalid");
    }
    std::vector<ExternalStateRecord> parsed;
    parsed.reserve(count);
    for (uint16_t index = 0; index < count; ++index) {
        const uint8_t* input =
                payload.data() + kBatchHeaderSize + index * kExternalGnssInputRecordSize;
        const uint64_t ageNs = ReadUint64(input);
        const uint32_t flags = ReadUint32(input + 8);
        if (ageNs > kMaximumGnssAgeNs || (flags & ~1U) != 0 || ReadUint32(input + 12) != 0 ||
            ageNs >= static_cast<uint64_t>(receive_timestamp_ns)) {
            return SetError(error, "external GNSS record header is invalid");
        }
        ExternalStateRecord record;
        record.flags = kExternalStateHasGnss;
        if ((flags & 1U) != 0) {
            record.flags |= kExternalStateDiscontinuity;
        }
        record.timestamp_ns = receive_timestamp_ns - static_cast<int64_t>(ageNs);
        record.latitude_degrees = ReadDouble(input + 16);
        record.longitude_degrees = ReadDouble(input + 24);
        record.altitude_meters = ReadDouble(input + 32);
        record.ground_speed_mps = ReadFloat(input + 40);
        record.bearing_degrees = ReadFloat(input + 44);
        record.horizontal_accuracy_meters = ReadFloat(input + 48);
        record.vertical_accuracy_meters = ReadFloat(input + 52);
        if (!IsValidGnss(record)) {
            return SetError(error, "external GNSS record contains invalid values");
        }
        parsed.push_back(record);
    }
    *records = std::move(parsed);
    return true;
}

bool SerializeSimulationUpdateResponse(control::ControlCommandId command_id, uint32_t request_id,
                                       const SimulationUpdate& update,
                                       control::ControlResponse* response, std::string* error) {
    if (!PrepareResponse(command_id, request_id, 16, response, error)) {
        return false;
    }
    WriteUint32(response->payload.data(), static_cast<uint32_t>(update.result));
    WriteUint32(response->payload.data() + 4, update.applied_count);
    WriteUint64(response->payload.data() + 8, update.generation);
    response->refreshes_authority_lease = update.result == SimulationUpdateResult::kApplied ||
                                          update.result == SimulationUpdateResult::kUnchanged;
    return true;
}

bool SerializeSensorConfigResponse(uint32_t request_id,
                                   const aidl::floral::device::simulation::SimulationConfig& config,
                                   control::ControlResponse* response, std::string* error) {
    if (!PrepareResponse(control::ControlCommandId::kGetSensorSimulationConfig, request_id, 40,
                         response, error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kSimulationControlVersion);
    WriteUint16(response->payload.data() + 2, 40);
    WriteUint64(response->payload.data() + 8, static_cast<uint64_t>(config.generation));
    WriteUint32(response->payload.data() + 16, static_cast<uint32_t>(config.motionSource));
    WriteUint32(response->payload.data() + 20, static_cast<uint32_t>(config.motionProfile));
    WriteFloat(response->payload.data() + 24, config.targetLightLux);
    WriteFloat(response->payload.data() + 28, config.targetProximityCm);
    WriteFloat(response->payload.data() + 32, config.targetPressureHpa);
    WriteUint32(response->payload.data() + 36, static_cast<uint32_t>(config.transitionDurationMs));
    return true;
}

bool SerializeSensorCatalogResponse(
        uint32_t request_id, uint64_t generation,
        const std::vector<aidl::floral::device::simulation::SensorDescriptor>& sensors,
        control::ControlResponse* response, std::string* error) {
    size_t size = 16;
    for (const auto& sensor : sensors) {
        const size_t paddedNameSize = (sensor.name.size() + 3U) & ~3U;
        if (sensor.name.size() > std::numeric_limits<uint16_t>::max() ||
            size > control::kMaximumControlPayloadSize - 24 - paddedNameSize) {
            return SetError(error, "sensor catalog exceeds the protocol limit");
        }
        size += 24 + paddedNameSize;
    }
    if (!PrepareResponse(control::ControlCommandId::kListSensors, request_id, size, response,
                         error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kSimulationControlVersion);
    WriteUint16(response->payload.data() + 2, 16);
    WriteUint32(response->payload.data() + 4, static_cast<uint32_t>(sensors.size()));
    WriteUint64(response->payload.data() + 8, generation);
    size_t offset = 16;
    for (const auto& sensor : sensors) {
        const size_t paddedNameSize = (sensor.name.size() + 3U) & ~3U;
        WriteUint16(response->payload.data() + offset, static_cast<uint16_t>(24 + paddedNameSize));
        WriteUint16(response->payload.data() + offset + 2,
                    static_cast<uint16_t>(sensor.name.size()));
        WriteUint32(response->payload.data() + offset + 4, static_cast<uint32_t>(sensor.handle));
        WriteUint32(response->payload.data() + offset + 8, static_cast<uint32_t>(sensor.type));
        WriteUint32(response->payload.data() + offset + 12, static_cast<uint32_t>(sensor.flags));
        WriteUint32(response->payload.data() + offset + 16,
                    static_cast<uint32_t>(sensor.minDelayUs));
        WriteUint32(response->payload.data() + offset + 20,
                    static_cast<uint32_t>(sensor.maxDelayUs));
        std::copy(sensor.name.begin(), sensor.name.end(), response->payload.begin() + offset + 24);
        offset += 24 + paddedNameSize;
    }
    return true;
}

bool SerializeSensorSnapshotResponse(
        uint32_t request_id, const aidl::floral::device::simulation::SensorSnapshot& snapshot,
        control::ControlResponse* response, std::string* error) {
    size_t size = 24;
    for (const auto& reading : snapshot.readings) {
        if (reading.values.size() > 16 || size > control::kMaximumControlPayloadSize - 24 ||
            reading.values.size() * sizeof(float) >
                    control::kMaximumControlPayloadSize - size - 24) {
            return SetError(error, "sensor snapshot exceeds the protocol limit");
        }
        size += 24 + reading.values.size() * sizeof(float);
    }
    if (!PrepareResponse(control::ControlCommandId::kGetSensorSnapshot, request_id, size, response,
                         error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kSimulationControlVersion);
    WriteUint16(response->payload.data() + 2, 24);
    WriteUint32(response->payload.data() + 4, static_cast<uint32_t>(snapshot.readings.size()));
    WriteUint64(response->payload.data() + 8, static_cast<uint64_t>(snapshot.generation));
    WriteUint64(response->payload.data() + 16, static_cast<uint64_t>(snapshot.timestampNs));
    size_t offset = 24;
    for (const auto& reading : snapshot.readings) {
        const size_t recordSize = 24 + reading.values.size() * sizeof(float);
        WriteUint16(response->payload.data() + offset, static_cast<uint16_t>(recordSize));
        WriteUint16(response->payload.data() + offset + 2,
                    static_cast<uint16_t>(reading.values.size()));
        WriteUint32(response->payload.data() + offset + 4, static_cast<uint32_t>(reading.handle));
        WriteUint32(response->payload.data() + offset + 8, static_cast<uint32_t>(reading.type));
        WriteUint64(response->payload.data() + offset + 16,
                    static_cast<uint64_t>(reading.stepCount));
        for (size_t index = 0; index < reading.values.size(); ++index) {
            WriteFloat(response->payload.data() + offset + 24 + index * sizeof(float),
                       reading.values[index]);
        }
        offset += recordSize;
    }
    return true;
}

bool SerializeGnssConfigResponse(uint32_t request_id,
                                 const aidl::floral::device::simulation::SimulationConfig& config,
                                 control::ControlResponse* response, std::string* error) {
    if (!PrepareResponse(control::ControlCommandId::kGetGnssConfig, request_id, 64, response,
                         error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kSimulationControlVersion);
    WriteUint16(response->payload.data() + 2, 64);
    WriteUint64(response->payload.data() + 8, static_cast<uint64_t>(config.generation));
    WriteUint32(response->payload.data() + 16, static_cast<uint32_t>(config.gnssSource));
    WriteUint32(response->payload.data() + 20, config.gnssEnabled ? 1U : 0U);
    WriteDouble(response->payload.data() + 24, config.anchorLatitudeDegrees);
    WriteDouble(response->payload.data() + 32, config.anchorLongitudeDegrees);
    WriteDouble(response->payload.data() + 40, config.anchorAltitudeMeters);
    WriteFloat(response->payload.data() + 48, config.groundSpeedMps);
    WriteFloat(response->payload.data() + 52, config.bearingDegrees);
    WriteUint32(response->payload.data() + 56, static_cast<uint32_t>(config.transitionDurationMs));
    return true;
}

bool SerializeGnssCapabilitiesResponse(uint32_t request_id, control::ControlResponse* response,
                                       std::string* error) {
    if (!PrepareResponse(control::ControlCommandId::kGetGnssCapabilities, request_id, 16, response,
                         error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kSimulationControlVersion);
    WriteUint16(response->payload.data() + 2, 16);
    WriteUint64(response->payload.data() + 8,
                kGnssCapabilityLocation | kGnssCapabilitySatelliteStatus | kGnssCapabilityNmea |
                        kGnssCapabilityMultipleConstellations | kGnssCapabilityExternalTruthStream);
    return true;
}

bool SerializeGnssSnapshotResponse(uint32_t request_id,
                                   const aidl::floral::device::simulation::GnssSnapshot& snapshot,
                                   control::ControlResponse* response, std::string* error) {
    constexpr size_t kHeaderSize = 88;
    constexpr size_t kSatelliteRecordSize = 24;
    const size_t size = kHeaderSize + snapshot.satellites.size() * kSatelliteRecordSize;
    if (!PrepareResponse(control::ControlCommandId::kGetGnssSnapshot, request_id, size, response,
                         error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kSimulationControlVersion);
    WriteUint16(response->payload.data() + 2, kHeaderSize);
    WriteUint32(response->payload.data() + 4, static_cast<uint32_t>(snapshot.satellites.size()));
    WriteUint64(response->payload.data() + 8, static_cast<uint64_t>(snapshot.generation));
    WriteUint64(response->payload.data() + 16, static_cast<uint64_t>(snapshot.elapsedRealtimeNs));
    WriteUint64(response->payload.data() + 24, static_cast<uint64_t>(snapshot.utcTimeMs));
    WriteUint32(response->payload.data() + 32, snapshot.hasFix ? 1U : 0U);
    WriteDouble(response->payload.data() + 40, snapshot.latitudeDegrees);
    WriteDouble(response->payload.data() + 48, snapshot.longitudeDegrees);
    WriteDouble(response->payload.data() + 56, snapshot.altitudeMeters);
    WriteFloat(response->payload.data() + 64, snapshot.groundSpeedMps);
    WriteFloat(response->payload.data() + 68, snapshot.bearingDegrees);
    WriteFloat(response->payload.data() + 72, snapshot.horizontalAccuracyMeters);
    WriteFloat(response->payload.data() + 76, snapshot.verticalAccuracyMeters);
    WriteFloat(response->payload.data() + 80, snapshot.speedAccuracyMps);
    WriteFloat(response->payload.data() + 84, snapshot.bearingAccuracyDegrees);
    size_t offset = kHeaderSize;
    for (const auto& satellite : snapshot.satellites) {
        WriteUint16(response->payload.data() + offset, kSatelliteRecordSize);
        WriteUint16(response->payload.data() + offset + 2, satellite.usedInFix ? 1U : 0U);
        WriteUint32(response->payload.data() + offset + 4, static_cast<uint32_t>(satellite.svid));
        WriteUint32(response->payload.data() + offset + 8,
                    static_cast<uint32_t>(satellite.constellation));
        WriteFloat(response->payload.data() + offset + 12, satellite.cn0DbHz);
        WriteFloat(response->payload.data() + offset + 16, satellite.elevationDegrees);
        WriteFloat(response->payload.data() + offset + 20, satellite.azimuthDegrees);
        offset += kSatelliteRecordSize;
    }
    return true;
}

}  // namespace floral::device::simulation
