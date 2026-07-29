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

#include "floral/stream/codec/EglFrameCopier.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <poll.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace floral::stream::codec {
namespace {

constexpr char kVertexShader[] = R"(
attribute vec2 position;
attribute vec2 texture_coordinate;
varying vec2 texture_coordinate_out;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    texture_coordinate_out = texture_coordinate;
}
)";

constexpr char kFragmentShader[] = R"(
#extension GL_OES_EGL_image_external : require
precision mediump float;
uniform samplerExternalOES source_texture;
varying vec2 texture_coordinate_out;

void main() {
    gl_FragColor = texture2D(source_texture, texture_coordinate_out);
}
)";

constexpr GLfloat kPositions[] = {
        -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f,
};

// Android hardware buffers use a top-left row origin. These coordinates both
// correct that origin and apply the inverse of the receiver display rotation.
// Rotation is therefore fused into the existing encoder-surface draw.
constexpr GLfloat kTextureCoordinates0[] = {
        0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
};
constexpr GLfloat kTextureCoordinates90[] = {
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
};
constexpr GLfloat kTextureCoordinates180[] = {
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
};
constexpr GLfloat kTextureCoordinates270[] = {
        1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
};

const GLfloat* TextureCoordinatesForDisplayRotation(DisplayRotation rotation) {
    switch (rotation) {
        case DisplayRotation::k0:
            return kTextureCoordinates0;
        case DisplayRotation::k90:
            return kTextureCoordinates90;
        case DisplayRotation::k180:
            return kTextureCoordinates180;
        case DisplayRotation::k270:
            return kTextureCoordinates270;
    }
    return nullptr;
}

bool SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool SetEglError(std::string* error, const char* operation) {
    std::ostringstream message;
    message << operation << " failed with EGL error 0x" << std::hex << eglGetError();
    return SetError(error, message.str());
}

bool SetGlError(std::string* error, const char* operation, GLenum glError = GL_NO_ERROR) {
    if (glError == GL_NO_ERROR) {
        glError = glGetError();
    }
    std::ostringstream message;
    message << operation << " failed with GL error 0x" << std::hex << glError;
    return SetError(error, message.str());
}

bool HasExtension(const char* extensions, const char* expected) {
    if (extensions == nullptr || expected == nullptr || std::strchr(expected, ' ') != nullptr) {
        return false;
    }
    const std::string haystack = std::string(" ") + extensions + " ";
    const std::string needle = std::string(" ") + expected + " ";
    return haystack.find(needle) != std::string::npos;
}

GLuint CompileShader(GLenum type, const char* source, std::string* error) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0) {
        SetGlError(error, "glCreateShader");
        return 0;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<size_t>(std::max(logLength, 1)), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    glDeleteShader(shader);
    SetError(error, "shader compilation failed: " + log);
    return 0;
}

GLuint CreateProgram(std::string* error) {
    const GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, kVertexShader, error);
    if (vertexShader == 0) {
        return 0;
    }
    const GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader, error);
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        return 0;
    }

    const GLuint program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(fragmentShader);
        glDeleteShader(vertexShader);
        SetGlError(error, "glCreateProgram");
        return 0;
    }
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glDeleteShader(fragmentShader);
    glDeleteShader(vertexShader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) {
        return program;
    }

    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<size_t>(std::max(logLength, 1)), '\0');
    glGetProgramInfoLog(program, logLength, nullptr, log.data());
    glDeleteProgram(program);
    SetError(error, "shader program link failed: " + log);
    return 0;
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

}  // namespace

struct EglFrameCopier::Impl {
    struct ImportedBuffer {
        AHardwareBuffer* hardware_buffer = nullptr;
        EGLImageKHR image = EGL_NO_IMAGE_KHR;
        GLuint texture = 0;
    };

    Impl(ANativeWindow* outputWindow, VideoGeometry videoGeometry)
        : output_window(outputWindow), geometry(videoGeometry) {
        ANativeWindow_acquire(output_window);
    }

    Impl(AHardwareBuffer* outputBuffer, VideoGeometry videoGeometry)
        : output_buffer(outputBuffer), geometry(videoGeometry) {
        AHardwareBuffer_acquire(output_buffer);
    }

