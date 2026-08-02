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

#include "floral/device/service/AudioEncoderControlProtocol.h"

#include <gtest/gtest.h>

namespace floral::device::service {
namespace {

void WriteUint32(std::vector<uint8_t>* output, size_t offset, uint32_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        (*output)[offset + index] =
                static_cast<uint8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

uint32_t ReadUint32(const std::vector<uint8_t>& input, size_t offset) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[offset + index];
    }
    return value;
}

TEST(AudioEncoderControlProtocolTest, ParsesBitrateUpdate) {
    std::vector<uint8_t> payload(kAudioEncoderConfigRequestSize, 0);
    WriteUint32(&payload, 0, 1);
    WriteUint32(&payload, 4, kAudioEncoderConfigBitrate);
    WriteUint32(&payload, 8, 96'000);

    AudioEncoderConfigUpdate update;
    std::string error;
    ASSERT_TRUE(ParseAudioEncoderConfigRequest(payload, &update, &error)) << error;
    EXPECT_EQ(1U, update.stream_id);
    EXPECT_EQ(kAudioEncoderConfigBitrate, update.fields);
    EXPECT_EQ(96'000U, update.bitrate_bps);
}

TEST(AudioEncoderControlProtocolTest, SerializesEffectiveState) {
    AudioEncoderRuntimeState state;
    state.generation = 4;
    state.bitrate_bps = 160'000;
    state.codec_id = 1;
    control::ControlResponse response;
    std::string error;
    ASSERT_TRUE(SerializeAudioEncoderConfigResponse(state, AudioEncoderConfigResult::kApplied, 9,
                                                    &response, &error))
            << error;
    EXPECT_EQ(static_cast<uint16_t>(control::ControlCommandId::kSetAudioEncoderConfig),
              response.header.command_id);
    EXPECT_EQ(9U, response.header.request_id);
    EXPECT_EQ(4U, ReadUint32(response.payload, 8));
    EXPECT_EQ(160'000U, ReadUint32(response.payload, 12));
}

}  // namespace
}  // namespace floral::device::service
