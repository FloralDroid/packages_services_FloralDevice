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

#include "FfmpegVaapiEncoderBackend.h"

#include "GrallocDmaBufExporter.h"
#include "floral/stream/codec/EglFrameCopier.h"

#include <drm/drm_fourcc.h>
#include <poll.h>
#include <va/va.h>
#include <va/va_drmcommon.h>
#include <va/va_vpp.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace floral::stream::codec {
namespace {

constexpr char kVaapiAvcCodecName[] = "h264_vaapi";
constexpr int64_t kCodecOperationTimeoutUs = 5'000'000;
constexpr int kVaapiFramePoolSize = 8;

bool SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

std::string AvError(int code) {
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(code, message, sizeof(message)) < 0) {
        return "FFmpeg error " + std::to_string(code);
    }
    return message;
}

bool SetAvError(std::string* error, const char* operation, int code) {
    return SetError(error, std::string(operation) + " failed: " + AvError(code));
}

bool SetVaError(std::string* error, const char* operation, VAStatus status) {
    return SetError(error, std::string(operation) + " failed: " + vaErrorStr(status));
}

bool WaitForFenceFd(int fenceFd, std::string* error) {
    pollfd descriptor{};
    descriptor.fd = fenceFd;
    descriptor.events = POLLIN;
    int result = 0;
    do {
        result = poll(&descriptor, 1, 5'000);
    } while (result < 0 && errno == EINTR);

    if (result > 0) {
        return true;
    }
    if (result == 0) {
        return SetError(error, "acquire fence did not signal within 5000 ms");
    }
    return SetError(error, std::string("polling acquire fence failed: ") + std::strerror(errno));
}

uint32_t VaFourccForDrmFormat(uint32_t drmFormat) {
    switch (drmFormat) {
        case DRM_FORMAT_ARGB8888:
            return VA_FOURCC_BGRA;
        case DRM_FORMAT_XRGB8888:
            return VA_FOURCC_BGRX;
        case DRM_FORMAT_ABGR8888:
            return VA_FOURCC_RGBA;
        case DRM_FORMAT_XBGR8888:
            return VA_FOURCC_RGBX;
        default:
            return 0;
    }
}

uint32_t VaRotationForDisplayRotation(DisplayRotation displayRotation) {
    switch (displayRotation) {
        case DisplayRotation::k0:
            return VA_ROTATION_NONE;
        case DisplayRotation::k90:
            return VA_ROTATION_270;
        case DisplayRotation::k180:
            return VA_ROTATION_180;
        case DisplayRotation::k270:
            return VA_ROTATION_90;
    }
    return VA_ROTATION_NONE;
}

}  // namespace

struct FfmpegVaapiEncoderBackend::Impl {
    struct RegisteredBuffer {
        AHardwareBuffer* hardware_buffer = nullptr;
        VASurfaceID surface = VA_INVALID_SURFACE;
    };

    Impl(EncoderConfig encoderConfig, VideoGeometry videoGeometry, std::string devicePath)
        : config(std::move(encoderConfig)),
          geometry(videoGeometry),
          va_device_path(std::move(devicePath)) {}

    ~Impl() {
        if (codec_context != nullptr) {
            avcodec_free_context(&codec_context);
        }
        if (packet != nullptr) {
            av_packet_free(&packet);
        }
        av_buffer_unref(&hw_frames_context);

        while (!buffers.empty()) {
            UnregisterBuffer(buffers.begin()->first);
        }
        rotation_copier.reset();
        if (rotation_surface != VA_INVALID_SURFACE && va_display != nullptr) {
            vaDestroySurfaces(va_display, &rotation_surface, 1);
        }
        if (rotation_buffer != nullptr) {
            AHardwareBuffer_release(rotation_buffer);
        }
        if (vpp_context != VA_INVALID_ID) {
            vaDestroyContext(va_display, vpp_context);
        }
        if (vpp_config != VA_INVALID_ID) {
            vaDestroyConfig(va_display, vpp_config);
        }
        av_buffer_unref(&hw_device_context);
    }

