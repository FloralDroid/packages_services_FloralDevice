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

#include "floral/device/display/topology/DisplayTopologyController.h"
#include "floral/device/display/topology/DisplayTopologyStateService.h"

#include <aidl/floral/device/display/topology/BnDisplayTopologyListener.h>
#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace floral::device::display::topology {
namespace {

class RecordingListener final
    : public aidl::floral::device::display::topology::BnDisplayTopologyListener {
  public:
    ndk::ScopedAStatus onTopologyChanged(
            const aidl::floral::device::display::topology::TopologySnapshot& snapshot) override {
        std::lock_guard lock(mutex_);
        snapshots_.push_back(snapshot);
        return ndk::ScopedAStatus::ok();
    }

    std::vector<aidl::floral::device::display::topology::TopologySnapshot> Snapshots() const {
        std::lock_guard lock(mutex_);
        return snapshots_;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<aidl::floral::device::display::topology::TopologySnapshot> snapshots_;
};

ManagedPhysicalDisplay ExternalDisplay(uint64_t id, uint8_t port) {
    ManagedPhysicalDisplay display;
    display.display_id = id;
    display.port = port;
    display.width = 1280;
    display.height = 720;
    display.dpi = 240;
    display.supported_refresh_rates_hz = {30, 60};
    display.active_refresh_rate_hz = 60;
    display.name = "Floral External Display " + std::to_string(id);
    return display;
}

TEST(DisplayTopologyStateServiceTest, SendsInitialAndReplacementSnapshots) {
    auto service = ndk::SharedRefBase::make<DisplayTopologyStateService>();
    DisplayTopologyController controller(service);
    auto listener = ndk::SharedRefBase::make<RecordingListener>();
    ASSERT_TRUE(service->registerListener(listener).isOk());

    auto snapshots = listener->Snapshots();
    ASSERT_EQ(snapshots.size(), 1u);
    EXPECT_EQ(snapshots[0].generation, 1);
    EXPECT_TRUE(snapshots[0].externalDisplays.empty());

    const TopologyUpdate update =
            controller.ReplaceExternalDisplays({ExternalDisplay(9, 2), ExternalDisplay(4, 1)});
    EXPECT_EQ(update.result, TopologyUpdateResult::kApplied);
    EXPECT_EQ(update.generation, 2u);
    snapshots = listener->Snapshots();
    ASSERT_EQ(snapshots.size(), 2u);
    EXPECT_EQ(snapshots[1].generation, 2);
    ASSERT_EQ(snapshots[1].externalDisplays.size(), 2u);
    EXPECT_EQ(snapshots[1].externalDisplays[0].displayId, 4);
    EXPECT_EQ(snapshots[1].externalDisplays[1].displayId, 9);
}

TEST(DisplayTopologyStateServiceTest, KeepsGenerationStableForIdenticalTopology) {
    auto service = ndk::SharedRefBase::make<DisplayTopologyStateService>();
    DisplayTopologyController controller(service);
    auto listener = ndk::SharedRefBase::make<RecordingListener>();
    ASSERT_TRUE(service->registerListener(listener).isOk());
    ASSERT_EQ(controller.ReplaceExternalDisplays({ExternalDisplay(2, 1)}).result,
              TopologyUpdateResult::kApplied);
    const TopologyUpdate unchanged = controller.ReplaceExternalDisplays({ExternalDisplay(2, 1)});
    EXPECT_EQ(unchanged.result, TopologyUpdateResult::kUnchanged);
    EXPECT_EQ(unchanged.generation, 2u);

    const auto snapshots = listener->Snapshots();
    ASSERT_EQ(snapshots.size(), 2u);
    EXPECT_EQ(snapshots.back().generation, 2);
}

TEST(DisplayTopologyStateServiceTest, RejectsPrimaryAndConflictingDisplays) {
    auto service = ndk::SharedRefBase::make<DisplayTopologyStateService>();
    DisplayTopologyController controller(service);
    EXPECT_EQ(controller.ReplaceExternalDisplays({ExternalDisplay(1, 1)}).result,
              TopologyUpdateResult::kInvalidDisplay);
    EXPECT_EQ(controller.ReplaceExternalDisplays({ExternalDisplay(2, 1), ExternalDisplay(2, 2)})
                      .result,
              TopologyUpdateResult::kDuplicateDisplayId);
    EXPECT_EQ(controller.ReplaceExternalDisplays({ExternalDisplay(2, 1), ExternalDisplay(3, 1)})
                      .result,
              TopologyUpdateResult::kDuplicatePort);

    aidl::floral::device::display::topology::TopologySnapshot snapshot;
    ASSERT_TRUE(service->getSnapshot(&snapshot).isOk());
    EXPECT_EQ(snapshot.generation, 1);
    EXPECT_TRUE(snapshot.externalDisplays.empty());
}

TEST(DisplayTopologyStateServiceTest, RejectsDuplicateRefreshRates) {
    auto service = ndk::SharedRefBase::make<DisplayTopologyStateService>();
    DisplayTopologyController controller(service);
    ManagedPhysicalDisplay display = ExternalDisplay(2, 1);
    display.supported_refresh_rates_hz = {60, 60};

    EXPECT_EQ(controller.ReplaceExternalDisplays({display}).result,
              TopologyUpdateResult::kInvalidDisplay);
}

TEST(DisplayTopologyStateServiceTest, StopsNotificationsAfterUnregister) {
    auto service = ndk::SharedRefBase::make<DisplayTopologyStateService>();
    DisplayTopologyController controller(service);
    auto listener = ndk::SharedRefBase::make<RecordingListener>();
    ASSERT_TRUE(service->registerListener(listener).isOk());
    ASSERT_TRUE(service->unregisterListener(listener).isOk());
    ASSERT_EQ(controller.ReplaceExternalDisplays({ExternalDisplay(2, 1)}).result,
              TopologyUpdateResult::kApplied);
    EXPECT_EQ(listener->Snapshots().size(), 1u);
}

}  // namespace
}  // namespace floral::device::display::topology
