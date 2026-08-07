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

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <set>

namespace floral::device::wifi {
namespace {

constexpr size_t kEnabledRequestSize = 16;
constexpr size_t kAccessPointsRequestHeaderSize = 16;
constexpr size_t kAccessPointRecordSize = 72;
constexpr size_t kConnectionRequestSize = 16;
constexpr size_t kLinkRequestSize = 32;
constexpr size_t kSamplesRequestHeaderSize = 16;
constexpr size_t kSampleRecordSize = 32;
constexpr size_t kUpdateResponseSize = 24;
constexpr size_t kProfileResponseSize = 48;
constexpr size_t kSnapshotResponseSize = 96;
constexpr size_t kListResponseHeaderSize = 16;
constexpr size_t kCapabilitiesResponseSize = 32;
constexpr size_t kMaximumAccessPoints = 64;
constexpr size_t kMaximumSamples = 256;

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
    return (static_cast<uint32_t>(input[0]) << 24) | (static_cast<uint32_t>(input[1]) << 16) |
           (static_cast<uint32_t>(input[2]) << 8) | static_cast<uint32_t>(input[3]);
}

uint64_t ReadUint64(const uint8_t* input) {
    return (static_cast<uint64_t>(ReadUint32(input)) << 32) | ReadUint32(input + 4);
}

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

bool HasEnvelope(const std::vector<uint8_t>& payload, size_t expected_size) {
    return payload.size() == expected_size && ReadUint16(payload.data()) == kWifiControlVersion &&
           ReadUint16(payload.data() + 2) == expected_size;
}

bool ValidLease(uint32_t lease_duration_ms) {
    return lease_duration_ms > 0 && lease_duration_ms <= kMaximumWifiControlLeaseMs;
}

bool ValidFrequency(int32_t frequency_mhz) {
    return (frequency_mhz >= 2412 && frequency_mhz <= 2484) ||
           (frequency_mhz >= 4900 && frequency_mhz <= 5895) ||
           (frequency_mhz >= 5925 && frequency_mhz <= 7125);
}

bool ValidChannelWidth(int32_t channel_width_mhz) {
    return channel_width_mhz == 20 || channel_width_mhz == 40 ||
           channel_width_mhz == 80 || channel_width_mhz == 160;
}

bool ValidLink(int32_t rssi_dbm, int32_t frequency_mhz, int32_t channel_width_mhz,
               int32_t link_speed_mbps) {
    return rssi_dbm >= -127 && rssi_dbm <= -1 && ValidFrequency(frequency_mhz) &&
           ValidChannelWidth(channel_width_mhz) && link_speed_mbps >= 1 &&
           link_speed_mbps <= 10'000;
}

std::string FormatMacAddress(const uint8_t* input) {
    std::array<char, 18> output{};
    std::snprintf(output.data(), output.size(), "%02x:%02x:%02x:%02x:%02x:%02x", input[0],
                  input[1], input[2], input[3], input[4], input[5]);
    return output.data();
}

int HexDigit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool ParseMacAddress(const std::string& value, uint8_t* output) {
    if (value.size() != 17) {
        return false;
    }
    for (size_t octet = 0; octet < 6; ++octet) {
        const size_t offset = octet * 3;
        const int high = HexDigit(value[offset]);
        const int low = HexDigit(value[offset + 1]);
        if (high < 0 || low < 0 || (octet < 5 && value[offset + 2] != ':')) {
            return false;
        }
        output[octet] = static_cast<uint8_t>((high << 4) | low);
    }
    return (output[0] & 1U) == 0;
}

bool PrepareResponse(control::ControlCommandId command_id, uint32_t request_id, size_t payload_size,
                     control::ControlResponse* response, std::string* error) {
    if (response == nullptr || payload_size > control::kMaximumControlPayloadSize ||
        payload_size > std::numeric_limits<uint16_t>::max()) {
        return SetError(error, "Wi-Fi response output is invalid");
    }
    response->header.command_id = static_cast<uint16_t>(command_id);
    response->header.route_kind = control::MakeFhc1RouteKind(control::ControlPacketKind::kResponse);
    response->header.request_id = request_id;
    response->header.payload_size = static_cast<uint32_t>(payload_size);
    response->payload.assign(payload_size, 0);
    response->refreshes_authority_lease = false;
    return true;
}

bool ParseAccessPointRecord(const uint8_t* input,
                            aidl::floral::device::wifi::WifiAccessPoint* access_point) {
    const uint64_t identity = ReadUint64(input);
    const uint16_t ssid_size = ReadUint16(input + 8);
    const uint16_t security = ReadUint16(input + 10);
    const uint16_t reserved = ReadUint16(input + 18);
    const int32_t rssi_dbm = static_cast<int32_t>(ReadUint32(input + 20));
    const int32_t frequency_mhz = static_cast<int32_t>(ReadUint32(input + 24));
    const int32_t channel_width_mhz = static_cast<int32_t>(ReadUint32(input + 28));
    const int32_t link_speed_mbps = static_cast<int32_t>(ReadUint32(input + 32));
    if (identity == 0 || identity > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        ssid_size == 0 || ssid_size > 32 || security > 2 || reserved != 0 ||
        ReadUint32(input + 68) != 0 ||
        !ValidLink(rssi_dbm, frequency_mhz, channel_width_mhz, link_speed_mbps) ||
        std::find(input + 36, input + 36 + ssid_size, 0) != input + 36 + ssid_size) {
        return false;
    }
    access_point->identity = static_cast<int64_t>(identity);
    access_point->ssid.assign(reinterpret_cast<const char*>(input + 36), ssid_size);
    access_point->bssid = FormatMacAddress(input + 12);
    access_point->security = security;
    access_point->rssiDbm = rssi_dbm;
    access_point->frequencyMhz = frequency_mhz;
    access_point->channelWidthMhz = channel_width_mhz;
    access_point->linkSpeedMbps = link_speed_mbps;
    return (input[12] & 1U) == 0;
}

bool WriteAccessPointRecord(uint8_t* output,
                            const aidl::floral::device::wifi::WifiAccessPoint& access_point) {
    if (access_point.identity <= 0 || access_point.ssid.empty() || access_point.ssid.size() > 32 ||
        access_point.security < 0 || access_point.security > 2 ||
        !ValidLink(access_point.rssiDbm, access_point.frequencyMhz,
                   access_point.channelWidthMhz, access_point.linkSpeedMbps) ||
        !ParseMacAddress(access_point.bssid, output + 12)) {
        return false;
    }
    WriteUint64(output, static_cast<uint64_t>(access_point.identity));
    WriteUint16(output + 8, static_cast<uint16_t>(access_point.ssid.size()));
    WriteUint16(output + 10, static_cast<uint16_t>(access_point.security));
    WriteUint32(output + 20, static_cast<uint32_t>(access_point.rssiDbm));
    WriteUint32(output + 24, static_cast<uint32_t>(access_point.frequencyMhz));
    WriteUint32(output + 28, static_cast<uint32_t>(access_point.channelWidthMhz));
    WriteUint32(output + 32, static_cast<uint32_t>(access_point.linkSpeedMbps));
    std::copy(access_point.ssid.begin(), access_point.ssid.end(), output + 36);
    return true;
}

}  // namespace

