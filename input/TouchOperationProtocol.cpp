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

namespace floral::device::input {
namespace {

constexpr size_t kBindRequestSize = 8;
constexpr size_t kBindResponseSize = 28;
constexpr size_t kUnbindRequestSize = 4;
constexpr size_t kUnbindResponseSize = 8;
constexpr size_t kTouchEventSize = 24;

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

void WriteUint16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value >> 8);
    output[1] = static_cast<uint8_t>(value);
}

void WriteUint32(uint8_t* output, uint32_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        output[index] = static_cast<uint8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

uint16_t ReadUint16(const uint8_t* input) {
    return static_cast<uint16_t>((static_cast<uint16_t>(input[0]) << 8) | input[1]);
}

uint32_t ReadUint32(const uint8_t* input) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[index];
    }
    return value;
}

bool IsKnownMode(InputTargetMode mode) {
    return mode == InputTargetMode::kExclusive;
}

bool IsKnownResult(InputOperationResult result) {
    switch (result) {
        case InputOperationResult::kApplied:
        case InputOperationResult::kUnchanged:
        case InputOperationResult::kInvalidTarget:
        case InputOperationResult::kTargetBusy:
        case InputOperationResult::kUnknownStream:
        case InputOperationResult::kStaleEpoch:
        case InputOperationResult::kInvalidState:
        case InputOperationResult::kInjectionFailed:
            return true;
    }
    return false;
}

bool IsSuccessfulResult(InputOperationResult result) {
    return result == InputOperationResult::kApplied || result == InputOperationResult::kUnchanged;
}

bool IsKnownRotation(uint16_t rotation) {
    return rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270;
}

bool IsKnownAction(TouchAction action) {
    switch (action) {
        case TouchAction::kDown:
        case TouchAction::kMove:
        case TouchAction::kUp:
        case TouchAction::kCancel:
            return true;
    }
    return false;
}

bool IsValidBindResponse(const BindInputTargetResponse& source) {
    if (!IsKnownResult(source.result) || !IsKnownRotation(source.rotation)) {
        return false;
    }
    if (IsSuccessfulResult(source.result)) {
        return source.stream_id != 0 && source.input_epoch != 0 && source.logical_width != 0 &&
               source.logical_height != 0;
    }
    return source.input_epoch == 0 && source.logical_width == 0 && source.logical_height == 0 &&
           source.rotation == 0;
}

bool IsValidTouchEvent(const TouchEvent& source) {
    if (!IsKnownAction(source.action) || source.pointer_id > kMaximumTouchPointerId ||
        source.input_epoch == 0 || source.sequence == 0) {
        return false;
    }
    if (source.action == TouchAction::kDown || source.action == TouchAction::kMove) {
        return source.pressure != 0;
    }
    if (source.action == TouchAction::kCancel) {
        return source.x == 0 && source.y == 0 && source.pressure == 0 && source.touch_major == 0;
    }
    return true;
}

}  // namespace

bool SerializeBindInputTargetRequest(const BindInputTargetRequest& source,
                                     std::vector<uint8_t>* payload, std::string* error) {
    if (payload == nullptr) {
        return SetError(error, "bind input target payload output is null");
    }
    if (!IsKnownMode(source.mode) || source.stream_id == 0) {
        return SetError(error, "bind input target request is invalid");
    }
    payload->assign(kBindRequestSize, 0);
    (*payload)[0] = source.target_slot;
    (*payload)[1] = static_cast<uint8_t>(source.mode);
    WriteUint32(payload->data() + 4, source.stream_id);
    return true;
}

bool ParseBindInputTargetRequest(const std::vector<uint8_t>& payload,
                                 BindInputTargetRequest* parsed, std::string* error) {
    if (parsed == nullptr) {
        return SetError(error, "parsed bind input target output is null");
    }
    if (payload.size() != kBindRequestSize || ReadUint16(payload.data() + 2) != 0) {
        return SetError(error, "bind input target request payload is invalid");
    }
    BindInputTargetRequest result;
    result.target_slot = payload[0];
    result.mode = static_cast<InputTargetMode>(payload[1]);
    result.stream_id = ReadUint32(payload.data() + 4);
    if (!IsKnownMode(result.mode) || result.stream_id == 0) {
        return SetError(error, "bind input target request is invalid");
    }
    *parsed = result;
    return true;
}

bool SerializeBindInputTargetResponse(const BindInputTargetResponse& source,
                                      std::vector<uint8_t>* payload, std::string* error) {
    if (payload == nullptr) {
        return SetError(error, "bind input target response payload output is null");
    }
    if (!IsValidBindResponse(source)) {
        return SetError(error, "bind input target response is invalid");
    }
    payload->assign(kBindResponseSize, 0);
    WriteUint32(payload->data(), static_cast<uint32_t>(source.result));
    (*payload)[4] = source.target_slot;
    WriteUint32(payload->data() + 8, source.stream_id);
    WriteUint32(payload->data() + 12, source.input_epoch);
    WriteUint32(payload->data() + 16, source.logical_width);
    WriteUint32(payload->data() + 20, source.logical_height);
    WriteUint16(payload->data() + 24, source.rotation);
    return true;
}

