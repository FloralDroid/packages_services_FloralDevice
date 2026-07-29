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
#include "floral/stream/codec/EglFrameCopier.h"
#include "floral/stream/codec/EncoderTypes.h"

#include <android-base/unique_fd.h>
#include <android/hardware_buffer.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace floral::stream::codec {

class EncoderBackend;

// Owns one encoder and its EGL copy path. Session methods are intended to be
// driven by one stream worker so frame ordering and presentation timestamps
// stay linear.
class EncoderSession {
  public:
    static std::unique_ptr<EncoderSession> Create(const EncoderConfig& config,
                                                  const VideoGeometry& geometry,
                                                  std::string* error);

    ~EncoderSession();

    EncoderSession(const EncoderSession&) = delete;
    EncoderSession& operator=(const EncoderSession&) = delete;

    bool RegisterBuffer(AHardwareBuffer* buffer, uint64_t* outBufferId, std::string* error);
    void UnregisterBuffer(uint64_t bufferId);
    FrameCopyResult SubmitFrame(uint64_t bufferId, android::base::unique_fd acquireFence,
                                int64_t presentationTimeNanos, std::string* error);

    bool SetBitrate(uint32_t bitrateBps, std::string* error);
    bool RequestKeyFrame(std::string* error);
    bool SignalEndOfInputStream(std::string* error);
    DequeueResult DequeueOutput(int64_t timeoutUs);

    const EncoderConfig& config() const;
    const std::string& codec_name() const;
    bool uses_native_fences() const;
    size_t registered_buffer_count() const;

  private:
    explicit EncoderSession(std::unique_ptr<EncoderBackend> backend);

    std::unique_ptr<EncoderBackend> backend_;
};

}  // namespace floral::stream::codec
