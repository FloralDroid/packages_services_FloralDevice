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

#include "floral/stream/display/HardwareBufferImporter.h"

#include <aidlcommonsupport/NativeHandle.h>
#include <cutils/native_handle.h>
#include <vndk/hardware_buffer.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

namespace floral::stream::display {
namespace {

struct NativeHandleViewDeleter {
    void operator()(native_handle_t* handle) const {
        if (handle != nullptr) {
            native_handle_delete(handle);
        }
    }
};

using NativeHandleView = std::unique_ptr<native_handle_t, NativeHandleViewDeleter>;

bool SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool ValidateDescription(
        const aidl::android::hardware::graphics::common::HardwareBufferDescription& description,
        std::string* error) {
    if (description.width <= 0 || description.height <= 0 || description.layers <= 0 ||
        description.stride <= 0) {
        return SetError(error, "hardware buffer dimensions, layers, and stride must be positive");
    }
    return true;
}

}  // namespace

void HardwareBufferDeleter::operator()(AHardwareBuffer* buffer) const {
    if (buffer != nullptr) {
        AHardwareBuffer_release(buffer);
    }
}

UniqueHardwareBuffer ImportHardwareBuffer(
        const aidl::android::hardware::graphics::common::HardwareBuffer& buffer,
        std::string* error) {
    if (!ValidateDescription(buffer.description, error)) {
        return {};
    }
    if (buffer.handle.fds.empty()) {
        SetError(error, "hardware buffer transport handle has no file descriptors");
        return {};
    }
    if (!std::all_of(buffer.handle.fds.begin(), buffer.handle.fds.end(),
                     [](const ndk::ScopedFileDescriptor& fd) { return fd.get() >= 0; })) {
        SetError(error, "hardware buffer transport handle contains an invalid file descriptor");
        return {};
    }

    NativeHandleView nativeHandle(android::makeFromAidl(buffer.handle));
    if (nativeHandle == nullptr) {
        SetError(error, "failed to create native handle view from AIDL handle");
        return {};
    }

    const AHardwareBuffer_Desc description{
            .width = static_cast<uint32_t>(buffer.description.width),
            .height = static_cast<uint32_t>(buffer.description.height),
            .layers = static_cast<uint32_t>(buffer.description.layers),
            .format = static_cast<uint32_t>(buffer.description.format),
            .usage = static_cast<uint64_t>(buffer.description.usage),
            .stride = static_cast<uint32_t>(buffer.description.stride),
    };
    AHardwareBuffer* imported = nullptr;
    const int status = AHardwareBuffer_createFromHandle(
            &description, nativeHandle.get(), AHARDWAREBUFFER_CREATE_FROM_HANDLE_METHOD_CLONE,
            &imported);
    if (status != 0 || imported == nullptr) {
        SetError(error,
                 "AHardwareBuffer_createFromHandle failed with status " + std::to_string(status));
        return {};
    }
    return UniqueHardwareBuffer(imported);
}

}  // namespace floral::stream::display
