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

#include <aidl/floral/device/power/IPowerState.h>

#include <memory>
#include <mutex>

namespace floral::device::power {

class PowerControlHandler final : public control::ControlRequestHandler {
  public:
    bool Handle(const control::ControlRequest& request, control::ControlResponse* response,
                std::string* error) override;
    void OnAuthorityLeaseExpired() override;

  private:
    std::shared_ptr<aidl::floral::device::power::IPowerState> GetService();
    void ForgetService();

    std::mutex mutex_;
    std::shared_ptr<aidl::floral::device::power::IPowerState> service_;
};

}  // namespace floral::device::power
