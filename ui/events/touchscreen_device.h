// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_EVENTS_TOUCHSCREEN_DEVICE_H_
#define UI_EVENTS_TOUCHSCREEN_DEVICE_H_

#include <string>

#include "ui/events/events_base_export.h"
#include "ui/events/input_device.h"
#include "ui/gfx/geometry/size.h"

namespace ui {

// Represents a Touchscreen device state.
struct EVENTS_BASE_EXPORT TouchscreenDevice : public InputDevice {
  TouchscreenDevice(int id,
                    InputDeviceType type,
                    const std::string& name,
                    const gfx::Size& size);

  // Size of the touch screen area.
  gfx::Size size;
};

}  // namespace ui

#endif  // UI_EVENTS_TOUCHSCREEN_DEVICE_H_
