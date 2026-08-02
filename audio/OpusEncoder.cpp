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

#include "floral/stream/audio/OpusEncoder.h"

#include "floral/device/audio/AudioPcmRecord.h"

#include <opus.h>

namespace floral::stream::audio {
namespace {

constexpr size_t kMaximumOpusPacketSize = 1275;

bool SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool IsValidBitrate(uint32_t bitrate_bps) {
    return bitrate_bps >= kMinimumOpusBitrate && bitrate_bps <= kMaximumOpusBitrate;
}

}  // namespace

std::unique_ptr<FloralOpusEncoder> FloralOpusEncoder::Create(uint32_t bitrate_bps,
                                                             std::string* error) {
    if (!IsValidBitrate(bitrate_bps)) {
        SetError(error, "Opus bitrate is outside the supported range");
        return nullptr;
    }
    int opusError = OPUS_OK;
    ::OpusEncoder* encoder = opus_encoder_create(floral::device::audio::kAudioPcmSampleRate,
                                                 floral::device::audio::kAudioPcmChannelCount,
                                                 OPUS_APPLICATION_AUDIO, &opusError);
    if (encoder == nullptr || opusError != OPUS_OK) {
        SetError(error, std::string("opus_encoder_create failed: ") + opus_strerror(opusError));
        return nullptr;
    }

    std::unique_ptr<FloralOpusEncoder> result(new FloralOpusEncoder(encoder, bitrate_bps));
    if (!result->SetBitrate(bitrate_bps, error)) {
        return nullptr;
    }
    return result;
}

FloralOpusEncoder::FloralOpusEncoder(::OpusEncoder* encoder, uint32_t bitrate_bps)
    : encoder_(encoder), bitrate_bps_(bitrate_bps) {}

FloralOpusEncoder::~FloralOpusEncoder() {
    opus_encoder_destroy(encoder_);
}

bool FloralOpusEncoder::Encode(const int16_t* samples, size_t frame_count,
                               std::vector<uint8_t>* payload, std::string* error) {
    if (samples == nullptr || payload == nullptr ||
        frame_count != floral::device::audio::kAudioPcmFramesPerRecord) {
        return SetError(error, "Opus input must be one complete 5 ms PCM block");
    }

    std::lock_guard lock(mutex_);
    payload->resize(kMaximumOpusPacketSize);
    const int encodedBytes = opus_encode(encoder_, samples, static_cast<int>(frame_count),
                                         payload->data(), payload->size());
    if (encodedBytes < 0) {
        payload->clear();
        return SetError(error, std::string("opus_encode failed: ") + opus_strerror(encodedBytes));
    }
    payload->resize(static_cast<size_t>(encodedBytes));
    return true;
}

bool FloralOpusEncoder::SetBitrate(uint32_t bitrate_bps, std::string* error) {
    if (!IsValidBitrate(bitrate_bps)) {
        return SetError(error, "Opus bitrate is outside the supported range");
    }
    std::lock_guard lock(mutex_);
    const int result = opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(static_cast<int>(bitrate_bps)));
    if (result != OPUS_OK) {
        return SetError(error, std::string("OPUS_SET_BITRATE failed: ") + opus_strerror(result));
    }
    bitrate_bps_ = bitrate_bps;
    return true;
}

bool FloralOpusEncoder::Reset(std::string* error) {
    std::lock_guard lock(mutex_);
    const int result = opus_encoder_ctl(encoder_, OPUS_RESET_STATE);
    if (result != OPUS_OK) {
        return SetError(error, std::string("OPUS_RESET_STATE failed: ") + opus_strerror(result));
    }
    return true;
}

uint32_t FloralOpusEncoder::bitrate_bps() const {
    std::lock_guard lock(mutex_);
    return bitrate_bps_;
}

}  // namespace floral::stream::audio
