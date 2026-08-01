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

#include "floral/device/display/topology/DisplayTopologyControlHandler.h"

#include "floral/device/display/topology/DisplayTopologyControlProtocol.h"
#include "floral/device/display/topology/DisplayTopologyController.h"

#include <utility>
#include <vector>

namespace floral::device::display::topology {

DisplayTopologyControlHandler::DisplayTopologyControlHandler(
        std::shared_ptr<DisplayTopologyController> controller)
    : controller_(std::move(controller)) {}

bool DisplayTopologyControlHandler::Handle(const control::ControlRequest& request,
                                           control::ControlResponse* response, std::string* error) {
    if (response == nullptr) {
        if (error != nullptr) {
            *error = "control response output is null";
        }
        return false;
    }
    if (request.header.request_id == 0) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.message_type,
                control::ControlError::kInvalidRequestId, response, error);
    }
    if (request.header.message_type !=
        static_cast<uint16_t>(control::ControlMessageType::kReplaceDisplayTopologyRequest)) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.message_type,
                control::ControlError::kUnsupportedMessage, response, error);
    }

    std::vector<ManagedPhysicalDisplay> displays;
    if (!ParseReplaceDisplayTopologyRequest(request.payload, &displays, error)) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.message_type,
                control::ControlError::kMalformedRequest, response, error);
    }
    if (controller_ == nullptr) {
        return control::SerializeControlErrorResponse(
                request.header.request_id, request.header.message_type,
                control::ControlError::kInternalError, response, error);
    }
    const TopologyUpdate update = controller_->ReplaceExternalDisplays(std::move(displays));
    return SerializeReplaceDisplayTopologyResponse(update, request.header.request_id, response,
                                                   error);
}

void DisplayTopologyControlHandler::OnAuthorityLeaseExpired() {
    if (controller_ != nullptr) {
        (void)controller_->ClearExternalDisplays();
    }
}

}  // namespace floral::device::display::topology
