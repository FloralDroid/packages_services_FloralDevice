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

#include "floral/stream/session/VideoOutputController.h"

#include "floral/stream/transport/EncodedPacketAdapter.h"

#include <media/NdkMediaCodec.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace floral::stream::session {
namespace {

bool SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool HasFlag(const codec::EncodedPacket& packet, uint32_t flag) {
    return (packet.flags & flag) != 0;
}

}  // namespace

VideoOutputController::VideoOutputController(VideoOutputConfig config)
    : config_(std::move(config)) {}

void VideoOutputController::RecordFrameSubmission(int64_t presentationTimeUs,
                                                  uint64_t submitTimeNs) {
    if (presentationTimeUs < 0 || submitTimeNs == 0) {
        return;
    }
    frame_submit_times_[presentationTimeUs] = submitTimeNs;
    const size_t limit = std::max<size_t>(config_.max_pending_frame_timestamps, 1);
    while (frame_submit_times_.size() > limit) {
        frame_submit_times_.erase(frame_submit_times_.begin());
        ++stats_.pruned_frame_timestamps;
    }
}

VideoOutputProcessResult VideoOutputController::Process(codec::DequeueResult output,
                                                        transport::HostVideoSink* sink,
                                                        std::string* error) {
    VideoOutputProcessResult result;
    switch (output.status) {
        case codec::DequeueStatus::kTryAgain:
            return result;
        case codec::DequeueStatus::kFormatChanged:
            result.made_progress = true;
            return result;
        case codec::DequeueStatus::kError:
            state_ = VideoOutputState::kError;
            result.success = SetError(error, "MediaCodec output failed with status " +
                                                     std::to_string(output.error_code));
            return result;
        case codec::DequeueStatus::kPacket:
        case codec::DequeueStatus::kEndOfStream:
            break;
    }
    if (sink == nullptr) {
        state_ = VideoOutputState::kError;
        result.success = SetError(error, "video output requires a host sink");
        return result;
    }

    result.made_progress = true;
    ++stats_.encoder_packets;
    codec::EncodedPacket& packet = output.packet;
    const bool codecConfig = HasFlag(packet, AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG);
    const bool keyFrame = HasFlag(packet, transport::kMediaCodecKeyFrameFlag);
    const bool endOfStream = output.status == codec::DequeueStatus::kEndOfStream ||
                             HasFlag(packet, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);

    // Cache standalone codec data so each decoder access point can be queued
    // atomically with the key frame that follows it.
    if (codecConfig && !keyFrame && !endOfStream) {
        codec_config_ = std::move(packet);
        ++stats_.codec_config_updates;
        return result;
    }

    const uint64_t submitTimeNs =
            codecConfig ? 0 : TakeFrameSubmissionTime(packet.presentation_time_us);
    if (endOfStream) {
        transport::HostVideoPacket hostPacket = MakePacket(std::move(packet), 0, submitTimeNs);
        transport::VideoEnqueueResult enqueue = sink->Submit(std::move(hostPacket), error);
        if (enqueue.status == transport::VideoEnqueueStatus::kQueueFull) {
            sink->DiscardPending();
            ++stats_.dropped_packets;
        } else if (enqueue.status == transport::VideoEnqueueStatus::kAccepted) {
            ++stats_.accepted_packets;
        } else if (enqueue.status == transport::VideoEnqueueStatus::kDisconnected ||
                   enqueue.status == transport::VideoEnqueueStatus::kStopped) {
            state_ = VideoOutputState::kDisconnected;
            result.success = SetError(error, "host video sink disconnected before end of stream");
            return result;
        } else {
            state_ = VideoOutputState::kError;
            result.success = false;
            return result;
        }
        state_ = VideoOutputState::kEndOfStream;
        result.end_of_stream = true;
        return result;
    }

    if ((state_ == VideoOutputState::kStarting || state_ == VideoOutputState::kRecovering) &&
        !keyFrame) {
        ++stats_.dropped_packets;
        return result;
    }
    if (keyFrame) {
        return SendAccessPoint(std::move(packet), submitTimeNs, sink, error);
    }
    return SendPacket(std::move(packet), submitTimeNs, sink, error);
}

