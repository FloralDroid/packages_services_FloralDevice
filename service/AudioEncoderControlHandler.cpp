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

#include "floral/device/service/AudioEncoderControlHandler.h"

#include "floral/device/control/ControlProtocol.h"
#include "floral/device/service/AudioEncoderControlProtocol.h"

#include <utility>

namespace floral::device::service {

AudioEncoderControlHandler::AudioEncoderControlHandler(std::shared_ptr<AudioEncoderControl> control)
    : control_(std::move(control)) {}

bool AudioEncoderControlHandler::Handle(const control::ControlRequest& request,
                                        control::ControlResponse* response, std::string* error) {
    if (response == nullptr) {
        if (error != nullptr) {
            *error = "control response output is null";
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
        request.header.command_id !=
                static_cast<uint16_t>(control::ControlCommandId::kSetAudioEncoderConfig)) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                control::ControlError::kUnsupportedMessage, response, error);
    }

    AudioEncoderConfigUpdate update;
    if (!ParseAudioEncoderConfigRequest(request.payload, &update, error)) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                control::ControlError::kMalformedRequest, response, error);
    }
    if (control_ == nullptr) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                control::ControlError::kInternalError, response, error);
    }

    AudioEncoderRuntimeState state;
    AudioEncoderConfigResult result = AudioEncoderConfigResult::kInvalidConfig;
    if (!control_->ApplyAudioEncoderConfig(update, &state, &result, error)) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.command_id,
                control::ControlError::kInternalError, response, error);
    }
    return SerializeAudioEncoderConfigResponse(state, result, request.header.request_id, response,
                                               error);
}

void AudioEncoderControlHandler::OnAuthorityLeaseExpired() {
    // Encoder tuning persists until an explicit replacement update, just like
    // the video encoder configuration.
}

}  // namespace floral::device::service
