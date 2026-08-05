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

#define LOG_TAG "floral-host-audio"

#include "floral/stream/audio/HostAudioSink.h"
#include "floral/device/socket/UnixSocketServer.h"

#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <log/log.h>

namespace floral::stream::audio {
namespace {

constexpr auto kReconnectDelay = std::chrono::milliseconds(250);

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool SendAll(int socket_fd, const void* bytes, size_t size) {
    const auto* data = static_cast<const uint8_t*>(bytes);
    size_t offset = 0;
    while (offset < size) {
        const ssize_t sent = send(socket_fd, data + offset, size - offset, MSG_NOSIGNAL);
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

struct HostAudioSink::Impl {
    Impl(HostAudioSinkConfig sink_config,
         std::unique_ptr<floral::device::socket::UnixSocketServer> socket_server)
        : config(std::move(sink_config)),
          server(std::move(socket_server)),
          sender(&Impl::SenderLoop, this) {}

    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stopped = true;
            queue.clear();
        }
        condition.notify_all();
        if (sender.joinable()) {
            sender.join();
        }
    }

    bool Enqueue(HostAudioPacket packet, std::string* error) {
        if (packet.payload.empty() || packet.payload.size() > 1275) {
            return SetError(error, "encoded audio payload is invalid");
        }
        packet.header.payload_size = static_cast<uint32_t>(packet.payload.size());

        std::lock_guard lock(mutex);
        if (stopped) {
            return SetError(error, "host audio sink is stopped");
        }
        if (active_generation != packet.header.generation) {
            stats.dropped_packets += queue.size();
            queue.clear();
            active_generation = packet.header.generation;
            force_discontinuity = true;
        }
        if (queue.size() == config.max_buffered_packets) {
            queue.pop_front();
            ++stats.dropped_packets;
            force_discontinuity = true;
        }
        queue.push_back(std::move(packet));
        ++stats.accepted_packets;
        stats.buffered_packets = queue.size();
        condition.notify_one();
        return true;
    }

    void MarkDiscontinuity() {
        std::lock_guard lock(mutex);
        force_discontinuity = true;
    }

    void DiscardPending() {
        std::lock_guard lock(mutex);
        stats.dropped_packets += queue.size();
        queue.clear();
        stats.buffered_packets = 0;
        force_discontinuity = true;
    }

    HostAudioSinkStats GetStats() const {
        std::lock_guard lock(mutex);
        return stats;
    }

    void SenderLoop() {
        int socketFd = -1;
        auto nextConnectAttempt = std::chrono::steady_clock::time_point::min();
        uint32_t deliveredGeneration = 0;
        uint64_t deliveredSequence = 0;

        while (true) {
            HostAudioPacket packet;
            bool discontinuity = false;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [this]() { return stopped || !queue.empty(); });
                if (stopped) {
                    break;
                }
                packet = std::move(queue.front());
                queue.pop_front();
                discontinuity = force_discontinuity;
                force_discontinuity = false;
                stats.buffered_packets = queue.size();
            }

            if (socketFd < 0) {
                const auto now = std::chrono::steady_clock::now();
                if (now < nextConnectAttempt) {
                    MarkDiscontinuity();
                    continue;
                }
                std::string acceptError;
                android::base::unique_fd accepted =
                        server->Accept(std::chrono::milliseconds::zero(), &acceptError);
                socketFd = accepted.release();
                if (socketFd < 0) {
                    if (!acceptError.empty()) {
                        std::lock_guard lock(mutex);
                        ++stats.connection_failures;
                        ALOGW("accepting host audio connection failed: %s", acceptError.c_str());
                    }
                    nextConnectAttempt = now + kReconnectDelay;
                    MarkDiscontinuity();
                    continue;
                }
                discontinuity = true;
                ALOGI("accepted host audio connection: %s", config.socket_path.c_str());
            }

            const bool streamStart = packet.header.generation != deliveredGeneration;
            packet.header.sequence = streamStart ? 1 : deliveredSequence + 1;
            if (streamStart) {
                packet.header.flags |= kAudioPacketStreamStart;
                discontinuity = true;
            }
            if (discontinuity) {
                packet.header.flags |= kAudioPacketDiscontinuity;
            }

            SerializedAudioPacketHeader header;
            std::string error;
            const bool serialized = SerializeAudioPacketHeader(packet.header, &header, &error);
            const bool sent = serialized && SendAll(socketFd, header.data(), header.size()) &&
                              SendAll(socketFd, packet.payload.data(), packet.payload.size());
            if (!sent) {
                if (!serialized) {
                    ALOGE("invalid audio packet: %s", error.c_str());
                }
                close(socketFd);
                socketFd = -1;
                nextConnectAttempt = std::chrono::steady_clock::now() + kReconnectDelay;
                {
                    std::lock_guard lock(mutex);
                    ++stats.send_failures;
                    force_discontinuity = true;
                }
                continue;
            }

            deliveredGeneration = packet.header.generation;
            deliveredSequence = packet.header.sequence;
            std::lock_guard lock(mutex);
            ++stats.sent_packets;
        }

        if (socketFd >= 0) {
            close(socketFd);
        }
    }

    const HostAudioSinkConfig config;
    const std::unique_ptr<floral::device::socket::UnixSocketServer> server;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::deque<HostAudioPacket> queue;
    bool stopped = false;
    bool force_discontinuity = true;
    uint32_t active_generation = 0;
    HostAudioSinkStats stats;
    std::thread sender;
};

std::unique_ptr<HostAudioSink> HostAudioSink::Create(HostAudioSinkConfig config,
                                                     std::string* error) {
    if (config.socket_path.empty() || config.max_buffered_packets == 0) {
        SetError(error, "host audio sink configuration is invalid");
        return nullptr;
    }
    std::unique_ptr<floral::device::socket::UnixSocketServer> server =
            floral::device::socket::UnixSocketServer::Create(config.socket_path, 1, error);
    if (server == nullptr) {
        return nullptr;
    }
    ALOGI("listening for host audio connections: %s", config.socket_path.c_str());
    return std::unique_ptr<HostAudioSink>(
            new HostAudioSink(std::make_unique<Impl>(std::move(config), std::move(server))));
}

HostAudioSink::HostAudioSink(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

HostAudioSink::~HostAudioSink() = default;

bool HostAudioSink::Enqueue(HostAudioPacket packet, std::string* error) {
    return impl_->Enqueue(std::move(packet), error);
}

void HostAudioSink::DiscardPending() {
    impl_->DiscardPending();
}

void HostAudioSink::MarkDiscontinuity() {
    impl_->MarkDiscontinuity();
}

HostAudioSinkStats HostAudioSink::GetStats() const {
    return impl_->GetStats();
}

}  // namespace floral::stream::audio
