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

#include "floral/stream/codec/MediaCodecSurfaceEncoder.h"

#include <media/NdkMediaFormat.h>

#include <limits>
#include <sstream>
#include <utility>

namespace floral::stream::codec {
namespace {

// MediaCodec's Surface input format is the Android opaque OMX color format.
constexpr int32_t kColorFormatSurface = 0x7f000789;
constexpr char kParameterKeyVideoBitrate[] = "video-bitrate";
constexpr char kParameterKeyRequestSyncFrame[] = "request-sync";

bool SetError(std::string* error, const char* operation, media_status_t status) {
    if (error != nullptr) {
        std::ostringstream message;
        message << operation << " failed with media status " << status;
        *error = message.str();
    }
    return false;
}

bool ValidateConfig(const EncoderConfig& config, std::string* error) {
    if (config.mime != "video/avc") {
        if (error != nullptr) {
            *error = "the MediaCodec software backend only supports video/avc";
        }
        return false;
    }
    if (config.width == 0 || config.height == 0 || config.bitrate_bps == 0 ||
        config.frame_rate == 0 ||
        config.width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        config.height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        config.bitrate_bps > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        config.frame_rate > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        config.i_frame_interval_seconds >
                static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        if (error != nullptr) {
            *error = "encoder configuration contains an invalid or out-of-range value";
        }
        return false;
    }
    return true;
}

}  // namespace

MediaCodecSurfaceEncoder::MediaCodecSurfaceEncoder(EncoderConfig config)
    : config_(std::move(config)) {}

std::unique_ptr<MediaCodecSurfaceEncoder> MediaCodecSurfaceEncoder::Create(
        const EncoderConfig& config, std::string* error) {
    if (!ValidateConfig(config, error)) {
        return nullptr;
    }

    auto encoder = std::unique_ptr<MediaCodecSurfaceEncoder>(new MediaCodecSurfaceEncoder(config));
    encoder->codec_ = AMediaCodec_createCodecByName(kSoftwareAvcCodecName);
    if (encoder->codec_ == nullptr) {
        if (error != nullptr) {
            *error = std::string("MediaCodec software encoder is unavailable: ") +
                     kSoftwareAvcCodecName;
        }
        return nullptr;
    }

    char* codecName = nullptr;
    media_status_t status = AMediaCodec_getName(encoder->codec_, &codecName);
    if (status != AMEDIA_OK || codecName == nullptr) {
        SetError(error, "AMediaCodec_getName", status);
        return nullptr;
    }
    encoder->codec_name_ = codecName;
    AMediaCodec_releaseName(encoder->codec_, codecName);

    AMediaFormat* format = AMediaFormat_new();
    if (format == nullptr) {
        if (error != nullptr) {
            *error = "AMediaFormat_new failed";
        }
        return nullptr;
    }
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, config.mime.c_str());
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, static_cast<int32_t>(config.width));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, static_cast<int32_t>(config.height));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE,
                          static_cast<int32_t>(config.bitrate_bps));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE,
                          static_cast<int32_t>(config.frame_rate));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL,
                          static_cast<int32_t>(config.i_frame_interval_seconds));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorFormatSurface);

    status = AMediaCodec_configure(encoder->codec_, format, nullptr, nullptr,
                                   AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    AMediaFormat_delete(format);
    if (status != AMEDIA_OK) {
        SetError(error, "AMediaCodec_configure", status);
        return nullptr;
    }

    status = AMediaCodec_createInputSurface(encoder->codec_, &encoder->input_surface_);
    if (status != AMEDIA_OK || encoder->input_surface_ == nullptr) {
        SetError(error, "AMediaCodec_createInputSurface", status);
        return nullptr;
    }

    status = AMediaCodec_start(encoder->codec_);
    if (status != AMEDIA_OK) {
        SetError(error, "AMediaCodec_start", status);
        return nullptr;
    }
    encoder->started_ = true;
    return encoder;
}

MediaCodecSurfaceEncoder::~MediaCodecSurfaceEncoder() {
    if (started_) {
        AMediaCodec_stop(codec_);
    }
    if (input_surface_ != nullptr) {
        ANativeWindow_release(input_surface_);
    }
    if (codec_ != nullptr) {
        AMediaCodec_delete(codec_);
    }
}

