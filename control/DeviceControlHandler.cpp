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

#include "floral/device/control/DeviceControlHandler.h"

#include "floral/device/control/ControlProtocol.h"

#include <utility>

namespace floral::device::control {

DeviceControlHandler::DeviceControlHandler(std::shared_ptr<ControlRequestHandler> topology_handler,
                                           std::shared_ptr<ControlRequestHandler> audio_handler,
                                           std::shared_ptr<ControlRequestHandler> video_handler,
                                           std::shared_ptr<ControlRequestHandler> simulation_handler)
    : topology_handler_(std::move(topology_handler)),
      audio_handler_(std::move(audio_handler)),
      video_handler_(std::move(video_handler)),
      simulation_handler_(std::move(simulation_handler)) {}

bool DeviceControlHandler::Handle(const ControlRequest& request, ControlResponse* response,
                                  std::string* error) {
    if (response == nullptr) {
        if (error != nullptr) {
            *error = "control response output is null";
        }
        return false;
    }
    if (request.header.request_id == 0) {
        return SerializeControlErrorResponse(request.header.request_id, request.header.command_id,
                                             ControlError::kInvalidRequestId, response, error);
    }

    switch (static_cast<ControlCommandId>(request.header.command_id)) {
        case ControlCommandId::kReplaceDisplayTopology:
            if (topology_handler_ != nullptr) {
                return topology_handler_->Handle(request, response, error);
            }
            break;
        case ControlCommandId::kSetAudioEncoderConfig:
            if (audio_handler_ != nullptr) {
                return audio_handler_->Handle(request, response, error);
            }
            break;
        case ControlCommandId::kSetVideoEncoderConfig:
            if (video_handler_ != nullptr) {
                return video_handler_->Handle(request, response, error);
            }
            break;
        case ControlCommandId::kSetMotionConfig:
        case ControlCommandId::kSetEnvironmentConfig:
        case ControlCommandId::kPushExternalPoseBatch:
        case ControlCommandId::kResetSensorSimulation:
        case ControlCommandId::kGetSensorSimulationConfig:
        case ControlCommandId::kListSensors:
        case ControlCommandId::kGetSensorSnapshot:
        case ControlCommandId::kSetGnssConfig:
        case ControlCommandId::kPushExternalGnssBatch:
        case ControlCommandId::kResetGnssSimulation:
        case ControlCommandId::kGetGnssConfig:
        case ControlCommandId::kGetGnssCapabilities:
        case ControlCommandId::kGetGnssSnapshot:
            if (simulation_handler_ != nullptr) {
                return simulation_handler_->Handle(request, response, error);
            }
            break;
        case ControlCommandId::kGenericError:
            break;
    }

    return SerializeControlErrorResponse(request.header.request_id, request.header.command_id,
                                         ControlError::kUnsupportedMessage, response, error);
}

void DeviceControlHandler::OnAuthorityLeaseExpired() {
    if (topology_handler_ != nullptr) {
        topology_handler_->OnAuthorityLeaseExpired();
    }
    if (video_handler_ != nullptr) {
        video_handler_->OnAuthorityLeaseExpired();
    }
    if (audio_handler_ != nullptr) {
        audio_handler_->OnAuthorityLeaseExpired();
    }
    if (simulation_handler_ != nullptr) {
        simulation_handler_->OnAuthorityLeaseExpired();
    }
}

}  // namespace floral::device::control
