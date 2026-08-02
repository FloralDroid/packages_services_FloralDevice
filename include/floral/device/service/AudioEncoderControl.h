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

#include <cstdint>
#include <string>

namespace floral::device::service {

enum AudioEncoderConfigField : uint32_t {
    kAudioEncoderConfigBitrate = 1U << 0,
};

constexpr uint32_t kKnownAudioEncoderConfigFields = kAudioEncoderConfigBitrate;

enum class AudioEncoderConfigResult : uint32_t {
    kApplied = 0,
    kInvalidConfig = 1,
    kUnsupported = 2,
    kUnknownStream = 3,
};

struct AudioEncoderConfigUpdate {
    uint32_t stream_id = 0;
    uint32_t fields = 0;
    uint32_t bitrate_bps = 0;
};

struct AudioEncoderRuntimeState {
    uint32_t generation = 0;
    uint32_t bitrate_bps = 0;
    uint32_t codec_id = 1;  // Opus in FSA1.
};

class AudioEncoderControl {
  public:
    virtual ~AudioEncoderControl() = default;

    virtual bool ApplyAudioEncoderConfig(const AudioEncoderConfigUpdate& update,
                                         AudioEncoderRuntimeState* state,
                                         AudioEncoderConfigResult* result, std::string* error) = 0;
};

}  // namespace floral::device::service