bool MediaCodecSurfaceEncoder::SetBitrate(uint32_t bitrateBps, std::string* error) {
    if (!started_ || bitrateBps == 0 ||
        bitrateBps > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        if (error != nullptr) {
            *error = "bitrate must be a positive signed 32-bit value while the "
                     "encoder is running";
        }
        return false;
    }

    AMediaFormat* parameters = AMediaFormat_new();
    if (parameters == nullptr) {
        if (error != nullptr) {
            *error = "AMediaFormat_new failed";
        }
        return false;
    }
    AMediaFormat_setInt32(parameters, kParameterKeyVideoBitrate, static_cast<int32_t>(bitrateBps));
    const media_status_t status = AMediaCodec_setParameters(codec_, parameters);
    AMediaFormat_delete(parameters);
    if (status != AMEDIA_OK) {
        return SetError(error, "AMediaCodec_setParameters", status);
    }
    config_.bitrate_bps = bitrateBps;
    return true;
}

bool MediaCodecSurfaceEncoder::RequestKeyFrame(std::string* error) {
    if (!started_) {
        if (error != nullptr) {
            *error = "encoder is not running";
        }
        return false;
    }

    AMediaFormat* parameters = AMediaFormat_new();
    if (parameters == nullptr) {
        if (error != nullptr) {
            *error = "AMediaFormat_new failed";
        }
        return false;
    }
    AMediaFormat_setInt32(parameters, kParameterKeyRequestSyncFrame, 0);
    const media_status_t status = AMediaCodec_setParameters(codec_, parameters);
    AMediaFormat_delete(parameters);
    return status == AMEDIA_OK || SetError(error, "AMediaCodec_setParameters", status);
}

bool MediaCodecSurfaceEncoder::SignalEndOfInputStream(std::string* error) {
    if (!started_) {
        if (error != nullptr) {
            *error = "encoder is not running";
        }
        return false;
    }
    const media_status_t status = AMediaCodec_signalEndOfInputStream(codec_);
    return status == AMEDIA_OK || SetError(error, "AMediaCodec_signalEndOfInputStream", status);
}

DequeueResult MediaCodecSurfaceEncoder::DequeueOutput(int64_t timeoutUs) {
    AMediaCodecBufferInfo info{};
    const ssize_t index = AMediaCodec_dequeueOutputBuffer(codec_, &info, timeoutUs);
    if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER ||
        index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        DequeueResult result;
        result.status = DequeueStatus::kTryAgain;
        return result;
    }
    if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        DequeueResult result;
        result.status = DequeueStatus::kFormatChanged;
        return result;
    }
    if (index < 0) {
        DequeueResult result;
        result.status = DequeueStatus::kError;
        result.error_code = static_cast<int32_t>(index);
        return result;
    }

    DequeueResult result;
    result.packet.presentation_time_us = info.presentationTimeUs;
    result.packet.flags = info.flags;
    if (info.size > 0) {
        size_t capacity = 0;
        uint8_t* buffer =
                AMediaCodec_getOutputBuffer(codec_, static_cast<size_t>(index), &capacity);
        const int64_t end = static_cast<int64_t>(info.offset) + info.size;
        if (buffer == nullptr || info.offset < 0 || info.size < 0 || end < 0 ||
            static_cast<uint64_t>(end) > capacity) {
            AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(index), false);
            result.status = DequeueStatus::kError;
            result.error_code = AMEDIA_ERROR_MALFORMED;
            return result;
        }
        result.packet.data.assign(buffer + info.offset, buffer + static_cast<size_t>(end));
    }

    const media_status_t releaseStatus =
            AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(index), false);
    if (releaseStatus != AMEDIA_OK) {
        result.status = DequeueStatus::kError;
        result.error_code = releaseStatus;
        return result;
    }
    result.status = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0
                            ? DequeueStatus::kEndOfStream
                            : DequeueStatus::kPacket;
    return result;
}

}  // namespace floral::stream::codec
