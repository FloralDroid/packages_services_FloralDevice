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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace floral::device::profile {

inline constexpr char kDefaultPath[] = "/ipc/floral_stream/device.prop";
inline constexpr size_t kMaximumSize = 16 * 1024;

class DeviceProfile final {
  public:
    const std::string* Find(std::string_view key) const;
    bool GetFloat(std::string_view key, float* value) const;
    const std::vector<std::pair<std::string, std::string>>& entries() const { return entries_; }

  private:
    friend bool ParseDeviceProfile(std::string_view, DeviceProfile*, std::string*);
    std::vector<std::pair<std::string, std::string>> entries_;
};

bool ParseDeviceProfile(std::string_view content, DeviceProfile* profile, std::string* error);
bool ReadDeviceProfile(std::string_view path, DeviceProfile* profile, std::string* error);

}  // namespace floral::device::profile
