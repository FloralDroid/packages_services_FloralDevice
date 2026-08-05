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

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>

namespace floral::device::socket {
namespace {

constexpr mode_t kSocketMode = 0660;

bool SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool PrepareSocketPath(const std::string& path, std::string* error) {
    struct stat status {};
    if (lstat(path.c_str(), &status) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        return SetError(error, std::string("lstat socket path failed: ") + std::strerror(errno));
    }
    if (!S_ISSOCK(status.st_mode)) {
        return SetError(error, "socket path already exists and is not a Unix socket");
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    android::base::unique_fd probe(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!probe.ok()) {
        return SetError(error,
                        std::string("creating Unix socket probe failed: ") + std::strerror(errno));
    }
    if (connect(probe.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
        return SetError(error, "Unix socket path already has an active listener");
    }
    if (errno != ECONNREFUSED && errno != ENOENT) {
        return SetError(error,
                        std::string("probing Unix socket path failed: ") + std::strerror(errno));
    }
    if (unlink(path.c_str()) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        return SetError(error,
                        std::string("removing stale socket failed: ") + std::strerror(errno));
    }
    return true;
}

}  // namespace

std::unique_ptr<UnixSocketServer> UnixSocketServer::Create(const std::string& path, int backlog,
                                                           std::string* error) {
    sockaddr_un address{};
    if (path.empty() || path.size() >= sizeof(address.sun_path)) {
        SetError(error, "Unix socket path is empty or too long");
        return nullptr;
    }
    if (backlog <= 0) {
        SetError(error, "Unix socket backlog must be positive");
        return nullptr;
    }
    if (!PrepareSocketPath(path, error)) {
        return nullptr;
    }

    android::base::unique_fd socketFd(
            ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    if (!socketFd.ok()) {
        SetError(error, std::string("socket(AF_UNIX) failed: ") + std::strerror(errno));
        return nullptr;
    }
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (bind(socketFd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        SetError(error, std::string("binding Unix socket failed: ") + std::strerror(errno));
        return nullptr;
    }
    if (chmod(path.c_str(), kSocketMode) != 0) {
        const int savedErrno = errno;
        unlink(path.c_str());
        SetError(error, std::string("setting Unix socket permissions failed: ") +
                                std::strerror(savedErrno));
        return nullptr;
    }
    if (listen(socketFd.get(), backlog) != 0) {
        const int savedErrno = errno;
        unlink(path.c_str());
        SetError(error,
                 std::string("listening on Unix socket failed: ") + std::strerror(savedErrno));
        return nullptr;
    }

    struct stat status {};
    if (lstat(path.c_str(), &status) != 0) {
        const int savedErrno = errno;
        unlink(path.c_str());
        SetError(error,
                 std::string("reading bound Unix socket failed: ") + std::strerror(savedErrno));
        return nullptr;
    }
    return std::unique_ptr<UnixSocketServer>(
            new UnixSocketServer(path, std::move(socketFd), static_cast<uint64_t>(status.st_dev),
                                 static_cast<uint64_t>(status.st_ino)));
}

UnixSocketServer::UnixSocketServer(std::string path, android::base::unique_fd socket,
                                   uint64_t pathDevice, uint64_t pathInode)
    : path_(std::move(path)),
      socket_(std::move(socket)),
      path_device_(pathDevice),
      path_inode_(pathInode) {}

UnixSocketServer::~UnixSocketServer() {
    struct stat status {};
    if (lstat(path_.c_str(), &status) == 0 && S_ISSOCK(status.st_mode) &&
        static_cast<uint64_t>(status.st_dev) == path_device_ &&
        static_cast<uint64_t>(status.st_ino) == path_inode_) {
        unlink(path_.c_str());
    }
}

android::base::unique_fd UnixSocketServer::Accept(std::chrono::milliseconds timeout,
                                                  std::string* error) const {
    const int64_t timeoutCount = timeout.count();
    const int timeoutMilliseconds = timeoutCount > std::numeric_limits<int>::max()
                                            ? std::numeric_limits<int>::max()
                                            : static_cast<int>(std::max<int64_t>(timeoutCount, 0));
    pollfd descriptor{};
    descriptor.fd = socket_.get();
    descriptor.events = POLLIN;
    int result = 0;
    do {
        result = poll(&descriptor, 1, timeoutMilliseconds);
    } while (result < 0 && errno == EINTR);
    if (result == 0) {
        return {};
    }
    if (result < 0) {
        SetError(error,
                 std::string("polling Unix socket listener failed: ") + std::strerror(errno));
        return {};
    }
    if ((descriptor.revents & POLLIN) == 0) {
        SetError(error, "Unix socket listener became unavailable");
        return {};
    }

    // Connected streams remain blocking: the existing bounded sender queues
    // keep media threads isolated while preserving complete packet writes.
    android::base::unique_fd accepted(accept4(socket_.get(), nullptr, nullptr, SOCK_CLOEXEC));
    if (!accepted.ok() && errno != EAGAIN && errno != EWOULDBLOCK) {
        SetError(error,
                 std::string("accepting Unix socket connection failed: ") + std::strerror(errno));
    }
    return accepted;
}

const std::string& UnixSocketServer::path() const {
    return path_;
}

}  // namespace floral::device::socket
