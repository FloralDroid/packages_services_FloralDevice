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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace floral::device::input {

constexpr uint8_t kMaximumTouchPointerId = 31;

enum class InputTargetMode : uint8_t {
    kExclusive = 0,
};

enum class InputOperationResult : uint32_t {
    kApplied = 0,
    kUnchanged = 1,
    kInvalidTarget = 2,
    kTargetBusy = 3,
    kUnknownStream = 4,
    kStaleEpoch = 5,
    kInvalidState = 6,
    kInjectionFailed = 7,
};

enum class TouchAction : uint8_t {
    kDown = 0,
    kMove = 1,
    kUp = 2,
    kCancel = 3,
};

enum class TargetInvalidationReason : uint8_t {
    kDisplayRemoved = 1,
    kGeometryChanged = 2,
};

struct BindInputTargetRequest {
    uint8_t target_slot = 0;
    InputTargetMode mode = InputTargetMode::kExclusive;
    uint8_t display_port = 0;
    uint32_t stream_id = 0;
};

struct BindInputTargetResponse {
    InputOperationResult result = InputOperationResult::kInvalidTarget;
    uint8_t target_slot = 0;
    uint8_t display_port = 0;
    uint32_t stream_id = 0;
    uint32_t input_epoch = 0;
    uint32_t logical_width = 0;
    uint32_t logical_height = 0;
    uint16_t rotation = 0;
};

struct UnbindInputTargetRequest {
    uint8_t target_slot = 0;
};

struct UnbindInputTargetResponse {
    InputOperationResult result = InputOperationResult::kInvalidTarget;
    uint8_t target_slot = 0;
};

struct TargetInvalidatedEvent {
    uint8_t target_slot = 0;
    TargetInvalidationReason reason = TargetInvalidationReason::kDisplayRemoved;
    uint32_t stream_id = 0;
    uint32_t input_epoch = 0;
};

// Coordinates, pressure, and touch_major use the full normalized uint16 range.
// The input epoch and sequence prevent delayed events from being applied after
// a target is rebound or reordered by a transport adapter.
struct TouchEvent {
    uint8_t target_slot = 0;
    TouchAction action = TouchAction::kDown;
    uint8_t pointer_id = 0;
    uint32_t input_epoch = 0;
    uint32_t sequence = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t pressure = 0;
    uint16_t touch_major = 0;
};

bool SerializeBindInputTargetRequest(const BindInputTargetRequest& source,
                                     std::vector<uint8_t>* payload, std::string* error);
bool ParseBindInputTargetRequest(const std::vector<uint8_t>& payload,
                                 BindInputTargetRequest* parsed, std::string* error);
bool SerializeBindInputTargetResponse(const BindInputTargetResponse& source,
                                      std::vector<uint8_t>* payload, std::string* error);
bool ParseBindInputTargetResponse(const std::vector<uint8_t>& payload,
                                  BindInputTargetResponse* parsed, std::string* error);

bool SerializeUnbindInputTargetRequest(const UnbindInputTargetRequest& source,
                                       std::vector<uint8_t>* payload, std::string* error);
bool ParseUnbindInputTargetRequest(const std::vector<uint8_t>& payload,
                                   UnbindInputTargetRequest* parsed, std::string* error);
bool SerializeUnbindInputTargetResponse(const UnbindInputTargetResponse& source,
                                        std::vector<uint8_t>* payload, std::string* error);
bool ParseUnbindInputTargetResponse(const std::vector<uint8_t>& payload,
                                    UnbindInputTargetResponse* parsed, std::string* error);

bool SerializeTargetInvalidatedEvent(const TargetInvalidatedEvent& source,
                                     std::vector<uint8_t>* payload, std::string* error);
bool ParseTargetInvalidatedEvent(const std::vector<uint8_t>& payload,
                                 TargetInvalidatedEvent* parsed, std::string* error);

bool SerializeTouchEvent(const TouchEvent& source, std::vector<uint8_t>* payload,
                         std::string* error);
bool ParseTouchEvent(const std::vector<uint8_t>& payload, TouchEvent* parsed, std::string* error);

}  // namespace floral::device::input
