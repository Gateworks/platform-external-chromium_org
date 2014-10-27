// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATHENA_RESOURCE_MANAGER_PUBLIC_RESOURCE_MANAGER_H_
#define ATHENA_RESOURCE_MANAGER_PUBLIC_RESOURCE_MANAGER_H_

#include "athena/athena_export.h"
#include "base/basictypes.h"

namespace athena {

// The resource manager is monitoring activity changes, low memory conditions
// and other events to control the activity state (pre-/un-/re-/loading them)
// to keep enough memory free that no jank/lag will show when new applications
// are loaded and / or a navigation between applications takes place.
class ATHENA_EXPORT ResourceManager {
 public:
  // The reported memory pressure. Note: The value is intentionally abstracted
  // since the real amount of free memory is only estimated (due to e.g. zram).
  // Note: The bigger the index of the pressure level, the more resources are
  // in use.
  enum MemoryPressure {
    MEMORY_PRESSURE_UNKNOWN = 0,   // The memory pressure cannot be determined.
    MEMORY_PRESSURE_LOW,           // Single call if fill level is below 50%.
    MEMORY_PRESSURE_MODERATE,      // Polled for fill level of ~50 .. 75%.
    MEMORY_PRESSURE_HIGH,          // Polled for fill level of ~75% .. 90%.
    MEMORY_PRESSURE_CRITICAL,      // Polled for fill level of above ~90%.
  };

  // Creates the instance handling the resources.
  static void Create();
  static ResourceManager* Get();
  static void Shutdown();

  ResourceManager();
  virtual ~ResourceManager();

  // Unit tests can simulate MemoryPressure changes with this call.
  // Note: Even though the default unit test ResourceManagerDelegte
  // implementation ensures that the MemoryPressure event will not go off,
  // this call will also explicitly stop the MemoryPressureNotifier.
  virtual void SetMemoryPressureAndStopMonitoring(
      ResourceManager::MemoryPressure pressure) = 0;

  // Resource management calls require time to show effect (until memory
  // gets actually released). This function lets override the time limiter
  // between two calls to allow for more/less aggressive timeouts.
  // By calling this function, the next call to the Resource manager will be
  // executed immediately.
  virtual void SetWaitTimeBetweenResourceManageCalls(int time_in_ms) = 0;

  // Suspend the resource manager temporarily if |pause| is set. This can be
  // called before e.g. re-arranging the order of activities. Once called with
  // |pause| == false any queued operations will be performed and the resource
  // manager will continue its work.
  virtual void Pause(bool pause) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ResourceManager);
};

// Use this scoped object to pause/restart the resource manager.
class ScopedPauseResourceManager {
 public:
  ScopedPauseResourceManager() {
    ResourceManager::Get()->Pause(true);
  }
  ~ScopedPauseResourceManager() {
    ResourceManager::Get()->Pause(false);
  }
 private:
  DISALLOW_COPY_AND_ASSIGN(ScopedPauseResourceManager);
};

}  // namespace athena

#endif  // ATHENA_RESOURCE_MANAGER_PUBLIC_RESOURCE_MANAGER_H_
