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

#include "floral/device/operation/OperationProtocol.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace floral::device::operation {
namespace {

TEST(OperationProtocolTest, RoundTripsRequestAndEventHeaders) {
    OperationPacketHeader source;
    source.operation_code = static_cast<uint16_t>(OperationCode::kBindInputTarget);
    source.request_id = 0x10203040;
    source.payload_size = 8;

    SerializedOperationPacketHeader serialized{};
    std::string error;
    ASSERT_TRUE(SerializeOperationPacketHeader(source, &serialized, &error)) << error;
    EXPECT_EQ(serialized[0], 'F');
    EXPECT_EQ(serialized[1], 'D');
    EXPECT_EQ(serialized[2], 'O');
    EXPECT_EQ(serialized[3], '1');

    OperationPacketHeader parsed;
    ASSERT_TRUE(ParseOperationPacketHeader(serialized, &parsed, &error)) << error;
    EXPECT_EQ(parsed.operation_code, source.operation_code);
    EXPECT_EQ(parsed.route_kind, MakeFdo1RouteKind(OperationPacketKind::kRequest));
    EXPECT_EQ(parsed.request_id, source.request_id);
    EXPECT_EQ(parsed.payload_size, source.payload_size);

    source.operation_code = static_cast<uint16_t>(OperationCode::kTouch);
    source.route_kind = MakeFdo1RouteKind(OperationPacketKind::kEvent);
    source.request_id = 0;
    source.payload_size = 24;
    ASSERT_TRUE(SerializeOperationPacketHeader(source, &serialized, &error)) << error;
    ASSERT_TRUE(ParseOperationPacketHeader(serialized, &parsed, &error)) << error;
    EXPECT_EQ(parsed.request_id, 0u);
}

TEST(OperationProtocolTest, EnforcesRequestIdAndReservedFields) {
    OperationPacketHeader source;
    source.operation_code = static_cast<uint16_t>(OperationCode::kTouch);
    SerializedOperationPacketHeader serialized{};
    std::string error;
    EXPECT_FALSE(SerializeOperationPacketHeader(source, &serialized, &error));

    source.route_kind = MakeFdo1RouteKind(OperationPacketKind::kEvent);
    ASSERT_TRUE(SerializeOperationPacketHeader(source, &serialized, &error)) << error;
    serialized[23] = 1;
    EXPECT_FALSE(ParseOperationPacketHeader(serialized, &source, &error));
}

TEST(OperationProtocolTest, RoundTripsErrorPayload) {
    OperationErrorPayload source;
    source.error = OperationError::kMalformedMessage;
    source.failed_operation_code = static_cast<uint16_t>(OperationCode::kTouch);

    std::vector<uint8_t> payload;
    std::string error;
    ASSERT_TRUE(SerializeOperationErrorPayload(source, &payload, &error)) << error;
    OperationErrorPayload parsed;
    ASSERT_TRUE(ParseOperationErrorPayload(payload, &parsed, &error)) << error;
    EXPECT_EQ(parsed.error, source.error);
    EXPECT_EQ(parsed.failed_operation_code, source.failed_operation_code);
}

}  // namespace
}  // namespace floral::device::operation
