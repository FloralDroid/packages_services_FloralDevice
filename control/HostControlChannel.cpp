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

#define LOG_TAG "floral-hal-control"

#include "floral/device/control/HostControlChannel.h"

#include "floral/device/control/ControlProtocol.h"
#include "floral/device/control/ControlRequestHandler.h"
#include "floral/device/socket/UnixSocketServer.h"

#include <log/log.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace floral::device::control {
namespace socket_transport = ::floral::device::socket;

using Clock = std::chrono::steady_clock;

class HostControlChannel::Impl final {
  public:
    Impl(HostControlChannelConfig config, std::shared_ptr<ControlRequestHandler> handler)
        : config_(std::move(config)), handler_(std::move(handler)), worker_(&Impl::Run, this) {}

    ~Impl() {
        stopping_.store(true);
        wake_.notify_all();
        {
            std::lock_guard lock(socket_mutex_);
            if (active_socket_fd_ >= 0) {
                shutdown(active_socket_fd_, SHUT_RDWR);
            }
        }
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    bool connected() const { return connected_.load(); }

  private:
    bool WaitFor(std::chrono::milliseconds interval) {
        std::unique_lock lock(wait_mutex_);
        return wake_.wait_for(lock, interval, [this]() { return stopping_.load(); });
    }

    std::chrono::milliseconds NextReconnectWait() const {
        if (!lease_deadline_.has_value()) {
            return config_.reconnect_interval;
        }
        const Clock::time_point now = Clock::now();
        if (now >= *lease_deadline_) {
            return std::chrono::milliseconds::zero();
        }
        return std::min(
                config_.reconnect_interval,
                std::chrono::duration_cast<std::chrono::milliseconds>(*lease_deadline_ - now));
    }

    void SetActiveSocket(int socketFd) {
        std::lock_guard lock(socket_mutex_);
        active_socket_fd_ = socketFd;
        connected_.store(socketFd >= 0);
    }

    void ClearActiveSocket(int socketFd) {
        std::lock_guard lock(socket_mutex_);
        if (active_socket_fd_ == socketFd) {
            active_socket_fd_ = -1;
            connected_.store(false);
        }
    }

    void ExpireLeaseIfNeeded() {
        if (!lease_deadline_.has_value() || Clock::now() < *lease_deadline_) {
            return;
        }
        lease_deadline_.reset();
        has_authoritative_snapshot_ = false;
        handler_->OnAuthorityLeaseExpired();
        ALOGI("Control authority lease expired; external displays were cleared");
    }

    int PollSocket(int socketFd, short events) {
        while (!stopping_.load()) {
            ExpireLeaseIfNeeded();
            pollfd descriptor{};
            descriptor.fd = socketFd;
            descriptor.events = events;
            const int result = poll(&descriptor, 1, 100);
            if (result > 0) {
                if ((descriptor.revents & events) != 0) {
                    return 1;
                }
                if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                    return 0;
                }
                continue;
            }
            if (result == 0) {
                continue;
            }
            if (errno != EINTR) {
                return 0;
            }
        }
        return 0;
    }