    bool Initialize(std::string* error) {
        if (config.mime != "video/avc" || config.width == 0 || config.height == 0 ||
            config.bitrate_bps == 0 || config.frame_rate == 0 || va_device_path.empty()) {
            return SetError(error, "VA-API encoder configuration is invalid");
        }

        exporter = GrallocDmaBufExporter::Create(error);
        if (exporter == nullptr) {
            return false;
        }

        int status = av_hwdevice_ctx_create(&hw_device_context, AV_HWDEVICE_TYPE_VAAPI,
                                            va_device_path.c_str(), nullptr, 0);
        if (status < 0) {
            return SetAvError(error, "av_hwdevice_ctx_create", status);
        }
        auto* deviceContext = reinterpret_cast<AVHWDeviceContext*>(hw_device_context->data);
        auto* vaapiDevice = reinterpret_cast<AVVAAPIDeviceContext*>(deviceContext->hwctx);
        va_display = vaapiDevice->display;

        if (!CreateHardwareFrames(error) || !CreateVideoProcessor(error) ||
            !CreateRotationFallback(error) || !CreateCodec(error)) {
            return false;
        }
        packet = av_packet_alloc();
        if (packet == nullptr) {
            return SetError(error, "av_packet_alloc failed");
        }
        return true;
    }

    bool CreateHardwareFrames(std::string* error) {
        hw_frames_context = av_hwframe_ctx_alloc(hw_device_context);
        if (hw_frames_context == nullptr) {
            return SetError(error, "av_hwframe_ctx_alloc failed");
        }
        auto* frames = reinterpret_cast<AVHWFramesContext*>(hw_frames_context->data);
        frames->format = AV_PIX_FMT_VAAPI;
        frames->sw_format = AV_PIX_FMT_NV12;
        frames->width = static_cast<int>(config.width);
        frames->height = static_cast<int>(config.height);
        frames->initial_pool_size = kVaapiFramePoolSize;
        const int status = av_hwframe_ctx_init(hw_frames_context);
        return status >= 0 || SetAvError(error, "av_hwframe_ctx_init", status);
    }

    bool CreateVideoProcessor(std::string* error) {
        VAConfigAttrib attribute{};
        attribute.type = VAConfigAttribRTFormat;
        VAStatus status = vaGetConfigAttributes(va_display, VAProfileNone, VAEntrypointVideoProc,
                                                &attribute, 1);
        if (status != VA_STATUS_SUCCESS) {
            return SetVaError(error, "vaGetConfigAttributes", status);
        }
        if (attribute.value == VA_ATTRIB_NOT_SUPPORTED ||
            (attribute.value & VA_RT_FORMAT_YUV420) == 0) {
            return SetError(error, "VA-API video processing does not support YUV420 output");
        }

        attribute.value = VA_RT_FORMAT_YUV420;
        status = vaCreateConfig(va_display, VAProfileNone, VAEntrypointVideoProc, &attribute, 1,
                                &vpp_config);
        if (status != VA_STATUS_SUCCESS) {
            return SetVaError(error, "vaCreateConfig", status);
        }
        status = vaCreateContext(va_display, vpp_config, static_cast<int>(config.width),
                                 static_cast<int>(config.height), VA_PROGRESSIVE, nullptr, 0,
                                 &vpp_context);
        if (status != VA_STATUS_SUCCESS) {
            return SetVaError(error, "vaCreateContext", status);
        }

        VAProcPipelineCaps capabilities{};
        status = vaQueryVideoProcPipelineCaps(va_display, vpp_context, nullptr, 0, &capabilities);
        if (status != VA_STATUS_SUCCESS) {
            return SetVaError(error, "vaQueryVideoProcPipelineCaps", status);
        }
        const uint32_t rotation = VaRotationForDisplayRotation(geometry.display_rotation);
        use_gles_rotation = rotation != VA_ROTATION_NONE &&
                            (capabilities.rotation_flags & (1U << rotation)) == 0;
        return true;
    }

    bool CreateRotationFallback(std::string* error) {
        if (!use_gles_rotation) {
            return true;
        }

        AHardwareBuffer_Desc description{};
        description.width = geometry.coded_width;
        description.height = geometry.coded_height;
        description.layers = 1;
        description.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
        description.usage =
                AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
        const int allocationStatus = AHardwareBuffer_allocate(&description, &rotation_buffer);
        if (allocationStatus != 0 || rotation_buffer == nullptr) {
            return SetError(error, "allocating the GLES rotation buffer failed: " +
                                           std::to_string(allocationStatus));
        }

        rotation_copier = EglFrameCopier::Create(rotation_buffer, geometry, error);
        if (rotation_copier == nullptr) {
            return false;
        }

        ExportedDmaBuf dmaBuf;
        if (!exporter->Export(rotation_buffer, &dmaBuf, error)) {
            return false;
        }
        if (dmaBuf.width != geometry.coded_width || dmaBuf.height != geometry.coded_height) {
            return SetError(error, "GLES rotation buffer dimensions do not match coded geometry");
        }
        return CreateImportedSurface(dmaBuf, &rotation_surface, error);
    }

