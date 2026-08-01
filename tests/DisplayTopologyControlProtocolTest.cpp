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

#include "floral/stream/topology/DisplayTopologyControlProtocol.h"

#include "floral/stream/topology/DisplayTopologyControlHandler.h"
#include "floral/stream/topology/DisplayTopologyController.h"
#include "floral/stream/topology/DisplayTopologyStateService.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace floral::stream::topology {
namespace {

ManagedPhysicalDisplay ExternalDisplay(uint64_t id, uint8_t port) {
    ManagedPhysicalDisplay display;
    display.display_id = id;
    display.port = port;
    display.width = 1920;
    display.height = 1080;
    display.dpi = 320;
    display.supported_refresh_rates_hz = {30, 60};
    display.active_refresh_rate_hz = 60;
    display.name = "Control Display " + std::to_string(id);
    return display;
}

TEST(DisplayTopologyControlProtocolTest, RoundTripsFullExternalDisplaySnapshot) {
    const std::vector<ManagedPhysicalDisplay> source = {
            ExternalDisplay(2, 1),
            ExternalDisplay(9, 3),
    };
    std::vector<uint8_t> payload;
    std::string error;
    ASSERT_TRUE(SerializeReplaceDisplayTopologyRequest(source, &payload, &error)) << error;

    std::vector<ManagedPhysicalDisplay> parsed;
    ASSERT_TRUE(ParseReplaceDisplayTopologyRequest(payload, &parsed, &error)) << error;
    ASSERT_EQ(parsed.size(), source.size());
    for (size_t index = 0; index < source.size(); ++index) {
        EXPECT_EQ(parsed[index].display_id, source[index].display_id);
        EXPECT_EQ(parsed[index].port, source[index].port);
        EXPECT_EQ(parsed[index].width, source[index].width);
        EXPECT_EQ(parsed[index].height, source[index].height);
        EXPECT_EQ(parsed[index].dpi, source[index].dpi);
        EXPECT_EQ(parsed[index].supported_refresh_rates_hz,
                  source[index].supported_refresh_rates_hz);
        EXPECT_EQ(parsed[index].active_refresh_rate_hz, source[index].active_refresh_rate_hz);
        EXPECT_EQ(parsed[index].name, source[index].name);
    }
}

TEST(DisplayTopologyControlProtocolTest, RejectsTruncatedAndTrailingPayloads) {
    std::vector<uint8_t> payload;
    std::string error;
    ASSERT_TRUE(SerializeReplaceDisplayTopologyRequest({ExternalDisplay(2, 1)}, &payload, &error));
    std::vector<ManagedPhysicalDisplay> parsed;

    payload.pop_back();
    EXPECT_FALSE(ParseReplaceDisplayTopologyRequest(payload, &parsed, &error));
    ASSERT_TRUE(SerializeReplaceDisplayTopologyRequest({ExternalDisplay(2, 1)}, &payload, &error));
    payload.push_back(0);
    EXPECT_FALSE(ParseReplaceDisplayTopologyRequest(payload, &parsed, &error));
}

TEST(DisplayTopologyControlProtocolTest, RejectsNonzeroRecordPadding) {
    ManagedPhysicalDisplay display = ExternalDisplay(2, 1);
    display.name = "Pad";
    std::vector<uint8_t> payload;
    std::string error;
    ASSERT_TRUE(SerializeReplaceDisplayTopologyRequest({display}, &payload, &error));
    payload.back() = 1;

    std::vector<ManagedPhysicalDisplay> parsed;
    EXPECT_FALSE(ParseReplaceDisplayTopologyRequest(payload, &parsed, &error));
}

TEST(DisplayTopologyControlProtocolTest, RejectsInvalidUtf8DisplayName) {
    ManagedPhysicalDisplay display = ExternalDisplay(2, 1);
    display.name = std::string("Invalid ") + static_cast<char>(0xc0) + static_cast<char>(0x80);
    std::vector<uint8_t> payload;
    std::string error;
    EXPECT_FALSE(SerializeReplaceDisplayTopologyRequest({display}, &payload, &error));
}

TEST(DisplayTopologyControlProtocolTest, HandlerPublishesSnapshotAndCorrelatesResponse) {
    auto stateService = ndk::SharedRefBase::make<DisplayTopologyStateService>();
    auto controller = std::make_shared<DisplayTopologyController>(stateService);
    DisplayTopologyControlHandler handler(controller);

    control::ControlRequest request;
    request.header.message_type =
            static_cast<uint16_t>(control::ControlMessageType::kReplaceDisplayTopologyRequest);
    request.header.request_id = 42;
    std::string error;
    ASSERT_TRUE(SerializeReplaceDisplayTopologyRequest({ExternalDisplay(2, 1)}, &request.payload,
                                                       &error))
            << error;
    request.header.payload_size = static_cast<uint32_t>(request.payload.size());

    control::ControlResponse response;
    ASSERT_TRUE(handler.Handle(request, &response, &error)) << error;
    EXPECT_EQ(response.header.message_type,
              static_cast<uint16_t>(control::ControlMessageType::kReplaceDisplayTopologyResponse));
    EXPECT_EQ(response.header.request_id, 42u);
    EXPECT_TRUE(response.refreshes_authority_lease);

    TopologyUpdate update;
    ASSERT_TRUE(ParseReplaceDisplayTopologyResponse(response.payload, &update, &error)) << error;
    EXPECT_EQ(update.result, TopologyUpdateResult::kApplied);
    EXPECT_EQ(update.generation, 2u);

    aidl::floral::display::topology::TopologySnapshot snapshot;
    ASSERT_TRUE(stateService->getSnapshot(&snapshot).isOk());
    ASSERT_EQ(snapshot.externalDisplays.size(), 1u);
    EXPECT_EQ(snapshot.externalDisplays[0].displayId, 2);

    handler.OnAuthorityLeaseExpired();
    ASSERT_TRUE(stateService->getSnapshot(&snapshot).isOk());
    EXPECT_EQ(snapshot.generation, 3);
    EXPECT_TRUE(snapshot.externalDisplays.empty());
}

TEST(DisplayTopologyControlProtocolTest, HandlerRejectsZeroRequestIdWithoutMutatingState) {
    auto stateService = ndk::SharedRefBase::make<DisplayTopologyStateService>();
    auto controller = std::make_shared<DisplayTopologyController>(stateService);
    DisplayTopologyControlHandler handler(controller);

    control::ControlRequest request;
    request.header.message_type =
            static_cast<uint16_t>(control::ControlMessageType::kReplaceDisplayTopologyRequest);
    ASSERT_TRUE(SerializeReplaceDisplayTopologyRequest({ExternalDisplay(2, 1)}, &request.payload,
                                                       nullptr));
    request.header.payload_size = static_cast<uint32_t>(request.payload.size());

    control::ControlResponse response;
    ASSERT_TRUE(handler.Handle(request, &response, nullptr));
    EXPECT_EQ(response.header.message_type,
              static_cast<uint16_t>(control::ControlMessageType::kErrorResponse));
    EXPECT_FALSE(response.refreshes_authority_lease);

    aidl::floral::display::topology::TopologySnapshot snapshot;
    ASSERT_TRUE(stateService->getSnapshot(&snapshot).isOk());
    EXPECT_EQ(snapshot.generation, 1);
    EXPECT_TRUE(snapshot.externalDisplays.empty());
}

}  // namespace
}  // namespace floral::stream::topology