bool ParseEnabledRequest(const std::vector<uint8_t>& payload, EnabledRequest* request,
                         std::string* error) {
    if (request == nullptr || !HasEnvelope(payload, kEnabledRequestSize)) {
        return SetError(error, "Wi-Fi enabled request envelope is invalid");
    }
    const uint32_t enabled = ReadUint32(payload.data() + 4);
    const uint32_t lease = ReadUint32(payload.data() + 8);
    if (enabled > 1 || !ValidLease(lease) || ReadUint32(payload.data() + 12) != 0) {
        return SetError(error, "Wi-Fi enabled request contains an invalid field");
    }
    request->enabled = enabled != 0;
    request->lease_duration_ms = lease;
    return true;
}

bool ParseAccessPointsRequest(
        const std::vector<uint8_t>& payload,
        std::vector<aidl::floral::device::wifi::WifiAccessPoint>* access_points,
        int64_t* lease_duration_ms, std::string* error) {
    if (access_points == nullptr || lease_duration_ms == nullptr ||
        payload.size() < kAccessPointsRequestHeaderSize ||
        ReadUint16(payload.data()) != kWifiControlVersion ||
        ReadUint16(payload.data() + 2) != payload.size()) {
        return SetError(error, "Wi-Fi access-point request envelope is invalid");
    }
    const uint32_t count = ReadUint32(payload.data() + 4);
    const uint32_t lease = ReadUint32(payload.data() + 8);
    if (count > kMaximumAccessPoints || !ValidLease(lease) ||
        ReadUint32(payload.data() + 12) != 0 ||
        payload.size() != kAccessPointsRequestHeaderSize + count * kAccessPointRecordSize) {
        return SetError(error, "Wi-Fi access-point request size or count is invalid");
    }
    access_points->clear();
    access_points->reserve(count);
    std::set<int64_t> identities;
    std::set<std::string> bssids;
    for (uint32_t index = 0; index < count; ++index) {
        aidl::floral::device::wifi::WifiAccessPoint access_point;
        const uint8_t* input =
                payload.data() + kAccessPointsRequestHeaderSize + index * kAccessPointRecordSize;
        if (!ParseAccessPointRecord(input, &access_point) ||
            !identities.insert(access_point.identity).second ||
            !bssids.insert(access_point.bssid).second) {
            return SetError(error, "Wi-Fi access-point request contains an invalid record");
        }
        access_points->push_back(std::move(access_point));
    }
    *lease_duration_ms = lease;
    return true;
}

