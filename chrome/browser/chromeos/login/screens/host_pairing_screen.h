// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_HOST_PAIRING_SCREEN_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_HOST_PAIRING_SCREEN_H_

#include "base/macros.h"
#include "chrome/browser/chromeos/login/screens/base_screen.h"
#include "chrome/browser/chromeos/login/screens/host_pairing_screen_actor.h"
#include "components/login/screens/screen_context.h"
#include "components/pairing/host_pairing_controller.h"

namespace chromeos {

class HostPairingScreen
    : public BaseScreen,
      public pairing_chromeos::HostPairingController::Observer,
      public HostPairingScreenActor::Delegate {
 public:
  class Delegate {
   public:
    virtual ~Delegate() {}
    virtual void ConfigureHost(bool accepted_eula,
                               const std::string& lang,
                               const std::string& timezone,
                               bool send_reports,
                               const std::string& keyboard_layout) = 0;
  };

  HostPairingScreen(BaseScreenDelegate* base_screen_delegate,
                    Delegate* delegate,
                    HostPairingScreenActor* actor,
                    pairing_chromeos::HostPairingController* remora_controller);
  virtual ~HostPairingScreen();

 private:
  typedef pairing_chromeos::HostPairingController::Stage Stage;

  void CommitContextChanges();

  // Overridden from BaseScreen:
  virtual void PrepareToShow() override;
  virtual void Show() override;
  virtual void Hide() override;
  virtual std::string GetName() const override;

  // pairing_chromeos::HostPairingController::Observer:
  virtual void PairingStageChanged(Stage new_stage) override;
  virtual void ConfigureHost(bool accepted_eula,
                             const std::string& lang,
                             const std::string& timezone,
                             bool send_reports,
                             const std::string& keyboard_layout) override;
  virtual void EnrollHost(const std::string& auth_token) override;

  // Overridden from ControllerPairingView::Delegate:
  virtual void OnActorDestroyed(HostPairingScreenActor* actor) override;

  // Context for sharing data between C++ and JS.
  // TODO(dzhioev): move to BaseScreen when possible.
  ::login::ScreenContext context_;

  Delegate* delegate_;

  HostPairingScreenActor* actor_;

  // Controller performing pairing. Owned by the wizard controller.
  pairing_chromeos::HostPairingController* remora_controller_;

  // Current stage of pairing process.
  Stage current_stage_;

  DISALLOW_COPY_AND_ASSIGN(HostPairingScreen);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_HOST_PAIRING_SCREEN_H_
