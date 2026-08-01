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
#include <functional>
#include <memory>
#include <string>

namespace floral::stream::control {

class ControlRequestHandler;

using ControlSocketConnector =
        std::function<android::base::unique_fd(const std::string& socket_path, std::string* error)>;

struct HostControlChannelConfig {
    std::string socket_path;
    std::chrono::milliseconds reconnect_interval{250};
    std::chrono::milliseconds authority_lease{3000};
    ControlSocketConnector connector;
};

class HostControlChannel final {
  public:
    static std::unique_ptr<HostControlChannel> Create(
            HostControlChannelConfig config, std::shared_ptr<ControlRequestHandler> handler,
            std::string* error);

    ~HostControlChannel();

    HostControlChannel(const HostControlChannel&) = delete;
    HostControlChannel& operator=(const HostControlChannel&) = delete;

    bool connected() const;

  private:
    class Impl;

    explicit HostControlChannel(std::unique_ptr<Impl> impl);

    const std::unique_ptr<Impl> impl_;
};

}  // namespace floral::stream::control
