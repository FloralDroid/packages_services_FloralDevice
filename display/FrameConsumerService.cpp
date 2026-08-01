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

#include <fcntl.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace floral::device::display {
namespace {

using AidlFrameStatus = aidl::floral::device::display::FrameStatus;

android::base::unique_fd DuplicateFence(const ndk::ScopedFileDescriptor& fence) {
    if (fence.get() < 0) {
        return {};
    }
    return android::base::unique_fd(fcntl(fence.get(), F_DUPFD_CLOEXEC, 0));
}

bool FitsInt32(uint32_t value) {
    return value <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
}

class InactiveFrameConsumerBackend final : public FrameConsumerBackend {
  public:
    DisplayConsumerStreamState GetStreamState(uint64_t displayId) override {
        DisplayConsumerStreamState state;
        state.display_id = displayId;
        return state;
    }

    AidlFrameStatus RegisterBuffer(ImportedBufferRegistration registration) override {
        (void)registration;
        return AidlFrameStatus::NO_ACTIVE_STREAM;
    }

    ImportedFrameResult SubmitFrame(ImportedFrameRequest request) override {
        (void)request;
        ImportedFrameResult result;
        result.status = AidlFrameStatus::NO_ACTIVE_STREAM;
        return result;
    }
};

}  // namespace

std::shared_ptr<FrameConsumerBackend> CreateInactiveFrameConsumerBackend() {
    return std::make_shared<InactiveFrameConsumerBackend>();
}

FrameConsumerService::FrameConsumerService(std::shared_ptr<FrameConsumerBackend> backend)
    : backend_(backend != nullptr ? std::move(backend) : CreateInactiveFrameConsumerBackend()) {}

ndk::ScopedAStatus FrameConsumerService::getStreamState(
        int64_t displayId, aidl::floral::device::display::StreamState* result) {
    if (result == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    if (displayId < 0) {
        *result = {};
        return ndk::ScopedAStatus::ok();
    }

    const DisplayConsumerStreamState state =
            backend_->GetStreamState(static_cast<uint64_t>(displayId));
    if (state.display_id != static_cast<uint64_t>(displayId) || !FitsInt32(state.generation)) {
        *result = {};
        result->displayId = displayId;
        return ndk::ScopedAStatus::ok();
    }
    result->displayId = static_cast<int64_t>(state.display_id);
    result->generation = static_cast<int32_t>(state.generation);
    result->acceptingFrames = state.accepting_frames;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus FrameConsumerService::registerBuffer(
        const aidl::floral::device::display::BufferRegistration& registration,
        AidlFrameStatus* result) {
    if (result == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    *result = AidlFrameStatus::UNSUPPORTED_BUFFER;
    if (registration.displayId < 0 || registration.generation < 0 || registration.bufferId < 0) {
        return ndk::ScopedAStatus::ok();
    }

    std::string error;
    UniqueHardwareBuffer buffer = ImportHardwareBuffer(registration.buffer, &error);
    if (buffer == nullptr) {
        return ndk::ScopedAStatus::ok();
    }

    ImportedBufferRegistration imported;
    imported.display_id = static_cast<uint64_t>(registration.displayId);
    imported.generation = static_cast<uint32_t>(registration.generation);
    imported.buffer_id = static_cast<uint64_t>(registration.bufferId);
    imported.buffer = std::move(buffer);
    *result = backend_->RegisterBuffer(std::move(imported));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus FrameConsumerService::submitFrame(
        const aidl::floral::device::display::FrameRequest& request,
        aidl::floral::device::display::FrameResult* result) {
    if (result == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    *result = {};
    result->status = AidlFrameStatus::INTERNAL_ERROR;
    if (request.displayId < 0 || request.generation < 0 || request.bufferId < 0 ||
        request.sourceSequence < 0) {
        return ndk::ScopedAStatus::ok();
    }

    android::base::unique_fd acquireFence = DuplicateFence(request.acquireFence);
    if (request.acquireFence.get() >= 0 && !acquireFence.ok()) {
        return ndk::ScopedAStatus::ok();
    }

    ImportedFrameRequest imported;
    imported.display_id = static_cast<uint64_t>(request.displayId);
    imported.generation = static_cast<uint32_t>(request.generation);
    imported.buffer_id = static_cast<uint64_t>(request.bufferId);
    imported.source_sequence = static_cast<uint64_t>(request.sourceSequence);
    imported.frame_submit_time_nanos = request.frameSubmitTimeNs;
    imported.presentation_time_nanos = request.presentationTimeNs;
    imported.dataspace = request.dataspace;
    imported.acquire_fence = std::move(acquireFence);

    ImportedFrameResult importedResult = backend_->SubmitFrame(std::move(imported));
    result->status = importedResult.status;
    if (result->status == AidlFrameStatus::ACCEPTED) {
        result->releaseFence = ndk::ScopedFileDescriptor(importedResult.release_fence.release());
    }
    return ndk::ScopedAStatus::ok();
}

}  // namespace floral::device::display
