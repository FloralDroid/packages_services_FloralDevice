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
#include "floral/stream/codec/EncoderTypes.h"

#include <cstdint>
#include <string>

namespace floral::device::service {

enum VideoEncoderConfigField : uint32_t {
    kVideoEncoderConfigBitrate = 1U << 0,
    kVideoEncoderConfigFrameRate = 1U << 1,
    kVideoEncoderConfigResolution = 1U << 2,
    kVideoEncoderConfigIFrameInterval = 1U << 3,
    kVideoEncoderConfigBackend = 1U << 4,
    kVideoEncoderConfigCodec = 1U << 5,
};

constexpr uint32_t kKnownVideoEncoderConfigFields =
        kVideoEncoderConfigBitrate | kVideoEncoderConfigFrameRate | kVideoEncoderConfigResolution |
        kVideoEncoderConfigIFrameInterval | kVideoEncoderConfigBackend | kVideoEncoderConfigCodec;

enum class VideoEncoderConfigResult : uint32_t {
    kApplied = 0,
    kPending = 1,
    kInvalidConfig = 2,
    kUnsupported = 3,
    kNoActiveStream = 4,
};

struct VideoEncoderConfigUpdate {
    uint64_t display_id = 0;
    uint32_t stream_id = 0;
    uint32_t fields = 0;
    floral::stream::codec::EncoderBackendType backend =
            floral::stream::codec::EncoderBackendType::kMediaCodecSoftware;
    uint32_t codec_id = 1;  // H.264 in the FSV2 stream.
    uint32_t coded_width = 0;
    uint32_t coded_height = 0;
    uint32_t frame_rate = 0;
    uint32_t bitrate_bps = 0;
    uint32_t i_frame_interval_seconds = 0;
};

struct VideoEncoderRuntimeState {
    uint32_t generation = 0;
    bool pending = false;
    floral::stream::codec::EncoderConfig encoder;
    floral::stream::VideoGeometry geometry;
};

class VideoEncoderControl {
  public:
    virtual ~VideoEncoderControl() = default;

    // Structural changes are reported as pending until a replacement session
    // is created and publishes its new generation to the frame consumer.
    virtual bool ApplyVideoEncoderConfig(const VideoEncoderConfigUpdate& update,
                                         VideoEncoderRuntimeState* state,
                                         VideoEncoderConfigResult* result, std::string* error) = 0;
};

}  // namespace floral::device::service
