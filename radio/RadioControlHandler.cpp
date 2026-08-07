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

#include "floral/device/radio/RadioControlHandler.h"

#include "floral/device/control/ControlProtocol.h"
#include "floral/device/radio/RadioControlProtocol.h"

#include <android/binder_manager.h>

namespace floral::device::radio {
namespace {

constexpr char kRadioStateInstance[] = "floral.device.radio.IRadioState/default";

bool IsRadioCommand(control::ControlCommandId command) {
    switch (command) {
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
            return true;
        default:
            return false;
    }
}

bool SerializeMalformed(const control::ControlRequest& request, control::ControlResponse* response,
                        std::string* error) {
    return control::SerializeControlErrorResponse(
            request.header.request_id, request.header.command_id,
            control::ControlError::kMalformedRequest, response, error);
}

bool SerializeInternal(const control::ControlRequest& request, control::ControlResponse* response,
                       std::string* error) {
    return control::SerializeControlErrorResponse(
            request.header.request_id, request.header.command_id,
            control::ControlError::kInternalError, response, error);
}

}  // namespace

std::shared_ptr<aidl::floral::device::radio::IRadioState> RadioControlHandler::GetService() {
    std::lock_guard lock(mutex_);
    if (service_ != nullptr && service_->asBinder().get() != nullptr &&
        AIBinder_isAlive(service_->asBinder().get())) {
        return service_;
    }
    service_.reset();
    ndk::SpAIBinder binder(AServiceManager_checkService(kRadioStateInstance));
    if (binder.get() != nullptr) {
        service_ = aidl::floral::device::radio::IRadioState::fromBinder(binder);
    }
    return service_;
}

void RadioControlHandler::ForgetService() {
    std::lock_guard lock(mutex_);
    service_.reset();
}

bool RadioControlHandler::Handle(const control::ControlRequest& request,
                                 control::ControlResponse* response, std::string* error) {
    const auto command = static_cast<control::ControlCommandId>(request.header.command_id);
    if (request.header.request_id == 0 ||
        !control::IsFhc1RouteKind(request.header.route_kind,
                                  control::ControlPacketKind::kRequest) ||
        !IsRadioCommand(command)) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                request.header.request_id == 0 ? control::ControlError::kInvalidRequestId
                                               : control::ControlError::kUnsupportedMessage,
                response, error);
    }

    if (command == control::ControlCommandId::kPushRadioSampleBatch) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                control::ControlError::kUnsupportedMessage, response, error);
    }
    if (command == control::ControlCommandId::kGetRadioCapabilities) {
        return request.payload.empty() ? SerializeRadioCapabilitiesResponse(
                                                 request.header.request_id, response, error)
                                       : SerializeMalformed(request, response, error);
    }

    const std::shared_ptr<aidl::floral::device::radio::IRadioState> service = GetService();
    if (service == nullptr) {
        return SerializeInternal(request, response, error);
    }

    aidl::floral::device::radio::RadioControlResult update;
    ndk::ScopedAStatus status = ndk::ScopedAStatus::ok();
    switch (command) {
        case control::ControlCommandId::kSetRadioRegistration: {
            aidl::floral::device::radio::RadioRegistrationControl parsed;
            if (!ParseRegistrationRequest(request.payload, &parsed, error)) {
                return SerializeMalformed(request, response, error);
            }
            status = service->setRegistration(parsed, &update);
            break;
        }
        case control::ControlCommandId::kSetRadioSignal: {
            aidl::floral::device::radio::RadioSignal signal;
            int64_t lease_duration_ms = 0;
            if (!ParseSignalRequest(request.payload, &signal, &lease_duration_ms, error)) {
                return SerializeMalformed(request, response, error);
            }
            status = service->setSignal(signal, lease_duration_ms, &update);
            break;
        }
        case control::ControlCommandId::kReplaceRadioCells: {
            std::vector<aidl::floral::device::radio::RadioCell> cells;
            int64_t lease_duration_ms = 0;
            if (!ParseCellsRequest(request.payload, &cells, &lease_duration_ms, error)) {
                return SerializeMalformed(request, response, error);
            }
            status = service->replaceCells(cells, lease_duration_ms, &update);
            break;
        }
        case control::ControlCommandId::kSetSimState: {
            SimStateRequest parsed;
            if (!ParseSimStateRequest(request.payload, &parsed, error)) {
                return SerializeMalformed(request, response, error);
            }
            status = service->setSimState(parsed.state, parsed.lease_duration_ms, &update);
            break;
        }
        case control::ControlCommandId::kInjectIncomingCall: {
            std::string number;
            if (!ParseIncomingCallRequest(request.payload, &number, error)) {
                return SerializeMalformed(request, response, error);
            }
            status = service->injectIncomingCall(number, &update);
            break;
        }
        case control::ControlCommandId::kSetRadioCallState: {
            CallStateRequest parsed;
            if (!ParseCallStateRequest(request.payload, &parsed, error)) {
                return SerializeMalformed(request, response, error);
            }
            status = service->setCallState(parsed.call_id, parsed.state, &update);
            break;
        }
        case control::ControlCommandId::kInjectIncomingSms: {
            IncomingSmsRequest parsed;
            if (!ParseIncomingSmsRequest(request.payload, &parsed, error)) {
                return SerializeMalformed(request, response, error);
            }
            status = service->injectIncomingSms(parsed.address, parsed.body, &update);
            break;
        }
        case control::ControlCommandId::kReleaseRadioControl:
            if (!request.payload.empty()) {
                return SerializeMalformed(request, response, error);
            }
            status = service->releaseExternalControl(&update);
            break;
        case control::ControlCommandId::kGetRadioProfile: {
            if (!request.payload.empty()) {
                return SerializeMalformed(request, response, error);
            }
            aidl::floral::device::radio::RadioProfile profile;
            status = service->getProfile(&profile);
            if (!status.isOk()) {
                ForgetService();
                return SerializeInternal(request, response, error);
            }
            return SerializeRadioProfileResponse(request.header.request_id, profile, response,
                                                 error);
        }
        case control::ControlCommandId::kGetRadioSnapshot:
        case control::ControlCommandId::kListRadioCells:
        case control::ControlCommandId::kListRadioCalls:
        case control::ControlCommandId::kListRadioSmsEvents: {
            if (!request.payload.empty()) {
                return SerializeMalformed(request, response, error);
            }
            aidl::floral::device::radio::RadioSnapshot snapshot;
            status = service->getSnapshot(&snapshot);
            if (!status.isOk()) {
                ForgetService();
                return SerializeInternal(request, response, error);
            }
            if (command == control::ControlCommandId::kGetRadioSnapshot) {
                return SerializeRadioSnapshotResponse(request.header.request_id, snapshot, response,
                                                      error);
            }
            if (command == control::ControlCommandId::kListRadioCells) {
                return SerializeRadioCellsResponse(request.header.request_id, snapshot.cells,
                                                   response, error);
            }
            if (command == control::ControlCommandId::kListRadioCalls) {
                return SerializeRadioCallsResponse(request.header.request_id, snapshot.calls,
                                                   response, error);
            }
            return SerializeRadioSmsEventsResponse(request.header.request_id, snapshot.smsEvents,
                                                   response, error);
        }
        default:
            return control::SerializeControlErrorResponse(
                    request.header.request_id, request.header.command_id,
                    control::ControlError::kUnsupportedMessage, response, error);
    }

    if (!status.isOk()) {
        ForgetService();
        return SerializeInternal(request, response, error);
    }
    return SerializeRadioUpdateResponse(command, request.header.request_id, update, response,
                                        error);
}

void RadioControlHandler::OnAuthorityLeaseExpired() {
    const std::shared_ptr<aidl::floral::device::radio::IRadioState> service = GetService();
    if (service == nullptr) {
        return;
    }
    aidl::floral::device::radio::RadioControlResult ignored;
    if (!service->releaseExternalControl(&ignored).isOk()) {
        ForgetService();
    }
}

}  // namespace floral::device::radio
