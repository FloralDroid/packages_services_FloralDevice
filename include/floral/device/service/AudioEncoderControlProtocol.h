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
#include "floral/device/service/AudioEncoderControl.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace floral::device::service {

constexpr size_t kAudioEncoderConfigRequestSize = 16;
constexpr size_t kAudioEncoderConfigResponseSize = 20;

bool ParseAudioEncoderConfigRequest(const std::vector<uint8_t>& payload,
                                    AudioEncoderConfigUpdate* update, std::string* error);
bool SerializeAudioEncoderConfigResponse(const AudioEncoderRuntimeState& state,
                                         AudioEncoderConfigResult result, uint32_t request_id,
                                         control::ControlResponse* response, std::string* error);

}  // namespace floral::device::service
