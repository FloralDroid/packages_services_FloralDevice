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

#include "floral/stream/transport/HostVideoSink.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace floral::stream::transport {
namespace {

bool SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool ValidateConfig(const HostVideoSinkConfig& config, std::string* error) {
    if (config.max_buffered_packets == 0 || config.max_buffered_bytes < kVideoPacketHeaderSize) {
        return SetError(error, "host video sink queue limits must not be zero");
    }
    return true;
}

bool ValidateConnectedStreamSocket(int socketFd, std::string* error) {
    if (socketFd < 0) {
        return SetError(error, "host video sink requires a connected socket");
    }
    int socketType = 0;
    socklen_t socketTypeSize = sizeof(socketType);
    if (getsockopt(socketFd, SOL_SOCKET, SO_TYPE, &socketType, &socketTypeSize) != 0) {
        return SetError(error, std::string("getsockopt(SO_TYPE) failed: ") + std::strerror(errno));
    }
    if (socketType != SOCK_STREAM) {
        return SetError(error, "host video sink requires a SOCK_STREAM socket");
    }
    return true;
}

bool SendAll(int socketFd, const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t sent = send(socketFd, data + offset, size - offset, MSG_NOSIGNAL);
        if (sent > 0) {
            offset += static_cast<size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

}  // namespace

struct HostVideoSink::Impl {
    struct OutboundPacket {
        SerializedVideoPacketHeader header;
        std::vector<uint8_t> payload;

        size_t WireSize() const { return header.size() + payload.size(); }
    };

    Impl(android::base::unique_fd connectedSocket, HostVideoSinkConfig sinkConfig)
        : socket(std::move(connectedSocket)), config(sinkConfig), sender(&Impl::SendLoop, this) {}

    ~Impl() { Stop(); }

    VideoEnqueueResult SubmitBatch(std::vector<HostVideoPacket> packets, std::string* error) {
        VideoEnqueueResult result;
        if (packets.empty()) {
            SetError(error, "host video packet batch must not be empty");
            return result;
        }

        std::vector<OutboundPacket> outboundPackets;
        outboundPackets.reserve(packets.size());
        size_t totalWireSize = 0;
        bool exceedsQueueByteLimit = false;
        bool requestKeyFrame = false;
        for (HostVideoPacket& packet : packets) {
            if (packet.payload.size() > std::numeric_limits<uint32_t>::max()) {
                SetError(error, "video packet payload exceeds the wire protocol limit");
                return result;
            }
            packet.header.payload_size = static_cast<uint32_t>(packet.payload.size());

            OutboundPacket outbound;
            if (!SerializeVideoPacketHeader(packet.header, &outbound.header, error)) {
                return result;
            }
            outbound.payload = std::move(packet.payload);
            const size_t wireSize = outbound.WireSize();
            if (wireSize > config.max_buffered_bytes ||
                totalWireSize > config.max_buffered_bytes - wireSize) {
                exceedsQueueByteLimit = true;
            } else {
                totalWireSize += wireSize;
            }
            requestKeyFrame |= (packet.header.flags & kVideoPacketEndOfStream) == 0;
            outboundPackets.push_back(std::move(outbound));
        }

        std::lock_guard lock(mutex);
        if (stopped) {
            result.status = VideoEnqueueStatus::kStopped;
            return result;
        }
        if (!is_connected) {
            result.status = VideoEnqueueStatus::kDisconnected;
            return result;
        }
        if (outboundPackets.size() > config.max_buffered_packets ||
            buffered_packets > config.max_buffered_packets - outboundPackets.size() ||
            exceedsQueueByteLimit || buffered_bytes > config.max_buffered_bytes - totalWireSize) {
            stats.rejected_queue_full += outboundPackets.size();
            result.status = VideoEnqueueStatus::kQueueFull;
            result.request_key_frame = requestKeyFrame;
            return result;
        }

        buffered_bytes += totalWireSize;
        buffered_packets += outboundPackets.size();
        stats.accepted_packets += outboundPackets.size();
        for (OutboundPacket& outbound : outboundPackets) {
            queue.push_back(std::move(outbound));
        }
        UpdateBufferedStatsLocked();
        condition.notify_one();
        result.status = VideoEnqueueStatus::kAccepted;
        return result;
    }

    HostVideoSinkDiscardResult DiscardPending() {
        std::lock_guard lock(mutex);
        HostVideoSinkDiscardResult result;
        result.packets = queue.size();
        for (const OutboundPacket& packet : queue) {
            result.bytes += packet.WireSize();
        }
        queue.clear();
        buffered_packets -= result.packets;
        buffered_bytes -= result.bytes;
        stats.discarded_pending_packets += result.packets;
        stats.discarded_pending_bytes += result.bytes;
        UpdateBufferedStatsLocked();
        return result;
    }

    void Stop() {
        {
            std::lock_guard lifecycleLock(lifecycle_mutex);
            if (joined) {
                return;
            }
            {
                std::lock_guard lock(mutex);
                stopped = true;
                queue.clear();
                buffered_packets = in_flight ? 1 : 0;
                buffered_bytes = in_flight_bytes;
                UpdateBufferedStatsLocked();
            }
            shutdown(socket.get(), SHUT_RDWR);
            condition.notify_all();
            if (sender.joinable()) {
                sender.join();
            }
            joined = true;
        }
    }

    bool connected() const {
        std::lock_guard lock(mutex);
        return is_connected && !stopped;
    }

    HostVideoSinkStats GetStats() const {
        std::lock_guard lock(mutex);
        return stats;
    }

    void SendLoop() {
        while (true) {
            OutboundPacket packet;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [this] { return stopped || !queue.empty(); });
                if (stopped) {
                    return;
                }
                packet = std::move(queue.front());
                queue.pop_front();
                in_flight = true;
                in_flight_bytes = packet.WireSize();
            }

            const bool sent = SendAll(socket.get(), packet.header.data(), packet.header.size()) &&
                              SendAll(socket.get(), packet.payload.data(), packet.payload.size());

            std::lock_guard lock(mutex);
            buffered_bytes -= in_flight_bytes;
            --buffered_packets;
            in_flight = false;
            in_flight_bytes = 0;
            if (!sent) {
                is_connected = false;
                ++stats.send_errors;
                queue.clear();
                buffered_packets = 0;
                buffered_bytes = 0;
                UpdateBufferedStatsLocked();
                return;
            }
            ++stats.sent_packets;
            stats.sent_bytes += packet.WireSize();
            UpdateBufferedStatsLocked();
        }
    }

