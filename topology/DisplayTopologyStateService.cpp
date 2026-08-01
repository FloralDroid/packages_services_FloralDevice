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

#include "floral/device/display/topology/DisplayTopologyStateService.h"

#include <aidl/floral/device/display/topology/PhysicalDisplaySpec.h>

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace floral::device::display::topology {
namespace {

constexpr uint64_t kPrimaryDisplayId = 1;
constexpr uint32_t kMinimumDimension = 320;
constexpr uint32_t kMaximumDimension = 7680;
constexpr uint32_t kMinimumDpi = 72;
constexpr uint32_t kMaximumDpi = 640;
constexpr uint32_t kMaximumRefreshRateHz = 60;

}  // namespace

void DisplayTopologyStateService::DeathRecipientDeleter::operator()(
        AIBinder_DeathRecipient* recipient) const {
    if (recipient != nullptr) {
        AIBinder_DeathRecipient_delete(recipient);
    }
}

DisplayTopologyStateService::DisplayTopologyStateService()
    : listener_death_recipient_(AIBinder_DeathRecipient_new(&OnListenerBinderDied)) {}

TopologyUpdate DisplayTopologyStateService::ReplaceExternalDisplays(
        std::vector<ManagedPhysicalDisplay> displays) {
    const auto rejected = [this](TopologyUpdateResult result) {
        std::lock_guard lock(mutex_);
        return TopologyUpdate{result, generation_};
    };

    std::sort(displays.begin(), displays.end(),
              [](const ManagedPhysicalDisplay& left, const ManagedPhysicalDisplay& right) {
                  return left.display_id < right.display_id;
              });

    std::unordered_set<uint64_t> displayIds;
    std::unordered_set<uint8_t> ports;
    for (const ManagedPhysicalDisplay& display : displays) {
        if (!IsValid(display)) {
            return rejected(TopologyUpdateResult::kInvalidDisplay);
        }
        if (!displayIds.insert(display.display_id).second) {
            return rejected(TopologyUpdateResult::kDuplicateDisplayId);
        }
        if (!ports.insert(display.port).second) {
            return rejected(TopologyUpdateResult::kDuplicatePort);
        }
    }

    Snapshot snapshot;
    TopologyUpdate update;
    std::vector<std::shared_ptr<Listener>> listeners;
    {
        std::lock_guard lock(mutex_);
        if (displays.size() == displays_.size() &&
            std::equal(displays.begin(), displays.end(), displays_.begin(), SameDisplay)) {
            return {TopologyUpdateResult::kUnchanged, generation_};
        }
        displays_ = std::move(displays);
        if (generation_ == static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            generation_ = 1;
        } else {
            ++generation_;
        }
        snapshot = BuildSnapshot(generation_, displays_);
        update = {TopologyUpdateResult::kApplied, generation_};
        listeners = listeners_;
    }
    NotifyListeners(snapshot, listeners);
    return update;
}

ndk::ScopedAStatus DisplayTopologyStateService::getSnapshot(Snapshot* result) {
    if (result == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    std::lock_guard lock(mutex_);
    *result = BuildSnapshot(generation_, displays_);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus DisplayTopologyStateService::registerListener(
        const std::shared_ptr<Listener>& listener) {
    if (listener == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }

    Snapshot snapshot;
    {
        std::lock_guard lock(mutex_);
        const auto found = std::find_if(listeners_.begin(), listeners_.end(),
                                        [&listener](const std::shared_ptr<Listener>& candidate) {
                                            return SameBinder(candidate, listener);
                                        });
        if (found == listeners_.end()) {
            if (listener_death_recipient_ == nullptr) {
                return ndk::ScopedAStatus::fromStatus(STATUS_NO_MEMORY);
            }
            const binder_status_t linkStatus = AIBinder_linkToDeath(
                    listener->asBinder().get(), listener_death_recipient_.get(), this);
            // In-process test listeners are local binders and cannot die
            // independently from this service.
            if (linkStatus != STATUS_OK && linkStatus != STATUS_INVALID_OPERATION) {
                return ndk::ScopedAStatus::fromStatus(linkStatus);
            }
            listeners_.push_back(listener);
        }
        snapshot = BuildSnapshot(generation_, displays_);
    }
    if (!listener->onTopologyChanged(snapshot).isOk()) {
        RemoveListener(listener);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus DisplayTopologyStateService::unregisterListener(
        const std::shared_ptr<Listener>& listener) {
    if (listener == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    RemoveListener(listener);
    return ndk::ScopedAStatus::ok();
}

bool DisplayTopologyStateService::IsValid(const ManagedPhysicalDisplay& display) {
    if (display.display_id <= kPrimaryDisplayId ||
        display.display_id > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        display.port == 0 || display.width < kMinimumDimension ||
        display.width > kMaximumDimension || display.height < kMinimumDimension ||
        display.height > kMaximumDimension || display.dpi < kMinimumDpi ||
        display.dpi > kMaximumDpi || display.name.empty() ||
        display.supported_refresh_rates_hz.empty() || display.active_refresh_rate_hz == 0 ||
        display.active_refresh_rate_hz > kMaximumRefreshRateHz) {
        return false;
    }
    bool containsActiveRate = false;
    std::unordered_set<uint32_t> refreshRates;
    for (uint32_t refreshRate : display.supported_refresh_rates_hz) {
        if (refreshRate == 0 || refreshRate > kMaximumRefreshRateHz ||
            !refreshRates.insert(refreshRate).second) {
            return false;
        }
        containsActiveRate |= refreshRate == display.active_refresh_rate_hz;
    }
    return containsActiveRate;
}

bool DisplayTopologyStateService::SameDisplay(const ManagedPhysicalDisplay& left,
                                              const ManagedPhysicalDisplay& right) {
    return left.display_id == right.display_id && left.port == right.port &&
           left.width == right.width && left.height == right.height && left.dpi == right.dpi &&
           left.supported_refresh_rates_hz == right.supported_refresh_rates_hz &&
           left.active_refresh_rate_hz == right.active_refresh_rate_hz && left.name == right.name;
}

DisplayTopologyStateService::Snapshot DisplayTopologyStateService::BuildSnapshot(
        uint64_t generation, const std::vector<ManagedPhysicalDisplay>& displays) {
    Snapshot snapshot;
    snapshot.generation = static_cast<int64_t>(generation);
    snapshot.externalDisplays.reserve(displays.size());
    for (const ManagedPhysicalDisplay& display : displays) {
        aidl::floral::display::topology::PhysicalDisplaySpec aidlDisplay;
        aidlDisplay.displayId = static_cast<int64_t>(display.display_id);
        aidlDisplay.port = static_cast<int32_t>(display.port);
        aidlDisplay.width = static_cast<int32_t>(display.width);
        aidlDisplay.height = static_cast<int32_t>(display.height);
        aidlDisplay.dpi = static_cast<int32_t>(display.dpi);
        aidlDisplay.supportedRefreshRatesHz.assign(display.supported_refresh_rates_hz.begin(),
                                                   display.supported_refresh_rates_hz.end());
        aidlDisplay.activeRefreshRateHz = static_cast<int32_t>(display.active_refresh_rate_hz);
        aidlDisplay.name = display.name;
        snapshot.externalDisplays.push_back(std::move(aidlDisplay));
    }
    return snapshot;
}

bool DisplayTopologyStateService::SameBinder(const std::shared_ptr<Listener>& left,
                                             const std::shared_ptr<Listener>& right) {
    return left != nullptr && right != nullptr && left->asBinder().get() == right->asBinder().get();
}

void DisplayTopologyStateService::OnListenerBinderDied(void* cookie) {
    if (cookie != nullptr) {
        static_cast<DisplayTopologyStateService*>(cookie)->RemoveDeadListeners();
    }
}

void DisplayTopologyStateService::RemoveListener(const std::shared_ptr<Listener>& listener) {
    bool removed = false;
    {
        std::lock_guard lock(mutex_);
        const auto retained =
                std::remove_if(listeners_.begin(), listeners_.end(),
                               [&listener](const std::shared_ptr<Listener>& candidate) {
                                   return SameBinder(candidate, listener);
                               });
        removed = retained != listeners_.end();
        listeners_.erase(retained, listeners_.end());
    }
    if (removed && listener != nullptr && listener_death_recipient_ != nullptr) {
        (void)AIBinder_unlinkToDeath(listener->asBinder().get(), listener_death_recipient_.get(),
                                     this);
    }
}

void DisplayTopologyStateService::RemoveDeadListeners() {
    std::lock_guard lock(mutex_);
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [](const std::shared_ptr<Listener>& listener) {
                                        return listener == nullptr ||
                                               !AIBinder_isAlive(listener->asBinder().get());
                                    }),
                     listeners_.end());
}

void DisplayTopologyStateService::NotifyListeners(
        const Snapshot& snapshot, const std::vector<std::shared_ptr<Listener>>& listeners) {
    for (const std::shared_ptr<Listener>& listener : listeners) {
        if (listener == nullptr || !listener->onTopologyChanged(snapshot).isOk()) {
            RemoveListener(listener);
        }
    }
}

}  // namespace floral::device::display::topology
