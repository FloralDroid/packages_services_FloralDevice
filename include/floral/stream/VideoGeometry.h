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

#include <cstdint>

namespace floral::stream {

// Clockwise rotation applied by the receiver to coded pixels to recover the
// Android logical display orientation.
enum class DisplayRotation : uint32_t {
    k0 = 0,
    k90 = 90,
    k180 = 180,
    k270 = 270,
};

struct VideoGeometry {
    uint32_t logical_width = 0;
    uint32_t logical_height = 0;
    uint32_t coded_width = 0;
    uint32_t coded_height = 0;
    DisplayRotation display_rotation = DisplayRotation::k0;
};

// Portrait displays are encoded into a landscape surface so hardware encoders
// with a lower maximum height can still accept the stream. The receiver applies
// display_rotation to recover Android's logical display orientation.
inline VideoGeometry MakeLandscapeCodedGeometry(uint32_t logicalWidth, uint32_t logicalHeight) {
    VideoGeometry geometry;
    geometry.logical_width = logicalWidth;
    geometry.logical_height = logicalHeight;
    if (logicalHeight > logicalWidth) {
        geometry.coded_width = logicalHeight;
        geometry.coded_height = logicalWidth;
        geometry.display_rotation = DisplayRotation::k90;
    } else {
        geometry.coded_width = logicalWidth;
        geometry.coded_height = logicalHeight;
    }
    return geometry;
}

inline bool IsValidDisplayRotation(DisplayRotation rotation) {
    switch (rotation) {
        case DisplayRotation::k0:
        case DisplayRotation::k90:
        case DisplayRotation::k180:
        case DisplayRotation::k270:
            return true;
    }
    return false;
}

inline bool HasMatchingDisplayAspect(const VideoGeometry& geometry) {
    if (geometry.logical_width == 0 || geometry.logical_height == 0 || geometry.coded_width == 0 ||
        geometry.coded_height == 0 || !IsValidDisplayRotation(geometry.display_rotation)) {
        return false;
    }

    const uint64_t logicalWidth = geometry.logical_width;
    const uint64_t logicalHeight = geometry.logical_height;
    const uint64_t codedWidth = geometry.coded_width;
    const uint64_t codedHeight = geometry.coded_height;
    if (geometry.display_rotation == DisplayRotation::k90 ||
        geometry.display_rotation == DisplayRotation::k270) {
        return logicalWidth * codedWidth == logicalHeight * codedHeight;
    }
    return logicalWidth * codedHeight == logicalHeight * codedWidth;
}

}  // namespace floral::stream
