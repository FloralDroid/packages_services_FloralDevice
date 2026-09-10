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

#include "floral/device/profile/DeviceProfile.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace floral::device::profile {
namespace {

constexpr size_t kMaximumKeyLength = 64;
constexpr size_t kMaximumValueLength = 256;
constexpr size_t kMaximumEntries = 256;
constexpr size_t kMaximumSensorNameLength = 96;
constexpr size_t kMaximumSensorVendorLength = 64;

constexpr std::array<std::string_view, 15> kRequiredFields = {
        "version",   "brand",    "manufacturer",     "model",           "device",
        "product",   "board",    "soc_manufacturer", "soc_model",       "gpu_vendor",
        "gpu_model", "build_id", "build_display",    "version_release", "security_patch",
};

constexpr std::array<std::string_view, 25> kSensorPrefixes = {
        "sensor_accelerometer",
        "sensor_accelerometer_uncalibrated",
        "sensor_gyroscope",
        "sensor_gyroscope_uncalibrated",
        "sensor_magnetic_field",
        "sensor_magnetic_field_uncalibrated",
        "sensor_light",
        "sensor_proximity",
        "sensor_pressure",
        "sensor_ambient_temperature",
        "sensor_gravity",
        "sensor_linear_acceleration",
        "sensor_rotation_vector",
        "sensor_game_rotation_vector",
        "sensor_step_detector",
        "sensor_step_counter",
        "sensor_significant_motion",
        "sensor_virtual_corrected_gyroscope",
        "sensor_virtual_game_rotation_vector",
        "sensor_virtual_gyroscope_bias",
        "sensor_virtual_geomagnetic_rotation_vector",
        "sensor_virtual_gravity",
        "sensor_virtual_linear_acceleration",
        "sensor_virtual_rotation_vector",
        "sensor_virtual_orientation",
};

std::string_view Trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

bool Fail(std::string message, std::string* error) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

bool IsKeyValid(std::string_view key) {
    if (key.empty() || key.size() > kMaximumKeyLength || (key.front() < 'a' || key.front() > 'z')) {
        return false;
    }
    for (const unsigned char character : key) {
        if (!std::islower(character) && !std::isdigit(character) && character != '_') {
            return false;
        }
    }
    return true;
}

bool IsTextValid(std::string_view value, size_t maximum_length) {
    if (value.empty() || value.size() > maximum_length) {
        return false;
    }
    for (const unsigned char character : value) {
        if (std::iscntrl(character)) {
            return false;
        }
    }
    return true;
}

bool IsIdentifierValid(std::string_view value) {
    if (!IsTextValid(value, 64) || !std::isalnum(static_cast<unsigned char>(value.front()))) {
        return false;
    }
    for (const unsigned char character : value) {
        if (!std::isalnum(character) && character != '_' && character != '-' && character != '.') {
            return false;
        }
    }
    return true;
}

bool IsSecurityPatchValid(std::string_view value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        return false;
    }
    for (size_t index = 0; index < value.size(); ++index) {
        if (index != 4 && index != 7 && !std::isdigit(static_cast<unsigned char>(value[index]))) {
            return false;
        }
    }
    const int year = (value[0] - '0') * 1000 + (value[1] - '0') * 100 + (value[2] - '0') * 10 +
                     value[3] - '0';
    const int month = (value[5] - '0') * 10 + value[6] - '0';
    const int day = (value[8] - '0') * 10 + value[9] - '0';
    if (year < 1970 || month < 1 || month > 12) {
        return false;
    }
    constexpr std::array<int, 12> kDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maximum_day = kDays[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
        ++maximum_day;
    }
    return day >= 1 && day <= maximum_day;
}

bool IsUnsignedDecimal(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    for (const unsigned char character : value) {
        if (!std::isdigit(character)) {
            return false;
        }
    }
    return true;
}

