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
#include "floral/device/display/HardwareBufferImporter.h"
#include "floral/stream/codec/EncoderSession.h"
#include "floral/stream/codec/MediaCodecSurfaceEncoder.h"
#include "floral/stream/session/VideoStreamSession.h"
#include "floral/stream/transport/HostVideoSink.h"

#include <android-base/unique_fd.h>
#include <android/hardware_buffer.h>
#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/HardwareBuffer.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <gtest/gtest.h>
#include <sys/socket.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace floral::stream::codec {
namespace {

using Clock = std::chrono::steady_clock;

constexpr char kOutputPath[] = "/data/local/tmp/floral-device-codec-test.h264";

bool ContainsAnnexBNalType(const std::vector<uint8_t>& data, uint8_t expectedType) {
    for (size_t index = 0; index + 4 < data.size(); ++index) {
        size_t nalOffset = 0;
        if (data[index] == 0 && data[index + 1] == 0 && data[index + 2] == 1) {
            nalOffset = index + 3;
        } else if (index + 4 < data.size() && data[index] == 0 && data[index + 1] == 0 &&
                   data[index + 2] == 0 && data[index + 3] == 1) {
            nalOffset = index + 4;
        }
        if (nalOffset != 0 && nalOffset < data.size() && (data[nalOffset] & 0x1f) == expectedType) {
            return true;
        }
    }
    return false;
}

class OutputCollector {
  public:
    explicit OutputCollector(const std::map<int64_t, Clock::time_point>* frameSubmissionTimes)
        : frame_submission_times_(frameSubmissionTimes) {}

    template <typename Encoder>
    bool DrainOnce(Encoder* encoder, int64_t timeoutUs, std::string* error) {
        DequeueResult result = encoder->DequeueOutput(timeoutUs);
        switch (result.status) {
            case DequeueStatus::kTryAgain:
                return true;
            case DequeueStatus::kFormatChanged:
                format_changed_ = true;
                return true;
            case DequeueStatus::kError:
                if (error != nullptr) {
                    *error = "encoder output dequeue failed with status " +
                             std::to_string(result.error_code);
                }
                return false;
            case DequeueStatus::kPacket:
            case DequeueStatus::kEndOfStream:
                CollectPacket(result.packet);
                end_of_stream_ = result.status == DequeueStatus::kEndOfStream;
                return true;
        }
        if (error != nullptr) {
            *error = "unexpected dequeue status";
        }
        return false;
    }

    template <typename Encoder>
    bool DrainAvailable(Encoder* encoder, std::string* error) {
        while (true) {
            const size_t previousPackets = packet_count_;
            const bool previousFormat = format_changed_;
            if (!DrainOnce(encoder, 0, error)) {
                return false;
            }
            if (end_of_stream_ ||
                (previousPackets == packet_count_ && previousFormat == format_changed_)) {
                return true;
            }
        }
    }

    bool format_changed() const { return format_changed_; }
    bool end_of_stream() const { return end_of_stream_; }
    size_t packet_count() const { return packet_count_; }
    size_t frame_packet_count() const { return frame_packet_count_; }
    const std::vector<uint8_t>& bitstream() const { return bitstream_; }
    const std::vector<double>& latency_millis() const { return latency_millis_; }

  private:
    void CollectPacket(const EncodedPacket& packet) {
        ++packet_count_;
        if ((packet.flags & kEncodedPacketFlagCodecConfig) == 0 && !packet.data.empty()) {
            ++frame_packet_count_;
        }
        bitstream_.insert(bitstream_.end(), packet.data.begin(), packet.data.end());

        const auto submission = frame_submission_times_->find(packet.presentation_time_us);
        if (submission != frame_submission_times_->end()) {
            const auto elapsed = Clock::now() - submission->second;
            latency_millis_.push_back(std::chrono::duration<double, std::milli>(elapsed).count());
        }
    }

