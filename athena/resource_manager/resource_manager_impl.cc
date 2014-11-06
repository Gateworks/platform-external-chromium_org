// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "athena/resource_manager/public/resource_manager.h"

#include <algorithm>
#include <vector>

#include "athena/activity/public/activity.h"
#include "athena/activity/public/activity_manager.h"
#include "athena/activity/public/activity_manager_observer.h"
#include "athena/resource_manager/memory_pressure_notifier.h"
#include "athena/resource_manager/public/resource_manager_delegate.h"
#include "athena/wm/public/window_list_provider.h"
#include "athena/wm/public/window_list_provider_observer.h"
#include "athena/wm/public/window_manager.h"
#include "athena/wm/public/window_manager_observer.h"
#include "base/containers/adapters.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#include "ui/aura/window.h"

namespace athena {
namespace {

class ResourceManagerImpl : public ResourceManager,
                            public WindowManagerObserver,
                            public ActivityManagerObserver,
                            public MemoryPressureObserver,
                            public WindowListProviderObserver {
 public:
  ResourceManagerImpl(ResourceManagerDelegate* delegate);
  ~ResourceManagerImpl() override;

  // ResourceManager:
  virtual void SetMemoryPressureAndStopMonitoring(
      MemoryPressure pressure) override;
  virtual void SetWaitTimeBetweenResourceManageCalls(int time_in_ms) override {
    wait_time_for_resource_deallocation_ =
        base::TimeDelta::FromMilliseconds(time_in_ms);
    // Reset the timeout to force the next resource call to execute immediately.
    next_resource_management_time_ = base::Time::Now();
  }

  virtual void Pause(bool pause) override {
    if (pause) {
      if (!pause_)
        queued_command_ = false;
      ++pause_;
    } else {
      DCHECK(pause_);
      --pause_;
      if (!pause && queued_command_)
        ManageResource();
    }
  }

  // ActivityManagerObserver:
  virtual void OnActivityStarted(Activity* activity) override;
  virtual void OnActivityEnding(Activity* activity) override;
  virtual void OnActivityOrderChanged() override;

  // WindowManagerObserver:
  virtual void OnOverviewModeEnter() override;
  virtual void OnOverviewModeExit() override;
  virtual void OnSplitViewModeEnter() override;
  virtual void OnSplitViewModeExit() override;

  // MemoryPressureObserver:
  virtual void OnMemoryPressure(MemoryPressure pressure) override;
  virtual ResourceManagerDelegate* GetDelegate() override;

  // WindowListProviderObserver:
  virtual void OnWindowStackingChangedInList() override;
  virtual void OnWindowAddedToList(aura::Window* added_window) override {}
  virtual void OnWindowRemovedFromList(aura::Window* removed_window,
                                       int index) override {}

 private:
  // Manage the resources for our activities.
  void ManageResource();

  // Check that the visibility of activities is properly set.
  void UpdateVisibilityStates();

  // Check if activities can be unloaded to reduce memory pressure.
  void TryToUnloadAnActivity();

  // Resources were released and a quiet period is needed before we release
  // more since it takes a while to trickle through the system.
  void OnResourcesReleased();

  // The memory pressure has increased, previously applied measures did not show
  // effect and immediate action is required.
  void OnMemoryPressureIncreased();

  // Returns true when the previous memory release was long enough ago to try
  // unloading another activity.
  bool AllowedToUnloadActivity();

  // The resource manager delegate.
  scoped_ptr<ResourceManagerDelegate> delegate_;

  // Keeping a reference to the current memory pressure.
  MemoryPressure current_memory_pressure_;

  // The memory pressure notifier.
  scoped_ptr<MemoryPressureNotifier> memory_pressure_notifier_;

  // A ref counter. As long as not 0, the management is on hold.
  int pause_;

  // If true, a command came in while the resource manager was paused.
  bool queued_command_;

  // Used by ManageResource() to determine an activity state change while it
  // changes Activity properties.
  bool activity_order_changed_;

  // True if in overview mode - activity order changes will be ignored if true
  // and postponed till after the overview mode is ending.
  bool in_overview_mode_;

  // True if we are in split view mode.
  bool in_split_view_mode_;

  // The last time the resource manager was called to release resources.
  // Avoid too aggressive resource de-allocation by enforcing a wait time of
  // |wait_time_for_resource_deallocation_| between executed calls.
  base::Time next_resource_management_time_;

  // The wait time between two resource managing executions.
  base::TimeDelta wait_time_for_resource_deallocation_;

  DISALLOW_COPY_AND_ASSIGN(ResourceManagerImpl);
};

namespace {
ResourceManagerImpl* instance = nullptr;

// We allow this many activities to be visible. All others must be at state of
// invisible or below.
const int kMaxVisibleActivities = 3;

}  // namespace

ResourceManagerImpl::ResourceManagerImpl(ResourceManagerDelegate* delegate)
    : delegate_(delegate),
      current_memory_pressure_(MEMORY_PRESSURE_UNKNOWN),
      memory_pressure_notifier_(new MemoryPressureNotifier(this)),
      pause_(false),
      queued_command_(false),
      activity_order_changed_(false),
      in_overview_mode_(false),
      in_split_view_mode_(false),
      next_resource_management_time_(base::Time::Now()),
      wait_time_for_resource_deallocation_(base::TimeDelta::FromMilliseconds(
          delegate_->MemoryPressureIntervalInMS())) {
  WindowManager::Get()->AddObserver(this);
  WindowManager::Get()->GetWindowListProvider()->AddObserver(this);
  ActivityManager::Get()->AddObserver(this);
}

ResourceManagerImpl::~ResourceManagerImpl() {
  ActivityManager::Get()->RemoveObserver(this);
  WindowManager::Get()->GetWindowListProvider()->RemoveObserver(this);
  WindowManager::Get()->RemoveObserver(this);
}

void ResourceManagerImpl::SetMemoryPressureAndStopMonitoring(
    MemoryPressure pressure) {
  memory_pressure_notifier_->StopObserving();
  OnMemoryPressure(pressure);
}

void ResourceManagerImpl::OnActivityStarted(Activity* activity) {
  // Update the activity states.
  ManageResource();
  activity_order_changed_ = true;
}

void ResourceManagerImpl::OnActivityEnding(Activity* activity) {
  activity_order_changed_ = true;
}

void ResourceManagerImpl::OnActivityOrderChanged() {
  activity_order_changed_ = true;
}

void ResourceManagerImpl::OnOverviewModeEnter() {
  in_overview_mode_ = true;
}

void ResourceManagerImpl::OnOverviewModeExit() {
  in_overview_mode_ = false;
  ManageResource();
}

void ResourceManagerImpl::OnSplitViewModeEnter() {
  // Re-apply the memory pressure to make sure enough items are visible.
  in_split_view_mode_ = true;
  ManageResource();
}


void ResourceManagerImpl::OnSplitViewModeExit() {
  // We don't do immediately something yet. The next ManageResource call will
  // come soon.
  in_split_view_mode_ = false;
}

void ResourceManagerImpl::OnWindowStackingChangedInList() {
  if (pause_) {
    queued_command_ = true;
    return;
  }

  // No need to do anything while being in overview mode.
  if (in_overview_mode_)
    return;

  // Manage the resources of each activity.
  ManageResource();
}

void ResourceManagerImpl::OnMemoryPressure(MemoryPressure pressure) {
  if (pressure > current_memory_pressure_)
    OnMemoryPressureIncreased();
  current_memory_pressure_ = pressure;
  ManageResource();
}

ResourceManagerDelegate* ResourceManagerImpl::GetDelegate() {
  return delegate_.get();
}

void ResourceManagerImpl::ManageResource() {
  // If there is none or only one app running we cannot do anything.
  if (ActivityManager::Get()->GetActivityList().size() <= 1U)
    return;

  if (pause_) {
    queued_command_ = true;
    return;
  }

  // Check that the visibility of items is properly set. Note that this might
  // already trigger a release of resources. If this happens,
  // AllowedToUnloadActivity() will return false.
  UpdateVisibilityStates();

  // Since resource deallocation takes time, we avoid to release more resources
  // in short succession. Note that we come here periodically and if one call
  // is not triggering an unload, the next one will.
  if (AllowedToUnloadActivity())
    TryToUnloadAnActivity();
}

void ResourceManagerImpl::UpdateVisibilityStates() {
  // The first n activities should be treated as "visible", means they updated
  // in overview mode and will keep their layer resources for faster switch
  // times. Usually we use |kMaxVisibleActivities| items, but when the memory
  // pressure gets critical we only hold as many as are really visible.
  size_t max_activities = kMaxVisibleActivities;
  if (current_memory_pressure_ == MEMORY_PRESSURE_CRITICAL)
    max_activities = in_split_view_mode_ ? 2 : 1;

  do {
    activity_order_changed_ = false;

    // Change the visibility of our activities in a pre-processing step. This is
    // required since it might change the order/number of activities.
    size_t count = 0;
    for (Activity* activity : ActivityManager::Get()->GetActivityList()) {
      Activity::ActivityState state = activity->GetCurrentState();

      // The first |kMaxVisibleActivities| entries should be visible, all others
      // invisible or at a lower activity state.
      if (count < max_activities ||
          (state == Activity::ACTIVITY_INVISIBLE ||
           state == Activity::ACTIVITY_VISIBLE)) {
        Activity::ActivityState visiblity_state =
            count < max_activities ? Activity::ACTIVITY_VISIBLE :
                                     Activity::ACTIVITY_INVISIBLE;
        // Only change the state when it changes. Note that when the memory
        // pressure is critical, only the primary activities (1 or 2) are made
        // visible. Furthermore, in relaxed mode we only want to turn visible,
        // never invisible.
        if (visiblity_state != state &&
            (current_memory_pressure_ != MEMORY_PRESSURE_LOW ||
             visiblity_state == Activity::ACTIVITY_VISIBLE)) {
          activity->SetCurrentState(visiblity_state);
          // If we turned an activity invisible, we are already releasing memory
          // and can hold off releasing more for now.
          if (visiblity_state == Activity::ACTIVITY_INVISIBLE)
            OnResourcesReleased();
        }
      }

      // See which count we should handle next.
      if (activity_order_changed_)
        break;
      ++count;
    }
    // If we stopped iterating over the list of activities because of the change
    // in ordering, then restart processing the activities from the beginning.
  } while (activity_order_changed_);
}

void ResourceManagerImpl::TryToUnloadAnActivity() {
  // TODO(skuhne): This algorithm needs to take all kinds of predictive analysis
  // and running applications into account. For this first patch we only do a
  // very simple "floating window" algorithm which is surely not good enough.
  size_t max_running_activities = 5;
  switch (current_memory_pressure_) {
    case MEMORY_PRESSURE_UNKNOWN:
      // If we do not know how much memory we have we assume that it must be a
      // high consumption.
      // Fallthrough.
    case MEMORY_PRESSURE_HIGH:
      max_running_activities = 5;
      break;
    case MEMORY_PRESSURE_CRITICAL:
      max_running_activities = 0;
      break;
    case MEMORY_PRESSURE_MODERATE:
      max_running_activities = 7;
      break;
    case MEMORY_PRESSURE_LOW:
      NOTREACHED();
      return;
  }

  // Check if / which activity we want to unload.
  Activity* oldest_media_activity = nullptr;
  Activity* oldest_unloadable_activity = nullptr;
  size_t unloadable_activity_count = 0;
  const ActivityList& activity_list = ActivityManager::Get()->GetActivityList();
  for (Activity* activity : activity_list) {
    Activity::ActivityState state = activity->GetCurrentState();
    // The activity should neither be unloaded nor visible.
    if (state != Activity::ACTIVITY_UNLOADED &&
        state != Activity::ACTIVITY_VISIBLE) {
      if (activity->GetMediaState() == Activity::ACTIVITY_MEDIA_STATE_NONE) {
        // Does not play media - so we can unload this immediately.
        ++unloadable_activity_count;
        oldest_unloadable_activity = activity;
      } else {
        oldest_media_activity = activity;
      }
    }
  }

  if (unloadable_activity_count > max_running_activities) {
    CHECK(oldest_unloadable_activity);
    OnResourcesReleased();
    oldest_unloadable_activity->SetCurrentState(Activity::ACTIVITY_UNLOADED);
    return;
  } else if (current_memory_pressure_ == MEMORY_PRESSURE_CRITICAL) {
    if (oldest_media_activity) {
      OnResourcesReleased();
      oldest_media_activity->SetCurrentState(Activity::ACTIVITY_UNLOADED);
      LOG(WARNING) << "Unloading item to releave critical memory pressure";
      return;
    }
    LOG(ERROR) << "[ResourceManager]: Single activity uses too much memory.";
    return;
  }

  if (current_memory_pressure_ != MEMORY_PRESSURE_UNKNOWN) {
    // Only show this warning when the memory pressure is actually known. This
    // will suppress warnings in e.g. unit tests.
    LOG(WARNING) << "[ResourceManager]: No way to release memory pressure (" <<
        current_memory_pressure_ <<
        "), Activities (running, allowed, unloadable)=(" <<
        activity_list.size() << ", " <<
        max_running_activities << ", " <<
        unloadable_activity_count << ")";
  }
}

void ResourceManagerImpl::OnResourcesReleased() {
  // Do not release too many activities in short succession since it takes time
  // to release resources. As such wait the memory pressure interval before the
  // next call.
  next_resource_management_time_ = base::Time::Now() +
                                   wait_time_for_resource_deallocation_;
}

void ResourceManagerImpl::OnMemoryPressureIncreased() {
  // By setting the timer to Now, the next call will immediately be performed.
  next_resource_management_time_ = base::Time::Now();
}

bool ResourceManagerImpl::AllowedToUnloadActivity() {
  return current_memory_pressure_ != MEMORY_PRESSURE_LOW &&
         base::Time::Now() >= next_resource_management_time_;
}

}  // namespace

// static
void ResourceManager::Create() {
  DCHECK(!instance);
  instance = new ResourceManagerImpl(
      ResourceManagerDelegate::CreateResourceManagerDelegate());
}

// static
ResourceManager* ResourceManager::Get() {
  return instance;
}

// static
void ResourceManager::Shutdown() {
  DCHECK(instance);
  delete instance;
  instance = nullptr;
}

ResourceManager::ResourceManager() {}

ResourceManager::~ResourceManager() {
  DCHECK(instance);
  instance = nullptr;
}

}  // namespace athena
