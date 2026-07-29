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

#include "floral/stream/VideoGeometry.h"

#include <android-base/unique_fd.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace floral::stream::codec {

struct FrameCopyResult {
    bool success = false;
    android::base::unique_fd release_fence;
    bool completed_synchronously = false;
};

// Imports registered hardware buffers once and blits them into an encoder
// input surface. Registration retains a buffer reference until unregistration.
class EglFrameCopier {
  public:
    static std::unique_ptr<EglFrameCopier> Create(ANativeWindow* outputWindow,
                                                  const VideoGeometry& geometry,
                                                  std::string* error);
    static std::unique_ptr<EglFrameCopier> Create(AHardwareBuffer* outputBuffer,
                                                  const VideoGeometry& geometry,
                                                  std::string* error);

    ~EglFrameCopier();

    EglFrameCopier(const EglFrameCopier&) = delete;
    EglFrameCopier& operator=(const EglFrameCopier&) = delete;

    bool RegisterBuffer(AHardwareBuffer* buffer, uint64_t* outBufferId, std::string* error);
    void UnregisterBuffer(uint64_t bufferId);

    // The acquire fence is consumed. A valid returned fence signals when the
    // source buffer may be reused; an invalid fence means completion was synchronous.
    // Hardware-buffer targets complete synchronously so their contents can be
    // consumed immediately by a graphics API without additional fence plumbing.
    FrameCopyResult CopyFrame(uint64_t bufferId, android::base::unique_fd acquireFence,
                              int64_t presentationTimeNanos, std::string* error);

    bool uses_native_fences() const;
    size_t registered_buffer_count() const;

  private:
    struct Impl;

    explicit EglFrameCopier(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace floral::stream::codec
