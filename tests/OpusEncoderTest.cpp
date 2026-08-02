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
#include "floral/stream/audio/OpusEncoder.h"

#include <gtest/gtest.h>

namespace floral::stream::audio {
namespace {

TEST(OpusEncoderTest, EncodesFiveMillisecondStereoBlockAndChangesBitrate) {
    std::string error;
    std::unique_ptr<FloralOpusEncoder> encoder = FloralOpusEncoder::Create(128'000, &error);
    ASSERT_NE(nullptr, encoder) << error;

    floral::device::audio::AudioPcmRecord pcm;
    std::vector<uint8_t> payload;
    ASSERT_TRUE(encoder->Encode(pcm.samples.data(), pcm.frame_count, &payload, &error)) << error;
    EXPECT_FALSE(payload.empty());
    EXPECT_LE(payload.size(), 1275U);

    ASSERT_TRUE(encoder->SetBitrate(64'000, &error)) << error;
    EXPECT_EQ(64'000U, encoder->bitrate_bps());
    ASSERT_TRUE(encoder->Reset(&error)) << error;
}

}  // namespace
}  // namespace floral::stream::audio
