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

#include "floral/device/control/ControlProtocol.h"

#include <aidl/floral/device/wifi/WifiAccessPoint.h>
#include <aidl/floral/device/wifi/WifiControlResult.h>
#include <aidl/floral/device/wifi/WifiLinkState.h>
#include <aidl/floral/device/wifi/WifiProfile.h>
#include <aidl/floral/device/wifi/WifiSample.h>
#include <aidl/floral/device/wifi/WifiSnapshot.h>

#include <cstdint>
#include <string>
#include <vector>

namespace floral::device::wifi {

constexpr uint16_t kWifiControlVersion = 1;
constexpr int64_t kMaximumWifiControlLeaseMs = 60'000;

struct EnabledRequest {
    bool enabled = false;
    int64_t lease_duration_ms = 0;
};

struct ConnectionRequest {
    int64_t access_point_id = 0;
    int64_t lease_duration_ms = 0;
};

bool ParseEnabledRequest(const std::vector<uint8_t>& payload, EnabledRequest* request,
                         std::string* error);
bool ParseAccessPointsRequest(
        const std::vector<uint8_t>& payload,
        std::vector<aidl::floral::device::wifi::WifiAccessPoint>* access_points,
        int64_t* lease_duration_ms, std::string* error);
bool ParseConnectionRequest(const std::vector<uint8_t>& payload, ConnectionRequest* request,
                            std::string* error);
bool ParseLinkRequest(const std::vector<uint8_t>& payload,
                      aidl::floral::device::wifi::WifiLinkState* link,
                      int64_t* lease_duration_ms, std::string* error);
bool ParseSamplesRequest(const std::vector<uint8_t>& payload,
                         std::vector<aidl::floral::device::wifi::WifiSample>* samples,
                         int64_t* lease_duration_ms, std::string* error);

bool SerializeWifiUpdateResponse(control::ControlCommandId command_id, uint32_t request_id,
                                 const aidl::floral::device::wifi::WifiControlResult& update,
                                 control::ControlResponse* response, std::string* error);
bool SerializeWifiProfileResponse(uint32_t request_id,
                                  const aidl::floral::device::wifi::WifiProfile& profile,
                                  size_t access_point_count,
                                  control::ControlResponse* response, std::string* error);
bool SerializeWifiSnapshotResponse(uint32_t request_id,
                                   const aidl::floral::device::wifi::WifiSnapshot& snapshot,
                                   control::ControlResponse* response, std::string* error);
bool SerializeWifiAccessPointsResponse(
        uint32_t request_id,
        const std::vector<aidl::floral::device::wifi::WifiAccessPoint>& access_points,
        control::ControlResponse* response, std::string* error);
bool SerializeWifiCapabilitiesResponse(uint32_t request_id, control::ControlResponse* response,
                                       std::string* error);

}  // namespace floral::device::wifi
