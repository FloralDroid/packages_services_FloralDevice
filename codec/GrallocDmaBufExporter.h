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

#include <android-base/unique_fd.h>
#include <android/hardware_buffer.h>
#include <floral/display/GrallocMetadata.h>
#include <hardware/gralloc.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace floral::stream::codec {

struct ExportedDmaBufObject {
    android::base::unique_fd fd;
    uint32_t flags = 0;
    uint64_t size = 0;
    uint64_t modifier = 0;
};

struct ExportedDmaBufPlane {
    uint32_t object_index = 0;
    uint64_t offset = 0;
    uint64_t pitch = 0;
};

struct ExportedDmaBuf {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t drm_format = 0;
    uint32_t object_count = 0;
    uint32_t plane_count = 0;
    std::array<ExportedDmaBufObject, FLORAL_GRALLOC_BUFFER_MAX_DRM_OBJECTS> objects;
    std::array<ExportedDmaBufPlane, FLORAL_GRALLOC_BUFFER_MAX_DRM_PLANES> planes;
};

// Resolves the allocator-owned DRM PRIME layout without depending on its
// private native_handle representation. ExportedDmaBuf owns duplicated fds.
class GrallocDmaBufExporter {
  public:
    static std::unique_ptr<GrallocDmaBufExporter> Create(std::string* error);

    GrallocDmaBufExporter(const GrallocDmaBufExporter&) = delete;
    GrallocDmaBufExporter& operator=(const GrallocDmaBufExporter&) = delete;

    bool Export(AHardwareBuffer* buffer, ExportedDmaBuf* outDmaBuf, std::string* error) const;

  private:
    explicit GrallocDmaBufExporter(const gralloc_module_t* grallocModule);

    const gralloc_module_t* gralloc_module_ = nullptr;
};

}  // namespace floral::stream::codec
