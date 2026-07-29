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

#include "floral/stream/transport/EncodedPacketAdapter.h"

#include <media/NdkMediaCodec.h>

#include <utility>

namespace floral::stream::transport {

HostVideoPacket MakeHostVideoPacket(codec::EncodedPacket packet,
                                    const EncodedStreamInfo& streamInfo) {
    HostVideoPacket hostPacket;
    hostPacket.header.stream_id = streamInfo.stream_id;
    hostPacket.header.generation = streamInfo.generation;
    hostPacket.header.sequence = streamInfo.sequence;
    hostPacket.header.presentation_time_us = packet.presentation_time_us;
    hostPacket.header.codec = VideoCodec::kH264;
    hostPacket.header.coded_width = streamInfo.geometry.coded_width;
    hostPacket.header.coded_height = streamInfo.geometry.coded_height;
    hostPacket.header.logical_width = streamInfo.geometry.logical_width;
    hostPacket.header.logical_height = streamInfo.geometry.logical_height;
    hostPacket.header.display_rotation = streamInfo.geometry.display_rotation;
    hostPacket.header.frame_submit_time_ns = streamInfo.frame_submit_time_ns;
    hostPacket.header.flags = streamInfo.additional_flags;
    if ((packet.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0) {
        hostPacket.header.flags |= kVideoPacketCodecConfig;
    }
    if ((packet.flags & kMediaCodecKeyFrameFlag) != 0) {
        hostPacket.header.flags |= kVideoPacketKeyFrame;
    }
    if ((packet.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
        hostPacket.header.flags |= kVideoPacketEndOfStream;
    }
    hostPacket.payload = std::move(packet.data);
    return hostPacket;
}

}  // namespace floral::stream::transport
