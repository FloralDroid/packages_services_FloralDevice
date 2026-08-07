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

#include "floral/device/simulation/SimulationControlHandler.h"
#include "floral/device/simulation/SimulationController.h"
#include "floral/device/simulation/SimulationStateService.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace floral::device::simulation {
namespace {

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

uint32_t ReadUint32(const uint8_t* input) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[index];
    }
    return value;
}

control::ControlRequest MakeQuery(control::ControlCommandId command, uint32_t request_id) {
    control::ControlRequest request;
    request.header.command_id = static_cast<uint16_t>(command);
    request.header.route_kind = control::MakeFhc1RouteKind(control::ControlPacketKind::kRequest);
    request.header.request_id = request_id;
    return request;
}

TEST(SimulationControlProtocolTest, ParsesVersionedConfigurationRequests) {
    std::vector<uint8_t> motion(24, 0);
    WriteUint16(motion.data(), kSimulationControlVersion);
    WriteUint16(motion.data() + 2, motion.size());
    WriteUint32(motion.data() + 4, 1);
    WriteUint32(motion.data() + 8, 2);
    WriteUint32(motion.data() + 12, 750);
    MotionConfigRequest parsedMotion;
    std::string error;
    ASSERT_TRUE(ParseMotionConfigRequest(motion, &parsedMotion, &error)) << error;
    EXPECT_EQ(parsedMotion.source, 1u);
    EXPECT_EQ(parsedMotion.profile, 2u);
    EXPECT_EQ(parsedMotion.transition_duration_ms, 750u);

    std::vector<uint8_t> environment(24, 0);
    WriteUint16(environment.data(), kSimulationControlVersion);
    WriteUint16(environment.data() + 2, environment.size());
    WriteFloat(environment.data() + 4, 480.0f);
    WriteFloat(environment.data() + 8, 1.5f);
    WriteFloat(environment.data() + 12, 1001.25f);
    WriteUint32(environment.data() + 16, 1200);
    EnvironmentConfigRequest parsedEnvironment;
    ASSERT_TRUE(ParseEnvironmentConfigRequest(environment, &parsedEnvironment, &error)) << error;
    EXPECT_FLOAT_EQ(parsedEnvironment.light_lux, 480.0f);
    EXPECT_FLOAT_EQ(parsedEnvironment.proximity_cm, 1.5f);
    EXPECT_FLOAT_EQ(parsedEnvironment.pressure_hpa, 1001.25f);
    EXPECT_EQ(parsedEnvironment.transition_duration_ms, 1200u);

    std::vector<uint8_t> gnss(56, 0);
    WriteUint16(gnss.data(), kSimulationControlVersion);
    WriteUint16(gnss.data() + 2, gnss.size());
    WriteUint32(gnss.data() + 4, 1);
    WriteUint32(gnss.data() + 8, 1);
    WriteDouble(gnss.data() + 16, 31.2304);
    WriteDouble(gnss.data() + 24, 121.4737);
    WriteDouble(gnss.data() + 32, 8.0);
    WriteFloat(gnss.data() + 40, 12.0f);
    WriteFloat(gnss.data() + 44, 90.0f);
    WriteUint32(gnss.data() + 48, 500);
    GnssConfigRequest parsedGnss;
    ASSERT_TRUE(ParseGnssConfigRequest(gnss, &parsedGnss, &error)) << error;
    EXPECT_EQ(parsedGnss.source, 1u);
    EXPECT_TRUE(parsedGnss.enabled);
    EXPECT_DOUBLE_EQ(parsedGnss.latitude_degrees, 31.2304);
    EXPECT_DOUBLE_EQ(parsedGnss.longitude_degrees, 121.4737);
    EXPECT_FLOAT_EQ(parsedGnss.bearing_degrees, 90.0f);
}

