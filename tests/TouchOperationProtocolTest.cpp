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

#include "floral/device/input/TouchOperationProtocol.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace floral::device::input {
namespace {

TEST(TouchOperationProtocolTest, RoundTripsTargetBinding) {
    BindInputTargetRequest source;
    source.target_slot = 3;
    source.display_port = 7;
    source.stream_id = 101;
    std::vector<uint8_t> payload;
    std::string error;
    ASSERT_TRUE(SerializeBindInputTargetRequest(source, &payload, &error)) << error;

    BindInputTargetRequest parsed;
    ASSERT_TRUE(ParseBindInputTargetRequest(payload, &parsed, &error)) << error;
    EXPECT_EQ(parsed.target_slot, source.target_slot);
    EXPECT_EQ(parsed.mode, InputTargetMode::kExclusive);
    EXPECT_EQ(parsed.display_port, source.display_port);
    EXPECT_EQ(parsed.stream_id, source.stream_id);

    BindInputTargetResponse response;
    response.result = InputOperationResult::kApplied;
    response.target_slot = source.target_slot;
    response.display_port = source.display_port;
    response.stream_id = source.stream_id;
    response.input_epoch = 9;
    response.logical_width = 1920;
    response.logical_height = 1080;
    response.rotation = 90;
    ASSERT_TRUE(SerializeBindInputTargetResponse(response, &payload, &error)) << error;

    BindInputTargetResponse parsedResponse;
    ASSERT_TRUE(ParseBindInputTargetResponse(payload, &parsedResponse, &error)) << error;
    EXPECT_EQ(parsedResponse.result, response.result);
    EXPECT_EQ(parsedResponse.target_slot, response.target_slot);
    EXPECT_EQ(parsedResponse.display_port, response.display_port);
    EXPECT_EQ(parsedResponse.stream_id, response.stream_id);
    EXPECT_EQ(parsedResponse.input_epoch, response.input_epoch);
    EXPECT_EQ(parsedResponse.logical_width, response.logical_width);
    EXPECT_EQ(parsedResponse.logical_height, response.logical_height);
    EXPECT_EQ(parsedResponse.rotation, response.rotation);
}

TEST(TouchOperationProtocolTest, RoundTripsNormalizedTouchEvent) {
    TouchEvent source;
    source.target_slot = 3;
    source.action = TouchAction::kMove;
    source.pointer_id = 2;
    source.input_epoch = 9;
    source.sequence = 27;
    source.x = 32768;
    source.y = 65535;
    source.pressure = 50000;
    source.touch_major = 2048;

    std::vector<uint8_t> payload;
    std::string error;
    ASSERT_TRUE(SerializeTouchEvent(source, &payload, &error)) << error;
    EXPECT_EQ(payload.size(), 24u);

    TouchEvent parsed;
    ASSERT_TRUE(ParseTouchEvent(payload, &parsed, &error)) << error;
    EXPECT_EQ(parsed.target_slot, source.target_slot);
    EXPECT_EQ(parsed.action, source.action);
    EXPECT_EQ(parsed.pointer_id, source.pointer_id);
    EXPECT_EQ(parsed.input_epoch, source.input_epoch);
    EXPECT_EQ(parsed.sequence, source.sequence);
    EXPECT_EQ(parsed.x, source.x);
    EXPECT_EQ(parsed.y, source.y);
    EXPECT_EQ(parsed.pressure, source.pressure);
    EXPECT_EQ(parsed.touch_major, source.touch_major);
}

TEST(TouchOperationProtocolTest, RejectsStaleOrMalformedTouchEvent) {
    TouchEvent source;
    source.action = TouchAction::kDown;
    source.input_epoch = 1;
    source.sequence = 1;
    source.pressure = 65535;
    std::vector<uint8_t> payload;
    std::string error;
    ASSERT_TRUE(SerializeTouchEvent(source, &payload, &error)) << error;

    payload[3] = 1;
    TouchEvent parsed;
    EXPECT_FALSE(ParseTouchEvent(payload, &parsed, &error));
    source.input_epoch = 0;
    EXPECT_FALSE(SerializeTouchEvent(source, &payload, &error));
    source.input_epoch = 1;
    source.pointer_id = kMaximumTouchPointerId + 1;
    EXPECT_FALSE(SerializeTouchEvent(source, &payload, &error));
}

TEST(TouchOperationProtocolTest, CancelCarriesNoCoordinates) {
    TouchEvent source;
    source.action = TouchAction::kCancel;
    source.input_epoch = 4;
    source.sequence = 5;
    std::vector<uint8_t> payload;
    std::string error;
    ASSERT_TRUE(SerializeTouchEvent(source, &payload, &error)) << error;

    source.x = 1;
    EXPECT_FALSE(SerializeTouchEvent(source, &payload, &error));
}

TEST(TouchOperationProtocolTest, RoundTripsTargetInvalidation) {
    TargetInvalidatedEvent source;
    source.target_slot = 4;
    source.reason = TargetInvalidationReason::kGeometryChanged;
    source.stream_id = 105;
    source.input_epoch = 12;

    std::vector<uint8_t> payload;
    std::string error;
    ASSERT_TRUE(SerializeTargetInvalidatedEvent(source, &payload, &error)) << error;
    TargetInvalidatedEvent parsed;
    ASSERT_TRUE(ParseTargetInvalidatedEvent(payload, &parsed, &error)) << error;
    EXPECT_EQ(parsed.target_slot, source.target_slot);
    EXPECT_EQ(parsed.reason, source.reason);
    EXPECT_EQ(parsed.stream_id, source.stream_id);
    EXPECT_EQ(parsed.input_epoch, source.input_epoch);
}

}  // namespace
}  // namespace floral::device::input
