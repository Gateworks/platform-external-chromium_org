// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/app_mode/app_launch_utils.h"

#include "chrome/browser/chromeos/app_mode/kiosk_app_launch_error.h"
#include "chrome/browser/chromeos/app_mode/startup_app_launcher.h"
#include "chrome/browser/lifetime/application_lifetime.h"

namespace chromeos {

// A simple manager for the app launch that starts the launch
// and deletes itself when the launch finishes. On launch failure,
// it exits the browser process.
class AppLaunchManager : public StartupAppLauncher::Observer {
 public:
  AppLaunchManager(Profile* profile, const std::string& app_id) {
    startup_app_launcher_.reset(new StartupAppLauncher(profile, app_id));
  }

  void Start() {
    startup_app_launcher_->AddObserver(this);
    startup_app_launcher_->Start();
  }

 private:
  virtual ~AppLaunchManager() {}

  void Cleanup() { delete this; }

  virtual void OnLoadingOAuthFile() OVERRIDE {}
  virtual void OnInitializingTokenService() OVERRIDE {}
  virtual void OnInitializingNetwork() OVERRIDE {}
  virtual void OnNetworkWaitTimedout() OVERRIDE {}
  virtual void OnInstallingApp() OVERRIDE {}
  virtual void OnLaunchSucceeded() OVERRIDE { Cleanup(); }
  virtual void OnLaunchFailed(KioskAppLaunchError::Error error) OVERRIDE {
    KioskAppLaunchError::Save(error);
    chrome::AttemptUserExit();
    Cleanup();
  }

  scoped_ptr<StartupAppLauncher> startup_app_launcher_;

  DISALLOW_COPY_AND_ASSIGN(AppLaunchManager);
};

void LaunchAppOrDie(Profile* profile, const std::string& app_id) {
  // AppLaunchManager manages its own lifetime.
  (new AppLaunchManager(profile, app_id))->Start();
}

}  // namespace chromeos