TEST(SimulationControlProtocolTest, ConvertsExternalBatchAgeToGuestBootTime) {
    std::vector<uint8_t> pose(8 + 56, 0);
    WriteUint16(pose.data(), kSimulationControlVersion);
    WriteUint16(pose.data() + 2, 56);
    WriteUint16(pose.data() + 4, 1);
    WriteUint64(pose.data() + 8, 250);
    WriteUint32(pose.data() + 16, 1);
    WriteFloat(pose.data() + 36, 1.0f);
    WriteFloat(pose.data() + 40, 0.5f);
    WriteFloat(pose.data() + 52, 0.25f);

    std::vector<ExternalStateRecord> records;
    std::string error;
    ASSERT_TRUE(ParseExternalPoseBatch(pose, 1000, &records, &error)) << error;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].timestamp_ns, 750);
    EXPECT_EQ(records[0].flags, kExternalStateHasPose | kExternalStateDiscontinuity);
    EXPECT_FLOAT_EQ(records[0].orientation_w, 1.0f);
    EXPECT_FLOAT_EQ(records[0].linear_acceleration_x, 0.5f);
    EXPECT_FLOAT_EQ(records[0].angular_velocity_x, 0.25f);

    std::vector<uint8_t> gnss(8 + 56, 0);
    WriteUint16(gnss.data(), kSimulationControlVersion);
    WriteUint16(gnss.data() + 2, 56);
    WriteUint16(gnss.data() + 4, 1);
    WriteUint64(gnss.data() + 8, 400);
    WriteDouble(gnss.data() + 24, 35.681236);
    WriteDouble(gnss.data() + 32, 139.767125);
    WriteDouble(gnss.data() + 40, 20.0);
    WriteFloat(gnss.data() + 48, 3.0f);
    WriteFloat(gnss.data() + 52, 180.0f);
    WriteFloat(gnss.data() + 56, 2.5f);
    WriteFloat(gnss.data() + 60, 4.0f);
    ASSERT_TRUE(ParseExternalGnssBatch(gnss, 2000, &records, &error)) << error;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].timestamp_ns, 1600);
    EXPECT_EQ(records[0].flags, kExternalStateHasGnss);
    EXPECT_DOUBLE_EQ(records[0].latitude_degrees, 35.681236);
    EXPECT_FLOAT_EQ(records[0].horizontal_accuracy_meters, 2.5f);
}

TEST(SimulationControlProtocolTest, RejectsInvalidExternalStateValues) {
    std::vector<uint8_t> pose(8 + 56, 0);
    WriteUint16(pose.data(), kSimulationControlVersion);
    WriteUint16(pose.data() + 2, 56);
    WriteUint16(pose.data() + 4, 1);
    WriteUint64(pose.data() + 8, 1);
    std::vector<ExternalStateRecord> records;
    EXPECT_FALSE(ParseExternalPoseBatch(pose, 1000, &records, nullptr));

    std::vector<uint8_t> gnss(8 + 56, 0);
    WriteUint16(gnss.data(), kSimulationControlVersion);
    WriteUint16(gnss.data() + 2, 56);
    WriteUint16(gnss.data() + 4, 1);
    WriteUint64(gnss.data() + 8, 1);
    WriteDouble(gnss.data() + 24, 91.0);
    EXPECT_FALSE(ParseExternalGnssBatch(gnss, 1000, &records, nullptr));
}

TEST(SimulationControlProtocolTest, HandlerServesEverySensorAndGnssQuery) {
    auto stateService = ndk::SharedRefBase::make<SimulationStateService>();
    auto controller = std::make_shared<SimulationController>(stateService);
    SimulationControlHandler handler(controller);
    const std::array commands{
            control::ControlCommandId::kGetSensorSimulationConfig,
            control::ControlCommandId::kListSensors,
            control::ControlCommandId::kGetSensorSnapshot,
            control::ControlCommandId::kGetGnssConfig,
            control::ControlCommandId::kGetGnssCapabilities,
            control::ControlCommandId::kGetGnssSnapshot,
    };

    for (size_t index = 0; index < commands.size(); ++index) {
        const uint32_t requestId = static_cast<uint32_t>(index + 1);
        const control::ControlRequest request = MakeQuery(commands[index], requestId);
        control::ControlResponse response;
        std::string error;
        ASSERT_TRUE(handler.Handle(request, &response, &error)) << error;
        EXPECT_EQ(response.header.command_id, static_cast<uint16_t>(commands[index]));
        EXPECT_EQ(response.header.request_id, requestId);
        EXPECT_EQ(response.header.route_kind,
                  control::MakeFhc1RouteKind(control::ControlPacketKind::kResponse));
        EXPECT_EQ(response.header.payload_size, response.payload.size());
        EXPECT_FALSE(response.refreshes_authority_lease);
    }

    control::ControlResponse catalogResponse;
    ASSERT_TRUE(handler.Handle(MakeQuery(control::ControlCommandId::kListSensors, 20),
                               &catalogResponse, nullptr));
    ASSERT_GE(catalogResponse.payload.size(), 16u);
    EXPECT_EQ(ReadUint32(catalogResponse.payload.data() + 4), 0u);
}

