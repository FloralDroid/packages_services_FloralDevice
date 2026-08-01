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

#define LOG_TAG "floral-stream"

#include "floral/stream/control/HostControlChannel.h"
#include "floral/stream/display/FrameConsumerService.h"
#include "floral/stream/service/VideoFrameConsumerBackend.h"
#include "floral/stream/topology/DisplayTopologyControlHandler.h"
#include "floral/stream/topology/DisplayTopologyController.h"
#include "floral/stream/topology/DisplayTopologyStateService.h"

#include <aidl/floral/display/topology/IDisplayTopologyState.h>
#include <aidl/floral/stream/display/IFrameConsumer.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace {

constexpr char kDefaultVideoSocketPath[] = "/mnt/vendor/floral_stream/video.sock";
constexpr char kDefaultControlSocketPath[] = "/mnt/vendor/floral_stream/control.sock";
constexpr char kDefaultVaDevicePath[] = "/dev/dri/renderD128";

uint32_t BoundedProperty(const char* name, uint32_t defaultValue, uint32_t minimum,
                         uint32_t maximum) {
    const uint32_t value = android::base::GetUintProperty<uint32_t>(name, defaultValue, maximum);
    return value < minimum ? defaultValue : value;
}

std::optional<floral::stream::service::VideoFrameConsumerBackendConfig> LoadConfig() {
    floral::stream::service::VideoFrameConsumerBackendConfig config;
    config.display_id = 1;
    config.video_socket_path =
            android::base::GetProperty("ro.boot.floral_video_socket", kDefaultVideoSocketPath);
    config.session_config.stream_id = 1;
    const uint32_t logicalWidth = BoundedProperty("ro.boot.redroid_width", 1920, 320, 7680);
    const uint32_t logicalHeight = BoundedProperty("ro.boot.redroid_height", 1080, 320, 4320);
    config.session_config.geometry =
            floral::stream::MakeLandscapeCodedGeometry(logicalWidth, logicalHeight);
    config.session_config.encoder.width = config.session_config.geometry.coded_width;
    config.session_config.encoder.height = config.session_config.geometry.coded_height;
    config.session_config.encoder.frame_rate = BoundedProperty("ro.boot.redroid_fps", 60, 1, 60);
    config.session_config.encoder.bitrate_bps =
            BoundedProperty("ro.boot.floral_video_bitrate", 8'000'000, 100'000, 100'000'000);
    config.session_config.encoder.i_frame_interval_seconds = 1;
    const std::string encoderBackend =
            android::base::GetProperty("ro.boot.floral_video_encoder", "software");
    if (encoderBackend == "software") {
        config.session_config.encoder.backend =
                floral::stream::codec::EncoderBackendType::kMediaCodecSoftware;
    } else if (encoderBackend == "vaapi") {
        config.session_config.encoder.backend =
                floral::stream::codec::EncoderBackendType::kFfmpegVaapi;
        config.session_config.encoder.va_device_path =
                android::base::GetProperty("ro.boot.floral_vaapi_device", kDefaultVaDevicePath);
    } else {
        LOG(ERROR) << "unsupported ro.boot.floral_video_encoder value: " << encoderBackend;
        return std::nullopt;
    }
    return config;
}

floral::stream::control::HostControlChannelConfig LoadControlConfig() {
    floral::stream::control::HostControlChannelConfig config;
    config.socket_path =
            android::base::GetProperty("ro.boot.floral_control_socket", kDefaultControlSocketPath);
    config.authority_lease = std::chrono::milliseconds(
            BoundedProperty("ro.boot.floral_control_disconnect_lease_ms", 3000, 100, 60'000));
    return config;
}

}  // namespace

int main(int argc, char** argv) {
    android::base::InitLogging(argv, android::base::KernelLogger);
    (void)argc;

    const std::optional<floral::stream::service::VideoFrameConsumerBackendConfig> config =
            LoadConfig();
    if (!config.has_value()) {
        return 1;
    }
    std::shared_ptr<floral::stream::display::FrameConsumerBackend> backend =
            floral::stream::service::CreateVideoFrameConsumerBackend(*config);
    if (backend == nullptr) {
        LOG(ERROR) << "failed to create the FloralStream video backend";
        return 1;
    }

    auto frameService = ndk::SharedRefBase::make<floral::stream::display::FrameConsumerService>(
            std::move(backend));
    auto topologyStateService =
            ndk::SharedRefBase::make<floral::stream::topology::DisplayTopologyStateService>();
    auto topologyController = std::make_shared<floral::stream::topology::DisplayTopologyController>(
            topologyStateService);
    auto topologyControlHandler =
            std::make_shared<floral::stream::topology::DisplayTopologyControlHandler>(
                    topologyController);
    const std::string frameInstance =
            std::string(aidl::floral::stream::display::IFrameConsumer::descriptor) + "/default";
    const std::string topologyStateInstance =
            std::string(aidl::floral::display::topology::IDisplayTopologyState::descriptor) +
            "/default";

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();
    binder_status_t status =
            AServiceManager_addService(frameService->asBinder().get(), frameInstance.c_str());
    if (status != STATUS_OK) {
        LOG(ERROR) << "failed to register " << frameInstance << ": binder status " << status;
        return 1;
    }
    status = AServiceManager_addService(topologyStateService->asBinder().get(),
                                        topologyStateInstance.c_str());
    if (status != STATUS_OK) {
        LOG(ERROR) << "failed to register " << topologyStateInstance << ": binder status "
                   << status;
        return 1;
    }

    std::string controlError;
    std::unique_ptr<floral::stream::control::HostControlChannel> controlChannel =
            floral::stream::control::HostControlChannel::Create(
                    LoadControlConfig(), std::move(topologyControlHandler), &controlError);
    if (controlChannel == nullptr) {
        LOG(ERROR) << "failed to start the host control channel: " << controlError;
        return 1;
    }

    LOG(INFO) << frameInstance << " and " << topologyStateInstance
              << " are ready; host control channel started";
    ABinderProcess_joinThreadPool();
    LOG(ERROR) << "Binder thread pool exited unexpectedly";
    return 1;
}
