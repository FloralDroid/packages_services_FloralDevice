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

#include <aidl/floral/stream/display/BnFrameConsumer.h>
#include <aidl/floral/stream/display/FrameStatus.h>
#include <android-base/unique_fd.h>

#include <cstdint>
#include <memory>

#include "floral/stream/display/HardwareBufferImporter.h"

namespace floral::stream::display {

struct DisplayConsumerStreamState {
    uint64_t display_id = 0;
    uint32_t generation = 0;
    bool accepting_frames = false;
};

struct ImportedBufferRegistration {
    uint64_t display_id = 0;
    uint32_t generation = 0;
    uint64_t buffer_id = 0;
    UniqueHardwareBuffer buffer;
};

struct ImportedFrameRequest {
    uint64_t display_id = 0;
    uint32_t generation = 0;
    uint64_t buffer_id = 0;
    uint64_t source_sequence = 0;
    int64_t frame_submit_time_nanos = 0;
    int64_t presentation_time_nanos = 0;
    int32_t dataspace = 0;
    android::base::unique_fd acquire_fence;
};

struct ImportedFrameResult {
    aidl::floral::stream::display::FrameStatus status =
            aidl::floral::stream::display::FrameStatus::INTERNAL_ERROR;
    android::base::unique_fd release_fence;
};

// Implementations own imported buffers and serialize access to their stream
// session. Binder worker threads may call this interface concurrently.
class FrameConsumerBackend {
  public:
    virtual ~FrameConsumerBackend() = default;

    virtual DisplayConsumerStreamState GetStreamState(uint64_t displayId) = 0;
    virtual aidl::floral::stream::display::FrameStatus RegisterBuffer(
            ImportedBufferRegistration registration) = 0;
    virtual ImportedFrameResult SubmitFrame(ImportedFrameRequest request) = 0;
};

std::shared_ptr<FrameConsumerBackend> CreateInactiveFrameConsumerBackend();

class FrameConsumerService final : public aidl::floral::stream::display::BnFrameConsumer {
  public:
    explicit FrameConsumerService(std::shared_ptr<FrameConsumerBackend> backend);

    ndk::ScopedAStatus getStreamState(int64_t displayId,
                                      aidl::floral::stream::display::StreamState* result) override;
    ndk::ScopedAStatus registerBuffer(
            const aidl::floral::stream::display::BufferRegistration& registration,
            aidl::floral::stream::display::FrameStatus* result) override;
    ndk::ScopedAStatus submitFrame(const aidl::floral::stream::display::FrameRequest& request,
                                   aidl::floral::stream::display::FrameResult* result) override;

  private:
    const std::shared_ptr<FrameConsumerBackend> backend_;
};

}  // namespace floral::stream::display