    ~Impl() {
        std::lock_guard lock(mutex);
        if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE) {
            eglMakeCurrent(display, surface, surface, context);
            glFinish();
            for (auto& [id, buffer] : buffers) {
                (void)id;
                ReleaseImportedBuffer(&buffer);
            }
            buffers.clear();
            if (output_framebuffer != 0) {
                glDeleteFramebuffers(1, &output_framebuffer);
            }
            if (output_renderbuffer != 0) {
                glDeleteRenderbuffers(1, &output_renderbuffer);
            }
            if (output_image != EGL_NO_IMAGE_KHR) {
                eglDestroyImageKHR(display, output_image);
            }
            if (program != 0) {
                glDeleteProgram(program);
            }
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
        }
        if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }
        if (display != EGL_NO_DISPLAY) {
            eglTerminate(display);
        }
        if (output_window != nullptr) {
            ANativeWindow_release(output_window);
        }
        if (output_buffer != nullptr) {
            AHardwareBuffer_release(output_buffer);
        }
    }

    bool Initialize(std::string* error) {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display == EGL_NO_DISPLAY || eglInitialize(display, nullptr, nullptr) != EGL_TRUE) {
            return SetEglError(error, "eglInitialize");
        }
        if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
            return SetEglError(error, "eglBindAPI");
        }

        const char* eglExtensions = eglQueryString(display, EGL_EXTENSIONS);
        if (!HasExtension(eglExtensions, "EGL_ANDROID_get_native_client_buffer") ||
            !HasExtension(eglExtensions, "EGL_ANDROID_image_native_buffer") ||
            !HasExtension(eglExtensions, "EGL_KHR_image_base") ||
            (output_window != nullptr &&
             !HasExtension(eglExtensions, "EGL_ANDROID_presentation_time"))) {
            return SetError(error, "EGL implementation lacks required Android buffer extensions");
        }
        native_fences = HasExtension(eglExtensions, "EGL_ANDROID_native_fence_sync") &&
                        HasExtension(eglExtensions, "EGL_KHR_wait_sync");

        const EGLint windowConfigAttributes[] = {
                EGL_RENDERABLE_TYPE,
                EGL_OPENGL_ES2_BIT,
                EGL_SURFACE_TYPE,
                EGL_WINDOW_BIT,
                EGL_RED_SIZE,
                8,
                EGL_GREEN_SIZE,
                8,
                EGL_BLUE_SIZE,
                8,
                EGL_ALPHA_SIZE,
                8,
                EGL_RECORDABLE_ANDROID,
                EGL_TRUE,
                EGL_NONE,
        };
        const EGLint bufferConfigAttributes[] = {
                EGL_RENDERABLE_TYPE,
                EGL_OPENGL_ES2_BIT,
                EGL_SURFACE_TYPE,
                EGL_PBUFFER_BIT,
                EGL_RED_SIZE,
                8,
                EGL_GREEN_SIZE,
                8,
                EGL_BLUE_SIZE,
                8,
                EGL_ALPHA_SIZE,
                8,
                EGL_NONE,
        };
        const EGLint* configAttributes =
                output_window != nullptr ? windowConfigAttributes : bufferConfigAttributes;
        EGLConfig config = nullptr;
        EGLint configCount = 0;
        if (eglChooseConfig(display, configAttributes, &config, 1, &configCount) != EGL_TRUE ||
            configCount != 1) {
            return SetEglError(error, "eglChooseConfig");
        }

        if (output_window != nullptr) {
            EGLint nativeFormat = 0;
            if (eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &nativeFormat) !=
                        EGL_TRUE ||
                ANativeWindow_setBuffersGeometry(output_window, 0, 0, nativeFormat) != 0) {
                return SetEglError(error, "ANativeWindow_setBuffersGeometry");
            }
        }

        const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
        if (context == EGL_NO_CONTEXT) {
            return SetEglError(error, "eglCreateContext");
        }
        if (output_window != nullptr) {
            surface = eglCreateWindowSurface(display, config, output_window, nullptr);
        } else {
            const EGLint pbufferAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
            surface = eglCreatePbufferSurface(display, config, pbufferAttributes);
        }
        if (surface == EGL_NO_SURFACE) {
            return SetEglError(error, output_window != nullptr ? "eglCreateWindowSurface"
                                                               : "eglCreatePbufferSurface");
        }
        if (!MakeCurrent(error)) {
            return false;
        }

        const char* glExtensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (!HasExtension(glExtensions, "GL_OES_EGL_image_external") ||
            (output_buffer != nullptr && !HasExtension(glExtensions, "GL_OES_EGL_image"))) {
            return SetError(error, "OpenGL ES implementation lacks required EGL image support");
        }

        if (output_buffer != nullptr && !InitializeHardwareBufferTarget(error)) {
            return false;
        }

        program = CreateProgram(error);
        if (program == 0) {
            return false;
        }
        position_location = glGetAttribLocation(program, "position");
        texture_coordinate_location = glGetAttribLocation(program, "texture_coordinate");
        source_texture_location = glGetUniformLocation(program, "source_texture");
        if (position_location < 0 || texture_coordinate_location < 0 ||
            source_texture_location < 0) {
            return SetError(error, "shader program is missing a required attribute or uniform");
        }
        return true;
    }

    bool InitializeHardwareBufferTarget(std::string* error) {
        EGLClientBuffer clientBuffer = eglGetNativeClientBufferANDROID(output_buffer);
        if (clientBuffer == nullptr) {
            return SetEglError(error, "eglGetNativeClientBufferANDROID(output)");
        }
        const EGLint imageAttributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        output_image = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                         clientBuffer, imageAttributes);
        if (output_image == EGL_NO_IMAGE_KHR) {
            return SetEglError(error, "eglCreateImageKHR(output)");
        }

        glGenRenderbuffers(1, &output_renderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, output_renderbuffer);
        glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, output_image);
        GLenum glError = glGetError();
        if (output_renderbuffer == 0 || glError != GL_NO_ERROR) {
            return SetGlError(error, "AHardwareBuffer renderbuffer import", glError);
        }

        glGenFramebuffers(1, &output_framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, output_framebuffer);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                  output_renderbuffer);
        const GLenum framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            return SetGlError(error, "AHardwareBuffer framebuffer setup", glError);
        }
        if (output_framebuffer == 0 || framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
            std::ostringstream message;
            message << "AHardwareBuffer framebuffer is incomplete: 0x" << std::hex
                    << framebufferStatus;
            return SetError(error, message.str());
        }
        return true;
    }

    bool MakeCurrent(std::string* error) const {
        if (eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
            return SetEglError(error, "eglMakeCurrent");
        }
        return true;
    }

    bool WaitAcquireFence(android::base::unique_fd acquireFence, std::string* error) const {
        if (!acquireFence.ok()) {
            return true;
        }
        if (!native_fences) {
            return WaitForFenceFd(acquireFence.get(), error);
        }

        const EGLint attributes[] = {
                EGL_SYNC_NATIVE_FENCE_FD_ANDROID,
                acquireFence.release(),
                EGL_NONE,
        };
        EGLSyncKHR sync = eglCreateSyncKHR(display, EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
        if (sync == EGL_NO_SYNC_KHR) {
            return SetEglError(error, "eglCreateSyncKHR(acquire fence)");
        }
        eglWaitSyncKHR(display, sync, 0);
        const EGLint waitError = eglGetError();
        eglDestroySyncKHR(display, sync);
        if (waitError != EGL_SUCCESS) {
            std::ostringstream message;
            message << "eglWaitSyncKHR failed with EGL error 0x" << std::hex << waitError;
            return SetError(error, message.str());
        }
        return true;
    }

    android::base::unique_fd CreateReleaseFence(bool* completedSynchronously) const {
        if (!native_fences) {
            glFinish();
            *completedSynchronously = true;
            return {};
        }

        EGLSyncKHR sync = eglCreateSyncKHR(display, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
        if (sync == EGL_NO_SYNC_KHR) {
            glFinish();
            *completedSynchronously = true;
            return {};
        }
        glFlush();
        android::base::unique_fd releaseFence(eglDupNativeFenceFDANDROID(display, sync));
        eglDestroySyncKHR(display, sync);
        if (!releaseFence.ok()) {
            glFinish();
            *completedSynchronously = true;
        }
        return releaseFence;
    }

    void ReleaseImportedBuffer(ImportedBuffer* buffer) const {
        if (buffer->texture != 0) {
            glDeleteTextures(1, &buffer->texture);
        }
        if (buffer->image != EGL_NO_IMAGE_KHR) {
            eglDestroyImageKHR(display, buffer->image);
        }
        if (buffer->hardware_buffer != nullptr) {
            AHardwareBuffer_release(buffer->hardware_buffer);
        }
        *buffer = {};
    }

    ANativeWindow* output_window = nullptr;
    AHardwareBuffer* output_buffer = nullptr;
    const VideoGeometry geometry;

    mutable std::mutex mutex;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLImageKHR output_image = EGL_NO_IMAGE_KHR;
    GLuint output_renderbuffer = 0;
    GLuint output_framebuffer = 0;
    GLuint program = 0;
    GLint position_location = -1;
    GLint texture_coordinate_location = -1;
    GLint source_texture_location = -1;
    bool native_fences = false;
    std::unordered_map<uint64_t, ImportedBuffer> buffers;
};

EglFrameCopier::EglFrameCopier(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<EglFrameCopier> EglFrameCopier::Create(ANativeWindow* outputWindow,
                                                       const VideoGeometry& geometry,
                                                       std::string* error) {
    if (outputWindow == nullptr || !HasMatchingDisplayAspect(geometry)) {
        SetError(error, "EGL frame copier requires a valid window and matching video geometry");
        return nullptr;
    }
    auto impl = std::make_unique<Impl>(outputWindow, geometry);
    if (!impl->Initialize(error)) {
        return nullptr;
    }
    return std::unique_ptr<EglFrameCopier>(new EglFrameCopier(std::move(impl)));
}

std::unique_ptr<EglFrameCopier> EglFrameCopier::Create(AHardwareBuffer* outputBuffer,
                                                       const VideoGeometry& geometry,
                                                       std::string* error) {
    if (outputBuffer == nullptr || !HasMatchingDisplayAspect(geometry)) {
        SetError(error,
                 "EGL frame copier requires a valid output buffer and matching video geometry");
        return nullptr;
    }
    AHardwareBuffer_Desc description{};
    AHardwareBuffer_describe(outputBuffer, &description);
    if (description.width != geometry.coded_width || description.height != geometry.coded_height ||
        description.layers != 1 || description.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM ||
        (description.usage & AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER) == 0) {
        SetError(error, "EGL output buffer is not a compatible coded-size RGBA render target");
        return nullptr;
    }
    auto impl = std::make_unique<Impl>(outputBuffer, geometry);
    if (!impl->Initialize(error)) {
        return nullptr;
    }
    return std::unique_ptr<EglFrameCopier>(new EglFrameCopier(std::move(impl)));
}

EglFrameCopier::~EglFrameCopier() = default;

bool EglFrameCopier::RegisterBuffer(AHardwareBuffer* buffer, uint64_t* outBufferId,
                                    std::string* error) {
    if (buffer == nullptr || outBufferId == nullptr) {
        return SetError(error, "buffer registration requires a buffer and output id");
    }

    uint64_t bufferId = 0;
    const int idStatus = AHardwareBuffer_getId(buffer, &bufferId);
    if (idStatus != 0) {
        return SetError(error,
                        "AHardwareBuffer_getId failed with status " + std::to_string(idStatus));
    }

    AHardwareBuffer_Desc description{};
    AHardwareBuffer_describe(buffer, &description);
    if (description.width != impl_->geometry.logical_width ||
        description.height != impl_->geometry.logical_height || description.layers != 1) {
        return SetError(error, "hardware buffer dimensions or layer count do not match session");
    }

    std::lock_guard lock(impl_->mutex);
    if (!impl_->MakeCurrent(error)) {
        return false;
    }
    if (impl_->buffers.find(bufferId) != impl_->buffers.end()) {
        *outBufferId = bufferId;
        return true;
    }

    Impl::ImportedBuffer imported;
    AHardwareBuffer_acquire(buffer);
    imported.hardware_buffer = buffer;

    EGLClientBuffer clientBuffer = eglGetNativeClientBufferANDROID(buffer);
    if (clientBuffer == nullptr) {
        impl_->ReleaseImportedBuffer(&imported);
        return SetEglError(error, "eglGetNativeClientBufferANDROID");
    }
    const EGLint imageAttributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    imported.image = eglCreateImageKHR(impl_->display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                       clientBuffer, imageAttributes);
    if (imported.image == EGL_NO_IMAGE_KHR) {
        impl_->ReleaseImportedBuffer(&imported);
        return SetEglError(error, "eglCreateImageKHR");
    }

    glGenTextures(1, &imported.texture);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, imported.texture);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, imported.image);
    const GLenum importError = glGetError();
    if (imported.texture == 0 || importError != GL_NO_ERROR) {
        impl_->ReleaseImportedBuffer(&imported);
        return SetGlError(error, "AHardwareBuffer texture import", importError);
    }

    impl_->buffers.emplace(bufferId, imported);
    *outBufferId = bufferId;
    return true;
}