bool ParseConnectionRequest(const std::vector<uint8_t>& payload, ConnectionRequest* request,
                            std::string* error) {
    if (request == nullptr || !HasEnvelope(payload, kConnectionRequestSize)) {
        return SetError(error, "Wi-Fi connection request envelope is invalid");
    }
    const uint64_t access_point_id = ReadUint64(payload.data() + 4);
    const uint32_t lease = ReadUint32(payload.data() + 12);
    if (access_point_id > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        !ValidLease(lease)) {
        return SetError(error, "Wi-Fi connection request contains an invalid field");
    }
    request->access_point_id = static_cast<int64_t>(access_point_id);
    request->lease_duration_ms = lease;
    return true;
}

bool ParseLinkRequest(const std::vector<uint8_t>& payload,
                      aidl::floral::device::wifi::WifiLinkState* link,
                      int64_t* lease_duration_ms, std::string* error) {
    if (link == nullptr || lease_duration_ms == nullptr || !HasEnvelope(payload, kLinkRequestSize)) {
        return SetError(error, "Wi-Fi link request envelope is invalid");
    }
    const uint64_t access_point_id = ReadUint64(payload.data() + 4);
    link->rssiDbm = static_cast<int32_t>(ReadUint32(payload.data() + 12));
    link->frequencyMhz = static_cast<int32_t>(ReadUint32(payload.data() + 16));
    link->channelWidthMhz = static_cast<int32_t>(ReadUint32(payload.data() + 20));
    link->linkSpeedMbps = static_cast<int32_t>(ReadUint32(payload.data() + 24));
    const uint32_t lease = ReadUint32(payload.data() + 28);
    if (access_point_id == 0 ||
        access_point_id > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        !ValidLink(link->rssiDbm, link->frequencyMhz, link->channelWidthMhz, link->linkSpeedMbps) ||
        !ValidLease(lease)) {
        return SetError(error, "Wi-Fi link request contains an invalid field");
    }
    link->accessPointId = static_cast<int64_t>(access_point_id);
    *lease_duration_ms = lease;
    return true;
}

