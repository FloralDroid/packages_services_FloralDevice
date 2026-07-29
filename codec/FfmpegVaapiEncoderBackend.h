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

#include "EncoderBackend.h"
#include "floral/stream/VideoGeometry.h"

#include <memory>
#include <string>

namespace floral::stream::codec {

class FfmpegVaapiEncoderBackend final : public EncoderBackend {
  public:
    static std::unique_ptr<FfmpegVaapiEncoderBackend> Create(const EncoderConfig& config,
                                                             const VideoGeometry& geometry,
                                                             std::string vaDevicePath,
                                                             std::string* error);

    ~FfmpegVaapiEncoderBackend() override;

    FfmpegVaapiEncoderBackend(const FfmpegVaapiEncoderBackend&) = delete;
    FfmpegVaapiEncoderBackend& operator=(const FfmpegVaapiEncoderBackend&) = delete;

    bool RegisterBuffer(AHardwareBuffer* buffer, uint64_t* outBufferId,
                        std::string* error) override;
    void UnregisterBuffer(uint64_t bufferId) override;
    FrameCopyResult SubmitFrame(uint64_t bufferId, android::base::unique_fd acquireFence,
                                int64_t presentationTimeNanos, std::string* error) override;

    bool SetBitrate(uint32_t bitrateBps, std::string* error) override;
    bool RequestKeyFrame(std::string* error) override;
    bool SignalEndOfInputStream(std::string* error) override;
    DequeueResult DequeueOutput(int64_t timeoutUs) override;

    const EncoderConfig& config() const override;
    const std::string& codec_name() const override;
    bool uses_native_fences() const override;
    size_t registered_buffer_count() const override;

  private:
    struct Impl;

    explicit FfmpegVaapiEncoderBackend(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace floral::stream::codec
