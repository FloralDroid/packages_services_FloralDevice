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

#include "floral/stream/control/ControlProtocol.h"

#include <gtest/gtest.h>

#include <string>

namespace floral::stream::control {
namespace {

TEST(ControlProtocolTest, RoundTripsFixedHeader) {
    ControlPacketHeader source;
    source.message_type = static_cast<uint16_t>(ControlMessageType::kReplaceDisplayTopologyRequest);
    source.request_id = 0x10203040;
    source.payload_size = 4096;

    SerializedControlPacketHeader serialized{};
    std::string error;
    ASSERT_TRUE(SerializeControlPacketHeader(source, &serialized, &error)) << error;
    EXPECT_EQ(serialized[0], 'F');
    EXPECT_EQ(serialized[1], 'S');
    EXPECT_EQ(serialized[2], 'C');
    EXPECT_EQ(serialized[3], '1');

    ControlPacketHeader parsed;
    ASSERT_TRUE(ParseControlPacketHeader(serialized, &parsed, &error)) << error;
    EXPECT_EQ(parsed.message_type, source.message_type);
    EXPECT_EQ(parsed.flags, 0);
    EXPECT_EQ(parsed.request_id, source.request_id);
    EXPECT_EQ(parsed.payload_size, source.payload_size);
}

TEST(ControlProtocolTest, RejectsUnsupportedFramingAndOversizedPayload) {
    ControlPacketHeader source;
    SerializedControlPacketHeader serialized{};
    std::string error;
    ASSERT_TRUE(SerializeControlPacketHeader(source, &serialized, &error));

    serialized[0] = 'X';
    EXPECT_FALSE(ParseControlPacketHeader(serialized, &source, &error));
    ASSERT_TRUE(SerializeControlPacketHeader(source, &serialized, &error));
    serialized[23] = 1;
    EXPECT_FALSE(ParseControlPacketHeader(serialized, &source, &error));

    source.flags = 1;
    EXPECT_FALSE(SerializeControlPacketHeader(source, &serialized, &error));
    source.flags = 0;
    source.payload_size = kMaximumControlPayloadSize + 1;
    EXPECT_FALSE(SerializeControlPacketHeader(source, &serialized, &error));
}

TEST(ControlProtocolTest, BuildsCorrelatedErrorResponse) {
    ControlResponse response;
    std::string error;
    ASSERT_TRUE(SerializeControlErrorResponse(19, 0x2345, ControlError::kUnsupportedMessage,
                                              &response, &error))
            << error;
    EXPECT_EQ(response.header.message_type,
              static_cast<uint16_t>(ControlMessageType::kErrorResponse));
    EXPECT_EQ(response.header.request_id, 19u);
    EXPECT_EQ(response.header.payload_size, 8u);
    EXPECT_EQ(response.payload.size(), 8u);
    EXPECT_FALSE(response.refreshes_authority_lease);
}

}  // namespace
}  // namespace floral::stream::control
