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

#include <gtest/gtest.h>

#include <string>

namespace floral::device::profile {
namespace {

constexpr char kValidProfile[] = R"(
# A profile may be extended by independent consumers.
version=1
brand=OPPO
manufacturer=OPPO
model=PGEM10
device=OP528BL1
product=PGEM10
board=taro
soc_manufacturer=Qualcomm
soc_model=SM8450
gpu_vendor=Qualcomm
gpu_model=Adreno 730
build_id=SKQ1.211006.001
build_display=PGEM10_11_A.12
version_release=12
security_patch=2022-01-05
future_consumer_field=retained
sensor_accelerometer_name=3-axis Accelerometer
sensor_accelerometer_vendor=STMicro
thermal_ambient_celsius=22.0
build_date=Fri Sep  4 08:08:35 UTC 2026
build_date_utc=1788509315
)";

TEST(DeviceProfileTest, ParsesAndRetainsKnownAndUnknownFields) {
    DeviceProfile profile;
    std::string error;
    ASSERT_TRUE(ParseDeviceProfile(kValidProfile, &profile, &error)) << error;
    ASSERT_NE(profile.Find("model"), nullptr);
    EXPECT_EQ(*profile.Find("model"), "PGEM10");
    ASSERT_NE(profile.Find("future_consumer_field"), nullptr);
    EXPECT_EQ(*profile.Find("future_consumer_field"), "retained");
    float ambient = 0.0f;
    ASSERT_TRUE(profile.GetFloat("thermal_ambient_celsius", &ambient));
    EXPECT_FLOAT_EQ(ambient, 22.0f);
}

TEST(DeviceProfileTest, RejectsDuplicateUnknownField) {
    const std::string content = std::string(kValidProfile) + "future_consumer_field=duplicate\n";
    DeviceProfile profile;
    std::string error;
    EXPECT_FALSE(ParseDeviceProfile(content, &profile, &error));
    EXPECT_NE(error.find("repeats field"), std::string::npos);
}

TEST(DeviceProfileTest, RejectsPartialSensorIdentity) {
    const std::string content =
            std::string(kValidProfile) + "sensor_pressure_name=Pressure sensor\n";
    DeviceProfile profile;
    std::string error;
    EXPECT_FALSE(ParseDeviceProfile(content, &profile, &error));
    EXPECT_NE(error.find("specified together"), std::string::npos);
}

TEST(DeviceProfileTest, RejectsOutOfRangeAmbientTemperature) {
    std::string content(kValidProfile);
    const size_t start = content.find("thermal_ambient_celsius=22.0");
    ASSERT_NE(start, std::string::npos);
    content.replace(start, std::string("thermal_ambient_celsius=22.0").size(),
                    "thermal_ambient_celsius=80");
    DeviceProfile profile;
    std::string error;
    EXPECT_FALSE(ParseDeviceProfile(content, &profile, &error));
    EXPECT_NE(error.find("between -20 and 50"), std::string::npos);
}

TEST(DeviceProfileTest, RejectsInvalidCalendarDateAndNonFiniteAmbientTemperature) {
    std::string invalid_date(kValidProfile);
    const size_t date = invalid_date.find("security_patch=2022-01-05");
    ASSERT_NE(date, std::string::npos);
    invalid_date.replace(date, std::string("security_patch=2022-01-05").size(),
                         "security_patch=2022-02-30");
    DeviceProfile profile;
    std::string error;
    EXPECT_FALSE(ParseDeviceProfile(invalid_date, &profile, &error));
    EXPECT_NE(error.find("invalid security_patch"), std::string::npos);

    std::string non_finite(kValidProfile);
    const size_t ambient = non_finite.find("thermal_ambient_celsius=22.0");
    ASSERT_NE(ambient, std::string::npos);
    non_finite.replace(ambient, std::string("thermal_ambient_celsius=22.0").size(),
                       "thermal_ambient_celsius=nan");
    EXPECT_FALSE(ParseDeviceProfile(non_finite, &profile, &error));
    EXPECT_NE(error.find("between -20 and 50"), std::string::npos);
}

TEST(DeviceProfileTest, RejectsPartialBuildDate) {
    std::string content(kValidProfile);
    const size_t start = content.find("build_date_utc=1788509315\n");
    ASSERT_NE(start, std::string::npos);
    content.erase(start, std::string("build_date_utc=1788509315\n").size());
    DeviceProfile profile;
    std::string error;
    EXPECT_FALSE(ParseDeviceProfile(content, &profile, &error));
    EXPECT_NE(error.find("valid pair"), std::string::npos);
}

TEST(DeviceProfileTest, RejectsOversizedProfile) {
    DeviceProfile profile;
    std::string error;
    EXPECT_FALSE(ParseDeviceProfile(std::string(kMaximumSize + 1, 'x'), &profile, &error));
    EXPECT_NE(error.find("16 KiB"), std::string::npos);
}

}  // namespace
}  // namespace floral::device::profile
