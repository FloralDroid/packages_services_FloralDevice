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

#include "floral/device/wifi/WifiControlHandler.h"

#include "floral/device/control/ControlProtocol.h"
#include "floral/device/wifi/WifiControlProtocol.h"

#include <android/binder_manager.h>

namespace floral::device::wifi {
namespace {

constexpr char kWifiStateInstance[] = "floral.device.wifi.IWifiState/default";

bool IsWifiCommand(control::ControlCommandId command) {
    switch (command) {
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

std::shared_ptr<aidl::floral::device::wifi::IWifiState> WifiControlHandler::GetService() {
    std::lock_guard lock(mutex_);
    if (service_ != nullptr && service_->asBinder().get() != nullptr &&
        AIBinder_isAlive(service_->asBinder().get())) {
        return service_;
    }
    service_.reset();
    ndk::SpAIBinder binder(AServiceManager_checkService(kWifiStateInstance));
    if (binder.get() != nullptr) {
        service_ = aidl::floral::device::wifi::IWifiState::fromBinder(binder);
    }
    return service_;
}

void WifiControlHandler::ForgetService() {
    std::lock_guard lock(mutex_);
    service_.reset();
}

bool WifiControlHandler::Handle(const control::ControlRequest& request,
                                control::ControlResponse* response, std::string* error) {
    const auto command = static_cast<control::ControlCommandId>(request.header.command_id);
    if (request.header.request_id == 0 ||
        !control::IsFhc1RouteKind(request.header.route_kind,
                                  control::ControlPacketKind::kRequest) ||
        !IsWifiCommand(command)) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                request.header.request_id == 0 ? control::ControlError::kInvalidRequestId
                                               : control::ControlError::kUnsupportedMessage,
                response, error);
    }
    if (command == control::ControlCommandId::kGetWifiCapabilities) {
        return request.payload.empty()
                       ? SerializeWifiCapabilitiesResponse(request.header.request_id, response,
                                                           error)
                       : SerializeMalformed(request, response, error);
    }

    const std::shared_ptr<aidl::floral::device::wifi::IWifiState> service = GetService();
    if (service == nullptr) {
        return SerializeInternal(request, response, error);
    }

    aidl::floral::device::wifi::WifiControlResult update;
    ndk::ScopedAStatus status = ndk::ScopedAStatus::ok();
    switch (command) {
        case control::ControlCommandId::kSetWifiEnabled: {
            EnabledRequest parsed;
            if (!ParseEnabledRequest(request.payload, &parsed, error)) {
                return SerializeMalformed(request, response, error);
            }
            status = service->setEnabled(parsed.enabled, parsed.lease_duration_ms, &update);
            break;
        }
        case control::ControlCommandId::kReplaceWifiAccessPoints: {
            std::vector<aidl::floral::device::wifi::WifiAccessPoint> access_points;
            int64_t lease_duration_ms = 0;
            if (!ParseAccessPointsRequest(request.payload, &access_points, &lease_duration_ms,
                                          error)) {
                return SerializeMalformed(request, response, error);
            }
            status = service->replaceAccessPoints(access_points, lease_duration_ms, &update);
            break;
        }
        case control::ControlCommandId::kSetWifiConnection: {
            ConnectionRequest parsed;
            if (!ParseConnectionRequest(request.payload, &parsed, error)) {
                return SerializeMalformed(request, response, error);
            }
            status = service->setConnection(parsed.access_point_id, parsed.lease_duration_ms,
                                            &update);
            break;
        }
        case control::ControlCommandId::kSetWifiLink: {
            aidl::floral::device::wifi::WifiLinkState link;
            int64_t lease_duration_ms = 0;
            if (!ParseLinkRequest(request.payload, &link, &lease_duration_ms, error)) {
                return SerializeMalformed(request, response, error);
            }
            status = service->setLink(link, lease_duration_ms, &update);
            break;
        }
        case control::ControlCommandId::kReleaseWifiControl:
            if (!request.payload.empty()) {
                return SerializeMalformed(request, response, error);
            }
            status = service->releaseExternalControl(&update);
            break;
        case control::ControlCommandId::kPushWifiSampleBatch: {
            std::vector<aidl::floral::device::wifi::WifiSample> samples;
            int64_t lease_duration_ms = 0;
            if (!ParseSamplesRequest(request.payload, &samples, &lease_duration_ms, error)) {
                return SerializeMalformed(request, response, error);
            }
            status = service->pushSamples(samples, lease_duration_ms, &update);
            break;
        }
        case control::ControlCommandId::kGetWifiProfile: {
            if (!request.payload.empty()) {
                return SerializeMalformed(request, response, error);
            }
            aidl::floral::device::wifi::WifiProfile profile;
            std::vector<aidl::floral::device::wifi::WifiAccessPoint> access_points;
            status = service->getProfile(&profile);
            if (status.isOk()) {
                status = service->getAccessPoints(&access_points);
            }
            if (!status.isOk()) {
                ForgetService();
                return SerializeInternal(request, response, error);
            }
            return SerializeWifiProfileResponse(request.header.request_id, profile,
                                                access_points.size(), response, error);
        }
        case control::ControlCommandId::kGetWifiSnapshot: {
            if (!request.payload.empty()) {
                return SerializeMalformed(request, response, error);
            }
            aidl::floral::device::wifi::WifiSnapshot snapshot;
            status = service->getSnapshot(&snapshot);
            if (!status.isOk()) {
                ForgetService();
                return SerializeInternal(request, response, error);
            }
            return SerializeWifiSnapshotResponse(request.header.request_id, snapshot, response,
                                                 error);
        }
        case control::ControlCommandId::kListWifiAccessPoints: {
            if (!request.payload.empty()) {
                return SerializeMalformed(request, response, error);
            }
            std::vector<aidl::floral::device::wifi::WifiAccessPoint> access_points;
            status = service->getAccessPoints(&access_points);
            if (!status.isOk()) {
                ForgetService();
                return SerializeInternal(request, response, error);
            }
            return SerializeWifiAccessPointsResponse(request.header.request_id, access_points,
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
    return SerializeWifiUpdateResponse(command, request.header.request_id, update, response, error);
}

void WifiControlHandler::OnAuthorityLeaseExpired() {
    const std::shared_ptr<aidl::floral::device::wifi::IWifiState> service = GetService();
    if (service == nullptr) {
        return;
    }
    aidl::floral::device::wifi::WifiControlResult ignored;
    if (!service->releaseExternalControl(&ignored).isOk()) {
        ForgetService();
    }
}

}  // namespace floral::device::wifi
