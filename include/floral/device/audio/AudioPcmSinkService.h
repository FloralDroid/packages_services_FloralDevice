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

#include "floral/stream/audio/AudioStreamSession.h"

#include <aidl/floral/device/audio/BnAudioPcmSink.h>

#include <memory>

namespace floral::device::audio {

class AudioPcmSinkService final : public aidl::floral::device::audio::BnAudioPcmSink {
  public:
    explicit AudioPcmSinkService(std::shared_ptr<stream::audio::AudioStreamSession> session);

    ndk::ScopedAStatus openOutput(
            const aidl::floral::device::audio::AudioPcmStreamConfig& config,
            aidl::android::hardware::common::fmq::MQDescriptor<
                    int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>*
                    _aidl_return) override;

  private:
    const std::shared_ptr<stream::audio::AudioStreamSession> session_;
};

}  // namespace floral::device::audio
