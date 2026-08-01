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

#include <cstdint>
#include <string>
#include <vector>

namespace floral::stream::topology {

struct ManagedPhysicalDisplay {
    uint64_t display_id = 0;
    uint8_t port = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t dpi = 0;
    std::vector<uint32_t> supported_refresh_rates_hz;
    uint32_t active_refresh_rate_hz = 0;
    std::string name;
};

enum class TopologyUpdateResult : uint32_t {
    kApplied = 0,
    kUnchanged = 1,
    kInvalidDisplay = 2,
    kDuplicateDisplayId = 3,
    kDuplicatePort = 4,
    kUnavailable = 5,
};

struct TopologyUpdate {
    TopologyUpdateResult result = TopologyUpdateResult::kUnavailable;
    uint64_t generation = 0;
};

}  // namespace floral::stream::topology
