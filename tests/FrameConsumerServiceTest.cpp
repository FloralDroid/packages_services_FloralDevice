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

#include "floral/device/display/FrameConsumerService.h"

#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <android/hardware_buffer.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>
#include <vndk/hardware_buffer.h>

#include <memory>
#include <utility>

namespace floral::device::display {
namespace {

using AidlBufferUsage = aidl::android::hardware::graphics::common::BufferUsage;
using AidlFrameStatus = aidl::floral::device::display::FrameStatus;
using AidlPixelFormat = aidl::android::hardware::graphics::common::PixelFormat;

struct HardwareBufferReleaser {
    void operator()(AHardwareBuffer* buffer) const {
        if (buffer != nullptr) {
            AHardwareBuffer_release(buffer);
        }
    }
};

using HardwareBuffer = std::unique_ptr<AHardwareBuffer, HardwareBufferReleaser>;

class RecordingBackend final : public FrameConsumerBackend {
  public:
    DisplayConsumerStreamState GetStreamState(uint64_t displayId) override {
        DisplayConsumerStreamState state;
        state.display_id = displayId;
        state.generation = 5;
        state.accepting_frames = true;
        return state;
    }

    AidlFrameStatus RegisterBuffer(ImportedBufferRegistration registration) override {
        ++register_count;
        last_buffer_id = registration.buffer_id;
        registered_buffer = std::move(registration.buffer);
        return AidlFrameStatus::ACCEPTED;
    }

    ImportedFrameResult SubmitFrame(ImportedFrameRequest request) override {
        ++submit_count;
        last_source_sequence = request.source_sequence;
        ImportedFrameResult result;
        result.status = AidlFrameStatus::ACCEPTED;
        result.release_fence = std::move(request.acquire_fence);
        return result;
    }

    uint64_t register_count = 0;
    uint64_t submit_count = 0;
    uint64_t last_buffer_id = 0;
    uint64_t last_source_sequence = 0;
    UniqueHardwareBuffer registered_buffer;
};

HardwareBuffer AllocateBuffer(AHardwareBuffer_Desc* outDescription) {
    AHardwareBuffer_Desc description{};
    description.width = 64;
    description.height = 32;
    description.layers = 1;
    description.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    description.usage =
            AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    AHardwareBuffer* buffer = nullptr;
    if (AHardwareBuffer_allocate(&description, &buffer) != 0) {
        return {};
    }
    AHardwareBuffer_describe(buffer, outDescription);
    return HardwareBuffer(buffer);
}

aidl::android::hardware::graphics::common::HardwareBuffer ToAidlHardwareBuffer(
        AHardwareBuffer* buffer, const AHardwareBuffer_Desc& description) {
    aidl::android::hardware::graphics::common::HardwareBuffer result;
    result.description.width = static_cast<int32_t>(description.width);
    result.description.height = static_cast<int32_t>(description.height);
    result.description.layers = static_cast<int32_t>(description.layers);
    result.description.format = static_cast<AidlPixelFormat>(description.format);
    result.description.usage = static_cast<AidlBufferUsage>(description.usage);
    result.description.stride = static_cast<int32_t>(description.stride);
    result.handle = android::dupToAidl(AHardwareBuffer_getNativeHandle(buffer));
    return result;
}

TEST(FrameConsumerServiceTest, ImportsRegistrationAndTransfersFence) {
    auto backend = std::make_shared<RecordingBackend>();
    auto service = ndk::SharedRefBase::make<FrameConsumerService>(backend);

    aidl::floral::device::display::StreamState state;
    ASSERT_TRUE(service->getStreamState(0, &state).isOk());
    EXPECT_EQ(state.displayId, 0);
    EXPECT_EQ(state.generation, 5);
    EXPECT_TRUE(state.acceptingFrames);

    AHardwareBuffer_Desc description{};
    HardwareBuffer source = AllocateBuffer(&description);
    ASSERT_NE(source, nullptr);
    aidl::floral::device::display::BufferRegistration registration;
    registration.displayId = 0;
    registration.generation = 5;
    registration.bufferId = 81;
    registration.buffer = ToAidlHardwareBuffer(source.get(), description);
    AidlFrameStatus registrationStatus = AidlFrameStatus::INTERNAL_ERROR;
    ASSERT_TRUE(service->registerBuffer(registration, &registrationStatus).isOk());
    EXPECT_EQ(registrationStatus, AidlFrameStatus::ACCEPTED);
    ASSERT_NE(backend->registered_buffer, nullptr);
    EXPECT_EQ(backend->last_buffer_id, 81u);
    AHardwareBuffer_Desc importedDescription{};
    AHardwareBuffer_describe(backend->registered_buffer.get(), &importedDescription);
    EXPECT_EQ(importedDescription.width, description.width);
    EXPECT_EQ(importedDescription.stride, description.stride);

    int pipeFds[2];
    ASSERT_EQ(pipe(pipeFds), 0);
    aidl::floral::device::display::FrameRequest request;
    request.displayId = 0;
    request.generation = 5;
    request.bufferId = 81;
    request.sourceSequence = 144;
    request.acquireFence = ndk::ScopedFileDescriptor(fcntl(pipeFds[0], F_DUPFD_CLOEXEC, 0));
    aidl::floral::device::display::FrameResult frameResult;
    ASSERT_TRUE(service->submitFrame(request, &frameResult).isOk());
    EXPECT_EQ(frameResult.status, AidlFrameStatus::ACCEPTED);
    EXPECT_GE(frameResult.releaseFence.get(), 0);
    EXPECT_EQ(backend->submit_count, 1u);
    EXPECT_EQ(backend->last_source_sequence, 144u);
    EXPECT_NE(fcntl(pipeFds[0], F_GETFD), -1);

    close(pipeFds[0]);
    close(pipeFds[1]);
}

TEST(FrameConsumerServiceTest, NullBackendRemainsInactive) {
    auto service = ndk::SharedRefBase::make<FrameConsumerService>(nullptr);
    aidl::floral::device::display::StreamState state;
    ASSERT_TRUE(service->getStreamState(7, &state).isOk());
    EXPECT_EQ(state.displayId, 7);
    EXPECT_FALSE(state.acceptingFrames);
}

}  // namespace
}  // namespace floral::device::display