    void UpdateBufferedStatsLocked() {
        stats.buffered_packets = buffered_packets;
        stats.buffered_bytes = buffered_bytes;
    }

    android::base::unique_fd socket;
    const HostVideoSinkConfig config;

    mutable std::mutex mutex;
    std::condition_variable condition;
    std::deque<OutboundPacket> queue;
    bool stopped = false;
    bool is_connected = true;
    bool in_flight = false;
    size_t in_flight_bytes = 0;
    size_t buffered_packets = 0;
    size_t buffered_bytes = 0;
    HostVideoSinkStats stats;

    std::mutex lifecycle_mutex;
    bool joined = false;
    std::thread sender;
};

HostVideoSink::HostVideoSink(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<HostVideoSink> HostVideoSink::Connect(const std::string& socketPath,
                                                      const HostVideoSinkConfig& config,
                                                      std::string* error) {
    if (!ValidateConfig(config, error)) {
        return nullptr;
    }
    sockaddr_un address{};
    if (socketPath.empty() || socketPath.size() >= sizeof(address.sun_path)) {
        SetError(error, "host video socket path is empty or too long");
        return nullptr;
    }

    android::base::unique_fd socketFd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!socketFd.ok()) {
        SetError(error, std::string("socket(AF_UNIX) failed: ") + std::strerror(errno));
        return nullptr;
    }

    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size() + 1);
    if (connect(socketFd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) !=
        0) {
        SetError(error,
                 std::string("connecting host video socket failed: ") + std::strerror(errno));
        return nullptr;
    }
    return CreateFromConnectedSocket(std::move(socketFd), config, error);
}

std::unique_ptr<HostVideoSink> HostVideoSink::CreateFromConnectedSocket(
        android::base::unique_fd socket, const HostVideoSinkConfig& config, std::string* error) {
    if (!ValidateConfig(config, error) || !ValidateConnectedStreamSocket(socket.get(), error)) {
        return nullptr;
    }
    auto impl = std::make_unique<Impl>(std::move(socket), config);
    return std::unique_ptr<HostVideoSink>(new HostVideoSink(std::move(impl)));
}

HostVideoSink::~HostVideoSink() = default;

VideoEnqueueResult HostVideoSink::Submit(HostVideoPacket packet, std::string* error) {
    std::vector<HostVideoPacket> packets;
    packets.push_back(std::move(packet));
    return impl_->SubmitBatch(std::move(packets), error);
}

VideoEnqueueResult HostVideoSink::SubmitBatch(std::vector<HostVideoPacket> packets,
                                              std::string* error) {
    return impl_->SubmitBatch(std::move(packets), error);
}

HostVideoSinkDiscardResult HostVideoSink::DiscardPending() {
    return impl_->DiscardPending();
}

void HostVideoSink::Stop() {
    impl_->Stop();
}

bool HostVideoSink::connected() const {
    return impl_->connected();
}

HostVideoSinkStats HostVideoSink::GetStats() const {
    return impl_->GetStats();
}

}  // namespace floral::stream::transport
