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

#include "floral/stream/control/ControlProtocol.h"
#include "floral/stream/topology/DisplayTopologyTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace floral::stream::topology {

constexpr size_t kMaximumControlledExternalDisplays = 64;
constexpr size_t kMaximumDisplayRefreshRates = 16;
constexpr size_t kMaximumDisplayNameBytes = 128;

bool SerializeReplaceDisplayTopologyRequest(const std::vector<ManagedPhysicalDisplay>& displays,
                                            std::vector<uint8_t>* payload, std::string* error);
bool ParseReplaceDisplayTopologyRequest(const std::vector<uint8_t>& payload,
                                        std::vector<ManagedPhysicalDisplay>* displays,
                                        std::string* error);
bool SerializeReplaceDisplayTopologyResponse(const TopologyUpdate& update, uint32_t request_id,
                                             control::ControlResponse* response,
                                             std::string* error);
bool ParseReplaceDisplayTopologyResponse(const std::vector<uint8_t>& payload,
                                         TopologyUpdate* update, std::string* error);

}  // namespace floral::stream::topology
