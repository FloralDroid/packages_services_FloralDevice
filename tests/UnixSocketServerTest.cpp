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

#include "floral/device/socket/UnixSocketServer.h"

#include <android-base/file.h>
#include <android-base/unique_fd.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>

namespace floral::device::socket {
namespace {

TEST(UnixSocketServerTest, AcceptsHostConnectionAndRemovesOwnedNode) {
    TemporaryDir directory;
    ASSERT_NE(directory.path[0], '\0');
    const std::string path = std::string(directory.path) + "/video.sock";

    std::string error;
    std::unique_ptr<UnixSocketServer> server = UnixSocketServer::Create(path, 1, &error);
    ASSERT_NE(server, nullptr) << error;

    struct stat status {};
    ASSERT_EQ(lstat(path.c_str(), &status), 0);
    EXPECT_TRUE(S_ISSOCK(status.st_mode));
    EXPECT_EQ(status.st_mode & 0777, 0660u);

    android::base::unique_fd client(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    ASSERT_TRUE(client.ok());
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    ASSERT_EQ(connect(client.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)),
              0);

    android::base::unique_fd accepted = server->Accept(std::chrono::milliseconds(100), &error);
    ASSERT_TRUE(accepted.ok()) << error;
    server.reset();
    EXPECT_NE(lstat(path.c_str(), &status), 0);
    EXPECT_EQ(errno, ENOENT);
}

TEST(UnixSocketServerTest, PreservesExistingRegularFile) {
    TemporaryDir directory;
    ASSERT_NE(directory.path[0], '\0');
    const std::string path = std::string(directory.path) + "/control.sock";
    ASSERT_TRUE(android::base::WriteStringToFile("keep", path));

    std::string error;
    EXPECT_EQ(UnixSocketServer::Create(path, 1, &error), nullptr);
    std::string content;
    EXPECT_TRUE(android::base::ReadFileToString(path, &content));
    EXPECT_EQ(content, "keep");
}

}  // namespace
}  // namespace floral::device::socket
