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

#define LOG_TAG "floral-device"

#include "floral/device/audio/AudioPcmSinkService.h"
#include "floral/device/control/DeviceControlHandler.h"
#include "floral/device/control/HostControlChannel.h"
#include "floral/device/display/FrameConsumerService.h"
#include "floral/device/display/topology/DisplayTopologyControlHandler.h"
#include "floral/device/display/topology/DisplayTopologyController.h"
#include "floral/device/display/topology/DisplayTopologyStateService.h"
#include "floral/device/service/AudioEncoderControlHandler.h"
#include "floral/device/service/VideoEncoderControlHandler.h"
#include "floral/device/service/VideoFrameConsumerBackend.h"
#include "floral/device/simulation/SimulationControlHandler.h"
#include "floral/device/simulation/SimulationController.h"
#include "floral/device/simulation/SimulationStateService.h"
#include "floral/stream/audio/AudioStreamSession.h"

#include <aidl/floral/device/audio/IAudioPcmSink.h>
#include <aidl/floral/device/display/IFrameConsumer.h>
#include <aidl/floral/device/display/topology/IDisplayTopologyState.h>
#include <aidl/floral/device/simulation/ISimulationState.h>
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
constexpr char kDefaultAudioSocketPath[] = "/mnt/vendor/floral_stream/audio.sock";
constexpr char kDefaultControlSocketPath[] = "/mnt/vendor/floral_stream/control.sock";
constexpr char kDefaultVaDevicePath[] = "/dev/dri/renderD128";

uint32_t BoundedProperty(const char* name, uint32_t defaultValue, uint32_t minimum,
                         uint32_t maximum) {
    const uint32_t value = android::base::GetUintProperty<uint32_t>(name, defaultValue, maximum);
    return value < minimum ? defaultValue : value;
}

std::optional<floral::device::service::VideoFrameConsumerBackendConfig> LoadConfig() {
    floral::device::service::VideoFrameConsumerBackendConfig config;
    config.display_id = 1;
    config.video_socket_path =
            android::base::GetProperty("ro.boot.floral_video_socket", kDefaultVideoSocketPath);
    config.session_config.stream_id = 1;
    const uint32_t logicalWidth = BoundedProperty("ro.boot.floral_width", 1920, 320, 7680);
    const uint32_t logicalHeight = BoundedProperty("ro.boot.floral_height", 1080, 320, 4320);
    config.session_config.geometry =
            floral::stream::MakeLandscapeCodedGeometry(logicalWidth, logicalHeight);
    config.session_config.encoder.width = config.session_config.geometry.coded_width;
    config.session_config.encoder.height = config.session_config.geometry.coded_height;
    config.session_config.encoder.frame_rate = BoundedProperty("ro.boot.floral_fps", 60, 1, 60);
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
        const std::string gbmDevice =
                android::base::GetProperty("gralloc.gbm.device", kDefaultVaDevicePath);
        config.session_config.encoder.va_device_path =
                android::base::GetProperty("ro.boot.floral_vaapi_device", gbmDevice);
    } else {
        LOG(ERROR) << "unsupported ro.boot.floral_video_encoder value: " << encoderBackend;
        return std::nullopt;
    }
    return config;
}

