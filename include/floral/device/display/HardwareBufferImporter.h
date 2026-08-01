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

#include <aidl/android/hardware/graphics/common/HardwareBuffer.h>
#include <android/hardware_buffer.h>

#include <memory>
#include <string>

namespace floral::device::display {

struct HardwareBufferDeleter {
    void operator()(AHardwareBuffer* buffer) const;
};

using UniqueHardwareBuffer = std::unique_ptr<AHardwareBuffer, HardwareBufferDeleter>;

// Clones the transported native handle. The returned buffer remains valid
// after the AIDL parcel and its file descriptors have been released.
UniqueHardwareBuffer ImportHardwareBuffer(
        const aidl::android::hardware::graphics::common::HardwareBuffer& buffer,
        std::string* error);

}  // namespace floral::device::display