    bool CreateCodec(std::string* error) {
        const AVCodec* encoder = avcodec_find_encoder_by_name(kVaapiAvcCodecName);
        if (encoder == nullptr) {
            return SetError(error, "FFmpeg h264_vaapi encoder is unavailable");
        }

        AVCodecContext* candidate = avcodec_alloc_context3(encoder);
        if (candidate == nullptr) {
            return SetError(error, "avcodec_alloc_context3 failed");
        }
        candidate->width = static_cast<int>(config.width);
        candidate->height = static_cast<int>(config.height);
        candidate->time_base = AVRational{1, static_cast<int>(config.frame_rate)};
        candidate->framerate = AVRational{static_cast<int>(config.frame_rate), 1};
        candidate->sample_aspect_ratio = AVRational{1, 1};
        candidate->pix_fmt = AV_PIX_FMT_VAAPI;
        candidate->bit_rate = config.bitrate_bps;
        candidate->rc_max_rate = config.bitrate_bps;
        candidate->rc_buffer_size = std::max<uint32_t>(config.bitrate_bps / 4, 1);
        candidate->gop_size = static_cast<int>(std::max<uint64_t>(
                1, static_cast<uint64_t>(config.frame_rate) * config.i_frame_interval_seconds));
        candidate->max_b_frames = 0;
        candidate->flags |= AV_CODEC_FLAG_LOW_DELAY;
        candidate->hw_frames_ctx = av_buffer_ref(hw_frames_context);
        if (candidate->hw_frames_ctx == nullptr) {
            avcodec_free_context(&candidate);
            return SetError(error, "av_buffer_ref failed for VA-API frame context");
        }

        int status = av_opt_set(candidate->priv_data, "rc_mode", "CBR", 0);
        if (status >= 0) {
            status = av_opt_set_int(candidate->priv_data, "async_depth", 1, 0);
        }
        if (status < 0) {
            avcodec_free_context(&candidate);
            return SetAvError(error, "configuring h264_vaapi", status);
        }
        status = avcodec_open2(candidate, encoder, nullptr);
        if (status < 0) {
            avcodec_free_context(&candidate);
            return SetAvError(error, "avcodec_open2", status);
        }

        codec_context = candidate;
        codec_name = encoder->name;
        format_change_pending = true;
        drain_submitted = false;
        end_of_stream = false;
        return true;
    }