TEST(SimulationControlProtocolTest, FullExternalQueueMarksNextDeliveredRecordDiscontinuous) {
    auto stateService = ndk::SharedRefBase::make<SimulationStateService>();
    auto controller = std::make_shared<SimulationController>(stateService);
    aidl::android::hardware::common::fmq::MQDescriptor<
            int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>
            descriptor;
    ASSERT_TRUE(stateService->openExternalStateStream(&descriptor).isOk());
    ExternalStateFmq queue(descriptor, false);
    ASSERT_TRUE(queue.isValid());

    ExternalStateRecord record;
    record.flags = kExternalStateHasPose;
    std::string error;
    for (size_t index = 0; index < kExternalStateFmqCapacity; ++index) {
        record.timestamp_ns = static_cast<int64_t>(index + 1);
        const SimulationUpdate update = controller->PublishExternalStates({record}, &error);
        ASSERT_EQ(update.result, SimulationUpdateResult::kApplied) << error;
    }

    record.timestamp_ns = static_cast<int64_t>(kExternalStateFmqCapacity + 1);
    ASSERT_EQ(controller->PublishExternalStates({record}, &error).result,
              SimulationUpdateResult::kApplied)
            << error;

    SerializedExternalStateRecord serialized{};
    ASSERT_TRUE(queue.read(serialized.data(), serialized.size()));
    record.timestamp_ns = static_cast<int64_t>(kExternalStateFmqCapacity + 2);
    ASSERT_EQ(controller->PublishExternalStates({record}, &error).result,
              SimulationUpdateResult::kApplied)
            << error;

    for (size_t index = 1; index < kExternalStateFmqCapacity; ++index) {
        ASSERT_TRUE(queue.read(serialized.data(), serialized.size()));
    }
    ASSERT_TRUE(queue.read(serialized.data(), serialized.size()));
    EXPECT_NE(ReadUint32(reinterpret_cast<const uint8_t*>(serialized.data()) + 16) &
                      kExternalStateDiscontinuity,
              0u);
}

TEST(SimulationControlProtocolTest, ExternalStateRecordRoundTripsThroughFss1Codec) {
    ExternalStateRecord input;
    input.generation = 7;
    input.flags = kExternalStateHasPose | kExternalStateHasGnss;
    input.timestamp_ns = 123456789;
    input.orientation_z = 0.25f;
    input.orientation_w = 0.9682458f;
    input.linear_acceleration_x = 1.5f;
    input.angular_velocity_y = -0.125f;
    input.latitude_degrees = 31.2304;
    input.longitude_degrees = 121.4737;
    input.altitude_meters = 12.5;
    input.ground_speed_mps = 4.25f;
    input.bearing_degrees = 87.0f;
    input.horizontal_accuracy_meters = 3.0f;
    input.vertical_accuracy_meters = 5.0f;

    SerializedExternalStateRecord serialized{};
    std::string error;
    ASSERT_TRUE(SerializeExternalStateRecord(input, &serialized, &error)) << error;
    ExternalStateRecord output;
    ASSERT_TRUE(ParseExternalStateRecord(serialized, &output, &error)) << error;
    EXPECT_EQ(output.generation, input.generation);
    EXPECT_EQ(output.flags, input.flags);
    EXPECT_EQ(output.timestamp_ns, input.timestamp_ns);
    EXPECT_FLOAT_EQ(output.orientation_z, input.orientation_z);
    EXPECT_FLOAT_EQ(output.linear_acceleration_x, input.linear_acceleration_x);
    EXPECT_DOUBLE_EQ(output.latitude_degrees, input.latitude_degrees);
    EXPECT_DOUBLE_EQ(output.longitude_degrees, input.longitude_degrees);
    EXPECT_FLOAT_EQ(output.ground_speed_mps, input.ground_speed_mps);
}

}  // namespace
}  // namespace floral::device::simulation
