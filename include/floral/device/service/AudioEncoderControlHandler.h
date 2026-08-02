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
#include "floral/device/service/AudioEncoderControl.h"

#include <memory>

namespace floral::device::service {

class AudioEncoderControlHandler final : public control::ControlRequestHandler {
  public:
    explicit AudioEncoderControlHandler(std::shared_ptr<AudioEncoderControl> control);

    bool Handle(const control::ControlRequest& request, control::ControlResponse* response,
                std::string* error) override;
    void OnAuthorityLeaseExpired() override;

  private:
    const std::shared_ptr<AudioEncoderControl> control_;
};

}  // namespace floral::device::service
