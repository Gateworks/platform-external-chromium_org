// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/touch/touch_transformer_controller.h"

#include "ash/display/display_controller.h"
#include "ash/display/display_manager.h"
#include "ash/host/ash_window_tree_host.h"
#include "ash/root_window_controller.h"
#include "ash/shell.h"
#include "ui/aura/window_tree_host.h"
#include "ui/display/chromeos/display_configurator.h"
#include "ui/display/types/display_snapshot.h"
#include "ui/events/device_data_manager.h"
#include "ui/events/x/device_data_manager_x11.h"

namespace ash {

namespace {

DisplayManager* GetDisplayManager() {
  return Shell::GetInstance()->display_manager();
}

}  // namespace

// This is to compute the scale ratio for the TouchEvent's radius. The
// configured resolution of the display is not always the same as the touch
// screen's reporting resolution, e.g. the display could be set as
// 1920x1080 while the touchscreen is reporting touch position range at
// 32767x32767. Touch radius is reported in the units the same as touch position
// so we need to scale the touch radius to be compatible with the display's
// resolution. We compute the scale as
// sqrt of (display_area / touchscreen_area)
double TouchTransformerController::GetTouchResolutionScale(
    const DisplayInfo& touch_display) const {
  if (touch_display.touch_device_id() == 0u)
    return 1.0;

  double min_x, max_x;
  double min_y, max_y;
  if (!ui::DeviceDataManagerX11::GetInstance()->GetDataRange(
          touch_display.touch_device_id(),
          ui::DeviceDataManagerX11::DT_TOUCH_POSITION_X,
          &min_x, &max_x) ||
      !ui::DeviceDataManagerX11::GetInstance()->GetDataRange(
          touch_display.touch_device_id(),
          ui::DeviceDataManagerX11::DT_TOUCH_POSITION_Y,
          &min_y, &max_y)) {
    return 1.0;
  }

  double width = touch_display.bounds_in_native().width();
  double height = touch_display.bounds_in_native().height();

  if (max_x == 0.0 || max_y == 0.0 || width == 0.0 || height == 0.0)
    return 1.0;

  // [0, max_x] -> touchscreen width = max_x + 1
  // [0, max_y] -> touchscreen height = max_y + 1
  max_x += 1.0;
  max_y += 1.0;

  double ratio = std::sqrt((width * height) / (max_x * max_y));

  VLOG(2) << "Screen width/height: " << width << "/" << height
          << ", Touchscreen width/height: " << max_x << "/" << max_y
          << ", Touch radius scale ratio: " << ratio;
  return ratio;
}

// This function computes the extended mode TouchTransformer for
// |touch_display|. The TouchTransformer maps the touch event position
// from framebuffer size to the display size.
gfx::Transform
TouchTransformerController::GetExtendedModeTouchTransformer(
    const DisplayInfo& touch_display, const gfx::Size& fb_size) const {
  gfx::Transform ctm;
  if (touch_display.touch_device_id() == 0u || fb_size.width() == 0.0 ||
      fb_size.height() == 0.0)
    return ctm;
  float width = touch_display.bounds_in_native().width();
  float height = touch_display.bounds_in_native().height();
  ctm.Scale(width / fb_size.width(), height / fb_size.height());
  return ctm;
}

bool TouchTransformerController::ShouldComputeMirrorModeTouchTransformer(
    const DisplayInfo& touch_display) const {
  if (force_compute_mirror_mode_touch_transformer_)
    return true;

  if (touch_display.touch_device_id() == 0u)
    return false;

  if (touch_display.size_in_pixel() == touch_display.GetNativeModeSize() ||
      !touch_display.is_aspect_preserving_scaling()) {
    return false;
  }

  return true;
}

// This function computes the mirror mode TouchTransformer for |touch_display|.
// When internal monitor is applied a resolution that does not have
// the same aspect ratio as its native resolution, there would be
// blank regions in the letterboxing/pillarboxing mode.
// The TouchTransformer will make sure the touch events on the blank region
// have negative coordinates and touch events within the chrome region
// have the correct positive coordinates.
gfx::Transform TouchTransformerController::GetMirrorModeTouchTransformer(
    const DisplayInfo& touch_display) const {
  gfx::Transform ctm;
  if (!ShouldComputeMirrorModeTouchTransformer(touch_display))
    return ctm;

  float mirror_width = touch_display.bounds_in_native().width();
  float mirror_height = touch_display.bounds_in_native().height();
  gfx::Size native_mode_size = touch_display.GetNativeModeSize();
  float native_width = native_mode_size.width();
  float native_height = native_mode_size.height();

  if (native_height == 0.0 || mirror_height == 0.0 ||
      native_width == 0.0 || mirror_width == 0.0)
    return ctm;

  float native_ar = native_width / native_height;
  float mirror_ar = mirror_width / mirror_height;

  if (mirror_ar > native_ar) {  // Letterboxing
    // Translate before scale.
    ctm.Translate(0.0, (1.0 - mirror_ar / native_ar) * 0.5 * mirror_height);
    ctm.Scale(1.0, mirror_ar / native_ar);
    return ctm;
  }

  if (native_ar > mirror_ar) {  // Pillarboxing
    // Translate before scale.
    ctm.Translate((1.0 - native_ar / mirror_ar) * 0.5 * mirror_width, 0.0);
    ctm.Scale(native_ar / mirror_ar, 1.0);
    return ctm;
  }

  return ctm;  // Same aspect ratio - return identity
}

TouchTransformerController::TouchTransformerController() :
    force_compute_mirror_mode_touch_transformer_ (false) {
  Shell::GetInstance()->display_controller()->AddObserver(this);
}

TouchTransformerController::~TouchTransformerController() {
  Shell::GetInstance()->display_controller()->RemoveObserver(this);
}

void TouchTransformerController::UpdateTouchTransformer() const {
  ui::DeviceDataManager* device_manager = ui::DeviceDataManager::GetInstance();
  device_manager->ClearTouchTransformerRecord();

  // Display IDs and DisplayInfo for mirror or extended mode.
  int64 display1_id = gfx::Display::kInvalidDisplayID;
  int64 display2_id = gfx::Display::kInvalidDisplayID;
  DisplayInfo display1;
  DisplayInfo display2;
  // Display ID and DisplayInfo for single display mode.
  int64 single_display_id = gfx::Display::kInvalidDisplayID;
  DisplayInfo single_display;

  DisplayController* display_controller =
      Shell::GetInstance()->display_controller();
  ui::MultipleDisplayState display_state =
      Shell::GetInstance()->display_configurator()->display_state();
  if (display_state == ui::MULTIPLE_DISPLAY_STATE_INVALID ||
      display_state == ui::MULTIPLE_DISPLAY_STATE_HEADLESS) {
    return;
  } else if (display_state == ui::MULTIPLE_DISPLAY_STATE_DUAL_MIRROR ||
             display_state == ui::MULTIPLE_DISPLAY_STATE_DUAL_EXTENDED) {
    DisplayIdPair id_pair = GetDisplayManager()->GetCurrentDisplayIdPair();
    display1_id = id_pair.first;
    display2_id = id_pair.second;
    DCHECK(display1_id != gfx::Display::kInvalidDisplayID &&
           display2_id != gfx::Display::kInvalidDisplayID);
    display1 = GetDisplayManager()->GetDisplayInfo(display1_id);
    display2 = GetDisplayManager()->GetDisplayInfo(display2_id);
    device_manager->UpdateTouchRadiusScale(display1.touch_device_id(),
                                           GetTouchResolutionScale(display1));
    device_manager->UpdateTouchRadiusScale(display2.touch_device_id(),
                                           GetTouchResolutionScale(display2));
  } else {
    single_display_id = GetDisplayManager()->first_display_id();
    DCHECK(single_display_id != gfx::Display::kInvalidDisplayID);
    single_display = GetDisplayManager()->GetDisplayInfo(single_display_id);
    device_manager->UpdateTouchRadiusScale(
        single_display.touch_device_id(),
        GetTouchResolutionScale(single_display));
  }

  if (display_state == ui::MULTIPLE_DISPLAY_STATE_DUAL_MIRROR) {
    // In mirror mode, both displays share the same root window so
    // both display ids are associated with the root window.
    aura::Window* root = display_controller->GetPrimaryRootWindow();
    RootWindowController::ForWindow(root)->ash_host()->UpdateDisplayID(
        display1_id, display2_id);
    device_manager->UpdateTouchInfoForDisplay(
        display1_id,
        display1.touch_device_id(),
        GetMirrorModeTouchTransformer(display1));
    device_manager->UpdateTouchInfoForDisplay(
        display2_id,
        display2.touch_device_id(),
        GetMirrorModeTouchTransformer(display2));
    return;
  }

  if (display_state == ui::MULTIPLE_DISPLAY_STATE_DUAL_EXTENDED) {
    gfx::Size fb_size =
        Shell::GetInstance()->display_configurator()->framebuffer_size();
    // In extended but software mirroring mode, ther is only one X root window
    // that associates with both displays.
    if (GetDisplayManager()->software_mirroring_enabled())  {
      aura::Window* root = display_controller->GetPrimaryRootWindow();
      RootWindowController::ForWindow(root)->ash_host()->UpdateDisplayID(
          display1_id, display2_id);
      DisplayInfo source_display =
          gfx::Display::InternalDisplayId() == display1_id ?
          display1 : display2;
      // Mapping from framebuffer size to the source display's native
      // resolution.
      device_manager->UpdateTouchInfoForDisplay(
          display1_id,
          display1.touch_device_id(),
          GetExtendedModeTouchTransformer(source_display, fb_size));
      device_manager->UpdateTouchInfoForDisplay(
          display2_id,
          display2.touch_device_id(),
          GetExtendedModeTouchTransformer(source_display, fb_size));
    } else {
      // In actual extended mode, each display is associated with one root
      // window.
      aura::Window* root1 =
          display_controller->GetRootWindowForDisplayId(display1_id);
      aura::Window* root2 =
          display_controller->GetRootWindowForDisplayId(display2_id);
      RootWindowController::ForWindow(root1)->ash_host()->UpdateDisplayID(
          display1_id, gfx::Display::kInvalidDisplayID);
      RootWindowController::ForWindow(root2)->ash_host()->UpdateDisplayID(
          display2_id, gfx::Display::kInvalidDisplayID);
      // Mapping from framebuffer size to each display's native resolution.
      device_manager->UpdateTouchInfoForDisplay(
          display1_id,
          display1.touch_device_id(),
          GetExtendedModeTouchTransformer(display1, fb_size));
      device_manager->UpdateTouchInfoForDisplay(
          display2_id,
          display2.touch_device_id(),
          GetExtendedModeTouchTransformer(display2, fb_size));
    }
    return;
  }

  // Single display mode. The root window has one associated display id.
  aura::Window* root =
      display_controller->GetRootWindowForDisplayId(single_display.id());
  RootWindowController::ForWindow(root)->ash_host()->UpdateDisplayID(
      single_display.id(), gfx::Display::kInvalidDisplayID);
  device_manager->UpdateTouchInfoForDisplay(single_display_id,
                                            single_display.touch_device_id(),
                                            gfx::Transform());
}

void TouchTransformerController::OnDisplaysInitialized() {
  UpdateTouchTransformer();
}

void TouchTransformerController::OnDisplayConfigurationChanged() {
  UpdateTouchTransformer();
}

}  // namespace ash
