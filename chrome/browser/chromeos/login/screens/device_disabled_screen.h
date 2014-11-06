// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_DEVICE_DISABLED_SCREEN_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_DEVICE_DISABLED_SCREEN_H_

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/login/screens/base_screen.h"
#include "chrome/browser/chromeos/login/screens/device_disabled_screen_actor.h"

namespace chromeos {

class BaseScreenDelegate;

// Screen informing the user that the device has been disabled by its owner.
class DeviceDisabledScreen : public BaseScreen,
                             public DeviceDisabledScreenActor::Delegate {
 public:
  DeviceDisabledScreen(BaseScreenDelegate* base_screen_delegate,
                       DeviceDisabledScreenActor* actor);
  ~DeviceDisabledScreen() override;

  // BaseScreen:
  void PrepareToShow() override;
  void Show() override;
  void Hide() override;
  std::string GetName() const override;

  // DeviceDisabledScreenActor::Delegate:
  void OnActorDestroyed(DeviceDisabledScreenActor* actor) override;

 private:
  // Indicate to the observer that the screen was skipped because the device is
  // not disabled.
  void IndicateDeviceNotDisabled();

  // Whether the screen is currently showing.
  bool showing_;

  DeviceDisabledScreenActor* actor_;

  base::WeakPtrFactory<DeviceDisabledScreen> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(DeviceDisabledScreen);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_DEVICE_DISABLED_SCREEN_H_
