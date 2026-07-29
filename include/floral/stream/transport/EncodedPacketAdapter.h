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

#include "floral/stream/VideoGeometry.h"
#include "floral/stream/codec/MediaCodecSurfaceEncoder.h"
#include "floral/stream/transport/VideoPacketProtocol.h"

#include <cstdint>

namespace floral::stream::transport {

// Android 12's NDK omits the key-frame constant even though MediaCodec and
// Stagefright expose the stable BUFFER_FLAG_KEY_FRAME value as bit zero.
constexpr uint32_t kMediaCodecKeyFrameFlag = 1u;

struct EncodedStreamInfo {
    uint32_t stream_id = 0;
    uint32_t generation = 0;
    uint64_t sequence = 0;
    VideoGeometry geometry;
    uint32_t additional_flags = 0;
    uint64_t frame_submit_time_ns = 0;
};

HostVideoPacket MakeHostVideoPacket(codec::EncodedPacket packet,
                                    const EncodedStreamInfo& streamInfo);

}  // namespace floral::stream::transport
