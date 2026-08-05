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

#include "floral/device/control/HostControlChannel.h"

#include "floral/device/control/ControlProtocol.h"
#include "floral/device/display/topology/DisplayTopologyControlHandler.h"
#include "floral/device/display/topology/DisplayTopologyControlProtocol.h"
#include "floral/device/display/topology/DisplayTopologyController.h"
#include "floral/device/display/topology/DisplayTopologyStateService.h"

#include <android-base/unique_fd.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <sys/socket.h>

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace floral::device::control {
namespace {

using topology::DisplayTopologyControlHandler;
using topology::DisplayTopologyController;
using topology::DisplayTopologyStateService;
using topology::ManagedPhysicalDisplay;
using topology::TopologyUpdate;
using topology::TopologyUpdateResult;

struct ConnectorQueue {
    std::mutex mutex;
    std::deque<android::base::unique_fd> sockets;
    std::atomic<uint32_t> calls{0};
};

std::array<android::base::unique_fd, 2> CreateSocketPair() {
    int sockets[2] = {-1, -1};
    EXPECT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets), 0);
    return {
            android::base::unique_fd(sockets[0]),
            android::base::unique_fd(sockets[1]),
    };
}

bool WriteExact(int socketFd, const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t sent = send(socketFd, data + offset, size - offset, MSG_NOSIGNAL);
        if (sent <= 0) {
            return false;
        }
        offset += static_cast<size_t>(sent);
    }
    return true;
}

bool ReadExact(int socketFd, uint8_t* output, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        pollfd descriptor{};
        descriptor.fd = socketFd;
        descriptor.events = POLLIN;
        if (poll(&descriptor, 1, 5000) <= 0 || (descriptor.revents & POLLIN) == 0) {
            return false;
        }
        const ssize_t received = recv(socketFd, output + offset, size - offset, 0);
        if (received <= 0) {
            return false;
        }
        offset += static_cast<size_t>(received);
    }
    return true;
}

ManagedPhysicalDisplay ExternalDisplay() {
    ManagedPhysicalDisplay display;
    display.display_id = 2;
    display.port = 1;
    display.width = 1280;
    display.height = 720;
    display.dpi = 240;
    display.supported_refresh_rates_hz = {30, 60};
    display.active_refresh_rate_hz = 60;
    display.name = "Leased External Display";
    return display;
}

bool SendTopologyRequest(int socketFd, uint32_t requestId,
                         const std::vector<ManagedPhysicalDisplay>& displays) {
    std::vector<uint8_t> payload;
    std::string error;
    if (!topology::SerializeReplaceDisplayTopologyRequest(displays, &payload, &error)) {
        return false;
    }
    ControlPacketHeader header;
    header.command_id = static_cast<uint16_t>(ControlCommandId::kReplaceDisplayTopology);
    header.route_kind = MakeFhc1RouteKind(ControlPacketKind::kRequest);
    header.request_id = requestId;
    header.payload_size = static_cast<uint32_t>(payload.size());
    SerializedControlPacketHeader serialized{};
    return SerializeControlPacketHeader(header, &serialized, &error) &&
           WriteExact(socketFd, serialized.data(), serialized.size()) &&
           WriteExact(socketFd, payload.data(), payload.size());
}

bool ReadTopologyResponse(int socketFd, uint32_t expectedRequestId, TopologyUpdate* update) {
    SerializedControlPacketHeader serialized{};
    if (!ReadExact(socketFd, serialized.data(), serialized.size())) {
        return false;
    }
    ControlPacketHeader header;
    std::string error;
    if (!ParseControlPacketHeader(serialized, &header, &error) ||
        header.command_id != static_cast<uint16_t>(ControlCommandId::kReplaceDisplayTopology) ||
        header.route_kind != MakeFhc1RouteKind(ControlPacketKind::kResponse) ||
        header.request_id != expectedRequestId) {
        return false;
    }
    std::vector<uint8_t> payload(header.payload_size);
    return ReadExact(socketFd, payload.data(), payload.size()) &&
           topology::ParseReplaceDisplayTopologyResponse(payload, update, &error);
}

bool WaitUntil(const std::function<bool()>& predicate) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

