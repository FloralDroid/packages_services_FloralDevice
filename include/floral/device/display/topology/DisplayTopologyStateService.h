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

#include "floral/device/display/topology/DisplayTopologyTypes.h"

#include <aidl/floral/device/display/topology/BnDisplayTopologyState.h>
#include <aidl/floral/device/display/topology/IDisplayTopologyListener.h>
#include <aidl/floral/device/display/topology/TopologySnapshot.h>
#include <android/binder_ibinder.h>

#include <memory>
#include <mutex>
#include <vector>

namespace floral::device::display::topology {

class DisplayTopologyController;

class DisplayTopologyStateService final
    : public aidl::floral::display::topology::BnDisplayTopologyState {
  public:
    DisplayTopologyStateService();
    ~DisplayTopologyStateService() override = default;

    ndk::ScopedAStatus getSnapshot(
            aidl::floral::display::topology::TopologySnapshot* result) override;
    ndk::ScopedAStatus registerListener(
            const std::shared_ptr<aidl::floral::display::topology::IDisplayTopologyListener>&
                    listener) override;
    ndk::ScopedAStatus unregisterListener(
            const std::shared_ptr<aidl::floral::display::topology::IDisplayTopologyListener>&
                    listener) override;

  private:
    friend class DisplayTopologyController;

    using Listener = aidl::floral::display::topology::IDisplayTopologyListener;
    using Snapshot = aidl::floral::display::topology::TopologySnapshot;

    struct DeathRecipientDeleter {
        void operator()(AIBinder_DeathRecipient* recipient) const;
    };

    TopologyUpdate ReplaceExternalDisplays(std::vector<ManagedPhysicalDisplay> displays);

    static bool IsValid(const ManagedPhysicalDisplay& display);
    static bool SameDisplay(const ManagedPhysicalDisplay& left,
                            const ManagedPhysicalDisplay& right);
    static Snapshot BuildSnapshot(uint64_t generation,
                                  const std::vector<ManagedPhysicalDisplay>& displays);
    static bool SameBinder(const std::shared_ptr<Listener>& left,
                           const std::shared_ptr<Listener>& right);
    static void OnListenerBinderDied(void* cookie);

    void RemoveListener(const std::shared_ptr<Listener>& listener);
    void RemoveDeadListeners();
    void NotifyListeners(const Snapshot& snapshot,
                         const std::vector<std::shared_ptr<Listener>>& listeners);

    // Desired topology is independent of any external client transport.
    mutable std::mutex mutex_;
    uint64_t generation_ = 1;
    std::vector<ManagedPhysicalDisplay> displays_;
    std::vector<std::shared_ptr<Listener>> listeners_;
    std::unique_ptr<AIBinder_DeathRecipient, DeathRecipientDeleter> listener_death_recipient_;
};

}  // namespace floral::device::display::topology
