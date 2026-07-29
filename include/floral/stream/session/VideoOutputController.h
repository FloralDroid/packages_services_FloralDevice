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

#include "floral/stream/VideoGeometry.h"
#include "floral/stream/codec/MediaCodecSurfaceEncoder.h"
#include "floral/stream/transport/HostVideoSink.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace floral::stream::session {

struct VideoOutputConfig {
    uint32_t stream_id = 0;
    uint32_t generation = 0;
    VideoGeometry geometry;
    size_t max_pending_frame_timestamps = 512;
};

enum class VideoOutputState {
    kStarting,
    kStreaming,
    kRecovering,
    kDisconnected,
    kEndOfStream,
    kError,
};

struct VideoOutputStats {
    uint64_t encoder_packets = 0;
    uint64_t accepted_packets = 0;
    uint64_t dropped_packets = 0;
    uint64_t codec_config_updates = 0;
    uint64_t recovery_events = 0;
    uint64_t key_frame_requests = 0;
    uint64_t matched_frame_timestamps = 0;
    uint64_t unmatched_frame_timestamps = 0;
    uint64_t pruned_frame_timestamps = 0;
};

struct VideoOutputProcessResult {
    bool success = true;
    bool made_progress = false;
    bool request_key_frame = false;
    bool end_of_stream = false;
};

// Applies stream ordering and recovery policy to MediaCodec output. All methods
// are called by the same stream worker that drives the associated encoder.
class VideoOutputController {
  public:
    explicit VideoOutputController(VideoOutputConfig config);

    void RecordFrameSubmission(int64_t presentationTimeUs, uint64_t submitTimeNs);
    VideoOutputProcessResult Process(codec::DequeueResult output, transport::HostVideoSink* sink,
                                     std::string* error);

    VideoOutputState state() const { return state_; }
    const VideoOutputStats& stats() const { return stats_; }
    size_t pending_frame_timestamps() const { return frame_submit_times_.size(); }

  private:
    uint64_t TakeFrameSubmissionTime(int64_t presentationTimeUs);
    transport::HostVideoPacket MakePacket(codec::EncodedPacket packet, uint32_t additionalFlags,
                                          uint64_t submitTimeNs);
    VideoOutputProcessResult SendAccessPoint(codec::EncodedPacket packet, uint64_t submitTimeNs,
                                             transport::HostVideoSink* sink, std::string* error);
    VideoOutputProcessResult SendPacket(codec::EncodedPacket packet, uint64_t submitTimeNs,
                                        transport::HostVideoSink* sink, std::string* error);
    VideoOutputProcessResult HandleEnqueueResult(const transport::VideoEnqueueResult& enqueue,
                                                 size_t packetCount, transport::HostVideoSink* sink,
                                                 std::string* error);

    const VideoOutputConfig config_;
    VideoOutputState state_ = VideoOutputState::kStarting;
    VideoOutputStats stats_;
    uint64_t next_sequence_ = 0;
    std::map<int64_t, uint64_t> frame_submit_times_;
    std::optional<codec::EncodedPacket> codec_config_;
};

}  // namespace floral::stream::session
