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

#include "floral/device/simulation/RandomProcess.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>

namespace floral::device::simulation {
namespace {

bool ReadSystemEntropy(void* destination, size_t size) {
    const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    size_t offset = 0;
    while (offset < size) {
        const ssize_t result =
                read(fd, static_cast<uint8_t*>(destination) + offset, size - offset);
        if (result > 0) {
            offset += static_cast<size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    close(fd);
    return offset == size;
}

}  // namespace

uint64_t GenerateSessionSeed() {
    uint64_t seed = 0;
    if (ReadSystemEntropy(&seed, sizeof(seed))) {
        return seed;
    }
    const uint64_t timeSeed = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    std::random_device fallback;
    return timeSeed ^ (static_cast<uint64_t>(fallback()) << 32) ^ fallback();
}

uint64_t DeriveSeed(uint64_t master_seed, uint64_t stream_id) {
    // SplitMix64 keeps neighboring stream identifiers decorrelated.
    uint64_t value = master_seed + 0x9e3779b97f4a7c15ULL * (stream_id + 1);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

RandomStream::RandomStream(uint64_t seed) : engine_(seed) {}

float RandomStream::Normal(float standard_deviation) {
    return normal_(engine_) * standard_deviation;
}

float RandomStream::Uniform(float minimum, float maximum) {
    return std::uniform_real_distribution<float>(minimum, maximum)(engine_);
}

GaussMarkovProcess::GaussMarkovProcess(uint64_t seed, float correlation_seconds,
                                       float standard_deviation, float initial_value)
    : random_(seed),
      correlation_seconds_(std::max(correlation_seconds, 1.0e-3f)),
      standard_deviation_(std::max(standard_deviation, 0.0f)),
      value_(initial_value) {}

float GaussMarkovProcess::Advance(float elapsed_seconds, float mean) {
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds <= 0.0f) {
        return value_;
    }
    const float decay = std::exp(-elapsed_seconds / correlation_seconds_);
    const float innovationScale =
            standard_deviation_ * std::sqrt(std::max(0.0f, 1.0f - decay * decay));
    value_ = mean + (value_ - mean) * decay + random_.Normal(innovationScale);
    return value_;
}

}  // namespace floral::device::simulation
