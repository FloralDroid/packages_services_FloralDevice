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

#include "EglSurfaceRenderer.h"

#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <sstream>

namespace floral::stream::codec::testing {
namespace {

bool SetEglError(std::string* error, const char* operation) {
    if (error != nullptr) {
        std::ostringstream message;
        message << operation << " failed with EGL error 0x" << std::hex << eglGetError();
        *error = message.str();
    }
    return false;
}

}  // namespace

EglSurfaceRenderer::EglSurfaceRenderer(EGLDisplay display, EGLContext context, EGLSurface surface,
                                       uint32_t width, uint32_t height)
    : display_(display), context_(context), surface_(surface), width_(width), height_(height) {}

std::unique_ptr<EglSurfaceRenderer> EglSurfaceRenderer::Create(ANativeWindow* window,
                                                               uint32_t width, uint32_t height,
                                                               std::string* error) {
    if (window == nullptr || width == 0 || height == 0) {
        if (error != nullptr) {
            *error = "EGL renderer requires a valid window and non-zero dimensions";
        }
        return nullptr;
    }

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || eglInitialize(display, nullptr, nullptr) != EGL_TRUE) {
        SetEglError(error, "eglInitialize");
        return nullptr;
    }
    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        SetEglError(error, "eglBindAPI");
        eglTerminate(display);
        return nullptr;
    }

    const EGLint configAttributes[] = {
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
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (eglChooseConfig(display, configAttributes, &config, 1, &configCount) != EGL_TRUE ||
        configCount != 1) {
        SetEglError(error, "eglChooseConfig");
        eglTerminate(display);
        return nullptr;
    }

    EGLint nativeFormat = 0;
    if (eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &nativeFormat) != EGL_TRUE ||
        ANativeWindow_setBuffersGeometry(window, 0, 0, nativeFormat) != 0) {
        SetEglError(error, "ANativeWindow_setBuffersGeometry");
        eglTerminate(display);
        return nullptr;
    }

    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    if (context == EGL_NO_CONTEXT) {
        SetEglError(error, "eglCreateContext");
        eglTerminate(display);
        return nullptr;
    }

    EGLSurface surface = eglCreateWindowSurface(display, config, window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        SetEglError(error, "eglCreateWindowSurface");
        eglDestroyContext(display, context);
        eglTerminate(display);
        return nullptr;
    }
    if (eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
        SetEglError(error, "eglMakeCurrent");
        eglDestroySurface(display, surface);
        eglDestroyContext(display, context);
        eglTerminate(display);
        return nullptr;
    }

    return std::unique_ptr<EglSurfaceRenderer>(
            new EglSurfaceRenderer(display, context, surface, width, height));
}

EglSurfaceRenderer::~EglSurfaceRenderer() {
    if (display_ == EGL_NO_DISPLAY) {
        return;
    }
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface_);
    }
    if (context_ != EGL_NO_CONTEXT) {
        eglDestroyContext(display_, context_);
    }
    eglTerminate(display_);
}

bool EglSurfaceRenderer::DrawFrame(uint64_t frameIndex, int64_t presentationTimeNanos,
                                   std::string* error) {
    const float phase = static_cast<float>(frameIndex % 60) / 59.0f;
    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
    glClearColor(phase, 1.0f - phase, frameIndex % 2 == 0 ? 0.25f : 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (glGetError() != GL_NO_ERROR) {
        if (error != nullptr) {
            *error = "OpenGL ES failed while rendering the test frame";
        }
        return false;
    }
    if (eglPresentationTimeANDROID(display_, surface_, presentationTimeNanos) != EGL_TRUE) {
        return SetEglError(error, "eglPresentationTimeANDROID");
    }
    if (eglSwapBuffers(display_, surface_) != EGL_TRUE) {
        return SetEglError(error, "eglSwapBuffers");
    }
    return true;
}

}  // namespace floral::stream::codec::testing
