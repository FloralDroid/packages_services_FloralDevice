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

#include "floral/device/display/topology/DisplayTopologyTypes.h"

#include <memory>
#include <vector>

namespace floral::device::display::topology {

class DisplayTopologyStateService;

// Owns the only mutation path for the published display topology. Transport
// adapters submit complete desired snapshots instead of incremental commands.
class DisplayTopologyController final {
  public:
    explicit DisplayTopologyController(std::shared_ptr<DisplayTopologyStateService> state_service);

    TopologyUpdate ReplaceExternalDisplays(std::vector<ManagedPhysicalDisplay> displays);
    TopologyUpdate ClearExternalDisplays();

  private:
    const std::shared_ptr<DisplayTopologyStateService> state_service_;
};

}  // namespace floral::device::display::topology