    bool ReadExact(int socketFd, uint8_t* output, size_t size) {
        size_t offset = 0;
        while (offset < size && !stopping_.load()) {
            if (PollSocket(socketFd, POLLIN) == 0) {
                return false;
            }
            const ssize_t received = recv(socketFd, output + offset, size - offset, 0);
            if (received > 0) {
                offset += static_cast<size_t>(received);
                continue;
            }
            if (received == 0) {
                return false;
            }
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                return false;
            }
        }
        return offset == size;
    }

    bool WriteExact(int socketFd, const uint8_t* data, size_t size) {
        size_t offset = 0;
        while (offset < size && !stopping_.load()) {
            if (PollSocket(socketFd, POLLOUT) == 0) {
                return false;
            }
            const ssize_t sent = send(socketFd, data + offset, size - offset, MSG_NOSIGNAL);
            if (sent > 0) {
                offset += static_cast<size_t>(sent);
                continue;
            }
            if (sent == 0) {
                return false;
            }
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                return false;
            }
        }
        return offset == size;
    }

    bool ProcessConnection(int socketFd, std::string* error) {
        while (!stopping_.load()) {
            SerializedControlPacketHeader serializedHeader{};
            if (!ReadExact(socketFd, serializedHeader.data(), serializedHeader.size())) {
                return false;
            }

            ControlRequest request;
            if (!ParseControlPacketHeader(serializedHeader, &request.header, error)) {
                return false;
            }
            request.payload.resize(request.header.payload_size);
            if (!request.payload.empty() &&
                !ReadExact(socketFd, request.payload.data(), request.payload.size())) {
                return false;
            }

            ControlResponse response;
            if (!handler_->Handle(request, &response, error)) {
                return false;
            }
            if (response.header.payload_size != response.payload.size()) {
                if (error != nullptr) {
                    *error = "control handler returned an inconsistent payload size";
                }
                return false;
            }
            if (response.refreshes_authority_lease) {
                has_authoritative_snapshot_ = true;
                lease_deadline_.reset();
            }

            SerializedControlPacketHeader serializedResponse{};
            if (!SerializeControlPacketHeader(response.header, &serializedResponse, error) ||
                !WriteExact(socketFd, serializedResponse.data(), serializedResponse.size()) ||
                (!response.payload.empty() &&
                 !WriteExact(socketFd, response.payload.data(), response.payload.size()))) {
                return false;
            }
        }
        return false;
    }

    void BeginDisconnectLease() {
        if (!has_authoritative_snapshot_ || lease_deadline_.has_value()) {
            return;
        }
        lease_deadline_ = Clock::now() + config_.authority_lease;
        ALOGI("Control channel disconnected; retaining external displays for %lld ms",
              static_cast<long long>(config_.authority_lease.count()));
    }

    void Run() {
        while (!stopping_.load()) {
            ExpireLeaseIfNeeded();
            std::string error;
            android::base::unique_fd socketFd = config_.acceptor(config_.socket_path, &error);
            if (socketFd.get() < 0) {
                if (!error.empty()) {
                    ALOGW("Accepting host control connection failed: %s", error.c_str());
                }
                const std::chrono::milliseconds wait = NextReconnectWait();
                if (wait <= std::chrono::milliseconds::zero()) {
                    ExpireLeaseIfNeeded();
                    continue;
                }
                if (WaitFor(wait)) {
                    break;
                }
                continue;
            }

            SetActiveSocket(socketFd.get());
            ALOGI("Accepted host control connection: %s", config_.socket_path.c_str());
            error.clear();
            (void)ProcessConnection(socketFd.get(), &error);
            ClearActiveSocket(socketFd.get());
            if (stopping_.load()) {
                break;
            }
            if (!error.empty()) {
                ALOGW("Host control connection ended: %s", error.c_str());
            }
            BeginDisconnectLease();
        }
        connected_.store(false);
    }

    // Connection configuration and request dispatch.
    const HostControlChannelConfig config_;
    const std::shared_ptr<ControlRequestHandler> handler_;

    // Worker lifecycle and reconnect wake-up.
    std::atomic<bool> stopping_{false};
    std::atomic<bool> connected_{false};
    std::mutex wait_mutex_;
    std::condition_variable wake_;
    std::thread worker_;

    // Active socket is borrowed by the worker and only used here to interrupt
    // blocking I/O during destruction.
    mutable std::mutex socket_mutex_;
    int active_socket_fd_ = -1;

    // Only the worker mutates authority state.
    bool has_authoritative_snapshot_ = false;
    std::optional<Clock::time_point> lease_deadline_;
};

std::unique_ptr<HostControlChannel> HostControlChannel::Create(
        HostControlChannelConfig config, std::shared_ptr<ControlRequestHandler> handler,
        std::string* error) {
    if (config.socket_path.empty()) {
        if (error != nullptr) {
            *error = "host control socket path is empty";
        }
        return nullptr;
    }
    if (handler == nullptr) {
        if (error != nullptr) {
            *error = "host control request handler is null";
        }
        return nullptr;
    }
    if (config.reconnect_interval <= std::chrono::milliseconds::zero() ||
        config.authority_lease <= std::chrono::milliseconds::zero()) {
        if (error != nullptr) {
            *error = "host control timing configuration must be positive";
        }
        return nullptr;
    }
    if (!config.acceptor) {
        std::unique_ptr<socket_transport::UnixSocketServer> listener =
                socket_transport::UnixSocketServer::Create(config.socket_path, 1, error);
        if (listener == nullptr) {
            return nullptr;
        }
        std::shared_ptr<socket_transport::UnixSocketServer> sharedListener(std::move(listener));
        config.acceptor = [sharedListener](const std::string&, std::string* acceptError) {
            return sharedListener->Accept(std::chrono::milliseconds::zero(), acceptError);
        };
        ALOGI("Listening for host control connections: %s", config.socket_path.c_str());
    }
    return std::unique_ptr<HostControlChannel>(
            new HostControlChannel(std::make_unique<Impl>(std::move(config), std::move(handler))));
}

HostControlChannel::HostControlChannel(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

HostControlChannel::~HostControlChannel() = default;

bool HostControlChannel::connected() const {
    return impl_ != nullptr && impl_->connected();
}

}  // namespace floral::device::control
