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

#include "floral/stream/codec/EncoderTypes.h"

#include <android/native_window.h>
#include <media/NdkMediaCodec.h>

#include <memory>
#include <string>

namespace floral::stream::codec {

inline constexpr char kSoftwareAvcCodecName[] = "OMX.google.h264.encoder";

class MediaCodecSurfaceEncoder {
  public:
    static std::unique_ptr<MediaCodecSurfaceEncoder> Create(const EncoderConfig& config,
                                                            std::string* error);

    ~MediaCodecSurfaceEncoder();

    MediaCodecSurfaceEncoder(const MediaCodecSurfaceEncoder&) = delete;
    MediaCodecSurfaceEncoder& operator=(const MediaCodecSurfaceEncoder&) = delete;

    ANativeWindow* input_surface() const { return input_surface_; }
    const std::string& codec_name() const { return codec_name_; }
    const EncoderConfig& config() const { return config_; }

    bool SetBitrate(uint32_t bitrateBps, std::string* error);
    bool RequestKeyFrame(std::string* error);
    bool SignalEndOfInputStream(std::string* error);
    DequeueResult DequeueOutput(int64_t timeoutUs);

  private:
    explicit MediaCodecSurfaceEncoder(EncoderConfig config);

    EncoderConfig config_;
    AMediaCodec* codec_ = nullptr;
    ANativeWindow* input_surface_ = nullptr;
    std::string codec_name_;
    bool started_ = false;
};

}  // namespace floral::stream::codec
