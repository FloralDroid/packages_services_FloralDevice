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

#include "floral/stream/audio/AudioPacketProtocol.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace floral::stream::audio {

struct HostAudioSinkConfig {
    std::string socket_path;
    size_t max_buffered_packets = 16;
};

struct HostAudioSinkStats {
    uint64_t accepted_packets = 0;
    uint64_t sent_packets = 0;
    uint64_t dropped_packets = 0;
    uint64_t connection_failures = 0;
    uint64_t send_failures = 0;
    size_t buffered_packets = 0;
};

// Owns the socket and a bounded sender queue so Opus encoding never waits for
// host I/O. A queue drop is exposed as a discontinuity on the next packet.
class HostAudioSink {
  public:
    static std::unique_ptr<HostAudioSink> Create(HostAudioSinkConfig config, std::string* error);
    ~HostAudioSink();

    HostAudioSink(const HostAudioSink&) = delete;
    HostAudioSink& operator=(const HostAudioSink&) = delete;

    bool Enqueue(HostAudioPacket packet, std::string* error);
    void DiscardPending();
    void MarkDiscontinuity();
    HostAudioSinkStats GetStats() const;

  private:
    struct Impl;

    explicit HostAudioSink(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace floral::stream::audio