bool ValidateSchema(const DeviceProfile& profile, std::string* error) {
    for (const std::string_view key : kRequiredFields) {
        if (profile.Find(key) == nullptr) {
            return Fail("missing required field '" + std::string(key) + "'", error);
        }
    }
    if (*profile.Find("version") != "1") {
        return Fail("unsupported profile version", error);
    }
    for (const std::string_view key : {"device", "product", "board", "build_id"}) {
        if (!IsIdentifierValid(*profile.Find(key))) {
            return Fail("invalid identifier field '" + std::string(key) + "'", error);
        }
    }
    if (!IsSecurityPatchValid(*profile.Find("security_patch"))) {
        return Fail("invalid security_patch", error);
    }
    for (const std::string_view key : {"brand", "manufacturer", "model", "soc_manufacturer",
                                       "soc_model", "gpu_vendor", "gpu_model"}) {
        if (!IsTextValid(*profile.Find(key), 64)) {
            return Fail("invalid identity field '" + std::string(key) + "'", error);
        }
    }
    if (!IsTextValid(*profile.Find("build_display"), 91) ||
        !IsTextValid(*profile.Find("version_release"), 64)) {
        return Fail("invalid public build identity", error);
    }

    for (const std::string_view key :
         {"build_description", "build_flavor", "build_incremental", "build_type", "build_tags",
          "build_user", "build_host", "build_date", "kernel_release", "kernel_version",
          "memory_type", "memory_frequency", "memory_channel", "serial", "hardware_revision"}) {
        const std::string* value = profile.Find(key);
        if (value != nullptr && !IsTextValid(*value, 91)) {
            return Fail("invalid optional identity field '" + std::string(key) + "'", error);
        }
    }
    const std::string* build_date = profile.Find("build_date");
    const std::string* build_date_utc = profile.Find("build_date_utc");
    if ((build_date == nullptr) != (build_date_utc == nullptr) ||
        (build_date_utc != nullptr && !IsUnsignedDecimal(*build_date_utc))) {
        return Fail("build_date and build_date_utc must be a valid pair", error);
    }

    for (const std::string_view prefix : kSensorPrefixes) {
        const std::string name_key = std::string(prefix) + "_name";
        const std::string vendor_key = std::string(prefix) + "_vendor";
        const std::string* name = profile.Find(name_key);
        const std::string* vendor = profile.Find(vendor_key);
        if ((name == nullptr) != (vendor == nullptr)) {
            return Fail("sensor name and vendor must be specified together for '" +
                                std::string(prefix) + "'",
                        error);
        }
        if (name != nullptr && (!IsTextValid(*name, kMaximumSensorNameLength) ||
                                !IsTextValid(*vendor, kMaximumSensorVendorLength))) {
            return Fail("invalid sensor identity for '" + std::string(prefix) + "'", error);
        }
    }

    for (const std::string_view key :
         {"thermal_cpu_name", "thermal_gpu_name", "thermal_battery_name", "thermal_skin_name"}) {
        const std::string* value = profile.Find(key);
        if (value != nullptr && !IsTextValid(*value, 64)) {
            return Fail("invalid thermal identity field '" + std::string(key) + "'", error);
        }
    }
    if (profile.Find("thermal_ambient_celsius") != nullptr) {
        float ambient = 0.0f;
        if (!profile.GetFloat("thermal_ambient_celsius", &ambient) || ambient < -20.0f ||
            ambient > 50.0f) {
            return Fail("thermal_ambient_celsius must be between -20 and 50", error);
        }
    }
    return true;
}

}  // namespace

const std::string* DeviceProfile::Find(std::string_view key) const {
    for (const auto& entry : entries_) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool DeviceProfile::GetFloat(std::string_view key, float* value) const {
    if (value == nullptr) {
        return false;
    }
    const std::string* text = Find(key);
    if (text == nullptr || text->empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(text->c_str(), &end);
    if (errno != 0 || end != text->c_str() + text->size() || !std::isfinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

bool ParseDeviceProfile(std::string_view content, DeviceProfile* profile, std::string* error) {
    if (profile == nullptr) {
        return Fail("profile output is null", error);
    }
    if (content.size() > kMaximumSize) {
        return Fail("profile exceeds 16 KiB", error);
    }
    if (content.find('\0') != std::string_view::npos) {
        return Fail("profile contains a NUL byte", error);
    }

    DeviceProfile parsed;
    size_t line_number = 0;
    size_t offset = 0;
    while (offset <= content.size()) {
        ++line_number;
        const size_t end = content.find('\n', offset);
        std::string_view line = content.substr(
                offset, end == std::string_view::npos ? content.size() - offset : end - offset);
        line = Trim(line);
        if (!line.empty() && line.front() != '#') {
            const size_t separator = line.find('=');
            if (separator == std::string_view::npos) {
                return Fail("line " + std::to_string(line_number) + " has no '=' separator", error);
            }
            const std::string_view key = Trim(line.substr(0, separator));
            const std::string_view value = Trim(line.substr(separator + 1));
            if (!IsKeyValid(key)) {
                return Fail("line " + std::to_string(line_number) + " has an invalid key", error);
            }
            if (!IsTextValid(value, kMaximumValueLength)) {
                return Fail("line " + std::to_string(line_number) + " has an invalid value", error);
            }
            if (parsed.Find(key) != nullptr) {
                return Fail("line " + std::to_string(line_number) + " repeats field '" +
                                    std::string(key) + "'",
                            error);
            }
            if (parsed.entries_.size() == kMaximumEntries) {
                return Fail("profile has too many fields", error);
            }
            parsed.entries_.emplace_back(key, value);
        }
        if (end == std::string_view::npos) {
            break;
        }
        offset = end + 1;
    }

    if (!ValidateSchema(parsed, error)) {
        return false;
    }
    *profile = std::move(parsed);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool ReadDeviceProfile(std::string_view path, DeviceProfile* profile, std::string* error) {
    const std::string path_string(path);
    const int fd = open(path_string.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return Fail("cannot open " + path_string + ": " + std::strerror(errno), error);
    }

    struct stat status {};
    if (fstat(fd, &status) != 0) {
        const int saved_errno = errno;
        close(fd);
        return Fail("cannot stat " + path_string + ": " + std::strerror(saved_errno), error);
    }
    if (!S_ISREG(status.st_mode) || status.st_size < 0 ||
        static_cast<uint64_t>(status.st_size) > kMaximumSize) {
        close(fd);
        return Fail("profile is not a regular file of at most 16 KiB", error);
    }

    std::string content;
    content.reserve(static_cast<size_t>(status.st_size));
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t count = read(fd, buffer.data(), buffer.size());
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int saved_errno = errno;
            close(fd);
            return Fail("cannot read " + path_string + ": " + std::strerror(saved_errno), error);
        }
        if (content.size() + static_cast<size_t>(count) > kMaximumSize) {
            close(fd);
            return Fail("profile exceeds 16 KiB", error);
        }
        content.append(buffer.data(), static_cast<size_t>(count));
    }
    close(fd);
    return ParseDeviceProfile(content, profile, error);
}

}  // namespace floral::device::profile