uint64_t VideoOutputController::TakeFrameSubmissionTime(int64_t presentationTimeUs) {
    const auto submission = frame_submit_times_.find(presentationTimeUs);
    if (submission == frame_submit_times_.end()) {
        ++stats_.unmatched_frame_timestamps;
        return 0;
    }
    const uint64_t submitTimeNs = submission->second;
    frame_submit_times_.erase(submission);
    ++stats_.matched_frame_timestamps;
    return submitTimeNs;
}

transport::HostVideoPacket VideoOutputController::MakePacket(codec::EncodedPacket packet,
                                                             uint32_t additionalFlags,
                                                             uint64_t submitTimeNs) {
    transport::EncodedStreamInfo streamInfo;
    streamInfo.stream_id = config_.stream_id;
    streamInfo.generation = config_.generation;
    streamInfo.sequence = next_sequence_++;
    streamInfo.geometry = config_.geometry;
    streamInfo.additional_flags = additionalFlags;
    streamInfo.frame_submit_time_ns = submitTimeNs;
    return transport::MakeHostVideoPacket(std::move(packet), streamInfo);
}

VideoOutputProcessResult VideoOutputController::SendAccessPoint(codec::EncodedPacket packet,
                                                                uint64_t submitTimeNs,
                                                                transport::HostVideoSink* sink,
                                                                std::string* error) {
    std::vector<transport::HostVideoPacket> packets;
    if (codec_config_.has_value()) {
        packets.push_back(MakePacket(*codec_config_, 0, 0));
    }
    const uint32_t additionalFlags =
            state_ == VideoOutputState::kRecovering ? transport::kVideoPacketDiscontinuity : 0;
    packets.push_back(MakePacket(std::move(packet), additionalFlags, submitTimeNs));
    const size_t packetCount = packets.size();
    transport::VideoEnqueueResult enqueue = sink->SubmitBatch(std::move(packets), error);
    VideoOutputProcessResult result = HandleEnqueueResult(enqueue, packetCount, sink, error);
    if (enqueue.status == transport::VideoEnqueueStatus::kAccepted) {
        state_ = VideoOutputState::kStreaming;
    }
    return result;
}

VideoOutputProcessResult VideoOutputController::SendPacket(codec::EncodedPacket packet,
                                                           uint64_t submitTimeNs,
                                                           transport::HostVideoSink* sink,
                                                           std::string* error) {
    transport::HostVideoPacket hostPacket = MakePacket(std::move(packet), 0, submitTimeNs);
    transport::VideoEnqueueResult enqueue = sink->Submit(std::move(hostPacket), error);
    return HandleEnqueueResult(enqueue, 1, sink, error);
}

VideoOutputProcessResult VideoOutputController::HandleEnqueueResult(
        const transport::VideoEnqueueResult& enqueue, size_t packetCount,
        transport::HostVideoSink* sink, std::string* error) {
    VideoOutputProcessResult result;
    result.made_progress = true;
    switch (enqueue.status) {
        case transport::VideoEnqueueStatus::kAccepted:
            stats_.accepted_packets += packetCount;
            return result;
        case transport::VideoEnqueueStatus::kQueueFull:
            sink->DiscardPending();
            stats_.dropped_packets += packetCount;
            if (state_ != VideoOutputState::kRecovering) {
                ++stats_.recovery_events;
            }
            state_ = VideoOutputState::kRecovering;
            result.request_key_frame = enqueue.request_key_frame;
            if (result.request_key_frame) {
                ++stats_.key_frame_requests;
            }
            return result;
        case transport::VideoEnqueueStatus::kDisconnected:
        case transport::VideoEnqueueStatus::kStopped:
            state_ = VideoOutputState::kDisconnected;
            result.success = SetError(error, "host video sink is disconnected");
            return result;
        case transport::VideoEnqueueStatus::kInvalidPacket:
            state_ = VideoOutputState::kError;
            result.success = false;
            return result;
    }
    state_ = VideoOutputState::kError;
    result.success = SetError(error, "host video sink returned an unknown status");
    return result;
}

}  // namespace floral::stream::session
