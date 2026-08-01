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

#include "floral/device/display/HardwareBufferImporter.h"

#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/HardwareBuffer.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <gtest/gtest.h>
#include <vndk/hardware_buffer.h>

#include <cstdint>
#include <string>

namespace floral::device::display {
namespace {

using aidl::android::hardware::graphics::common::BufferUsage;
using aidl::android::hardware::graphics::common::HardwareBuffer;
using aidl::android::hardware::graphics::common::PixelFormat;

HardwareBuffer ToAidlBuffer(AHardwareBuffer* buffer) {
    AHardwareBuffer_Desc description{};
    AHardwareBuffer_describe(buffer, &description);

    HardwareBuffer result;
    result.description.width = static_cast<int32_t>(description.width);
    result.description.height = static_cast<int32_t>(description.height);
    result.description.layers = static_cast<int32_t>(description.layers);
    result.description.format = static_cast<PixelFormat>(description.format);
    result.description.usage = static_cast<BufferUsage>(description.usage);
    result.description.stride = static_cast<int32_t>(description.stride);
    result.handle = android::dupToAidl(AHardwareBuffer_getNativeHandle(buffer));
    return result;
}

TEST(HardwareBufferImporterTest, ClonesTransportedHandleAndPreservesDescription) {
    const AHardwareBuffer_Desc sourceDescription{
            .width = 64,
            .height = 32,
            .layers = 1,
            .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
            .usage =
                    AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER,
            .stride = 0,
    };
    AHardwareBuffer* allocatedSource = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&sourceDescription, &allocatedSource), 0);
    UniqueHardwareBuffer source(allocatedSource);
    ASSERT_NE(source, nullptr);

    UniqueHardwareBuffer imported;
    {
        HardwareBuffer parcel = ToAidlBuffer(source.get());
        std::string error;
        imported = ImportHardwareBuffer(parcel, &error);
        ASSERT_NE(imported, nullptr) << error;
    }
    source.reset();

    AHardwareBuffer_Desc importedDescription{};
    AHardwareBuffer_describe(imported.get(), &importedDescription);
    EXPECT_EQ(importedDescription.width, 64u);
    EXPECT_EQ(importedDescription.height, 32u);
    EXPECT_EQ(importedDescription.layers, 1u);
    EXPECT_EQ(importedDescription.format, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
    EXPECT_GT(importedDescription.stride, 0u);
}

TEST(HardwareBufferImporterTest, RejectsInvalidDescriptionBeforeImport) {
    HardwareBuffer parcel;
    parcel.description.width = 0;
    parcel.description.height = 32;
    parcel.description.layers = 1;
    parcel.description.stride = 64;

    std::string error;
    UniqueHardwareBuffer imported = ImportHardwareBuffer(parcel, &error);

    EXPECT_EQ(imported, nullptr);
    EXPECT_EQ(error, "hardware buffer dimensions, layers, and stride must be positive");
}

}  // namespace
}  // namespace floral::device::display
