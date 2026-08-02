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

#include "floral/stream/audio/AudioPacketProtocol.h"

#include <gtest/gtest.h>

namespace floral::stream::audio {
namespace {

TEST(AudioPacketProtocolTest, RoundTripsOpusHeader) {
    AudioPacketHeader header;
    header.stream_id = 1;
    header.generation = 3;
    header.sequence = 9;
    header.presentation_timestamp_ns = 123'456'789;
    header.payload_size = 87;
    header.flags = kAudioPacketStreamStart | kAudioPacketDiscontinuity;
    header.first_frame_position = 4'800;

    SerializedAudioPacketHeader serialized;
    std::string error;
    ASSERT_TRUE(SerializeAudioPacketHeader(header, &serialized, &error)) << error;

    AudioPacketHeader parsed;
    ASSERT_TRUE(ParseAudioPacketHeader(serialized, &parsed, &error)) << error;
    EXPECT_EQ(header.stream_id, parsed.stream_id);
    EXPECT_EQ(header.generation, parsed.generation);
    EXPECT_EQ(header.sequence, parsed.sequence);
    EXPECT_EQ(header.presentation_timestamp_ns, parsed.presentation_timestamp_ns);
    EXPECT_EQ(header.codec, parsed.codec);
    EXPECT_EQ(header.payload_size, parsed.payload_size);
    EXPECT_EQ(header.flags, parsed.flags);
    EXPECT_EQ(header.first_frame_position, parsed.first_frame_position);
}

TEST(AudioPacketProtocolTest, RejectsPcmOnDefaultEncodedChannel) {
    AudioPacketHeader header;
    header.stream_id = 1;
    header.generation = 1;
    header.sequence = 1;
    header.codec = AudioCodec::kPcmS16Le;
    header.payload_size = 960;
    SerializedAudioPacketHeader serialized;
    std::string error;
    EXPECT_FALSE(SerializeAudioPacketHeader(header, &serialized, &error));
}

}  // namespace
}  // namespace floral::stream::audio
