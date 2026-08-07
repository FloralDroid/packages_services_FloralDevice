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

#include "floral/device/power/PowerControlProtocol.h"

#include <gtest/gtest.h>

namespace floral::device::power {
namespace {

void WriteUint16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value >> 8);
    output[1] = static_cast<uint8_t>(value);
}

void WriteUint32(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value >> 24);
    output[1] = static_cast<uint8_t>(value >> 16);
    output[2] = static_cast<uint8_t>(value >> 8);
    output[3] = static_cast<uint8_t>(value);
}

TEST(PowerControlProtocolTest, ParsesManualIdleRequest) {
    std::vector<uint8_t> payload(24, 0);
    WriteUint16(payload.data(), kPowerControlVersion);
    WriteUint16(payload.data() + 2, payload.size());
    WriteUint32(payload.data() + 4, static_cast<uint32_t>(ManualPowerMode::kIdle));
    WriteUint32(payload.data() + 12, 30'000);

    ManualPowerRequest request;
    std::string error;
    ASSERT_TRUE(ParseManualPowerRequest(payload, &request, &error)) << error;
    EXPECT_EQ(request.mode, ManualPowerMode::kIdle);
    EXPECT_EQ(request.current_ua, 0);
    EXPECT_EQ(request.lease_duration_ms, 30'000);
}

TEST(PowerControlProtocolTest, RejectsIdleWithCurrent) {
    std::vector<uint8_t> payload(24, 0);
    WriteUint16(payload.data(), kPowerControlVersion);
    WriteUint16(payload.data() + 2, payload.size());
    WriteUint32(payload.data() + 4, static_cast<uint32_t>(ManualPowerMode::kIdle));
    WriteUint32(payload.data() + 8, 100'000);
    WriteUint32(payload.data() + 12, 30'000);

    ManualPowerRequest request;
    std::string error;
    EXPECT_FALSE(ParseManualPowerRequest(payload, &request, &error));
}

TEST(PowerControlProtocolTest, SerializesFixedSnapshot) {
    aidl::floral::device::power::PowerSnapshot snapshot;
    snapshot.generation = 7;
    snapshot.timestampNs = 8;
    snapshot.level = 63;
    snapshot.voltageMv = 3870;
    snapshot.currentUa = -400'000;
    snapshot.cpuTemperatureCelsius = 41.5f;

    control::ControlResponse response;
    std::string error;
    ASSERT_TRUE(SerializePowerSnapshotResponse(9, snapshot, &response, &error)) << error;
    EXPECT_EQ(response.header.command_id,
              static_cast<uint16_t>(control::ControlCommandId::kGetPowerSnapshot));
    EXPECT_EQ(response.header.request_id, 9U);
    EXPECT_EQ(response.payload.size(), 96U);
    EXPECT_FALSE(response.refreshes_authority_lease);
}

}  // namespace
}  // namespace floral::device::power
