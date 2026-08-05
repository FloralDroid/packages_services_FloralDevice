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

#include <android-base/unique_fd.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace floral::device::socket {

// Owns one filesystem Unix stream listener. Creation only replaces an existing
// socket node; regular files and other filesystem objects are never removed.
class UnixSocketServer final {
  public:
    static std::unique_ptr<UnixSocketServer> Create(const std::string& path, int backlog,
                                                    std::string* error);

    ~UnixSocketServer();

    UnixSocketServer(const UnixSocketServer&) = delete;
    UnixSocketServer& operator=(const UnixSocketServer&) = delete;

    android::base::unique_fd Accept(std::chrono::milliseconds timeout, std::string* error) const;
    const std::string& path() const;

  private:
    UnixSocketServer(std::string path, android::base::unique_fd socket, uint64_t pathDevice,
                     uint64_t pathInode);

    const std::string path_;
    const android::base::unique_fd socket_;
    const uint64_t path_device_;
    const uint64_t path_inode_;
};

}  // namespace floral::device::socket
