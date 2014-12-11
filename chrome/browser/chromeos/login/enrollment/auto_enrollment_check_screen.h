// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_ENROLLMENT_AUTO_ENROLLMENT_CHECK_SCREEN_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_ENROLLMENT_AUTO_ENROLLMENT_CHECK_SCREEN_H_

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/login/enrollment/auto_enrollment_check_screen_actor.h"
#include "chrome/browser/chromeos/login/enrollment/auto_enrollment_controller.h"
#include "chrome/browser/chromeos/login/screens/error_screen.h"
#include "chromeos/network/portal_detector/network_portal_detector.h"

namespace chromeos {

class BaseScreenDelegate;
class ErrorScreensHistogramHelper;
class ScreenManager;

// Handles the control flow after OOBE auto-update completes to wait for the
// enterprise auto-enrollment check that happens as part of OOBE. This includes
// keeping track of current auto-enrollment state and displaying and updating
// the error screen upon failures. Similar to a screen controller, but it
// doesn't actually drive a dedicated screen.
class AutoEnrollmentCheckScreen
    : public AutoEnrollmentCheckScreenActor::Delegate,
      public BaseScreen,
      public NetworkPortalDetector::Observer {
 public:
  AutoEnrollmentCheckScreen(BaseScreenDelegate* base_screen_delegate,
                            AutoEnrollmentCheckScreenActor* actor);
  virtual ~AutoEnrollmentCheckScreen();

  static AutoEnrollmentCheckScreen* Get(ScreenManager* manager);

  // Clears the cached state causing the forced enrollment check to be retried.
  void ClearState();

  void set_auto_enrollment_controller(
      AutoEnrollmentController* auto_enrollment_controller) {
    auto_enrollment_controller_ = auto_enrollment_controller;
  }

  // BaseScreen implementation:
  virtual void PrepareToShow() override;
  virtual void Show() override;
  virtual void Hide() override;
  virtual std::string GetName() const override;

  // AutoEnrollmentCheckScreenActor::Delegate implementation:
  virtual void OnActorDestroyed(AutoEnrollmentCheckScreenActor* actor) override;

  // NetworkPortalDetector::Observer implementation:
  virtual void OnPortalDetectionCompleted(
      const NetworkState* network,
      const NetworkPortalDetector::CaptivePortalState& state) override;

 private:
  // Handles update notifications regarding the auto-enrollment check.
  void OnAutoEnrollmentCheckProgressed(policy::AutoEnrollmentState state);

  // Handles a state update, updating the UI and saving the state.
  void UpdateState();

  // Configures the UI to reflect |new_captive_portal_status|. Returns true if
  // and only if a UI change has been made.
  bool UpdateCaptivePortalStatus(
      NetworkPortalDetector::CaptivePortalStatus new_captive_portal_status);

  // Configures the UI to reflect |auto_enrollment_state|. Returns true if and
  // only if a UI change has been made.
  bool UpdateAutoEnrollmentState(
      policy::AutoEnrollmentState auto_enrollment_state);

  // Configures the error screen.
  void ShowErrorScreen(ErrorScreen::ErrorState error_state);

  // Asynchronously signals completion. The owner might destroy |this| in
  // response, so no code should be run after the completion of a message loop
  // task, in which this function was called.
  void SignalCompletion();

  // Terminates the screen.
  void CallOnExit();

  // Returns whether enrollment check was completed and decision was made.
  bool IsCompleted() const;

  AutoEnrollmentCheckScreenActor* actor_;
  AutoEnrollmentController* auto_enrollment_controller_;

  scoped_ptr<AutoEnrollmentController::ProgressCallbackList::Subscription>
      auto_enrollment_progress_subscription_;

  NetworkPortalDetector::CaptivePortalStatus captive_portal_status_;
  policy::AutoEnrollmentState auto_enrollment_state_;

  scoped_ptr<ErrorScreensHistogramHelper> histogram_helper_;

  base::WeakPtrFactory<AutoEnrollmentCheckScreen> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(AutoEnrollmentCheckScreen);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_ENROLLMENT_AUTO_ENROLLMENT_CHECK_SCREEN_H_