std::unique_ptr<HostControlChannel> CreateChannel(
        const std::shared_ptr<ConnectorQueue>& connectorQueue,
        const std::shared_ptr<DisplayTopologyStateService>& stateService,
        std::chrono::milliseconds lease) {
    auto controller = std::make_shared<DisplayTopologyController>(stateService);
    auto handler = std::make_shared<DisplayTopologyControlHandler>(controller);
    HostControlChannelConfig config;
    config.socket_path = "injected-control-socket";
    config.reconnect_interval = std::chrono::milliseconds(2);
    config.authority_lease = lease;
    config.acceptor = [connectorQueue](const std::string&, std::string* error) {
        ++connectorQueue->calls;
        std::lock_guard lock(connectorQueue->mutex);
        if (connectorQueue->sockets.empty()) {
            if (error != nullptr) {
                *error = "no injected control socket";
            }
            return android::base::unique_fd{};
        }
        android::base::unique_fd socket = std::move(connectorQueue->sockets.front());
        connectorQueue->sockets.pop_front();
        return socket;
    };
    std::string error;
    return HostControlChannel::Create(std::move(config), std::move(handler), &error);
}

TEST(HostControlChannelTest, ClearsExternalDisplaysWhenAuthorityLeaseExpires) {
    auto sockets = CreateSocketPair();
    auto connectorQueue = std::make_shared<ConnectorQueue>();
    connectorQueue->sockets.push_back(std::move(sockets[0]));
    auto stateService = ndk::SharedRefBase::make<DisplayTopologyStateService>();
    std::unique_ptr<HostControlChannel> channel =
            CreateChannel(connectorQueue, stateService, std::chrono::milliseconds(30));
    ASSERT_NE(channel, nullptr);
    ASSERT_TRUE(WaitUntil([&channel]() { return channel->connected(); }));

    ASSERT_TRUE(SendTopologyRequest(sockets[1].get(), 7, {ExternalDisplay()}));
    TopologyUpdate update;
    ASSERT_TRUE(ReadTopologyResponse(sockets[1].get(), 7, &update));
    EXPECT_EQ(update.result, TopologyUpdateResult::kApplied);

    aidl::floral::device::display::topology::TopologySnapshot snapshot;
    ASSERT_TRUE(stateService->getSnapshot(&snapshot).isOk());
    ASSERT_EQ(snapshot.externalDisplays.size(), 1u);
    sockets[1].reset();

    ASSERT_TRUE(WaitUntil([&stateService]() {
        aidl::floral::device::display::topology::TopologySnapshot current;
        return stateService->getSnapshot(&current).isOk() && current.externalDisplays.empty();
    }));
    ASSERT_TRUE(stateService->getSnapshot(&snapshot).isOk());
    EXPECT_EQ(snapshot.generation, 3);
}

TEST(HostControlChannelTest, ReconnectedSnapshotCancelsPendingLeaseWithoutHotplug) {
    auto first = CreateSocketPair();
    auto second = CreateSocketPair();
    auto connectorQueue = std::make_shared<ConnectorQueue>();
    connectorQueue->sockets.push_back(std::move(first[0]));
    connectorQueue->sockets.push_back(std::move(second[0]));
    auto stateService = ndk::SharedRefBase::make<DisplayTopologyStateService>();
    std::unique_ptr<HostControlChannel> channel =
            CreateChannel(connectorQueue, stateService, std::chrono::milliseconds(100));
    ASSERT_NE(channel, nullptr);
    ASSERT_TRUE(WaitUntil([&channel]() { return channel->connected(); }));

    ASSERT_TRUE(SendTopologyRequest(first[1].get(), 1, {ExternalDisplay()}));
    TopologyUpdate update;
    ASSERT_TRUE(ReadTopologyResponse(first[1].get(), 1, &update));
    ASSERT_EQ(update.result, TopologyUpdateResult::kApplied);
    first[1].reset();

    ASSERT_TRUE(WaitUntil([&connectorQueue]() { return connectorQueue->calls.load() >= 2; }));
    ASSERT_TRUE(SendTopologyRequest(second[1].get(), 2, {ExternalDisplay()}));
    ASSERT_TRUE(ReadTopologyResponse(second[1].get(), 2, &update));
    EXPECT_EQ(update.result, TopologyUpdateResult::kUnchanged);
    EXPECT_EQ(update.generation, 2u);

    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    aidl::floral::device::display::topology::TopologySnapshot snapshot;
    ASSERT_TRUE(stateService->getSnapshot(&snapshot).isOk());
    EXPECT_EQ(snapshot.generation, 2);
    ASSERT_EQ(snapshot.externalDisplays.size(), 1u);
    EXPECT_EQ(snapshot.externalDisplays[0].displayId, 2);
}

}  // namespace
}  // namespace floral::device::control