    bool CreateImportedSurface(const ExportedDmaBuf& dmaBuf, VASurfaceID* outSurface,
                               std::string* error) {
        const uint32_t vaFourcc = VaFourccForDrmFormat(dmaBuf.drm_format);
        if (vaFourcc == 0) {
            std::ostringstream message;
            message << "unsupported DRM RGB format 0x" << std::hex << dmaBuf.drm_format;
            return SetError(error, message.str());
        }

        VADRMPRIMESurfaceDescriptor descriptor{};
        descriptor.fourcc = vaFourcc;
        descriptor.width = dmaBuf.width;
        descriptor.height = dmaBuf.height;
        descriptor.num_objects = dmaBuf.object_count;
        for (uint32_t object = 0; object < dmaBuf.object_count; ++object) {
            if (!dmaBuf.objects[object].fd.ok() ||
                dmaBuf.objects[object].size > std::numeric_limits<uint32_t>::max()) {
                return SetError(error, "DMA-BUF object is invalid or exceeds VA-API limits");
            }
            descriptor.objects[object].fd = dmaBuf.objects[object].fd.get();
            descriptor.objects[object].size = static_cast<uint32_t>(dmaBuf.objects[object].size);
            descriptor.objects[object].drm_format_modifier = dmaBuf.objects[object].modifier;
        }
        descriptor.num_layers = 1;
        descriptor.layers[0].drm_format = dmaBuf.drm_format;
        descriptor.layers[0].num_planes = dmaBuf.plane_count;
        for (uint32_t plane = 0; plane < dmaBuf.plane_count; ++plane) {
            if (dmaBuf.planes[plane].offset > std::numeric_limits<uint32_t>::max() ||
                dmaBuf.planes[plane].pitch > std::numeric_limits<uint32_t>::max()) {
                return SetError(error, "DMA-BUF plane exceeds VA-API offset or pitch limits");
            }
            descriptor.layers[0].object_index[plane] = dmaBuf.planes[plane].object_index;
            descriptor.layers[0].offset[plane] = static_cast<uint32_t>(dmaBuf.planes[plane].offset);
            descriptor.layers[0].pitch[plane] = static_cast<uint32_t>(dmaBuf.planes[plane].pitch);
        }

        VASurfaceAttrib attributes[2]{};
        attributes[0].type = VASurfaceAttribMemoryType;
        attributes[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attributes[0].value.type = VAGenericValueTypeInteger;
        attributes[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2;
        attributes[1].type = VASurfaceAttribExternalBufferDescriptor;
        attributes[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attributes[1].value.type = VAGenericValueTypePointer;
        attributes[1].value.value.p = &descriptor;

        const VAStatus status = vaCreateSurfaces(va_display, VA_RT_FORMAT_RGB32, dmaBuf.width,
                                                 dmaBuf.height, outSurface, 1, attributes, 2);
        return status == VA_STATUS_SUCCESS || SetVaError(error, "vaCreateSurfaces", status);
    }

    bool RegisterBuffer(AHardwareBuffer* buffer, uint64_t* outBufferId, std::string* error) {
        if (buffer == nullptr || outBufferId == nullptr) {
            return SetError(error, "buffer registration requires a buffer and output id");
        }
        uint64_t bufferId = 0;
        const int idStatus = AHardwareBuffer_getId(buffer, &bufferId);
        if (idStatus != 0) {
            return SetError(error,
                            "AHardwareBuffer_getId failed with status " + std::to_string(idStatus));
        }
        if (buffers.find(bufferId) != buffers.end()) {
            *outBufferId = bufferId;
            return true;
        }

        AHardwareBuffer_Desc description{};
        AHardwareBuffer_describe(buffer, &description);
        if (description.width != geometry.logical_width ||
            description.height != geometry.logical_height || description.layers != 1) {
            return SetError(error, "hardware buffer dimensions do not match video geometry");
        }

        RegisteredBuffer registered;
        if (use_gles_rotation) {
            uint64_t importedBufferId = 0;
            if (!rotation_copier->RegisterBuffer(buffer, &importedBufferId, error)) {
                return false;
            }
            if (importedBufferId != bufferId) {
                rotation_copier->UnregisterBuffer(importedBufferId);
                return SetError(error, "EGL and VA-API buffer identifiers do not match");
            }
        } else {
            ExportedDmaBuf dmaBuf;
            if (!exporter->Export(buffer, &dmaBuf, error)) {
                return false;
            }
            if (!CreateImportedSurface(dmaBuf, &registered.surface, error)) {
                return false;
            }
        }
        AHardwareBuffer_acquire(buffer);
        registered.hardware_buffer = buffer;
        buffers.emplace(bufferId, registered);
        *outBufferId = bufferId;
        return true;
    }

    void UnregisterBuffer(uint64_t bufferId) {
        const auto buffer = buffers.find(bufferId);
        if (buffer == buffers.end()) {
            return;
        }
        if (rotation_copier != nullptr) {
            rotation_copier->UnregisterBuffer(bufferId);
        }
        ReleaseRegisteredBuffer(&buffer->second);
        buffers.erase(buffer);
    }

    void ReleaseRegisteredBuffer(RegisteredBuffer* buffer) {
        if (buffer->surface != VA_INVALID_SURFACE && va_display != nullptr) {
            vaDestroySurfaces(va_display, &buffer->surface, 1);
            buffer->surface = VA_INVALID_SURFACE;
        }
        if (buffer->hardware_buffer != nullptr) {
            AHardwareBuffer_release(buffer->hardware_buffer);
            buffer->hardware_buffer = nullptr;
        }
    }

    bool ProcessFrame(VASurfaceID source, VASurfaceID destination, std::string* error) {
        const uint32_t sourceWidth =
                use_gles_rotation ? geometry.coded_width : geometry.logical_width;
        const uint32_t sourceHeight =
                use_gles_rotation ? geometry.coded_height : geometry.logical_height;
        VARectangle sourceRegion{0, 0, static_cast<uint16_t>(sourceWidth),
                                 static_cast<uint16_t>(sourceHeight)};
        VARectangle outputRegion{0, 0, static_cast<uint16_t>(geometry.coded_width),
                                 static_cast<uint16_t>(geometry.coded_height)};
        VAProcPipelineParameterBuffer parameters{};
        parameters.surface = source;
        parameters.surface_region = &sourceRegion;
        parameters.output_region = &outputRegion;
        parameters.rotation_state =
                use_gles_rotation ? VA_ROTATION_NONE
                                  : VaRotationForDisplayRotation(geometry.display_rotation);

        VABufferID parameterBuffer = VA_INVALID_ID;
        VAStatus status = vaCreateBuffer(va_display, vpp_context, VAProcPipelineParameterBufferType,
                                         sizeof(parameters), 1, &parameters, &parameterBuffer);
        if (status != VA_STATUS_SUCCESS) {
            return SetVaError(error, "vaCreateBuffer", status);
        }

        status = vaBeginPicture(va_display, vpp_context, destination);
        if (status == VA_STATUS_SUCCESS) {
            status = vaRenderPicture(va_display, vpp_context, &parameterBuffer, 1);
        }
        if (status == VA_STATUS_SUCCESS) {
            status = vaEndPicture(va_display, vpp_context);
        }
        vaDestroyBuffer(va_display, parameterBuffer);
        if (status != VA_STATUS_SUCCESS) {
            return SetVaError(error, "VA-API video processing", status);
        }
        status = vaSyncSurface(va_display, destination);
        return status == VA_STATUS_SUCCESS || SetVaError(error, "vaSyncSurface", status);
    }

    enum class ReceiveStatus {
        kPacket,
        kTryAgain,
        kEndOfStream,
        kError,
    };

    ReceiveStatus ReceivePacket(EncodedPacket* outPacket, int* outError) {
        av_packet_unref(packet);
        const int status = avcodec_receive_packet(codec_context, packet);
        if (status == AVERROR(EAGAIN)) {
            return ReceiveStatus::kTryAgain;
        }
        if (status == AVERROR_EOF) {
            return ReceiveStatus::kEndOfStream;
        }
        if (status < 0) {
            *outError = status;
            return ReceiveStatus::kError;
        }

        EncodedPacket encoded;
        if (packet->size > 0) {
            encoded.data.assign(packet->data, packet->data + packet->size);
        }
        if (packet->pts != AV_NOPTS_VALUE) {
            encoded.presentation_time_us =
                    av_rescale_q(packet->pts, codec_context->time_base, AVRational{1, 1'000'000});
        }
        if ((packet->flags & AV_PKT_FLAG_KEY) != 0) {
            encoded.flags |= kEncodedPacketFlagKeyFrame;
        }
        *outPacket = std::move(encoded);
        return ReceiveStatus::kPacket;
    }

    bool QueuePacketsUntilInputAccepted(const AVFrame* frame, std::string* error) {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::microseconds(kCodecOperationTimeoutUs);
        while (true) {
            const int sendStatus = avcodec_send_frame(codec_context, frame);
            if (sendStatus >= 0) {
                return true;
            }
            if (sendStatus != AVERROR(EAGAIN)) {
                return SetAvError(error, "avcodec_send_frame", sendStatus);
            }

            EncodedPacket encoded;
            int receiveError = 0;
            const ReceiveStatus receiveStatus = ReceivePacket(&encoded, &receiveError);
            if (receiveStatus == ReceiveStatus::kPacket) {
                pending_packets.push_back(std::move(encoded));
                continue;
            }
            if (receiveStatus == ReceiveStatus::kError) {
                return SetAvError(error, "avcodec_receive_packet", receiveError);
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return SetError(error, "FFmpeg encoder did not accept input within 5000 ms");
            }
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
    }

    FrameCopyResult SubmitFrame(uint64_t bufferId, android::base::unique_fd acquireFence,
                                int64_t presentationTimeNanos, std::string* error) {
        FrameCopyResult result;
        if (presentationTimeNanos < 0 || drain_submitted) {
            SetError(error, "frame timestamp is invalid or the encoder is draining");
            return result;
        }
        const auto source = buffers.find(bufferId);
        if (source == buffers.end()) {
            SetError(error, "frame references an unregistered hardware buffer");
            return result;
        }
        VASurfaceID sourceSurface = source->second.surface;
        if (use_gles_rotation) {
            FrameCopyResult rotation = rotation_copier->CopyFrame(bufferId, std::move(acquireFence),
                                                                  presentationTimeNanos, error);
            if (!rotation.success || !rotation.completed_synchronously ||
                rotation.release_fence.ok()) {
                if (rotation.success) {
                    SetError(error, "GLES rotation did not complete synchronously");
                }
                return result;
            }
            sourceSurface = rotation_surface;
        } else if (acquireFence.ok() && !WaitForFenceFd(acquireFence.get(), error)) {
            return result;
        }

        AVFrame* frame = av_frame_alloc();
        if (frame == nullptr) {
            SetError(error, "av_frame_alloc failed");
            return result;
        }
        int status = av_hwframe_get_buffer(hw_frames_context, frame, 0);
        if (status < 0) {
            av_frame_free(&frame);
            SetAvError(error, "av_hwframe_get_buffer", status);
            return result;
        }

        const VASurfaceID destination =
                static_cast<VASurfaceID>(reinterpret_cast<uintptr_t>(frame->data[3]));
        if (!ProcessFrame(sourceSurface, destination, error)) {
            av_frame_free(&frame);
            return result;
        }
        frame->pts = av_rescale_q(presentationTimeNanos, AVRational{1, 1'000'000'000},
                                  codec_context->time_base);
        if (force_key_frame) {
            frame->pict_type = AV_PICTURE_TYPE_I;
            force_key_frame = false;
        }

        const bool accepted = QueuePacketsUntilInputAccepted(frame, error);
        av_frame_free(&frame);
        if (!accepted) {
            return result;
        }
        result.success = true;
        result.completed_synchronously = true;
        return result;
    }

    bool DrainCodecToQueue(std::string* error) {
        if (!QueuePacketsUntilInputAccepted(nullptr, error)) {
            return false;
        }
        drain_submitted = true;

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::microseconds(kCodecOperationTimeoutUs);
        while (true) {
            EncodedPacket encoded;
            int receiveError = 0;
            const ReceiveStatus status = ReceivePacket(&encoded, &receiveError);
            if (status == ReceiveStatus::kPacket) {
                pending_packets.push_back(std::move(encoded));
                continue;
            }
            if (status == ReceiveStatus::kEndOfStream) {
                return true;
            }
            if (status == ReceiveStatus::kError) {
                return SetAvError(error, "avcodec_receive_packet", receiveError);
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return SetError(error, "FFmpeg encoder did not drain within 5000 ms");
            }
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
    }

    bool SetBitrate(uint32_t bitrateBps, std::string* error) {
        if (bitrateBps == 0 ||
            bitrateBps > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            drain_submitted) {
            return SetError(error, "bitrate is invalid or the encoder is draining");
        }
        if (bitrateBps == config.bitrate_bps) {
            return true;
        }
        if (!DrainCodecToQueue(error)) {
            return false;
        }
        avcodec_free_context(&codec_context);
        config.bitrate_bps = bitrateBps;
        if (!CreateCodec(error)) {
            return false;
        }
        force_key_frame = true;
        return true;
    }

    DequeueResult DequeueOutput(int64_t timeoutUs) {
        DequeueResult result;
        if (format_change_pending) {
            format_change_pending = false;
            result.status = DequeueStatus::kFormatChanged;
            return result;
        }
        if (!pending_packets.empty()) {
            result.status = DequeueStatus::kPacket;
            result.packet = std::move(pending_packets.front());
            pending_packets.pop_front();
            return result;
        }
        if (end_of_stream) {
            result.status = DequeueStatus::kEndOfStream;
            return result;
        }

        const bool waitForever = timeoutUs < 0;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::microseconds(std::max<int64_t>(timeoutUs, 0));
        while (true) {
            int receiveError = 0;
            const ReceiveStatus status = ReceivePacket(&result.packet, &receiveError);
            if (status == ReceiveStatus::kPacket) {
                result.status = DequeueStatus::kPacket;
                return result;
            }
            if (status == ReceiveStatus::kEndOfStream) {
                end_of_stream = true;
                result.status = DequeueStatus::kEndOfStream;
                return result;
            }
            if (status == ReceiveStatus::kError) {
                result.status = DequeueStatus::kError;
                result.error_code = receiveError;
                return result;
            }
            if ((!waitForever && std::chrono::steady_clock::now() >= deadline) || timeoutUs == 0) {
                result.status = DequeueStatus::kTryAgain;
                return result;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
    }

    // Immutable video geometry and selected VA device.
    EncoderConfig config;
    const VideoGeometry geometry;
    const std::string va_device_path;
    std::string codec_name;

    // Buffer export, FFmpeg VA device/frame pool, and VA video processor.
    std::unique_ptr<GrallocDmaBufExporter> exporter;
    AVBufferRef* hw_device_context = nullptr;
    AVBufferRef* hw_frames_context = nullptr;
    VADisplay va_display = nullptr;
    VAConfigID vpp_config = VA_INVALID_ID;
    VAContextID vpp_context = VA_INVALID_ID;

    // GPUs without VA VPP rotation use one reusable GLES-rendered source surface.
    bool use_gles_rotation = false;
    AHardwareBuffer* rotation_buffer = nullptr;
    VASurfaceID rotation_surface = VA_INVALID_SURFACE;
    std::unique_ptr<EglFrameCopier> rotation_copier;

    // FFmpeg encoder and pending encoded output.
    AVCodecContext* codec_context = nullptr;
    AVPacket* packet = nullptr;
    std::deque<EncodedPacket> pending_packets;
    bool format_change_pending = false;
    bool force_key_frame = false;
    bool drain_submitted = false;
    bool end_of_stream = false;

    // Registered Android source buffers and direct-path VA imports.
    std::unordered_map<uint64_t, RegisteredBuffer> buffers;
};

std::unique_ptr<FfmpegVaapiEncoderBackend> FfmpegVaapiEncoderBackend::Create(
        const EncoderConfig& config, const VideoGeometry& geometry, std::string vaDevicePath,
        std::string* error) {
    auto impl = std::make_unique<Impl>(config, geometry, std::move(vaDevicePath));
    if (!impl->Initialize(error)) {
        return nullptr;
    }
    return std::unique_ptr<FfmpegVaapiEncoderBackend>(
            new FfmpegVaapiEncoderBackend(std::move(impl)));
}

FfmpegVaapiEncoderBackend::FfmpegVaapiEncoderBackend(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

FfmpegVaapiEncoderBackend::~FfmpegVaapiEncoderBackend() = default;

bool FfmpegVaapiEncoderBackend::RegisterBuffer(AHardwareBuffer* buffer, uint64_t* outBufferId,
                                               std::string* error) {
    return impl_->RegisterBuffer(buffer, outBufferId, error);
}

void FfmpegVaapiEncoderBackend::UnregisterBuffer(uint64_t bufferId) {
    impl_->UnregisterBuffer(bufferId);
}

FrameCopyResult FfmpegVaapiEncoderBackend::SubmitFrame(uint64_t bufferId,
                                                       android::base::unique_fd acquireFence,
                                                       int64_t presentationTimeNanos,
                                                       std::string* error) {
    return impl_->SubmitFrame(bufferId, std::move(acquireFence), presentationTimeNanos, error);
}

bool FfmpegVaapiEncoderBackend::SetBitrate(uint32_t bitrateBps, std::string* error) {
    return impl_->SetBitrate(bitrateBps, error);
}

bool FfmpegVaapiEncoderBackend::RequestKeyFrame(std::string* error) {
    if (impl_->drain_submitted) {
        return SetError(error, "encoder is draining");
    }
    impl_->force_key_frame = true;
    return true;
}

bool FfmpegVaapiEncoderBackend::SignalEndOfInputStream(std::string* error) {
    if (impl_->drain_submitted) {
        return true;
    }
    if (!impl_->QueuePacketsUntilInputAccepted(nullptr, error)) {
        return false;
    }
    impl_->drain_submitted = true;
    return true;
}

DequeueResult FfmpegVaapiEncoderBackend::DequeueOutput(int64_t timeoutUs) {
    return impl_->DequeueOutput(timeoutUs);
}

const EncoderConfig& FfmpegVaapiEncoderBackend::config() const {
    return impl_->config;
}

const std::string& FfmpegVaapiEncoderBackend::codec_name() const {
    return impl_->codec_name;
}

bool FfmpegVaapiEncoderBackend::uses_native_fences() const {
    return false;
}

size_t FfmpegVaapiEncoderBackend::registered_buffer_count() const {
    return impl_->buffers.size();
}

}  // namespace floral::stream::codec
