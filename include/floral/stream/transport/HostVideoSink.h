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

#pragma once

#include "floral/stream/transport/VideoPacketProtocol.h"

#include <android-base/unique_fd.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace floral::stream::transport {

struct HostVideoSinkConfig {
    size_t max_buffered_packets = 8;
    size_t max_buffered_bytes = 16 * 1024 * 1024;
};

enum class VideoEnqueueStatus {
    kAccepted,
    kQueueFull,
    kDisconnected,
    kStopped,
    kInvalidPacket,
};

struct VideoEnqueueResult {
    VideoEnqueueStatus status = VideoEnqueueStatus::kInvalidPacket;
    bool request_key_frame = false;
};

struct HostVideoSinkStats {
    uint64_t accepted_packets = 0;
    uint64_t sent_packets = 0;
    uint64_t sent_bytes = 0;
    uint64_t rejected_queue_full = 0;
    uint64_t discarded_pending_packets = 0;
    uint64_t discarded_pending_bytes = 0;
    uint64_t send_errors = 0;
    size_t buffered_packets = 0;
    size_t buffered_bytes = 0;
};

struct HostVideoSinkDiscardResult {
    size_t packets = 0;
    size_t bytes = 0;
};

// Sends framed video access units without blocking the encoder thread. The
// caller owns recovery policy after a queue-full or disconnected result.
class HostVideoSink {
  public:
    static std::unique_ptr<HostVideoSink> Connect(const std::string& socketPath,
                                                  const HostVideoSinkConfig& config,
                                                  std::string* error);
    static std::unique_ptr<HostVideoSink> CreateFromConnectedSocket(
            android::base::unique_fd socket, const HostVideoSinkConfig& config, std::string* error);

    ~HostVideoSink();

    HostVideoSink(const HostVideoSink&) = delete;
    HostVideoSink& operator=(const HostVideoSink&) = delete;

    VideoEnqueueResult Submit(HostVideoPacket packet, std::string* error);
    VideoEnqueueResult SubmitBatch(std::vector<HostVideoPacket> packets, std::string* error);
    HostVideoSinkDiscardResult DiscardPending();
    void Stop();

    bool connected() const;
    HostVideoSinkStats GetStats() const;

  private:
    struct Impl;

    explicit HostVideoSink(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace floral::stream::transport
