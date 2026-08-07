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

#include "floral/device/wifi/WifiControlProtocol.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace floral::device::wifi {
namespace {

void WriteUint16(uint8_t *output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value >> 8);
    output[1] = static_cast<uint8_t>(value);
}

void WriteUint32(uint8_t *output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value >> 24);
    output[1] = static_cast<uint8_t>(value >> 16);
    output[2] = static_cast<uint8_t>(value >> 8);
    output[3] = static_cast<uint8_t>(value);
}

void WriteUint64(uint8_t *output, uint64_t value) {
    WriteUint32(output, static_cast<uint32_t>(value >> 32));
    WriteUint32(output + 4, static_cast<uint32_t>(value));
}

uint32_t ReadUint32(const uint8_t *input) {
    return (static_cast<uint32_t>(input[0]) << 24) |
           (static_cast<uint32_t>(input[1]) << 16) |
           (static_cast<uint32_t>(input[2]) << 8) |
           static_cast<uint32_t>(input[3]);
}

void WriteAccessPoint(uint8_t *output, uint64_t identity,
                      const std::string &ssid, uint8_t bssid_suffix) {
    WriteUint64(output, identity);
    WriteUint16(output + 8, static_cast<uint16_t>(ssid.size()));
    WriteUint16(output + 10, 1);
    const uint8_t bssid[] = {0x02, 0x00, 0x00, 0x12, 0x00, bssid_suffix};
    std::copy(std::begin(bssid), std::end(bssid), output + 12);
    WriteUint32(output + 20, static_cast<uint32_t>(-50));
    WriteUint32(output + 24, 5180);
    WriteUint32(output + 28, 80);
    WriteUint32(output + 32, 866);
    std::copy(ssid.begin(), ssid.end(), output + 36);
}

TEST(WifiControlProtocolTest, ParsesEnabledRequest) {
    std::vector<uint8_t> payload(16, 0);
    WriteUint16(payload.data(), kWifiControlVersion);
    WriteUint16(payload.data() + 2, payload.size());
    WriteUint32(payload.data() + 4, 1);
    WriteUint32(payload.data() + 8, 30'000);
    EnabledRequest request;
    std::string error;

    ASSERT_TRUE(ParseEnabledRequest(payload, &request, &error)) << error;
    EXPECT_TRUE(request.enabled);
    EXPECT_EQ(30'000, request.lease_duration_ms);
}

TEST(WifiControlProtocolTest, ParsesMultipleAccessPoints) {
    std::vector<uint8_t> payload(16 + 2 * 72, 0);
    WriteUint16(payload.data(), kWifiControlVersion);
    WriteUint16(payload.data() + 2, payload.size());
    WriteUint32(payload.data() + 4, 2);
    WriteUint32(payload.data() + 8, 20'000);
    WriteAccessPoint(payload.data() + 16, 1, "Floral", 1);
    WriteAccessPoint(payload.data() + 88, 2, "Guest", 2);
    std::vector<aidl::floral::device::wifi::WifiAccessPoint> access_points;
    int64_t lease_duration_ms = 0;
    std::string error;

    ASSERT_TRUE(ParseAccessPointsRequest(payload, &access_points,
                                         &lease_duration_ms, &error))
        << error;
    ASSERT_EQ(2U, access_points.size());
    EXPECT_EQ("Floral", access_points[0].ssid);
    EXPECT_EQ("02:00:00:12:00:02", access_points[1].bssid);
    EXPECT_EQ(20'000, lease_duration_ms);
}

TEST(WifiControlProtocolTest, RejectsDuplicateAccessPointIdentity) {
    std::vector<uint8_t> payload(16 + 2 * 72, 0);
    WriteUint16(payload.data(), kWifiControlVersion);
    WriteUint16(payload.data() + 2, payload.size());
    WriteUint32(payload.data() + 4, 2);
    WriteUint32(payload.data() + 8, 20'000);
    WriteAccessPoint(payload.data() + 16, 1, "Floral", 1);
    WriteAccessPoint(payload.data() + 88, 1, "Guest", 2);
    std::vector<aidl::floral::device::wifi::WifiAccessPoint> access_points;
    int64_t lease_duration_ms = 0;

    EXPECT_FALSE(ParseAccessPointsRequest(payload, &access_points,
                                          &lease_duration_ms, nullptr));
}

TEST(WifiControlProtocolTest, ParsesSampleBatch) {
    std::vector<uint8_t> payload(16 + 2 * 32, 0);
    WriteUint16(payload.data(), kWifiControlVersion);
    WriteUint16(payload.data() + 2, payload.size());
    WriteUint32(payload.data() + 4, 2);
    WriteUint32(payload.data() + 8, 5'000);
    for (size_t index = 0; index < 2; ++index) {
        uint8_t *record = payload.data() + 16 + index * 32;
        WriteUint64(record, 1'000'000'000 + index * 20'000'000);
        WriteUint64(record + 8, 1);
        WriteUint32(record + 16, static_cast<uint32_t>(-50 - index));
        WriteUint32(record + 20, 5180);
        WriteUint32(record + 24, 866);
    }
    std::vector<aidl::floral::device::wifi::WifiSample> samples;
    int64_t lease_duration_ms = 0;
    std::string error;

    ASSERT_TRUE(ParseSamplesRequest(payload, &samples, &lease_duration_ms,
                                    &error))
        << error;
    ASSERT_EQ(2U, samples.size());
    EXPECT_EQ(-51, samples[1].rssiDbm);
}

TEST(WifiControlProtocolTest, SerializesDisconnectedSnapshot) {
    aidl::floral::device::wifi::WifiSnapshot snapshot;
    snapshot.generation = 7;
    snapshot.timestampNs = 9;
    snapshot.enabled = true;
    snapshot.ssid = "";
    snapshot.bssid = "";
    snapshot.rssiDbm = -127;
    control::ControlResponse response;
    std::string error;

    ASSERT_TRUE(SerializeWifiSnapshotResponse(42, snapshot, &response, &error))
        << error;
    EXPECT_EQ(static_cast<uint16_t>(control::ControlCommandId::kGetWifiSnapshot),
              response.header.command_id);
    ASSERT_EQ(96U, response.payload.size());
    EXPECT_EQ(1U, ReadUint32(response.payload.data() + 4));
}

TEST(WifiControlProtocolTest, CapabilitiesDeclareAllTenCommandFamilyFeatures) {
    control::ControlResponse response;
    std::string error;

    ASSERT_TRUE(SerializeWifiCapabilitiesResponse(7, &response, &error))
        << error;
    ASSERT_EQ(32U, response.payload.size());
    EXPECT_EQ(0x3fU, ReadUint32(response.payload.data() + 4));
    EXPECT_EQ(64U, ReadUint32(response.payload.data() + 8));
    EXPECT_EQ(256U, ReadUint32(response.payload.data() + 12));
}

}  // namespace
}  // namespace floral::device::wifi
