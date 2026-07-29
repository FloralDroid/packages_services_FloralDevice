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

#include "floral/stream/session/VideoOutputController.h"
#include "floral/stream/transport/EncodedPacketAdapter.h"
#include "floral/stream/transport/HostVideoSink.h"
#include "floral/stream/transport/VideoPacketProtocol.h"

#include <android-base/unique_fd.h>
#include <gtest/gtest.h>
#include <media/NdkMediaCodec.h>
#include <poll.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace floral::stream::transport {
namespace {

bool ReceiveAll(int socketFd, uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        pollfd descriptor{};
        descriptor.fd = socketFd;
        descriptor.events = POLLIN;
        int pollResult = 0;
        do {
            pollResult = poll(&descriptor, 1, 5'000);
        } while (pollResult < 0 && errno == EINTR);
        if (pollResult <= 0) {
            return false;
        }

        const ssize_t received = recv(socketFd, data + offset, size - offset, 0);
        if (received > 0) {
            offset += static_cast<size_t>(received);
            continue;
        }
        if (received < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

std::array<android::base::unique_fd, 2> CreateSocketPair() {
    int sockets[2] = {-1, -1};
    EXPECT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets), 0);
    return {android::base::unique_fd(sockets[0]), android::base::unique_fd(sockets[1])};
}

HostVideoPacket CreatePacket(size_t payloadSize) {
    HostVideoPacket packet;
    packet.header.coded_width = 1920;
    packet.header.coded_height = 1080;
    packet.header.logical_width = 1920;
    packet.header.logical_height = 1080;
    packet.payload.resize(payloadSize, 0x5a);
    return packet;
}

VideoGeometry CreateIdentityGeometry(uint32_t width, uint32_t height) {
    VideoGeometry geometry;
    geometry.logical_width = width;
    geometry.logical_height = height;
    geometry.coded_width = width;
    geometry.coded_height = height;
    return geometry;
}

codec::EncodedPacket CreateEncodedPacket(std::vector<uint8_t> data, int64_t presentationTimeUs,
                                         uint32_t flags) {
    codec::EncodedPacket packet;
    packet.data = std::move(data);
    packet.presentation_time_us = presentationTimeUs;
    packet.flags = flags;
    return packet;
}

VideoPacketHeader ReceivePacket(int socketFd, std::vector<uint8_t>* payload) {
    SerializedVideoPacketHeader serialized{};
    EXPECT_TRUE(ReceiveAll(socketFd, serialized.data(), serialized.size()));
    VideoPacketHeader header;
    std::string error;
    EXPECT_TRUE(ParseVideoPacketHeader(serialized, &header, &error)) << error;
    payload->resize(header.payload_size);
    EXPECT_TRUE(ReceiveAll(socketFd, payload->data(), payload->size()));
    return header;
}

TEST(VideoGeometryTest, UsesLandscapeCodedSurfaceForPortraitDisplay) {
    const VideoGeometry portrait = MakeLandscapeCodedGeometry(720, 1280);
    EXPECT_EQ(portrait.logical_width, 720u);
    EXPECT_EQ(portrait.logical_height, 1280u);
    EXPECT_EQ(portrait.coded_width, 1280u);
    EXPECT_EQ(portrait.coded_height, 720u);
    EXPECT_EQ(portrait.display_rotation, DisplayRotation::k90);
    EXPECT_TRUE(HasMatchingDisplayAspect(portrait));

    const VideoGeometry landscape = MakeLandscapeCodedGeometry(1920, 1080);
    EXPECT_EQ(landscape.coded_width, 1920u);
    EXPECT_EQ(landscape.coded_height, 1080u);
    EXPECT_EQ(landscape.display_rotation, DisplayRotation::k0);
    EXPECT_TRUE(HasMatchingDisplayAspect(landscape));
}

TEST(VideoPacketProtocolTest, SerializesStableNetworkByteOrderHeader) {
    VideoPacketHeader header;
    header.stream_id = 0x01020304;
    header.generation = 0x05060708;
    header.sequence = 0x0102030405060708ULL;
    header.presentation_time_us = 1'234'567;
    header.flags = kVideoPacketKeyFrame | kVideoPacketDiscontinuity;
    header.codec = VideoCodec::kH264;
    header.coded_width = 1280;
    header.coded_height = 720;
    header.logical_width = 720;
    header.logical_height = 1280;
    header.display_rotation = DisplayRotation::k90;
    header.payload_size = 4096;
    header.frame_submit_time_ns = 0x1112131415161718ULL;

    SerializedVideoPacketHeader serialized{};
    std::string error;
    ASSERT_TRUE(SerializeVideoPacketHeader(header, &serialized, &error)) << error;
    EXPECT_EQ(serialized[0], 'F');
    EXPECT_EQ(serialized[1], 'S');
    EXPECT_EQ(serialized[2], 'V');
    EXPECT_EQ(serialized[3], '2');
    EXPECT_EQ(serialized[4], 0);
    EXPECT_EQ(serialized[5], kVideoPacketVersion);
    EXPECT_EQ(serialized[6], 0);
    EXPECT_EQ(serialized[7], kVideoPacketHeaderSize);
    EXPECT_EQ(serialized[56], 0);
    EXPECT_EQ(serialized[57], 0);
    EXPECT_EQ(serialized[58], 0x10);
    EXPECT_EQ(serialized[59], 0);
    EXPECT_EQ(serialized[60], 0);
    EXPECT_EQ(serialized[61], 0);
    EXPECT_EQ(serialized[62], 0);
    EXPECT_EQ(serialized[63], 90);
    EXPECT_EQ(serialized[64], 0x11);
    EXPECT_EQ(serialized[65], 0x12);
    EXPECT_EQ(serialized[66], 0x13);
    EXPECT_EQ(serialized[67], 0x14);
    EXPECT_EQ(serialized[68], 0x15);
    EXPECT_EQ(serialized[69], 0x16);
    EXPECT_EQ(serialized[70], 0x17);
    EXPECT_EQ(serialized[71], 0x18);
    EXPECT_EQ(serialized[72], 0);
    EXPECT_EQ(serialized[79], 0);

    VideoPacketHeader parsed;
    ASSERT_TRUE(ParseVideoPacketHeader(serialized, &parsed, &error)) << error;
    EXPECT_EQ(parsed.stream_id, header.stream_id);
    EXPECT_EQ(parsed.generation, header.generation);
    EXPECT_EQ(parsed.sequence, header.sequence);
    EXPECT_EQ(parsed.presentation_time_us, header.presentation_time_us);
    EXPECT_EQ(parsed.flags, header.flags);
    EXPECT_EQ(parsed.codec, header.codec);
    EXPECT_EQ(parsed.coded_width, header.coded_width);
    EXPECT_EQ(parsed.coded_height, header.coded_height);
    EXPECT_EQ(parsed.logical_width, header.logical_width);
    EXPECT_EQ(parsed.logical_height, header.logical_height);
    EXPECT_EQ(parsed.display_rotation, header.display_rotation);
    EXPECT_EQ(parsed.payload_size, header.payload_size);
    EXPECT_EQ(parsed.frame_submit_time_ns, header.frame_submit_time_ns);
}

TEST(VideoPacketProtocolTest, RejectsRotationThatDoesNotMatchCodedAspect) {
    VideoPacketHeader header;
    header.coded_width = 1280;
    header.coded_height = 720;
    header.logical_width = 720;
    header.logical_height = 1280;
    header.display_rotation = DisplayRotation::k0;

    SerializedVideoPacketHeader serialized{};
    std::string error;
    EXPECT_FALSE(SerializeVideoPacketHeader(header, &serialized, &error));
    EXPECT_FALSE(error.empty());
}

TEST(EncodedPacketAdapterTest, MapsMediaCodecFlagsWithoutCopyingProtocolMetadata) {
    codec::EncodedPacket encoded;
    encoded.data = {0, 0, 0, 1, 0x65};
    encoded.presentation_time_us = 99;
    encoded.flags = kMediaCodecKeyFrameFlag | AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;

    EncodedStreamInfo streamInfo;
    streamInfo.stream_id = 3;
    streamInfo.generation = 4;
    streamInfo.sequence = 5;
    streamInfo.geometry = MakeLandscapeCodedGeometry(720, 1280);
    streamInfo.additional_flags = kVideoPacketDiscontinuity;
    streamInfo.frame_submit_time_ns = 123'456'789;
    HostVideoPacket hostPacket = MakeHostVideoPacket(std::move(encoded), streamInfo);

    EXPECT_EQ(hostPacket.header.stream_id, 3u);
    EXPECT_EQ(hostPacket.header.generation, 4u);
    EXPECT_EQ(hostPacket.header.sequence, 5u);
    EXPECT_EQ(hostPacket.header.presentation_time_us, 99);
    EXPECT_EQ(hostPacket.header.coded_width, 1280u);
    EXPECT_EQ(hostPacket.header.coded_height, 720u);
    EXPECT_EQ(hostPacket.header.logical_width, 720u);
    EXPECT_EQ(hostPacket.header.logical_height, 1280u);
    EXPECT_EQ(hostPacket.header.display_rotation, DisplayRotation::k90);
    EXPECT_EQ(hostPacket.header.frame_submit_time_ns, 123'456'789u);
    EXPECT_NE(hostPacket.header.flags & kVideoPacketKeyFrame, 0u);
    EXPECT_NE(hostPacket.header.flags & kVideoPacketEndOfStream, 0u);
    EXPECT_NE(hostPacket.header.flags & kVideoPacketDiscontinuity, 0u);
    EXPECT_EQ(hostPacket.payload, (std::vector<uint8_t>{0, 0, 0, 1, 0x65}));
}

TEST(HostVideoSinkTest, SendsHeaderAndAccessUnitOverStreamSocket) {
    auto sockets = CreateSocketPair();
    HostVideoSinkConfig config;
    std::string error;
    std::unique_ptr<HostVideoSink> sink =
            HostVideoSink::CreateFromConnectedSocket(std::move(sockets[0]), config, &error);
    ASSERT_NE(sink, nullptr) << error;

    HostVideoPacket packet;
    packet.header.stream_id = 7;
    packet.header.generation = 2;
    packet.header.sequence = 19;
    packet.header.presentation_time_us = 33'333;
    packet.header.flags = kVideoPacketKeyFrame;
    packet.header.coded_width = 1280;
    packet.header.coded_height = 720;
    packet.header.logical_width = 720;
    packet.header.logical_height = 1280;
    packet.header.display_rotation = DisplayRotation::k90;
    packet.header.frame_submit_time_ns = 9'876'543'210;
    packet.payload = {0, 0, 0, 1, 0x65, 1, 2, 3};
    ASSERT_EQ(sink->Submit(std::move(packet), &error).status, VideoEnqueueStatus::kAccepted)
            << error;

    SerializedVideoPacketHeader serialized{};
    ASSERT_TRUE(ReceiveAll(sockets[1].get(), serialized.data(), serialized.size()));
    VideoPacketHeader receivedHeader;
    ASSERT_TRUE(ParseVideoPacketHeader(serialized, &receivedHeader, &error)) << error;
    std::vector<uint8_t> payload(receivedHeader.payload_size);
    ASSERT_TRUE(ReceiveAll(sockets[1].get(), payload.data(), payload.size()));

    EXPECT_EQ(receivedHeader.stream_id, 7u);
    EXPECT_EQ(receivedHeader.generation, 2u);
    EXPECT_EQ(receivedHeader.sequence, 19u);
    EXPECT_EQ(receivedHeader.presentation_time_us, 33'333);
    EXPECT_EQ(receivedHeader.flags, kVideoPacketKeyFrame);
    EXPECT_EQ(receivedHeader.coded_width, 1280u);
    EXPECT_EQ(receivedHeader.coded_height, 720u);
    EXPECT_EQ(receivedHeader.logical_width, 720u);
    EXPECT_EQ(receivedHeader.logical_height, 1280u);
    EXPECT_EQ(receivedHeader.display_rotation, DisplayRotation::k90);
    EXPECT_EQ(receivedHeader.frame_submit_time_ns, 9'876'543'210u);
    EXPECT_EQ(payload, (std::vector<uint8_t>{0, 0, 0, 1, 0x65, 1, 2, 3}));

    sink->Stop();
    const HostVideoSinkStats stats = sink->GetStats();
    EXPECT_EQ(stats.accepted_packets, 1u);
    EXPECT_EQ(stats.sent_packets, 1u);
    EXPECT_EQ(stats.sent_bytes, kVideoPacketHeaderSize + payload.size());
    EXPECT_EQ(stats.buffered_packets, 0u);
}

TEST(HostVideoSinkTest, RejectsOversizedPacketWithoutBlockingCaller) {
    auto sockets = CreateSocketPair();
    HostVideoSinkConfig config;
    config.max_buffered_packets = 1;
    config.max_buffered_bytes = kVideoPacketHeaderSize + 3;
    std::string error;
    std::unique_ptr<HostVideoSink> sink =
            HostVideoSink::CreateFromConnectedSocket(std::move(sockets[0]), config, &error);
    ASSERT_NE(sink, nullptr) << error;

    HostVideoPacket packet;
    packet.header.coded_width = 640;
    packet.header.coded_height = 360;
    packet.header.logical_width = 640;
    packet.header.logical_height = 360;
    packet.payload = {1, 2, 3, 4};
    const VideoEnqueueResult result = sink->Submit(std::move(packet), &error);

    EXPECT_EQ(result.status, VideoEnqueueStatus::kQueueFull);
    EXPECT_TRUE(result.request_key_frame);
    const HostVideoSinkStats stats = sink->GetStats();
    EXPECT_EQ(stats.accepted_packets, 0u);
    EXPECT_EQ(stats.rejected_queue_full, 1u);
    EXPECT_EQ(stats.buffered_packets, 0u);
}

TEST(HostVideoSinkTest, ReportsBackpressureWhileSenderIsBlocked) {
    auto sockets = CreateSocketPair();
    ASSERT_TRUE(sockets[0].ok());
    ASSERT_TRUE(sockets[1].ok());
    int sendBufferSize = 4 * 1024;
    ASSERT_EQ(setsockopt(sockets[0].get(), SOL_SOCKET, SO_SNDBUF, &sendBufferSize,
                         sizeof(sendBufferSize)),
              0);

    HostVideoSinkConfig config;
    config.max_buffered_packets = 1;
    config.max_buffered_bytes = 2 * 1024 * 1024;
    std::string error;
    std::unique_ptr<HostVideoSink> sink =
            HostVideoSink::CreateFromConnectedSocket(std::move(sockets[0]), config, &error);
    ASSERT_NE(sink, nullptr) << error;

    ASSERT_EQ(sink->Submit(CreatePacket(1024 * 1024), &error).status, VideoEnqueueStatus::kAccepted)
            << error;

    const auto start = std::chrono::steady_clock::now();
    const VideoEnqueueResult result = sink->Submit(CreatePacket(1), &error);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(result.status, VideoEnqueueStatus::kQueueFull);
    EXPECT_TRUE(result.request_key_frame);
    EXPECT_LT(elapsed, std::chrono::milliseconds(100));
    EXPECT_EQ(sink->GetStats().rejected_queue_full, 1u);
}

TEST(HostVideoSinkTest, RejectsBatchAtomicallyWhenItExceedsPacketBudget) {
    auto sockets = CreateSocketPair();
    HostVideoSinkConfig config;
    config.max_buffered_packets = 1;
    std::string error;
    std::unique_ptr<HostVideoSink> sink =
            HostVideoSink::CreateFromConnectedSocket(std::move(sockets[0]), config, &error);
    ASSERT_NE(sink, nullptr) << error;

    std::vector<HostVideoPacket> packets;
    packets.push_back(CreatePacket(1));
    packets.push_back(CreatePacket(1));
    const VideoEnqueueResult result = sink->SubmitBatch(std::move(packets), &error);

    EXPECT_EQ(result.status, VideoEnqueueStatus::kQueueFull);
    EXPECT_TRUE(result.request_key_frame);
    EXPECT_EQ(sink->GetStats().accepted_packets, 0u);
    pollfd descriptor{};
    descriptor.fd = sockets[1].get();
    descriptor.events = POLLIN;
    EXPECT_EQ(poll(&descriptor, 1, 20), 0);
}

TEST(VideoOutputControllerTest, SendsCachedCodecConfigWithInitialKeyFrameAndTimestamp) {
    auto sockets = CreateSocketPair();
    std::string error;
    std::unique_ptr<HostVideoSink> sink = HostVideoSink::CreateFromConnectedSocket(
            std::move(sockets[0]), HostVideoSinkConfig{}, &error);
    ASSERT_NE(sink, nullptr) << error;

    session::VideoOutputConfig config;
    config.stream_id = 4;
    config.generation = 2;
    config.geometry = MakeLandscapeCodedGeometry(720, 1280);
    session::VideoOutputController controller(config);
    controller.RecordFrameSubmission(1'000, 55'000);

    codec::DequeueResult codecConfig;
    codecConfig.status = codec::DequeueStatus::kPacket;
    codecConfig.packet =
            CreateEncodedPacket({0, 0, 0, 1, 0x67}, 0, AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG);
    session::VideoOutputProcessResult configResult =
            controller.Process(std::move(codecConfig), sink.get(), &error);
    ASSERT_TRUE(configResult.success) << error;
    EXPECT_EQ(controller.state(), session::VideoOutputState::kStarting);

    codec::DequeueResult keyFrame;
    keyFrame.status = codec::DequeueStatus::kPacket;
    keyFrame.packet = CreateEncodedPacket({0, 0, 0, 1, 0x65}, 1'000, kMediaCodecKeyFrameFlag);
    session::VideoOutputProcessResult keyResult =
            controller.Process(std::move(keyFrame), sink.get(), &error);
    ASSERT_TRUE(keyResult.success) << error;
    EXPECT_FALSE(keyResult.request_key_frame);
    EXPECT_EQ(controller.state(), session::VideoOutputState::kStreaming);

    std::vector<uint8_t> codecPayload;
    const VideoPacketHeader codecHeader = ReceivePacket(sockets[1].get(), &codecPayload);
    std::vector<uint8_t> keyPayload;
    const VideoPacketHeader keyHeader = ReceivePacket(sockets[1].get(), &keyPayload);
    EXPECT_NE(codecHeader.flags & kVideoPacketCodecConfig, 0u);
    EXPECT_EQ(codecHeader.sequence, 0u);
    EXPECT_EQ(codecHeader.frame_submit_time_ns, 0u);
    EXPECT_NE(keyHeader.flags & kVideoPacketKeyFrame, 0u);
    EXPECT_EQ(keyHeader.flags & kVideoPacketDiscontinuity, 0u);
    EXPECT_EQ(keyHeader.sequence, 1u);
    EXPECT_EQ(keyHeader.frame_submit_time_ns, 55'000u);
    EXPECT_EQ(keyHeader.coded_width, 1280u);
    EXPECT_EQ(keyHeader.coded_height, 720u);
    EXPECT_EQ(keyHeader.logical_width, 720u);
    EXPECT_EQ(keyHeader.logical_height, 1280u);
    EXPECT_EQ(keyHeader.display_rotation, DisplayRotation::k90);
    EXPECT_EQ(controller.stats().matched_frame_timestamps, 1u);
}

TEST(VideoOutputControllerTest, DropsDependentFramesAndRecoversAtDiscontinuousKeyFrame) {
    auto sockets = CreateSocketPair();
    HostVideoSinkConfig sinkConfig;
    sinkConfig.max_buffered_packets = 2;
    sinkConfig.max_buffered_bytes = kVideoPacketHeaderSize + 4;
    std::string error;
    std::unique_ptr<HostVideoSink> sink =
            HostVideoSink::CreateFromConnectedSocket(std::move(sockets[0]), sinkConfig, &error);
    ASSERT_NE(sink, nullptr) << error;

    session::VideoOutputConfig config;
    config.geometry = CreateIdentityGeometry(640, 360);
    session::VideoOutputController controller(config);

    codec::DequeueResult initialKey;
    initialKey.status = codec::DequeueStatus::kPacket;
    initialKey.packet = CreateEncodedPacket({0x65}, 1, kMediaCodecKeyFrameFlag);
    ASSERT_TRUE(controller.Process(std::move(initialKey), sink.get(), &error).success) << error;
    std::vector<uint8_t> initialPayload;
    EXPECT_EQ(ReceivePacket(sockets[1].get(), &initialPayload).sequence, 0u);

    codec::DequeueResult oversizedFrame;
    oversizedFrame.status = codec::DequeueStatus::kPacket;
    oversizedFrame.packet = CreateEncodedPacket({1, 2, 3, 4, 5}, 2, 0);
    const session::VideoOutputProcessResult overflow =
            controller.Process(std::move(oversizedFrame), sink.get(), &error);
    EXPECT_TRUE(overflow.success);
    EXPECT_TRUE(overflow.request_key_frame);
    EXPECT_EQ(controller.state(), session::VideoOutputState::kRecovering);

    codec::DequeueResult dependentFrame;
    dependentFrame.status = codec::DequeueStatus::kPacket;
    dependentFrame.packet = CreateEncodedPacket({1}, 3, 0);
    const session::VideoOutputProcessResult dropped =
            controller.Process(std::move(dependentFrame), sink.get(), &error);
    EXPECT_TRUE(dropped.success);
    EXPECT_FALSE(dropped.request_key_frame);

    codec::DequeueResult recoveryKey;
    recoveryKey.status = codec::DequeueStatus::kPacket;
    recoveryKey.packet = CreateEncodedPacket({0x65}, 4, kMediaCodecKeyFrameFlag);
    const session::VideoOutputProcessResult recovered =
            controller.Process(std::move(recoveryKey), sink.get(), &error);
    ASSERT_TRUE(recovered.success) << error;
    EXPECT_EQ(controller.state(), session::VideoOutputState::kStreaming);

    std::vector<uint8_t> recoveryPayload;
    const VideoPacketHeader recoveryHeader = ReceivePacket(sockets[1].get(), &recoveryPayload);
    EXPECT_EQ(recoveryHeader.sequence, 2u);
    EXPECT_NE(recoveryHeader.flags & kVideoPacketDiscontinuity, 0u);
    EXPECT_EQ(controller.stats().recovery_events, 1u);
    EXPECT_EQ(controller.stats().key_frame_requests, 1u);
    EXPECT_EQ(controller.stats().dropped_packets, 2u);
}

}  // namespace
}  // namespace floral::stream::transport
