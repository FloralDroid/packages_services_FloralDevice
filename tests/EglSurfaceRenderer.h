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

#include <EGL/egl.h>
#include <android/native_window.h>

#include <cstdint>
#include <memory>
#include <string>

namespace floral::stream::codec::testing {

class EglSurfaceRenderer {
  public:
    static std::unique_ptr<EglSurfaceRenderer> Create(ANativeWindow* window, uint32_t width,
                                                      uint32_t height, std::string* error);

    ~EglSurfaceRenderer();

    EglSurfaceRenderer(const EglSurfaceRenderer&) = delete;
    EglSurfaceRenderer& operator=(const EglSurfaceRenderer&) = delete;

    bool DrawFrame(uint64_t frameIndex, int64_t presentationTimeNanos, std::string* error);

  private:
    EglSurfaceRenderer(EGLDisplay display, EGLContext context, EGLSurface surface, uint32_t width,
                       uint32_t height);

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

}  // namespace floral::stream::codec::testing
