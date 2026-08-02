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

#include "floral/device/control/ControlRequestHandler.h"

#include <memory>

namespace floral::device::control {

// Dispatches the independent HAL configuration command families through one
// FHC1 connection while keeping each family's implementation isolated.
class DeviceControlHandler final : public ControlRequestHandler {
  public:
    DeviceControlHandler(std::shared_ptr<ControlRequestHandler> topology_handler,
                         std::shared_ptr<ControlRequestHandler> audio_handler,
                         std::shared_ptr<ControlRequestHandler> video_handler);

    bool Handle(const ControlRequest& request, ControlResponse* response,
                std::string* error) override;
    void OnAuthorityLeaseExpired() override;

  private:
    const std::shared_ptr<ControlRequestHandler> topology_handler_;
    const std::shared_ptr<ControlRequestHandler> audio_handler_;
    const std::shared_ptr<ControlRequestHandler> video_handler_;
};

}  // namespace floral::device::control
