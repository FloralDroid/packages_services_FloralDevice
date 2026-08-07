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

#include <aidl/floral/device/radio/RadioCall.h>
#include <aidl/floral/device/radio/RadioCell.h>
#include <aidl/floral/device/radio/RadioControlResult.h>
#include <aidl/floral/device/radio/RadioProfile.h>
#include <aidl/floral/device/radio/RadioRegistrationControl.h>
#include <aidl/floral/device/radio/RadioSignal.h>
#include <aidl/floral/device/radio/RadioSmsEvent.h>
#include <aidl/floral/device/radio/RadioSnapshot.h>

#include <cstdint>
#include <string>
#include <vector>

namespace floral::device::radio {

constexpr uint16_t kRadioControlVersion = 1;
constexpr int64_t kMaximumRadioControlLeaseMs = 60'000;

struct SimStateRequest {
    int32_t state = 0;
    int64_t lease_duration_ms = 0;
};

struct CallStateRequest {
    int64_t call_id = 0;
    int32_t state = 0;
};

struct IncomingSmsRequest {
    std::string address;
    std::string body;
};

bool ParseRegistrationRequest(const std::vector<uint8_t>& payload,
                              aidl::floral::device::radio::RadioRegistrationControl* request,
                              std::string* error);
bool ParseSignalRequest(const std::vector<uint8_t>& payload,
                        aidl::floral::device::radio::RadioSignal* signal,
                        int64_t* lease_duration_ms, std::string* error);
bool ParseCellsRequest(const std::vector<uint8_t>& payload,
                       std::vector<aidl::floral::device::radio::RadioCell>* cells,
                       int64_t* lease_duration_ms, std::string* error);
bool ParseSimStateRequest(const std::vector<uint8_t>& payload, SimStateRequest* request,
                          std::string* error);
bool ParseIncomingCallRequest(const std::vector<uint8_t>& payload, std::string* number,
                              std::string* error);
bool ParseCallStateRequest(const std::vector<uint8_t>& payload, CallStateRequest* request,
                           std::string* error);
bool ParseIncomingSmsRequest(const std::vector<uint8_t>& payload, IncomingSmsRequest* request,
                             std::string* error);

bool SerializeRadioUpdateResponse(control::ControlCommandId command_id, uint32_t request_id,
                                  const aidl::floral::device::radio::RadioControlResult& update,
                                  control::ControlResponse* response, std::string* error);
bool SerializeRadioProfileResponse(uint32_t request_id,
                                   const aidl::floral::device::radio::RadioProfile& profile,
                                   control::ControlResponse* response, std::string* error);
bool SerializeRadioSnapshotResponse(uint32_t request_id,
                                    const aidl::floral::device::radio::RadioSnapshot& snapshot,
                                    control::ControlResponse* response, std::string* error);
bool SerializeRadioCellsResponse(uint32_t request_id,
                                 const std::vector<aidl::floral::device::radio::RadioCell>& cells,
                                 control::ControlResponse* response, std::string* error);
bool SerializeRadioCallsResponse(uint32_t request_id,
                                 const std::vector<aidl::floral::device::radio::RadioCall>& calls,
                                 control::ControlResponse* response, std::string* error);
bool SerializeRadioSmsEventsResponse(
        uint32_t request_id, const std::vector<aidl::floral::device::radio::RadioSmsEvent>& events,
        control::ControlResponse* response, std::string* error);
bool SerializeRadioCapabilitiesResponse(uint32_t request_id, control::ControlResponse* response,
                                        std::string* error);

}  // namespace floral::device::radio