    const std::map<int64_t, Clock::time_point>* frame_submission_times_;
    bool format_changed_ = false;
    bool end_of_stream_ = false;
    size_t packet_count_ = 0;
    size_t frame_packet_count_ = 0;
    std::vector<uint8_t> bitstream_;
    std::vector<double> latency_millis_;
};

using HardwareBufferPtr = std::unique_ptr<AHardwareBuffer, decltype(&AHardwareBuffer_release)>;

aidl::android::hardware::graphics::common::HardwareBuffer ToTransportedBuffer(
        AHardwareBuffer* buffer) {
    using aidl::android::hardware::graphics::common::BufferUsage;
    using aidl::android::hardware::graphics::common::HardwareBuffer;
    using aidl::android::hardware::graphics::common::PixelFormat;

    AHardwareBuffer_Desc description{};
    AHardwareBuffer_describe(buffer, &description);

    HardwareBuffer transported;
    transported.description.width = static_cast<int32_t>(description.width);
    transported.description.height = static_cast<int32_t>(description.height);
    transported.description.layers = static_cast<int32_t>(description.layers);
    transported.description.format = static_cast<PixelFormat>(description.format);
    transported.description.usage = static_cast<BufferUsage>(description.usage);
    transported.description.stride = static_cast<int32_t>(description.stride);
    transported.handle = android::dupToAidl(AHardwareBuffer_getNativeHandle(buffer));
    return transported;
}

bool FillHardwareBuffer(AHardwareBuffer* buffer, android::base::unique_fd releaseFence,
                        uint32_t frameIndex, android::base::unique_fd* outAcquireFence,
                        std::string* error) {
    AHardwareBuffer_Desc description{};
    AHardwareBuffer_describe(buffer, &description);

    void* pixels = nullptr;
    const int lockStatus = AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
                                                releaseFence.release(), nullptr, &pixels);
    if (lockStatus != 0 || pixels == nullptr) {
        if (error != nullptr) {
            *error = "AHardwareBuffer_lock failed with status " + std::to_string(lockStatus);
        }
        return false;
    }

    const uint8_t red = static_cast<uint8_t>((frameIndex * 17) % 256);
    const uint8_t green = static_cast<uint8_t>(255 - red);
    const uint8_t blue = frameIndex % 2 == 0 ? 64 : 192;
    auto* rows = static_cast<uint32_t*>(pixels);
    const uint32_t pixel = static_cast<uint32_t>(red) | (static_cast<uint32_t>(green) << 8) |
                           (static_cast<uint32_t>(blue) << 16) | 0xff000000u;
    for (uint32_t row = 0; row < description.height; ++row) {
        std::fill_n(rows + row * description.stride, description.width, pixel);
    }

    int32_t acquireFence = -1;
    const int unlockStatus = AHardwareBuffer_unlock(buffer, &acquireFence);
    if (unlockStatus != 0) {
        if (error != nullptr) {
            *error = "AHardwareBuffer_unlock failed with status " + std::to_string(unlockStatus);
        }
        return false;
    }
    outAcquireFence->reset(acquireFence);
    return true;
}

TEST(MediaCodecSurfaceEncoderTest, RejectsInvalidConfigurationBeforeCodecCreation) {
    EncoderConfig config;
    config.width = 0;
    std::string error;
    EXPECT_EQ(MediaCodecSurfaceEncoder::Create(config, &error), nullptr);
    EXPECT_FALSE(error.empty());
}

TEST(MediaCodecSurfaceEncoderTest, RejectsUnsupportedMimeBeforeCodecCreation) {
    EncoderConfig config;
    config.mime = "video/hevc";
    std::string error;
    EXPECT_EQ(MediaCodecSurfaceEncoder::Create(config, &error), nullptr);
    EXPECT_EQ(error, "the MediaCodec software backend only supports video/avc");
}

