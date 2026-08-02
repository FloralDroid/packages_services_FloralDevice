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

#include "floral/device/audio/AudioPcmRecord.h"

#include <gtest/gtest.h>

namespace floral::device::audio {
namespace {

TEST(AudioPcmRecordTest, RoundTripsFixedPcmBlock) {
    AudioPcmRecord record;
    record.stream_generation = 7;
    record.flags = kAudioPcmRecordStreamStart | kAudioPcmRecordDiscontinuity;
    record.first_frame_position = 12'345;
    record.presentation_timestamp_ns = 987'654'321;
    for (size_t index = 0; index < record.samples.size(); ++index) {
        record.samples[index] = static_cast<int16_t>(static_cast<int32_t>(index) - 240);
    }

    SerializedAudioPcmRecord serialized;
    std::string error;
    ASSERT_TRUE(SerializeAudioPcmRecord(record, &serialized, &error)) << error;

    AudioPcmRecord parsed;
    ASSERT_TRUE(ParseAudioPcmRecord(serialized, &parsed, &error)) << error;
    EXPECT_EQ(record.stream_generation, parsed.stream_generation);
    EXPECT_EQ(record.flags, parsed.flags);
    EXPECT_EQ(record.first_frame_position, parsed.first_frame_position);
    EXPECT_EQ(record.presentation_timestamp_ns, parsed.presentation_timestamp_ns);
    EXPECT_EQ(record.samples, parsed.samples);
}

TEST(AudioPcmRecordTest, RejectsUnknownFlags) {
    AudioPcmRecord record;
    record.stream_generation = 1;
    record.flags = 1U << 31;
    SerializedAudioPcmRecord serialized;
    std::string error;
    EXPECT_FALSE(SerializeAudioPcmRecord(record, &serialized, &error));
}

}  // namespace
}  // namespace floral::device::audio
