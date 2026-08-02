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

#include "floral/device/service/AudioEncoderControl.h"
#include "floral/stream/audio/HostAudioSink.h"
#include "floral/stream/audio/OpusEncoder.h"

#include <aidl/android/hardware/common/fmq/SynchronizedReadWrite.h>
#include <fmq/AidlMessageQueue.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace floral::stream::audio {

using AudioPcmFmq =
        android::AidlMessageQueue<int8_t,
                                  aidl::android::hardware::common::fmq::SynchronizedReadWrite>;

struct AudioStreamSessionConfig {
    std::string socket_path;
    uint32_t stream_id = 1;
    uint32_t bitrate_bps = 128'000;
};

class AudioStreamSession final : public device::service::AudioEncoderControl {
  public:
    static std::shared_ptr<AudioStreamSession> Create(AudioStreamSessionConfig config,
                                                      std::string* error);
    ~AudioStreamSession() override;

    AudioStreamSession(const AudioStreamSession&) = delete;
    AudioStreamSession& operator=(const AudioStreamSession&) = delete;

    bool SetInputQueue(std::shared_ptr<AudioPcmFmq> queue, std::string* error);

    bool ApplyAudioEncoderConfig(const device::service::AudioEncoderConfigUpdate& update,
                                 device::service::AudioEncoderRuntimeState* state,
                                 device::service::AudioEncoderConfigResult* result,
                                 std::string* error) override;

  private:
    AudioStreamSession(AudioStreamSessionConfig config, std::unique_ptr<FloralOpusEncoder> encoder,
                       std::unique_ptr<HostAudioSink> sink);

    void WorkerLoop();
    void PopulateRuntimeState(device::service::AudioEncoderRuntimeState* state) const;

    const AudioStreamSessionConfig config_;
    const std::unique_ptr<FloralOpusEncoder> encoder_;
    const std::unique_ptr<HostAudioSink> sink_;

    mutable std::mutex input_mutex_;
    std::condition_variable input_condition_;
    std::shared_ptr<AudioPcmFmq> input_queue_;
    uint64_t input_revision_ = 0;

    mutable std::mutex state_mutex_;
    uint32_t input_stream_generation_ = 0;
    uint32_t output_generation_ = 0;

    std::atomic<bool> running_{true};
    std::thread worker_;
};

}  // namespace floral::stream::audio