bool ParseSamplesRequest(const std::vector<uint8_t>& payload,
                         std::vector<aidl::floral::device::wifi::WifiSample>* samples,
                         int64_t* lease_duration_ms, std::string* error) {
    if (samples == nullptr || lease_duration_ms == nullptr ||
        payload.size() < kSamplesRequestHeaderSize ||
        ReadUint16(payload.data()) != kWifiControlVersion ||
        ReadUint16(payload.data() + 2) != payload.size()) {
        return SetError(error, "Wi-Fi samples request envelope is invalid");
    }
    const uint32_t count = ReadUint32(payload.data() + 4);
    const uint32_t lease = ReadUint32(payload.data() + 8);
    if (count == 0 || count > kMaximumSamples || !ValidLease(lease) ||
        ReadUint32(payload.data() + 12) != 0 ||
        payload.size() != kSamplesRequestHeaderSize + count * kSampleRecordSize) {
        return SetError(error, "Wi-Fi samples request size or count is invalid");
    }
    samples->clear();
    samples->reserve(count);
    int64_t previous_timestamp_ns = 0;
    for (uint32_t index = 0; index < count; ++index) {
        const uint8_t* input =
                payload.data() + kSamplesRequestHeaderSize + index * kSampleRecordSize;
        const uint64_t timestamp_ns = ReadUint64(input);
        const uint64_t access_point_id = ReadUint64(input + 8);
        aidl::floral::device::wifi::WifiSample sample;
        sample.rssiDbm = static_cast<int32_t>(ReadUint32(input + 16));
        sample.frequencyMhz = static_cast<int32_t>(ReadUint32(input + 20));
        sample.linkSpeedMbps = static_cast<int32_t>(ReadUint32(input + 24));
        if (timestamp_ns == 0 ||
            timestamp_ns > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
            static_cast<int64_t>(timestamp_ns) <= previous_timestamp_ns || access_point_id == 0 ||
            access_point_id > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
            sample.rssiDbm < -127 || sample.rssiDbm > -1 ||
            !ValidFrequency(sample.frequencyMhz) || sample.linkSpeedMbps < 1 ||
            sample.linkSpeedMbps > 10'000 || ReadUint32(input + 28) != 0) {
            return SetError(error, "Wi-Fi samples request contains an invalid record");
        }
        sample.timestampNs = static_cast<int64_t>(timestamp_ns);
        sample.accessPointId = static_cast<int64_t>(access_point_id);
        previous_timestamp_ns = sample.timestampNs;
        samples->push_back(std::move(sample));
    }
    *lease_duration_ms = lease;
    return true;
}

