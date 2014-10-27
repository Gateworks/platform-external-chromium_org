// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATHENA_SCREEN_PUBLIC_SCREEN_MANAGER_H_
#define ATHENA_SCREEN_PUBLIC_SCREEN_MANAGER_H_

#include <string>

#include "athena/athena_export.h"
#include "ui/gfx/display.h"

namespace aura {
class Window;
}

namespace athena {
class ScreenManagerDelegate;

// Mananges basic UI components on the screen such as background, and provide
// API for other UI components, such as window manager, home card, to
// create and manage their windows on the screen.
class ATHENA_EXPORT ScreenManager {
 public:
  struct ContainerParams {
    ContainerParams(const std::string& name, int z_order_priority);
    std::string name;

    // True if the container can activate its child window.
    bool can_activate_children;

    // True if the container will block evnets from containers behind it.
    bool block_events;

    // Defines the z_order priority of the container.
    int z_order_priority;

    // True if this container should be used as a default parent.
    bool default_parent;

    // The container priority used to open modal dialog window
    // created with this container as a transient parent  (Note: A modal window
    // should
    // use a trnasient parent, not a direct parent, or no transient parent.)
    //
    // Default is -1, and it will fallback to the container behind this
    // container,
    // that has the modal container proiroty.
    //
    // The modal container for modal window is selected as follows.
    // 1) a window must be created with |aura::client::kModalKey| property
    //   without explicit parent set.
    // 2.a) If aura::client::kAlwaysOnTopKey is NOT set, it uses the stand flow
    //   described above. (fallback to containers behind this).
    // 2.b) If aura::client::kAlwaysOnTopKey is set, it searches the top most
    //   container which has |modal_container_priority| != -1.
    // 3) Look for the container with |modal_container_priority|, and create
    //   one if it doesn't exist.
    //
    // Created modal container will self destruct if last modal window
    // is deleted.
    int modal_container_priority;
  };

  // Creates, returns and deletes the singleton object of the ScreenManager
  // implementation.
  static ScreenManager* Create(aura::Window* root);
  static ScreenManager* Get();
  static void Shutdown();

  virtual ~ScreenManager() {}

  // Creates the container window on the screen.
  virtual aura::Window* CreateContainer(const ContainerParams& params) = 0;

  // Return the context object to be used for widget creation.
  virtual aura::Window* GetContext() = 0;

  // Set screen rotation.
  // TODO(flackr): Extract and use ash DisplayManager to set rotation
  // instead: http://crbug.com/401044.
  virtual void SetRotation(gfx::Display::Rotation rotation) = 0;
  virtual void SetRotationLocked(bool rotation_locked) = 0;
};

}  // namespace athena

#endif  // ATHENA_SCREEN_PUBLIC_SCREEN_MANAGER_H_