bool ParseBindInputTargetResponse(const std::vector<uint8_t>& payload,
                                  BindInputTargetResponse* parsed, std::string* error) {
    if (parsed == nullptr) {
        return SetError(error, "parsed bind input target response output is null");
    }
    if (payload.size() != kBindResponseSize || payload[5] != 0 || payload[6] != 0 ||
        payload[7] != 0 || ReadUint16(payload.data() + 26) != 0) {
        return SetError(error, "bind input target response payload is invalid");
    }
    BindInputTargetResponse result;
    result.result = static_cast<InputOperationResult>(ReadUint32(payload.data()));
    result.target_slot = payload[4];
    result.stream_id = ReadUint32(payload.data() + 8);
    result.input_epoch = ReadUint32(payload.data() + 12);
    result.logical_width = ReadUint32(payload.data() + 16);
    result.logical_height = ReadUint32(payload.data() + 20);
    result.rotation = ReadUint16(payload.data() + 24);
    if (!IsValidBindResponse(result)) {
        return SetError(error, "bind input target response is invalid");
    }
    *parsed = result;
    return true;
}

bool SerializeUnbindInputTargetRequest(const UnbindInputTargetRequest& source,
                                       std::vector<uint8_t>* payload, std::string* error) {
    if (payload == nullptr) {
        return SetError(error, "unbind input target payload output is null");
    }
    payload->assign(kUnbindRequestSize, 0);
    (*payload)[0] = source.target_slot;
    return true;
}

bool ParseUnbindInputTargetRequest(const std::vector<uint8_t>& payload,
                                   UnbindInputTargetRequest* parsed, std::string* error) {
    if (parsed == nullptr) {
        return SetError(error, "parsed unbind input target output is null");
    }
    if (payload.size() != kUnbindRequestSize || payload[1] != 0 || payload[2] != 0 ||
        payload[3] != 0) {
        return SetError(error, "unbind input target request payload is invalid");
    }
    parsed->target_slot = payload[0];
    return true;
}

bool SerializeUnbindInputTargetResponse(const UnbindInputTargetResponse& source,
                                        std::vector<uint8_t>* payload, std::string* error) {
    if (payload == nullptr) {
        return SetError(error, "unbind input target response payload output is null");
    }
    if (!IsKnownResult(source.result)) {
        return SetError(error, "unbind input target result is unknown");
    }
    payload->assign(kUnbindResponseSize, 0);
    WriteUint32(payload->data(), static_cast<uint32_t>(source.result));
    (*payload)[4] = source.target_slot;
    return true;
}

bool ParseUnbindInputTargetResponse(const std::vector<uint8_t>& payload,
                                    UnbindInputTargetResponse* parsed, std::string* error) {
    if (parsed == nullptr) {
        return SetError(error, "parsed unbind input target response output is null");
    }
    if (payload.size() != kUnbindResponseSize || payload[5] != 0 || payload[6] != 0 ||
        payload[7] != 0) {
        return SetError(error, "unbind input target response payload is invalid");
    }
    UnbindInputTargetResponse result;
    result.result = static_cast<InputOperationResult>(ReadUint32(payload.data()));
    result.target_slot = payload[4];
    if (!IsKnownResult(result.result)) {
        return SetError(error, "unbind input target result is unknown");
    }
    *parsed = result;
    return true;
}

bool SerializeTouchEvent(const TouchEvent& source, std::vector<uint8_t>* payload,
                         std::string* error) {
    if (payload == nullptr) {
        return SetError(error, "touch event payload output is null");
    }
    if (!IsValidTouchEvent(source)) {
        return SetError(error, "touch event is invalid");
    }
    payload->assign(kTouchEventSize, 0);
    (*payload)[0] = source.target_slot;
    (*payload)[1] = static_cast<uint8_t>(source.action);
    (*payload)[2] = source.pointer_id;
    WriteUint32(payload->data() + 4, source.input_epoch);
    WriteUint32(payload->data() + 8, source.sequence);
    WriteUint16(payload->data() + 12, source.x);
    WriteUint16(payload->data() + 14, source.y);
    WriteUint16(payload->data() + 16, source.pressure);
    WriteUint16(payload->data() + 18, source.touch_major);
    return true;
}

bool ParseTouchEvent(const std::vector<uint8_t>& payload, TouchEvent* parsed, std::string* error) {
    if (parsed == nullptr) {
        return SetError(error, "parsed touch event output is null");
    }
    if (payload.size() != kTouchEventSize || payload[3] != 0 ||
        ReadUint32(payload.data() + 20) != 0) {
        return SetError(error, "touch event payload is invalid");
    }
    TouchEvent result;
    result.target_slot = payload[0];
    result.action = static_cast<TouchAction>(payload[1]);
    result.pointer_id = payload[2];
    result.input_epoch = ReadUint32(payload.data() + 4);
    result.sequence = ReadUint32(payload.data() + 8);
    result.x = ReadUint16(payload.data() + 12);
    result.y = ReadUint16(payload.data() + 14);
    result.pressure = ReadUint16(payload.data() + 16);
    result.touch_major = ReadUint16(payload.data() + 18);
    if (!IsValidTouchEvent(result)) {
        return SetError(error, "touch event is invalid");
    }
    *parsed = result;
    return true;
}

}  // namespace floral::device::input
