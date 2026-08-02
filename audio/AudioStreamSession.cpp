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

#define LOG_TAG "floral-audio-session"

#include "floral/stream/audio/AudioStreamSession.h"

#include "floral/device/audio/AudioPcmRecord.h"

#include <log/log.h>

#include <utility>

namespace floral::stream::audio {
namespace {

constexpr int64_t kFmqReadTimeoutNs = 50'000'000;

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

}  // namespace

std::shared_ptr<AudioStreamSession> AudioStreamSession::Create(AudioStreamSessionConfig config,
                                                               std::string* error) {
    if (config.socket_path.empty() || config.stream_id == 0) {
        SetError(error, "audio stream session configuration is invalid");
        return nullptr;
    }
    std::unique_ptr<FloralOpusEncoder> encoder =
            FloralOpusEncoder::Create(config.bitrate_bps, error);
    if (encoder == nullptr) {
        return nullptr;
    }
    HostAudioSinkConfig sinkConfig;
    sinkConfig.socket_path = config.socket_path;
    std::unique_ptr<HostAudioSink> sink = HostAudioSink::Create(std::move(sinkConfig), error);
    if (sink == nullptr) {
        return nullptr;
    }
    return std::shared_ptr<AudioStreamSession>(
            new AudioStreamSession(std::move(config), std::move(encoder), std::move(sink)));
}

AudioStreamSession::AudioStreamSession(AudioStreamSessionConfig config,
                                       std::unique_ptr<FloralOpusEncoder> encoder,
                                       std::unique_ptr<HostAudioSink> sink)
    : config_(std::move(config)),
      encoder_(std::move(encoder)),
      sink_(std::move(sink)),
      worker_(&AudioStreamSession::WorkerLoop, this) {}

AudioStreamSession::~AudioStreamSession() {
    running_.store(false);
    input_condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool AudioStreamSession::SetInputQueue(std::shared_ptr<AudioPcmFmq> queue, std::string* error) {
    if (queue == nullptr || !queue->isValid()) {
        return SetError(error, "audio PCM FMQ is invalid");
    }
    {
        std::lock_guard lock(input_mutex_);
        input_queue_ = std::move(queue);
        ++input_revision_;
    }
    {
        std::lock_guard lock(state_mutex_);
        input_stream_generation_ = 0;
    }
    sink_->DiscardPending();
    input_condition_.notify_all();
    return true;
}

bool AudioStreamSession::ApplyAudioEncoderConfig(
        const device::service::AudioEncoderConfigUpdate& update,
        device::service::AudioEncoderRuntimeState* state,
        device::service::AudioEncoderConfigResult* result, std::string* error) {
    if (state == nullptr || result == nullptr) {
        return SetError(error, "audio encoder control outputs are null");
    }
    if (update.stream_id != config_.stream_id) {
        *result = device::service::AudioEncoderConfigResult::kUnknownStream;
        PopulateRuntimeState(state);
        return true;
    }
    if (update.fields != device::service::kAudioEncoderConfigBitrate ||
        update.bitrate_bps < kMinimumOpusBitrate || update.bitrate_bps > kMaximumOpusBitrate) {
        *result = device::service::AudioEncoderConfigResult::kInvalidConfig;
        PopulateRuntimeState(state);
        return true;
    }
    if (!encoder_->SetBitrate(update.bitrate_bps, error)) {
        return false;
    }
    *result = device::service::AudioEncoderConfigResult::kApplied;
    PopulateRuntimeState(state);
    return true;
}

void AudioStreamSession::PopulateRuntimeState(
        device::service::AudioEncoderRuntimeState* state) const {
    std::lock_guard lock(state_mutex_);
    state->generation = output_generation_;
    state->bitrate_bps = encoder_->bitrate_bps();
    state->codec_id = static_cast<uint32_t>(AudioCodec::kOpus);
}

void AudioStreamSession::WorkerLoop() {
    while (running_.load()) {
        std::shared_ptr<AudioPcmFmq> queue;
        uint64_t revision = 0;
        {
            std::unique_lock lock(input_mutex_);
            input_condition_.wait(lock,
                                  [this]() { return !running_.load() || input_queue_ != nullptr; });
            if (!running_.load()) {
                return;
            }
            queue = input_queue_;
            revision = input_revision_;
        }

        floral::device::audio::SerializedAudioPcmRecord serialized;
        if (!queue->readBlocking(serialized.data(), serialized.size(), kFmqReadTimeoutNs)) {
            continue;
        }
        {
            std::lock_guard lock(input_mutex_);
            if (revision != input_revision_) {
                continue;
            }
        }

        floral::device::audio::AudioPcmRecord record;
        std::string error;
        if (!floral::device::audio::ParseAudioPcmRecord(serialized, &record, &error)) {
            ALOGE("discarding malformed PCM record: %s", error.c_str());
            sink_->MarkDiscontinuity();
            continue;
        }

        bool newGeneration = false;
        uint32_t outputGeneration = 0;
        {
            std::lock_guard lock(state_mutex_);
            if (input_stream_generation_ != record.stream_generation) {
                input_stream_generation_ = record.stream_generation;
                ++output_generation_;
                if (output_generation_ == 0) {
                    output_generation_ = 1;
                }
                newGeneration = true;
            }
            outputGeneration = output_generation_;
        }
        const bool discontinuity =
                newGeneration ||
                (record.flags & floral::device::audio::kAudioPcmRecordDiscontinuity) != 0;
        if (discontinuity && !encoder_->Reset(&error)) {
            ALOGE("failed to reset Opus after discontinuity: %s", error.c_str());
            sink_->MarkDiscontinuity();
            continue;
        }

        HostAudioPacket packet;
        packet.header.stream_id = config_.stream_id;
        packet.header.generation = outputGeneration;
        packet.header.presentation_timestamp_ns = record.presentation_timestamp_ns;
        packet.header.frame_count = record.frame_count;
        packet.header.first_frame_position = record.first_frame_position;
        if (discontinuity) {
            packet.header.flags |= kAudioPacketDiscontinuity;
        }
        if (!encoder_->Encode(record.samples.data(), record.frame_count, &packet.payload, &error)) {
            ALOGE("failed to encode PCM record: %s", error.c_str());
            sink_->MarkDiscontinuity();
            continue;
        }
        if (!sink_->Enqueue(std::move(packet), &error)) {
            ALOGE("failed to enqueue encoded audio: %s", error.c_str());
        }
    }
}

}  // namespace floral::stream::audio
