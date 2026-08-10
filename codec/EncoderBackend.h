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

#include "floral/stream/codec/EglFrameCopier.h"
#include "floral/stream/codec/EncoderTypes.h"

#include <android-base/unique_fd.h>
#include <android/hardware_buffer.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace floral::stream::codec {

// Common data path implemented by each encoder backend. The outer stream
// session remains independent of whether frames enter MediaCodec or VA-API.
class EncoderBackend {
  public:
    virtual ~EncoderBackend() = default;

    virtual bool RegisterBuffer(AHardwareBuffer* buffer, uint64_t* outBufferId,
                                std::string* error) = 0;
    virtual void UnregisterBuffer(uint64_t bufferId) = 0;
    virtual FrameCopyResult SubmitFrame(uint64_t bufferId, android::base::unique_fd acquireFence,
                                        int64_t presentationTimeNanos, std::string* error) = 0;
    virtual FrameCopyResult RepeatLastFrame(int64_t presentationTimeNanos, std::string* error) = 0;

    virtual bool SetBitrate(uint32_t bitrateBps, std::string* error) = 0;
    virtual bool RequestKeyFrame(std::string* error) = 0;
    virtual bool SignalEndOfInputStream(std::string* error) = 0;
    virtual DequeueResult DequeueOutput(int64_t timeoutUs) = 0;
    virtual StaticFrameRepeatMode static_frame_repeat_mode() const = 0;

    virtual const EncoderConfig& config() const = 0;
    virtual const std::string& codec_name() const = 0;
    virtual bool uses_native_fences() const = 0;
    virtual size_t registered_buffer_count() const = 0;
};

}  // namespace floral::stream::codec
