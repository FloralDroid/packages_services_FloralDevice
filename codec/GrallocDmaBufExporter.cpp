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

#include "GrallocDmaBufExporter.h"

#include <cutils/native_handle.h>
#include <fcntl.h>
#include <hardware/hardware.h>
#include <vndk/hardware_buffer.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace floral::stream::codec {
namespace {

static_assert(sizeof(floral_gralloc_buffer_metadata_v1_t) == FLORAL_GRALLOC_BUFFER_METADATA_V1_SIZE,
              "Floral gralloc metadata ABI layout changed");
static_assert(sizeof(floral_gralloc_drm_object_v1_t) == FLORAL_GRALLOC_DRM_OBJECT_V1_SIZE,
              "Floral gralloc DRM object ABI layout changed");
static_assert(sizeof(floral_gralloc_drm_plane_v1_t) == FLORAL_GRALLOC_DRM_PLANE_V1_SIZE,
              "Floral gralloc DRM plane ABI layout changed");

constexpr uint32_t kKnownMetadataFlags = FLORAL_GRALLOC_BUFFER_METADATA_FLAG_DRM_PRIME |
                                         FLORAL_GRALLOC_BUFFER_METADATA_FLAG_PROTECTED;
constexpr uint32_t kKnownObjectFlags = FLORAL_GRALLOC_DRM_OBJECT_FLAG_DEDICATED;

bool SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

}  // namespace

GrallocDmaBufExporter::GrallocDmaBufExporter(const gralloc_module_t* grallocModule)
    : gralloc_module_(grallocModule) {}

std::unique_ptr<GrallocDmaBufExporter> GrallocDmaBufExporter::Create(std::string* error) {
    const hw_module_t* module = nullptr;
    const int status = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
    if (status != 0 || module == nullptr) {
        SetError(error, "failed to load the gralloc hardware module: " + std::to_string(status));
        return nullptr;
    }
    const auto* grallocModule = reinterpret_cast<const gralloc_module_t*>(module);
    if (grallocModule->perform == nullptr) {
        SetError(error, "the gralloc hardware module has no perform entry point");
        return nullptr;
    }
    return std::unique_ptr<GrallocDmaBufExporter>(new GrallocDmaBufExporter(grallocModule));
}

bool GrallocDmaBufExporter::Export(AHardwareBuffer* buffer, ExportedDmaBuf* outDmaBuf,
                                   std::string* error) const {
    if (buffer == nullptr || outDmaBuf == nullptr || gralloc_module_ == nullptr ||
        gralloc_module_->perform == nullptr) {
        return SetError(error, "DMA-BUF export requires a buffer, output, and gralloc module");
    }

    const native_handle_t* handle = AHardwareBuffer_getNativeHandle(buffer);
    if (handle == nullptr || handle->numFds < 0) {
        return SetError(error, "AHardwareBuffer has no valid native handle");
    }

    floral_gralloc_buffer_metadata_v1_t metadata{};
    metadata.struct_size = sizeof(metadata);
    metadata.version = FLORAL_GRALLOC_BUFFER_METADATA_VERSION_1;
    const int status = gralloc_module_->perform(
            gralloc_module_, FLORAL_GRALLOC_MODULE_PERFORM_GET_BUFFER_METADATA, handle, &metadata);
    if (status != 0) {
        return SetError(error, "gralloc does not provide Floral buffer metadata: " +
                                       std::to_string(status));
    }
    if (metadata.struct_size != sizeof(metadata) ||
        metadata.version != FLORAL_GRALLOC_BUFFER_METADATA_VERSION_1) {
        return SetError(error, "gralloc returned an incompatible Floral metadata version");
    }
    if ((metadata.flags & ~kKnownMetadataFlags) != 0) {
        return SetError(error, "gralloc returned unknown Floral metadata flags");
    }
    if ((metadata.flags & FLORAL_GRALLOC_BUFFER_METADATA_FLAG_PROTECTED) != 0) {
        return SetError(error, "protected buffers are not supported by the video encoder");
    }
    if ((metadata.flags & FLORAL_GRALLOC_BUFFER_METADATA_FLAG_DRM_PRIME) == 0) {
        return SetError(error, "gralloc does not expose a DRM PRIME buffer description");
    }

    AHardwareBuffer_Desc description{};
    AHardwareBuffer_describe(buffer, &description);
    if (metadata.width == 0 || metadata.height == 0 || metadata.layers != 1 ||
        metadata.format == 0 || metadata.stride == 0 || metadata.buffer_id == 0 ||
        metadata.width != description.width || metadata.height != description.height ||
        metadata.layers != description.layers ||
        metadata.format != static_cast<int32_t>(description.format) ||
        metadata.stride != description.stride || metadata.usage != description.usage) {
        return SetError(error, "gralloc metadata does not match the AHardwareBuffer description");
    }
    if (metadata.drm_format == 0 || metadata.drm_object_count == 0 ||
        metadata.drm_object_count > FLORAL_GRALLOC_BUFFER_MAX_DRM_OBJECTS ||
        metadata.drm_plane_count == 0 ||
        metadata.drm_plane_count > FLORAL_GRALLOC_BUFFER_MAX_DRM_PLANES) {
        return SetError(error, "gralloc returned invalid DRM PRIME object or plane counts");
    }

    ExportedDmaBuf exported;
    exported.width = metadata.width;
    exported.height = metadata.height;
    exported.drm_format = metadata.drm_format;
    exported.object_count = metadata.drm_object_count;
    exported.plane_count = metadata.drm_plane_count;
    for (uint32_t object = 0; object < metadata.drm_object_count; ++object) {
        const floral_gralloc_drm_object_v1_t& source = metadata.drm_objects[object];
        if (source.fd_index >= static_cast<uint32_t>(handle->numFds) || source.size == 0 ||
            (source.flags & ~kKnownObjectFlags) != 0) {
            return SetError(error, "gralloc returned an invalid DRM PRIME object");
        }
        const int sourceFd = handle->data[source.fd_index];
        if (sourceFd < 0) {
            return SetError(error, "gralloc referenced an invalid native handle fd");
        }
        const int duplicatedFd = fcntl(sourceFd, F_DUPFD_CLOEXEC, 0);
        if (duplicatedFd < 0) {
            return SetError(
                    error, std::string("duplicating a DMA-BUF fd failed: ") + std::strerror(errno));
        }
        exported.objects[object].fd.reset(duplicatedFd);
        exported.objects[object].flags = source.flags;
        exported.objects[object].size = source.size;
        exported.objects[object].modifier = source.modifier;
    }
    for (uint32_t plane = 0; plane < metadata.drm_plane_count; ++plane) {
        const floral_gralloc_drm_plane_v1_t& source = metadata.drm_planes[plane];
        if (source.object_index >= metadata.drm_object_count || source.pitch == 0 ||
            source.offset >= metadata.drm_objects[source.object_index].size) {
            return SetError(error, "gralloc returned an invalid DRM PRIME plane");
        }
        exported.planes[plane].object_index = source.object_index;
        exported.planes[plane].offset = source.offset;
        exported.planes[plane].pitch = source.pitch;
    }

    *outDmaBuf = std::move(exported);
    return true;
}

}  // namespace floral::stream::codec
