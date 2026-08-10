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

#include "floral/device/radio/RadioControlProtocol.h"

#include <gtest/gtest.h>

#include <limits>

namespace floral::device::radio {
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

void WriteUint64(uint8_t* output, uint64_t value) {
    WriteUint32(output, static_cast<uint32_t>(value >> 32));
    WriteUint32(output + 4, static_cast<uint32_t>(value));
}

uint32_t ReadUint32(const uint8_t* input) {
    return (static_cast<uint32_t>(input[0]) << 24) | (static_cast<uint32_t>(input[1]) << 16) |
           (static_cast<uint32_t>(input[2]) << 8) | static_cast<uint32_t>(input[3]);
}

TEST(RadioControlProtocolTest, ParsesRegistrationRequest) {
    std::vector<uint8_t> payload(24, 0);
    WriteUint16(payload.data(), kRadioControlVersion);
    WriteUint16(payload.data() + 2, payload.size());
    WriteUint32(payload.data() + 4, 1);
    WriteUint32(payload.data() + 8, 0);
    WriteUint32(payload.data() + 12, 3);
    WriteUint32(payload.data() + 16, 30'000);

    aidl::floral::device::radio::RadioRegistrationControl request;
    std::string error;
    ASSERT_TRUE(ParseRegistrationRequest(payload, &request, &error)) << error;
    EXPECT_EQ(request.voiceRegistration, 1);
    EXPECT_EQ(request.dataRegistration, 0);
    EXPECT_EQ(request.technology, 3);
    EXPECT_EQ(request.leaseDurationMs, 30'000);
}

TEST(RadioControlProtocolTest, RejectsCellsWithoutServingCell) {
    constexpr size_t kHeaderSize = 16;
    constexpr size_t kRecordSize = 60;
    std::vector<uint8_t> payload(kHeaderSize + kRecordSize, 0);
    WriteUint16(payload.data(), kRadioControlVersion);
    WriteUint16(payload.data() + 2, payload.size());
    WriteUint32(payload.data() + 4, 1);
    WriteUint32(payload.data() + 8, 30'000);

    uint8_t* cell = payload.data() + kHeaderSize;
    WriteUint64(cell, 1);
    WriteUint32(cell + 12, 100);
    WriteUint64(cell + 16, 20'001);
    WriteUint32(cell + 24, 12);
    WriteUint32(cell + 28, 1'300);
    WriteUint32(cell + 32, 10'000);
    WriteUint32(cell + 36, static_cast<uint32_t>(-70));
    WriteUint32(cell + 40, static_cast<uint32_t>(-95));
    WriteUint32(cell + 44, static_cast<uint32_t>(-10));
    WriteUint32(cell + 48, 100);
    WriteUint32(cell + 52, 10);
    WriteUint32(cell + 56, 2);

    std::vector<aidl::floral::device::radio::RadioCell> cells;
    int64_t lease_duration_ms = 0;
    std::string error;
    EXPECT_FALSE(ParseCellsRequest(payload, &cells, &lease_duration_ms, &error));
}

TEST(RadioControlProtocolTest, SerializesVariableLengthProfile) {
    aidl::floral::device::radio::RadioProfile profile;
    profile.operatorLongName = "Floral Mobile";
    profile.operatorShortName = "Floral";
    profile.mcc = "001";
    profile.mnc = "01";
    profile.imei = "490154203237518";
    profile.imeisv = "01";
    profile.imsi = "001010123456789";
    profile.iccid = "8901001012345678901";
    profile.msisdn = "+15551234567";
    profile.basebandVersion = "floral-1.0";

    control::ControlResponse response;
    std::string error;
    ASSERT_TRUE(SerializeRadioProfileResponse(7, profile, &response, &error)) << error;
    EXPECT_EQ(response.header.command_id,
              static_cast<uint16_t>(control::ControlCommandId::kGetRadioProfile));
    EXPECT_EQ(response.header.request_id, 7U);
    EXPECT_GT(response.payload.size(), 48U);
    EXPECT_FALSE(response.refreshes_authority_lease);
}

TEST(RadioControlProtocolTest, SerializesFixedSnapshot) {
    aidl::floral::device::radio::RadioSnapshot snapshot;
    snapshot.generation = 4;
    snapshot.timestampNs = 8;
    snapshot.radioOn = true;
    snapshot.simState = 1;
    snapshot.voiceRegistration = 1;
    snapshot.technology = 3;
    snapshot.signal.rssiDbm = -70;
    snapshot.signal.rsrpDbm = -95;
    snapshot.signal.rsrqDb = -10;
    snapshot.signal.rssnrTenthDb = 100;
    snapshot.signal.cqi = 10;
    snapshot.signal.timingAdvance = 2;

    control::ControlResponse response;
    std::string error;
    ASSERT_TRUE(SerializeRadioSnapshotResponse(9, snapshot, &response, &error)) << error;
    EXPECT_EQ(response.header.command_id,
              static_cast<uint16_t>(control::ControlCommandId::kGetRadioSnapshot));
    EXPECT_EQ(response.header.request_id, 9U);
    EXPECT_EQ(response.payload.size(), 80U);
    EXPECT_FALSE(response.refreshes_authority_lease);
}

TEST(RadioControlProtocolTest, CapabilitiesDoNotClaimProfilePersistence) {
    control::ControlResponse response;
    std::string error;
    ASSERT_TRUE(SerializeRadioCapabilitiesResponse(10, &response, &error)) << error;
    EXPECT_EQ(ReadUint32(response.payload.data() + 4), 0x0dU);
}

TEST(RadioControlProtocolTest, SmsListKeepsNewestEventsWithinPayloadLimit) {
    std::vector<aidl::floral::device::radio::RadioSmsEvent> events(64);
    for (size_t index = 0; index < events.size(); ++index) {
        events[index].sequence = index + 1;
        events[index].incoming = true;
        events[index].timestampNs = index;
        events[index].address = "+15551234567";
        events[index].body.assign(1'024, 'a');
    }

    control::ControlResponse response;
    std::string error;
    ASSERT_TRUE(SerializeRadioSmsEventsResponse(10, events, &response, &error)) << error;
    EXPECT_LE(response.payload.size(), control::kMaximumControlPayloadSize);
    EXPECT_LE(response.payload.size(), std::numeric_limits<uint16_t>::max());
    EXPECT_LT(ReadUint32(response.payload.data() + 4), events.size());
}

}  // namespace
}  // namespace floral::device::radio
