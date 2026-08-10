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

#include "floral/stream/codec/EncoderSession.h"

#include "EncoderBackend.h"
#include "floral/stream/codec/MediaCodecSurfaceEncoder.h"

#if defined(__x86_64__)
#include "FfmpegVaapiEncoderBackend.h"
#endif

#include <utility>

namespace floral::stream::codec {
namespace {

class MediaCodecEncoderBackend final : public EncoderBackend {
  public:
    static std::unique_ptr<MediaCodecEncoderBackend> Create(const EncoderConfig& config,
                                                            const VideoGeometry& geometry,
                                                            std::string* error) {
        std::unique_ptr<MediaCodecSurfaceEncoder> encoder =
                MediaCodecSurfaceEncoder::Create(config, error);
        if (encoder == nullptr) {
            return nullptr;
        }

        std::unique_ptr<EglFrameCopier> frameCopier =
                EglFrameCopier::Create(encoder->input_surface(), geometry, error);
        if (frameCopier == nullptr) {
            return nullptr;
        }
        return std::unique_ptr<MediaCodecEncoderBackend>(
                new MediaCodecEncoderBackend(std::move(encoder), std::move(frameCopier)));
    }

    bool RegisterBuffer(AHardwareBuffer* buffer, uint64_t* outBufferId,
                        std::string* error) override {
        return frame_copier_->RegisterBuffer(buffer, outBufferId, error);
    }

    void UnregisterBuffer(uint64_t bufferId) override { frame_copier_->UnregisterBuffer(bufferId); }

    FrameCopyResult SubmitFrame(uint64_t bufferId, android::base::unique_fd acquireFence,
                                int64_t presentationTimeNanos, std::string* error) override {
        return frame_copier_->CopyFrame(bufferId, std::move(acquireFence), presentationTimeNanos,
                                        error);
    }

    FrameCopyResult RepeatLastFrame(int64_t presentationTimeNanos, std::string* error) override {
        (void)presentationTimeNanos;
        if (error != nullptr) {
            *error = "MediaCodec owns static frame repetition";
        }
        return {};
    }

    bool SetBitrate(uint32_t bitrateBps, std::string* error) override {
        return encoder_->SetBitrate(bitrateBps, error);
    }

    bool RequestKeyFrame(std::string* error) override { return encoder_->RequestKeyFrame(error); }

    bool SignalEndOfInputStream(std::string* error) override {
        return encoder_->SignalEndOfInputStream(error);
    }

    DequeueResult DequeueOutput(int64_t timeoutUs) override {
        return encoder_->DequeueOutput(timeoutUs);
    }

    StaticFrameRepeatMode static_frame_repeat_mode() const override {
        return StaticFrameRepeatMode::kCodecManaged;
    }

    const EncoderConfig& config() const override { return encoder_->config(); }

    const std::string& codec_name() const override { return encoder_->codec_name(); }

    bool uses_native_fences() const override { return frame_copier_->uses_native_fences(); }

    size_t registered_buffer_count() const override {
        return frame_copier_->registered_buffer_count();
    }

  private:
    MediaCodecEncoderBackend(std::unique_ptr<MediaCodecSurfaceEncoder> encoder,
                             std::unique_ptr<EglFrameCopier> frameCopier)
        : encoder_(std::move(encoder)), frame_copier_(std::move(frameCopier)) {}

    // Declaration order keeps the encoder input surface alive until the copier is
    // destroyed.
    std::unique_ptr<MediaCodecSurfaceEncoder> encoder_;
    std::unique_ptr<EglFrameCopier> frame_copier_;
};

}  // namespace

EncoderSession::EncoderSession(std::unique_ptr<EncoderBackend> backend)
    : backend_(std::move(backend)) {}

std::unique_ptr<EncoderSession> EncoderSession::Create(const EncoderConfig& config,
                                                       const VideoGeometry& geometry,
                                                       std::string* error) {
    if (!HasMatchingDisplayAspect(geometry)) {
        if (error != nullptr) {
            *error = "encoder session video geometry is invalid or changes the "
                     "display aspect";
        }
        return nullptr;
    }
    if (config.width != geometry.coded_width || config.height != geometry.coded_height) {
        if (error != nullptr) {
            *error = "encoder dimensions do not match coded video geometry";
        }
        return nullptr;
    }
    std::unique_ptr<EncoderBackend> backend;
    switch (config.backend) {
        case EncoderBackendType::kMediaCodecSoftware:
            backend = MediaCodecEncoderBackend::Create(config, geometry, error);
            break;
        case EncoderBackendType::kFfmpegVaapi:
#if defined(__x86_64__)
            backend = FfmpegVaapiEncoderBackend::Create(config, geometry, config.va_device_path,
                                                        error);
#else
            if (error != nullptr) {
                *error = "the FFmpeg VA-API backend is only built for x86_64";
            }
#endif
            break;
    }
    if (backend == nullptr) {
        return nullptr;
    }
    return std::unique_ptr<EncoderSession>(new EncoderSession(std::move(backend)));
}

EncoderSession::~EncoderSession() = default;

bool EncoderSession::RegisterBuffer(AHardwareBuffer* buffer, uint64_t* outBufferId,
                                    std::string* error) {
    return backend_->RegisterBuffer(buffer, outBufferId, error);
}

void EncoderSession::UnregisterBuffer(uint64_t bufferId) {
    backend_->UnregisterBuffer(bufferId);
}

FrameCopyResult EncoderSession::SubmitFrame(uint64_t bufferId,
                                            android::base::unique_fd acquireFence,
                                            int64_t presentationTimeNanos, std::string* error) {
    return backend_->SubmitFrame(bufferId, std::move(acquireFence), presentationTimeNanos, error);
}

FrameCopyResult EncoderSession::RepeatLastFrame(int64_t presentationTimeNanos, std::string* error) {
    return backend_->RepeatLastFrame(presentationTimeNanos, error);
}

bool EncoderSession::SetBitrate(uint32_t bitrateBps, std::string* error) {
    return backend_->SetBitrate(bitrateBps, error);
}

bool EncoderSession::RequestKeyFrame(std::string* error) {
    return backend_->RequestKeyFrame(error);
}

bool EncoderSession::SignalEndOfInputStream(std::string* error) {
    return backend_->SignalEndOfInputStream(error);
}

DequeueResult EncoderSession::DequeueOutput(int64_t timeoutUs) {
    return backend_->DequeueOutput(timeoutUs);
}

StaticFrameRepeatMode EncoderSession::static_frame_repeat_mode() const {
    return backend_->static_frame_repeat_mode();
}

const EncoderConfig& EncoderSession::config() const {
    return backend_->config();
}

const std::string& EncoderSession::codec_name() const {
    return backend_->codec_name();
}

bool EncoderSession::uses_native_fences() const {
    return backend_->uses_native_fences();
}

size_t EncoderSession::registered_buffer_count() const {
    return backend_->registered_buffer_count();
}

}  // namespace floral::stream::codec