bool SerializeWifiUpdateResponse(control::ControlCommandId command_id, uint32_t request_id,
                                 const aidl::floral::device::wifi::WifiControlResult& update,
                                 control::ControlResponse* response, std::string* error) {
    if (!PrepareResponse(command_id, request_id, kUpdateResponseSize, response, error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kWifiControlVersion);
    WriteUint16(response->payload.data() + 2, kUpdateResponseSize);
    WriteUint32(response->payload.data() + 4, static_cast<uint32_t>(update.result));
    WriteUint64(response->payload.data() + 8, static_cast<uint64_t>(update.generation));
    WriteUint64(response->payload.data() + 16, static_cast<uint64_t>(update.objectId));
    response->refreshes_authority_lease = update.result == 0 || update.result == 1;
    return true;
}

bool SerializeWifiProfileResponse(uint32_t request_id,
                                  const aidl::floral::device::wifi::WifiProfile& profile,
                                  size_t access_point_count, control::ControlResponse* response,
                                  std::string* error) {
    if (profile.countryCode.size() != 2 || access_point_count > kMaximumAccessPoints ||
        !PrepareResponse(control::ControlCommandId::kGetWifiProfile, request_id,
                         kProfileResponseSize, response, error) ||
        !ParseMacAddress(profile.stationMacAddress, response->payload.data() + 16)) {
        return SetError(error, "Wi-Fi profile response contains an invalid field");
    }
    WriteUint16(response->payload.data(), kWifiControlVersion);
    WriteUint16(response->payload.data() + 2, kProfileResponseSize);
    WriteUint32(response->payload.data() + 4, static_cast<uint32_t>(profile.version));
    WriteUint32(response->payload.data() + 8, profile.enabled ? 1U : 0U);
    response->payload[12] = static_cast<uint8_t>(profile.countryCode[0]);
    response->payload[13] = static_cast<uint8_t>(profile.countryCode[1]);
    WriteUint64(response->payload.data() + 24,
                static_cast<uint64_t>(profile.connectedAccessPointId));
    WriteUint32(response->payload.data() + 32, static_cast<uint32_t>(access_point_count));
    WriteUint32(response->payload.data() + 36, 0x3fU);
    return true;
}

bool SerializeWifiSnapshotResponse(uint32_t request_id,
                                   const aidl::floral::device::wifi::WifiSnapshot& snapshot,
                                   control::ControlResponse* response, std::string* error) {
    if (snapshot.ssid.size() > 32 ||
        !PrepareResponse(control::ControlCommandId::kGetWifiSnapshot, request_id,
                         kSnapshotResponseSize, response, error)) {
        return SetError(error, "Wi-Fi snapshot response contains an invalid field");
    }
    uint32_t flags = (snapshot.enabled ? 1U : 0U) |
                     (snapshot.externallyControlled ? 2U : 0U) |
                     (snapshot.connectedAccessPointId != 0 ? 4U : 0U);
    if (snapshot.connectedAccessPointId != 0 &&
        !ParseMacAddress(snapshot.bssid, response->payload.data() + 52)) {
        return SetError(error, "Wi-Fi snapshot BSSID is invalid");
    }
    WriteUint16(response->payload.data(), kWifiControlVersion);
    WriteUint16(response->payload.data() + 2, kSnapshotResponseSize);
    WriteUint32(response->payload.data() + 4, flags);
    WriteUint64(response->payload.data() + 8, static_cast<uint64_t>(snapshot.generation));
    WriteUint64(response->payload.data() + 16, static_cast<uint64_t>(snapshot.timestampNs));
    WriteUint64(response->payload.data() + 24,
                static_cast<uint64_t>(snapshot.connectedAccessPointId));
    WriteUint32(response->payload.data() + 32, static_cast<uint32_t>(snapshot.security));
    WriteUint32(response->payload.data() + 36, static_cast<uint32_t>(snapshot.rssiDbm));
    WriteUint32(response->payload.data() + 40, static_cast<uint32_t>(snapshot.frequencyMhz));
    WriteUint32(response->payload.data() + 44, static_cast<uint32_t>(snapshot.channelWidthMhz));
    WriteUint32(response->payload.data() + 48, static_cast<uint32_t>(snapshot.linkSpeedMbps));
    WriteUint16(response->payload.data() + 58, static_cast<uint16_t>(snapshot.ssid.size()));
    std::copy(snapshot.ssid.begin(), snapshot.ssid.end(), response->payload.begin() + 60);
    return true;
}

bool SerializeWifiAccessPointsResponse(
        uint32_t request_id,
        const std::vector<aidl::floral::device::wifi::WifiAccessPoint>& access_points,
        control::ControlResponse* response, std::string* error) {
    const size_t payload_size = kListResponseHeaderSize + access_points.size() * kAccessPointRecordSize;
    if (access_points.size() > kMaximumAccessPoints ||
        !PrepareResponse(control::ControlCommandId::kListWifiAccessPoints, request_id, payload_size,
                         response, error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kWifiControlVersion);
    WriteUint16(response->payload.data() + 2, static_cast<uint16_t>(payload_size));
    WriteUint32(response->payload.data() + 4, static_cast<uint32_t>(access_points.size()));
    for (size_t index = 0; index < access_points.size(); ++index) {
        if (!WriteAccessPointRecord(
                    response->payload.data() + kListResponseHeaderSize + index * kAccessPointRecordSize,
                    access_points[index])) {
            return SetError(error, "Wi-Fi access-point response contains an invalid record");
        }
    }
    return true;
}

bool SerializeWifiCapabilitiesResponse(uint32_t request_id, control::ControlResponse* response,
                                       std::string* error) {
    if (!PrepareResponse(control::ControlCommandId::kGetWifiCapabilities, request_id,
                         kCapabilitiesResponseSize, response, error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kWifiControlVersion);
    WriteUint16(response->payload.data() + 2, kCapabilitiesResponseSize);
    WriteUint32(response->payload.data() + 4, 0x3fU);
    WriteUint32(response->payload.data() + 8, kMaximumAccessPoints);
    WriteUint32(response->payload.data() + 12, kMaximumSamples);
    WriteUint32(response->payload.data() + 16,
                static_cast<uint32_t>(kMaximumWifiControlLeaseMs));
    WriteUint32(response->payload.data() + 20, 0x07U);
    WriteUint32(response->payload.data() + 24, 1U);  // Ethernet-backed presentation.
    return true;
}

}  // namespace floral::device::wifi
