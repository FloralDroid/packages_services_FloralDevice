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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct OpusEncoder;

namespace floral::stream::audio {

constexpr uint32_t kMinimumOpusBitrate = 16'000;
constexpr uint32_t kMaximumOpusBitrate = 512'000;

class FloralOpusEncoder {
  public:
    static std::unique_ptr<FloralOpusEncoder> Create(uint32_t bitrate_bps, std::string* error);
    ~FloralOpusEncoder();

    FloralOpusEncoder(const FloralOpusEncoder&) = delete;
    FloralOpusEncoder& operator=(const FloralOpusEncoder&) = delete;

    bool Encode(const int16_t* samples, size_t frame_count, std::vector<uint8_t>* payload,
                std::string* error);
    bool SetBitrate(uint32_t bitrate_bps, std::string* error);
    bool Reset(std::string* error);
    uint32_t bitrate_bps() const;

  private:
    FloralOpusEncoder(::OpusEncoder* encoder, uint32_t bitrate_bps);

    mutable std::mutex mutex_;
    ::OpusEncoder* encoder_ = nullptr;
    uint32_t bitrate_bps_ = 0;
};

}  // namespace floral::stream::audio
