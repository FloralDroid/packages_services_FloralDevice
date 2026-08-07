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

#include "floral/device/power/PowerControlHandler.h"

#include "floral/device/control/ControlProtocol.h"
#include "floral/device/power/PowerControlProtocol.h"

#include <android/binder_manager.h>

namespace floral::device::power {
namespace {

constexpr char kPowerStateInstance[] = "floral.device.power.IPowerState/default";

bool IsPowerCommand(uint16_t command_id) {
    return command_id >= static_cast<uint16_t>(control::ControlCommandId::kSetPowerControl) &&
           command_id <= static_cast<uint16_t>(control::ControlCommandId::kGetPowerCapabilities);
}

}  // namespace

std::shared_ptr<aidl::floral::device::power::IPowerState> PowerControlHandler::GetService() {
    std::lock_guard lock(mutex_);
    if (service_ != nullptr && service_->asBinder().get() != nullptr &&
        AIBinder_isAlive(service_->asBinder().get())) {
        return service_;
    }
    service_.reset();
    ndk::SpAIBinder binder(AServiceManager_checkService(kPowerStateInstance));
    if (binder.get() != nullptr) {
        service_ = aidl::floral::device::power::IPowerState::fromBinder(binder);
    }
    return service_;
}

void PowerControlHandler::ForgetService() {
    std::lock_guard lock(mutex_);
    service_.reset();
}

bool PowerControlHandler::Handle(const control::ControlRequest& request,
                                 control::ControlResponse* response, std::string* error) {
    if (request.header.request_id == 0 ||
        !control::IsFhc1RouteKind(request.header.route_kind,
                                  control::ControlPacketKind::kRequest) ||
        !IsPowerCommand(request.header.command_id)) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                request.header.request_id == 0 ? control::ControlError::kInvalidRequestId
                                               : control::ControlError::kUnsupportedMessage,
                response, error);
    }

    const auto command = static_cast<control::ControlCommandId>(request.header.command_id);
    if (command == control::ControlCommandId::kGetPowerCapabilities) {
        if (!request.payload.empty()) {
            return control::SerializeControlErrorResponse(
                    request.header.request_id, request.header.command_id,
                    control::ControlError::kMalformedRequest, response, error);
        }
        return SerializePowerCapabilitiesResponse(request.header.request_id, response, error);
    }

    const std::shared_ptr<aidl::floral::device::power::IPowerState> service = GetService();
    if (service == nullptr) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                control::ControlError::kInternalError, response, error);
    }

    if (command == control::ControlCommandId::kSetPowerControl) {
        ManualPowerRequest parsed;
        if (!ParseManualPowerRequest(request.payload, &parsed, error)) {
            return control::SerializeControlErrorResponse(
                    request.header.request_id, request.header.command_id,
                    control::ControlError::kMalformedRequest, response, error);
        }
        aidl::floral::device::power::PowerControlResult update;
        const ndk::ScopedAStatus status =
                service->setManualControl(static_cast<int32_t>(parsed.mode), parsed.current_ua,
                                          parsed.lease_duration_ms, &update);
        if (!status.isOk()) {
            ForgetService();
            return control::SerializeControlErrorResponse(
                    request.header.request_id, request.header.command_id,
                    control::ControlError::kInternalError, response, error);
        }
        return SerializePowerUpdateResponse(command, request.header.request_id, update, response,
                                            error);
    }
    if (!request.payload.empty()) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                control::ControlError::kMalformedRequest, response, error);
    }
    if (command == control::ControlCommandId::kReleasePowerControl) {
        aidl::floral::device::power::PowerControlResult update;
        const ndk::ScopedAStatus status = service->releaseManualControl(&update);
        if (!status.isOk()) {
            ForgetService();
            return control::SerializeControlErrorResponse(
                    request.header.request_id, request.header.command_id,
                    control::ControlError::kInternalError, response, error);
        }
        return SerializePowerUpdateResponse(command, request.header.request_id, update, response,
                                            error);
    }

    aidl::floral::device::power::PowerSnapshot snapshot;
    const ndk::ScopedAStatus status = service->getSnapshot(&snapshot);
    if (!status.isOk()) {
        ForgetService();
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                control::ControlError::kInternalError, response, error);
    }
    return SerializePowerSnapshotResponse(request.header.request_id, snapshot, response, error);
}

void PowerControlHandler::OnAuthorityLeaseExpired() {
    const std::shared_ptr<aidl::floral::device::power::IPowerState> service = GetService();
    if (service == nullptr) {
        return;
    }
    aidl::floral::device::power::PowerControlResult ignored;
    if (!service->releaseManualControl(&ignored).isOk()) {
        ForgetService();
    }
}

}  // namespace floral::device::power