void EglFrameCopier::UnregisterBuffer(uint64_t bufferId) {
    std::lock_guard lock(impl_->mutex);
    const auto iterator = impl_->buffers.find(bufferId);
    if (iterator == impl_->buffers.end() || !impl_->MakeCurrent(nullptr)) {
        return;
    }
    impl_->ReleaseImportedBuffer(&iterator->second);
    impl_->buffers.erase(iterator);
}

FrameCopyResult EglFrameCopier::CopyFrame(uint64_t bufferId, android::base::unique_fd acquireFence,
                                          int64_t presentationTimeNanos, std::string* error) {
    FrameCopyResult result;
    if (presentationTimeNanos < 0) {
        SetError(error, "presentation timestamp must not be negative");
        return result;
    }

    std::lock_guard lock(impl_->mutex);
    const auto iterator = impl_->buffers.find(bufferId);
    if (iterator == impl_->buffers.end()) {
        SetError(error, "frame references an unregistered hardware buffer");
        return result;
    }
    if (!impl_->MakeCurrent(error) || !impl_->WaitAcquireFence(std::move(acquireFence), error)) {
        return result;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, impl_->output_framebuffer);
    glViewport(0, 0, static_cast<GLsizei>(impl_->geometry.coded_width),
               static_cast<GLsizei>(impl_->geometry.coded_height));
    glDisable(GL_BLEND);
    glUseProgram(impl_->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, iterator->second.texture);
    glUniform1i(impl_->source_texture_location, 0);

    glEnableVertexAttribArray(static_cast<GLuint>(impl_->position_location));
    glEnableVertexAttribArray(static_cast<GLuint>(impl_->texture_coordinate_location));
    glVertexAttribPointer(static_cast<GLuint>(impl_->position_location), 2, GL_FLOAT, GL_FALSE, 0,
                          kPositions);
    const GLfloat* textureCoordinates =
            TextureCoordinatesForDisplayRotation(impl_->geometry.display_rotation);
    glVertexAttribPointer(static_cast<GLuint>(impl_->texture_coordinate_location), 2, GL_FLOAT,
                          GL_FALSE, 0, textureCoordinates);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(static_cast<GLuint>(impl_->texture_coordinate_location));
    glDisableVertexAttribArray(static_cast<GLuint>(impl_->position_location));
    const GLenum blitError = glGetError();
    if (blitError != GL_NO_ERROR) {
        SetGlError(error, "hardware buffer blit", blitError);
        return result;
    }

    if (impl_->output_buffer != nullptr) {
        glFinish();
        const GLenum finishError = glGetError();
        if (finishError != GL_NO_ERROR) {
            SetGlError(error, "AHardwareBuffer render completion", finishError);
            return result;
        }
        result.completed_synchronously = true;
        result.success = true;
        return result;
    }

    result.release_fence = impl_->CreateReleaseFence(&result.completed_synchronously);
    if (eglPresentationTimeANDROID(impl_->display, impl_->surface, presentationTimeNanos) !=
        EGL_TRUE) {
        SetEglError(error, "eglPresentationTimeANDROID");
        glFinish();
        return {};
    }
    if (eglSwapBuffers(impl_->display, impl_->surface) != EGL_TRUE) {
        SetEglError(error, "eglSwapBuffers");
        glFinish();
        return {};
    }
    result.success = true;
    return result;
}

bool EglFrameCopier::uses_native_fences() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->output_window != nullptr && impl_->native_fences;
}

size_t EglFrameCopier::registered_buffer_count() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->buffers.size();
}

}  // namespace floral::stream::codec
