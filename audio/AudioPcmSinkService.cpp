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

#include "floral/device/audio/AudioPcmSinkService.h"

#include "floral/device/audio/AudioPcmRecord.h"

#include <utility>

namespace floral::device::audio {

AudioPcmSinkService::AudioPcmSinkService(std::shared_ptr<stream::audio::AudioStreamSession> session)
    : session_(std::move(session)) {}

ndk::ScopedAStatus AudioPcmSinkService::openOutput(
        const aidl::floral::device::audio::AudioPcmStreamConfig& config,
        aidl::android::hardware::common::fmq::MQDescriptor<
                int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>*
                _aidl_return) {
    if (_aidl_return == nullptr || config.sampleRate != static_cast<int32_t>(kAudioPcmSampleRate) ||
        config.channelCount != static_cast<int32_t>(kAudioPcmChannelCount) ||
        config.sampleFormat != static_cast<int32_t>(kAudioPcmSampleFormatS16Le) ||
        config.framesPerPacket != static_cast<int32_t>(kAudioPcmFramesPerRecord)) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                EX_ILLEGAL_ARGUMENT, "unsupported Floral audio PCM configuration");
    }
    if (session_ == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                EX_ILLEGAL_STATE, "Floral audio stream session is unavailable");
    }

    auto queue = std::make_shared<stream::audio::AudioPcmFmq>(
            kAudioPcmRecordSize * kAudioPcmFmqPacketCapacity, true);
    if (!queue->isValid()) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                EX_ILLEGAL_STATE, "failed to create Floral audio PCM FMQ");
    }
    std::string error;
    if (!session_->SetInputQueue(queue, &error)) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_STATE, error.c_str());
    }
    *_aidl_return = queue->dupeDesc();
    return ndk::ScopedAStatus::ok();
}

}  // namespace floral::device::audio
