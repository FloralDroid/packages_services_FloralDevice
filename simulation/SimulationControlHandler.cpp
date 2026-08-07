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

#include "floral/device/simulation/SimulationControlHandler.h"

#include "floral/device/control/ControlProtocol.h"
#include "floral/device/simulation/SimulationControlProtocol.h"
#include "floral/device/simulation/SimulationController.h"

#include <utils/Timers.h>

#include <utility>
#include <vector>

namespace floral::device::simulation {
namespace {

bool IsSimulationCommand(uint16_t command_id) {
    return (command_id >= static_cast<uint16_t>(control::ControlCommandId::kSetMotionConfig) &&
            command_id <= static_cast<uint16_t>(control::ControlCommandId::kGetSensorSnapshot)) ||
           (command_id >= static_cast<uint16_t>(control::ControlCommandId::kSetGnssConfig) &&
            command_id <= static_cast<uint16_t>(control::ControlCommandId::kGetGnssSnapshot));
}

}  // namespace

SimulationControlHandler::SimulationControlHandler(std::shared_ptr<SimulationController> controller)
    : controller_(std::move(controller)) {}

bool SimulationControlHandler::Handle(const control::ControlRequest& request,
                                      control::ControlResponse* response, std::string* error) {
    if (response == nullptr) {
        if (error != nullptr) {
            *error = "simulation control response output is null";
        }
        return false;
    }
    if (request.header.request_id == 0) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                control::ControlError::kInvalidRequestId, response, error);
    }
    if (!control::IsFhc1RouteKind(request.header.route_kind,
                                  control::ControlPacketKind::kRequest) ||
        !IsSimulationCommand(request.header.command_id)) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                control::ControlError::kUnsupportedMessage, response, error);
    }
    if (controller_ == nullptr) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                control::ControlError::kInternalError, response, error);
    }

    const auto command = static_cast<control::ControlCommandId>(request.header.command_id);
    SimulationUpdate update;
    switch (command) {
        case control::ControlCommandId::kSetMotionConfig: {
            MotionConfigRequest parsed;
            if (!ParseMotionConfigRequest(request.payload, &parsed, error)) {
                break;
            }
            update = controller_->SetMotionConfig(parsed);
            return SerializeSimulationUpdateResponse(command, request.header.request_id, update,
                                                     response, error);
        }
        case control::ControlCommandId::kSetEnvironmentConfig: {
            EnvironmentConfigRequest parsed;
            if (!ParseEnvironmentConfigRequest(request.payload, &parsed, error)) {
                break;
            }
            update = controller_->SetEnvironmentConfig(parsed);
            return SerializeSimulationUpdateResponse(command, request.header.request_id, update,
                                                     response, error);
        }
        case control::ControlCommandId::kPushExternalPoseBatch:
        case control::ControlCommandId::kPushExternalGnssBatch: {
            std::vector<ExternalStateRecord> records;
            const int64_t receiveTimestampNs = systemTime(SYSTEM_TIME_BOOTTIME);
            const bool parsed =
                    command == control::ControlCommandId::kPushExternalPoseBatch
                            ? ParseExternalPoseBatch(request.payload, receiveTimestampNs, &records,
                                                     error)
                            : ParseExternalGnssBatch(request.payload, receiveTimestampNs, &records,
                                                     error);
            if (!parsed) {
                break;
            }
            update = controller_->PublishExternalStates(std::move(records), error);
            return SerializeSimulationUpdateResponse(command, request.header.request_id, update,
                                                     response, error);
        }
        case control::ControlCommandId::kResetSensorSimulation:
        case control::ControlCommandId::kResetGnssSimulation:
            if (!request.payload.empty()) {
                break;
            }
            update = command == control::ControlCommandId::kResetSensorSimulation
                             ? controller_->ResetSensors()
                             : controller_->ResetGnss();
            return SerializeSimulationUpdateResponse(command, request.header.request_id, update,
                                                     response, error);
        case control::ControlCommandId::kGetSensorSimulationConfig:
            if (request.payload.empty()) {
                return SerializeSensorConfigResponse(request.header.request_id,
                                                     controller_->GetConfig(), response, error);
            }
            break;
        case control::ControlCommandId::kListSensors:
            if (request.payload.empty()) {
                const auto config = controller_->GetConfig();
                return SerializeSensorCatalogResponse(
                        request.header.request_id, static_cast<uint64_t>(config.generation),
                        controller_->GetSensorCatalog(), response, error);
            }
            break;
        case control::ControlCommandId::kGetSensorSnapshot:
            if (request.payload.empty()) {
                return SerializeSensorSnapshotResponse(request.header.request_id,
                                                       controller_->GetSensorSnapshot(), response,
                                                       error);
            }
            break;
        case control::ControlCommandId::kSetGnssConfig: {
            GnssConfigRequest parsed;
            if (!ParseGnssConfigRequest(request.payload, &parsed, error)) {
                break;
            }
            update = controller_->SetGnssConfig(parsed);
            return SerializeSimulationUpdateResponse(command, request.header.request_id, update,
                                                     response, error);
        }
        case control::ControlCommandId::kGetGnssConfig:
            if (request.payload.empty()) {
                return SerializeGnssConfigResponse(request.header.request_id,
                                                   controller_->GetConfig(), response, error);
            }
            break;
        case control::ControlCommandId::kGetGnssCapabilities:
            if (request.payload.empty()) {
                return SerializeGnssCapabilitiesResponse(request.header.request_id, response,
                                                         error);
            }
            break;
        case control::ControlCommandId::kGetGnssSnapshot:
            if (request.payload.empty()) {
                return SerializeGnssSnapshotResponse(
                        request.header.request_id, controller_->GetGnssSnapshot(), response, error);
            }
            break;
        case control::ControlCommandId::kGenericError:
        case control::ControlCommandId::kReplaceDisplayTopology:
        case control::ControlCommandId::kSetAudioEncoderConfig:
        case control::ControlCommandId::kSetVideoEncoderConfig:
        case control::ControlCommandId::kSetPowerControl:
        case control::ControlCommandId::kReleasePowerControl:
        case control::ControlCommandId::kGetPowerSnapshot:
        case control::ControlCommandId::kGetPowerCapabilities:
        case control::ControlCommandId::kSetRadioRegistration:
        case control::ControlCommandId::kSetRadioSignal:
        case control::ControlCommandId::kReplaceRadioCells:
        case control::ControlCommandId::kSetSimState:
        case control::ControlCommandId::kInjectIncomingCall:
        case control::ControlCommandId::kSetRadioCallState:
        case control::ControlCommandId::kInjectIncomingSms:
        case control::ControlCommandId::kReleaseRadioControl:
        case control::ControlCommandId::kPushRadioSampleBatch:
        case control::ControlCommandId::kGetRadioProfile:
        case control::ControlCommandId::kGetRadioSnapshot:
        case control::ControlCommandId::kListRadioCells:
        case control::ControlCommandId::kListRadioCalls:
        case control::ControlCommandId::kListRadioSmsEvents:
        case control::ControlCommandId::kGetRadioCapabilities:
        case control::ControlCommandId::kSetWifiEnabled:
        case control::ControlCommandId::kReplaceWifiAccessPoints:
        case control::ControlCommandId::kSetWifiConnection:
        case control::ControlCommandId::kSetWifiLink:
        case control::ControlCommandId::kReleaseWifiControl:
        case control::ControlCommandId::kPushWifiSampleBatch:
        case control::ControlCommandId::kGetWifiProfile:
        case control::ControlCommandId::kGetWifiSnapshot:
        case control::ControlCommandId::kListWifiAccessPoints:
        case control::ControlCommandId::kGetWifiCapabilities:
            break;
    }
    return control::SerializeControlErrorResponse(
            request.header.request_id, request.header.command_id,
            control::ControlError::kMalformedRequest, response, error);
}

void SimulationControlHandler::OnAuthorityLeaseExpired() {
    if (controller_ != nullptr) {
        controller_->ClearExternalSources();
    }
}

}  // namespace floral::device::simulation