TEST(MediaCodecSurfaceEncoderTest, Encodes1080p60SurfaceFramesAndChangesBitrate) {
    EncoderConfig config;
    config.width = 1920;
    config.height = 1080;
    config.bitrate_bps = 8'000'000;
    config.frame_rate = 60;
    config.i_frame_interval_seconds = 1;

    std::string error;
    std::unique_ptr<MediaCodecSurfaceEncoder> encoder =
            MediaCodecSurfaceEncoder::Create(config, &error);
    ASSERT_NE(encoder, nullptr) << error;
    EXPECT_EQ(encoder->codec_name(), kSoftwareAvcCodecName);

    std::unique_ptr<testing::EglSurfaceRenderer> renderer = testing::EglSurfaceRenderer::Create(
            encoder->input_surface(), config.width, config.height, &error);
    ASSERT_NE(renderer, nullptr) << error;

    constexpr uint64_t kFrameCount = 120;
    const int64_t framePeriodNanos = 1'000'000'000LL / config.frame_rate;
    const auto start = Clock::now();
    std::map<int64_t, Clock::time_point> frameSubmissionTimes;
    OutputCollector collector(&frameSubmissionTimes);

    for (uint64_t frame = 0; frame < kFrameCount; ++frame) {
        std::this_thread::sleep_until(start + std::chrono::nanoseconds(frame * framePeriodNanos));
        const int64_t presentationTimeNanos = static_cast<int64_t>(frame + 1) * framePeriodNanos;
        ASSERT_TRUE(renderer->DrawFrame(frame, presentationTimeNanos, &error)) << error;
        frameSubmissionTimes[presentationTimeNanos / 1'000] = Clock::now();

        if (frame == kFrameCount / 2) {
            ASSERT_TRUE(encoder->SetBitrate(4'000'000, &error)) << error;
        }
        if (frame == kFrameCount / 4) {
            ASSERT_TRUE(encoder->RequestKeyFrame(&error)) << error;
        }
        ASSERT_TRUE(collector.DrainAvailable(encoder.get(), &error)) << error;
    }

    ASSERT_TRUE(encoder->SignalEndOfInputStream(&error)) << error;
    const auto drainDeadline = Clock::now() + std::chrono::seconds(10);
    while (!collector.end_of_stream() && Clock::now() < drainDeadline) {
        ASSERT_TRUE(collector.DrainOnce(encoder.get(), 100'000, &error)) << error;
    }

    ASSERT_TRUE(collector.end_of_stream());
    ASSERT_TRUE(collector.format_changed());
    ASSERT_GT(collector.frame_packet_count(), 0u);
    ASSERT_FALSE(collector.bitstream().empty());
    EXPECT_TRUE(ContainsAnnexBNalType(collector.bitstream(), 7)) << "missing H.264 SPS";
    EXPECT_TRUE(ContainsAnnexBNalType(collector.bitstream(), 5)) << "missing H.264 IDR frame";
    ASSERT_FALSE(collector.latency_millis().empty());

    std::ofstream output(kOutputPath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(reinterpret_cast<const char*>(collector.bitstream().data()),
                 static_cast<std::streamsize>(collector.bitstream().size()));
    output.close();
    ASSERT_TRUE(output.good());

    std::vector<double> sortedLatency = collector.latency_millis();
    std::sort(sortedLatency.begin(), sortedLatency.end());
    const size_t p95Index = (sortedLatency.size() - 1) * 95 / 100;
    std::cout << "floral_stream_codec_name=" << encoder->codec_name() << '\n'
              << "floral_stream_frame_packets=" << collector.frame_packet_count() << '\n'
              << "floral_stream_output_bytes=" << collector.bitstream().size() << '\n'
              << "floral_stream_latency_samples=" << sortedLatency.size() << '\n'
              << "floral_stream_latency_p50_ms=" << sortedLatency[sortedLatency.size() / 2] << '\n'
              << "floral_stream_latency_p95_ms=" << sortedLatency[p95Index] << '\n'
              << "floral_stream_output_path=" << kOutputPath << std::endl;
}

TEST(EncoderSessionTest, RejectsMismatchedCodedGeometryBeforeCodecCreation) {
    EncoderConfig config;
    config.width = 640;
    config.height = 360;

    VideoGeometry geometry = MakeLandscapeCodedGeometry(360, 640);
    geometry.coded_width = 1280;
    std::string error;
    EXPECT_EQ(EncoderSession::Create(config, geometry, &error), nullptr);
    EXPECT_FALSE(error.empty());
}

TEST(EncoderSessionTest, EncodesTransportedGuestBufferAndPreservesFenceOwnership) {
    EncoderConfig config;
    config.width = 640;
    config.height = 360;
    config.bitrate_bps = 2'000'000;
    config.frame_rate = 30;

    std::string error;
    const VideoGeometry geometry = MakeLandscapeCodedGeometry(360, 640);
    std::unique_ptr<EncoderSession> session = EncoderSession::Create(config, geometry, &error);
    ASSERT_NE(session, nullptr) << error;

    AHardwareBuffer_Desc description{};
    description.width = geometry.logical_width;
    description.height = geometry.logical_height;
    description.layers = 1;
    description.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    description.usage =
            AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    AHardwareBuffer* rawBuffer = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&description, &rawBuffer), 0);
    HardwareBufferPtr sourceBuffer(rawBuffer, AHardwareBuffer_release);

    floral::device::display::UniqueHardwareBuffer buffer;
    {
        aidl::android::hardware::graphics::common::HardwareBuffer transported =
                ToTransportedBuffer(sourceBuffer.get());
        buffer = floral::device::display::ImportHardwareBuffer(transported, &error);
        ASSERT_NE(buffer, nullptr) << error;
    }
    sourceBuffer.reset();

    uint64_t bufferId = 0;
    ASSERT_TRUE(session->RegisterBuffer(buffer.get(), &bufferId, &error)) << error;
    EXPECT_EQ(session->registered_buffer_count(), 1u);
    uint64_t repeatedBufferId = 0;
    ASSERT_TRUE(session->RegisterBuffer(buffer.get(), &repeatedBufferId, &error)) << error;
    EXPECT_EQ(repeatedBufferId, bufferId);
    EXPECT_EQ(session->registered_buffer_count(), 1u);

    constexpr uint32_t kFrameCount = 30;
    const int64_t framePeriodNanos = 1'000'000'000LL / config.frame_rate;
    std::map<int64_t, Clock::time_point> unusedSubmissionTimes;
    OutputCollector collector(&unusedSubmissionTimes);
    android::base::unique_fd releaseFence;
    uint32_t asynchronousReleaseFences = 0;

    for (uint32_t frame = 0; frame < kFrameCount; ++frame) {
        android::base::unique_fd acquireFence;
        ASSERT_TRUE(FillHardwareBuffer(buffer.get(), std::move(releaseFence), frame, &acquireFence,
                                       &error))
                << error;
        const int64_t presentationTimeNanos = static_cast<int64_t>(frame + 1) * framePeriodNanos;
        FrameCopyResult copyResult = session->SubmitFrame(bufferId, std::move(acquireFence),
                                                          presentationTimeNanos, &error);
        ASSERT_TRUE(copyResult.success) << error;
        if (copyResult.release_fence.ok()) {
            ++asynchronousReleaseFences;
        }
        releaseFence = std::move(copyResult.release_fence);

        if (frame == kFrameCount / 2) {
            ASSERT_TRUE(session->SetBitrate(1'000'000, &error)) << error;
        }
        ASSERT_TRUE(collector.DrainAvailable(session.get(), &error)) << error;
    }

    // Passing the final release fence into the CPU lock proves that the buffer
    // can be safely reclaimed before its cached EGL resources are removed.
    void* finalPixels = nullptr;
    ASSERT_EQ(AHardwareBuffer_lock(buffer.get(), AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
                                   releaseFence.release(), nullptr, &finalPixels),
              0);
    ASSERT_NE(finalPixels, nullptr);
    ASSERT_EQ(AHardwareBuffer_unlock(buffer.get(), nullptr), 0);
    session->UnregisterBuffer(bufferId);
    EXPECT_EQ(session->registered_buffer_count(), 0u);

    ASSERT_TRUE(session->SignalEndOfInputStream(&error)) << error;
    const auto drainDeadline = Clock::now() + std::chrono::seconds(10);
    while (!collector.end_of_stream() && Clock::now() < drainDeadline) {
        ASSERT_TRUE(collector.DrainOnce(session.get(), 100'000, &error)) << error;
    }

    ASSERT_TRUE(collector.end_of_stream());
    ASSERT_TRUE(collector.format_changed());
    EXPECT_EQ(collector.frame_packet_count(), kFrameCount);
    EXPECT_TRUE(ContainsAnnexBNalType(collector.bitstream(), 7)) << "missing H.264 SPS";
    EXPECT_TRUE(ContainsAnnexBNalType(collector.bitstream(), 5)) << "missing H.264 IDR frame";

    std::cout << "floral_stream_buffer_id=" << bufferId << '\n'
              << "floral_stream_logical_size=" << geometry.logical_width << "x"
              << geometry.logical_height << '\n'
              << "floral_stream_coded_size=" << geometry.coded_width << "x" << geometry.coded_height
              << '\n'
              << "floral_stream_native_fences=" << (session->uses_native_fences() ? 1 : 0) << '\n'
              << "floral_stream_async_release_fences=" << asynchronousReleaseFences << '\n'
              << "floral_stream_imported_frame_packets=" << collector.frame_packet_count()
              << std::endl;
}

#if defined(__x86_64__)
void RunFfmpegVaapiDmaBufEncodingTest(const VideoGeometry& geometry) {
    EncoderConfig config;
    config.backend = EncoderBackendType::kFfmpegVaapi;
    config.width = geometry.coded_width;
    config.height = geometry.coded_height;
    config.bitrate_bps = 4'000'000;
    config.frame_rate = 30;

    std::string error;
    std::unique_ptr<EncoderSession> session = EncoderSession::Create(config, geometry, &error);
    ASSERT_NE(session, nullptr) << error;
    EXPECT_EQ(session->codec_name(), "h264_vaapi");
    EXPECT_FALSE(session->uses_native_fences());

    AHardwareBuffer_Desc description{};
    description.width = geometry.logical_width;
    description.height = geometry.logical_height;
    description.layers = 1;
    description.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    description.usage =
            AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    AHardwareBuffer* rawBuffer = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&description, &rawBuffer), 0);
    HardwareBufferPtr buffer(rawBuffer, AHardwareBuffer_release);

    uint64_t bufferId = 0;
    ASSERT_TRUE(session->RegisterBuffer(buffer.get(), &bufferId, &error)) << error;
    constexpr uint32_t kFrameCount = 12;
    const int64_t framePeriodNanos = 1'000'000'000LL / config.frame_rate;
    std::map<int64_t, Clock::time_point> unusedSubmissionTimes;
    OutputCollector collector(&unusedSubmissionTimes);
    android::base::unique_fd releaseFence;

    for (uint32_t frame = 0; frame < kFrameCount; ++frame) {
        android::base::unique_fd acquireFence;
        ASSERT_TRUE(FillHardwareBuffer(buffer.get(), std::move(releaseFence), frame, &acquireFence,
                                       &error))
                << error;
        const int64_t presentationTimeNanos = static_cast<int64_t>(frame + 1) * framePeriodNanos;
        FrameCopyResult submitted = session->SubmitFrame(bufferId, std::move(acquireFence),
                                                         presentationTimeNanos, &error);
        ASSERT_TRUE(submitted.success) << error;
        EXPECT_TRUE(submitted.completed_synchronously);
        EXPECT_FALSE(submitted.release_fence.ok());

        if (frame == kFrameCount / 2) {
            ASSERT_TRUE(session->SetBitrate(2'000'000, &error)) << error;
        }
        ASSERT_TRUE(collector.DrainAvailable(session.get(), &error)) << error;
    }

    session->UnregisterBuffer(bufferId);
    ASSERT_TRUE(session->SignalEndOfInputStream(&error)) << error;
    const auto drainDeadline = Clock::now() + std::chrono::seconds(10);
    while (!collector.end_of_stream() && Clock::now() < drainDeadline) {
        ASSERT_TRUE(collector.DrainOnce(session.get(), 100'000, &error)) << error;
    }

    ASSERT_TRUE(collector.end_of_stream());
    ASSERT_TRUE(collector.format_changed());
    EXPECT_EQ(collector.frame_packet_count(), kFrameCount);
    EXPECT_TRUE(ContainsAnnexBNalType(collector.bitstream(), 7)) << "missing H.264 SPS";
    EXPECT_TRUE(ContainsAnnexBNalType(collector.bitstream(), 5)) << "missing H.264 IDR frame";
}

TEST(FfmpegVaapiEncoderBackendTest, EncodesLandscapeDmaBufsAndChangesBitrate) {
    RunFfmpegVaapiDmaBufEncodingTest(MakeLandscapeCodedGeometry(1280, 720));
}

TEST(FfmpegVaapiEncoderBackendTest, EncodesRotatedDmaBufsAndChangesBitrate) {
    RunFfmpegVaapiDmaBufEncodingTest(MakeLandscapeCodedGeometry(720, 1280));
}
#endif

TEST(VideoStreamSessionTest, SendsEncodedFramesWithMatchedSubmissionTimestamps) {
    int sockets[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets), 0);
    std::string error;
    std::unique_ptr<transport::HostVideoSink> sink =
            transport::HostVideoSink::CreateFromConnectedSocket(
                    android::base::unique_fd(sockets[0]), transport::HostVideoSinkConfig{}, &error);
    ASSERT_NE(sink, nullptr) << error;
    android::base::unique_fd receiver(sockets[1]);

    session::VideoStreamSessionConfig config;
    config.encoder.width = 320;
    config.encoder.height = 180;
    config.encoder.bitrate_bps = 1'000'000;
    config.encoder.frame_rate = 30;
    config.geometry = MakeLandscapeCodedGeometry(config.encoder.width, config.encoder.height);
    config.stream_id = 1;
    config.generation = 3;
    std::unique_ptr<session::VideoStreamSession> stream =
            session::VideoStreamSession::Create(config, std::move(sink), &error);
    ASSERT_NE(stream, nullptr) << error;

    AHardwareBuffer_Desc description{};
    description.width = config.geometry.logical_width;
    description.height = config.geometry.logical_height;
    description.layers = 1;
    description.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    description.usage =
            AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    AHardwareBuffer* rawBuffer = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&description, &rawBuffer), 0);
    HardwareBufferPtr buffer(rawBuffer, AHardwareBuffer_release);
    uint64_t bufferId = 0;
    ASSERT_TRUE(stream->RegisterBuffer(buffer.get(), &bufferId, &error)) << error;

    android::base::unique_fd releaseFence;
    constexpr uint32_t kFrameCount = 10;
    for (uint32_t frame = 0; frame < kFrameCount; ++frame) {
        android::base::unique_fd acquireFence;
        ASSERT_TRUE(FillHardwareBuffer(buffer.get(), std::move(releaseFence), frame, &acquireFence,
                                       &error))
                << error;
        codec::FrameCopyResult copy =
                stream->SubmitFrame(bufferId, std::move(acquireFence),
                                    static_cast<int64_t>(frame + 1) * 33'333'333, &error);
        ASSERT_TRUE(copy.success) << error;
        releaseFence = std::move(copy.release_fence);
        while (true) {
            const session::VideoStreamDrainResult drain = stream->DrainOutput(0, &error);
            ASSERT_TRUE(drain.success) << error;
            if (!drain.made_progress) {
                break;
            }
        }
    }

    ASSERT_TRUE(stream->SignalEndOfInputStream(&error)) << error;
    bool endOfStream = false;
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!endOfStream && Clock::now() < deadline) {
        const session::VideoStreamDrainResult drain = stream->DrainOutput(100'000, &error);
        ASSERT_TRUE(drain.success) << error;
        endOfStream = drain.end_of_stream;
    }
    ASSERT_TRUE(endOfStream);
    EXPECT_GT(stream->output_stats().matched_frame_timestamps, 0u);
    EXPECT_GT(stream->output_stats().accepted_packets, 0u);
    EXPECT_GT(stream->transport_stats().accepted_packets, 0u);

    void* finalPixels = nullptr;
    ASSERT_EQ(AHardwareBuffer_lock(buffer.get(), AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
                                   releaseFence.release(), nullptr, &finalPixels),
              0);
    ASSERT_NE(finalPixels, nullptr);
    ASSERT_EQ(AHardwareBuffer_unlock(buffer.get(), nullptr), 0);
    stream->UnregisterBuffer(bufferId);
}

}  // namespace
}  // namespace floral::stream::codec