floral::stream::audio::AudioStreamSessionConfig LoadAudioConfig() {
    floral::stream::audio::AudioStreamSessionConfig config;
    config.socket_path =
            android::base::GetProperty("ro.boot.floral_audio_socket", kDefaultAudioSocketPath);
    config.stream_id = 1;
    config.bitrate_bps = BoundedProperty("ro.boot.floral_audio_bitrate", 128'000, 16'000, 512'000);
    return config;
}

floral::device::control::HostControlChannelConfig LoadControlConfig() {
    floral::device::control::HostControlChannelConfig config;
    config.socket_path =
            android::base::GetProperty("ro.boot.floral_control_socket", kDefaultControlSocketPath);
    config.authority_lease = std::chrono::milliseconds(
            BoundedProperty("ro.boot.floral_control_disconnect_lease_ms", 3000, 100, 60'000));
    return config;
}

}  // namespace

int main(int argc, char** argv) {
    android::base::InitLogging(argv, android::base::LogdLogger());
    (void)argc;

    std::string audioError;
    std::shared_ptr<floral::stream::audio::AudioStreamSession> audioSession =
            floral::stream::audio::AudioStreamSession::Create(LoadAudioConfig(), &audioError);
    if (audioSession == nullptr) {
        LOG(ERROR) << "failed to create the FloralDevice audio session: " << audioError;
        return 1;
    }

    const std::optional<floral::device::service::VideoFrameConsumerBackendConfig> config =
            LoadConfig();
    if (!config.has_value()) {
        return 1;
    }
    std::shared_ptr<floral::device::service::VideoEncoderControl> videoControl;
    std::shared_ptr<floral::device::display::FrameConsumerBackend> backend =
            floral::device::service::CreateVideoFrameConsumerBackend(*config, &videoControl);
    if (backend == nullptr) {
        LOG(ERROR) << "failed to create the FloralDevice video backend";
        return 1;
    }
    if (videoControl == nullptr) {
        LOG(ERROR) << "video backend does not expose runtime encoder control";
        return 1;
    }

    auto frameService = ndk::SharedRefBase::make<floral::device::display::FrameConsumerService>(
            std::move(backend));
    auto audioPcmService =
            ndk::SharedRefBase::make<floral::device::audio::AudioPcmSinkService>(audioSession);
    auto topologyStateService = ndk::SharedRefBase::make<
            floral::device::display::topology::DisplayTopologyStateService>();
    auto topologyController =
            std::make_shared<floral::device::display::topology::DisplayTopologyController>(
                    topologyStateService);
    auto simulationStateService =
            ndk::SharedRefBase::make<floral::device::simulation::SimulationStateService>();
    auto simulationController =
            std::make_shared<floral::device::simulation::SimulationController>(
                    simulationStateService);
    auto topologyControlHandler =
            std::make_shared<floral::device::display::topology::DisplayTopologyControlHandler>(
                    topologyController);
    auto videoControlHandler =
            std::make_shared<floral::device::service::VideoEncoderControlHandler>(videoControl);
    auto audioControlHandler =
            std::make_shared<floral::device::service::AudioEncoderControlHandler>(audioSession);
    auto simulationControlHandler =
            std::make_shared<floral::device::simulation::SimulationControlHandler>(
                    simulationController);
    auto deviceControlHandler = std::make_shared<floral::device::control::DeviceControlHandler>(
            std::move(topologyControlHandler), std::move(audioControlHandler),
            std::move(videoControlHandler), std::move(simulationControlHandler));
    const std::string audioPcmInstance =
            std::string(aidl::floral::device::audio::IAudioPcmSink::descriptor) + "/default";
    const std::string frameInstance =
            std::string(aidl::floral::device::display::IFrameConsumer::descriptor) + "/default";
    const std::string topologyStateInstance =
            std::string(
                    aidl::floral::device::display::topology::IDisplayTopologyState::descriptor) +
            "/default";
    const std::string simulationStateInstance =
            std::string(aidl::floral::device::simulation::ISimulationState::descriptor) +
            "/default";

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();
    binder_status_t status =
            AServiceManager_addService(frameService->asBinder().get(), frameInstance.c_str());
    if (status != STATUS_OK) {
        LOG(ERROR) << "failed to register " << frameInstance << ": binder status " << status;
        return 1;
    }
    status =
            AServiceManager_addService(audioPcmService->asBinder().get(), audioPcmInstance.c_str());
    if (status != STATUS_OK) {
        LOG(ERROR) << "failed to register " << audioPcmInstance << ": binder status " << status;
        return 1;
    }
    status = AServiceManager_addService(topologyStateService->asBinder().get(),
                                        topologyStateInstance.c_str());
    if (status != STATUS_OK) {
        LOG(ERROR) << "failed to register " << topologyStateInstance << ": binder status "
                   << status;
        return 1;
    }
    status = AServiceManager_addService(simulationStateService->asBinder().get(),
                                        simulationStateInstance.c_str());
    if (status != STATUS_OK) {
        LOG(ERROR) << "failed to register " << simulationStateInstance << ": binder status "
                   << status;
        return 1;
    }

    std::string controlError;
    std::unique_ptr<floral::device::control::HostControlChannel> controlChannel =
            floral::device::control::HostControlChannel::Create(
                    LoadControlConfig(), std::move(deviceControlHandler), &controlError);
    if (controlChannel == nullptr) {
        LOG(ERROR) << "failed to start the host control channel: " << controlError;
        return 1;
    }

    LOG(INFO) << frameInstance << ", " << audioPcmInstance << ", " << topologyStateInstance
              << " and " << simulationStateInstance << " are ready; host control channel started";
    ABinderProcess_joinThreadPool();
    LOG(ERROR) << "Binder thread pool exited unexpectedly";
    return 1;
}
