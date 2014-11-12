// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "athena/test/chrome/athena_app_browser_test.h"

#include "athena/extensions/public/extensions_delegate.h"
#include "athena/test/base/activity_lifetime_tracker.h"
#include "athena/test/base/test_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "content/public/browser/notification_service.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/test_utils.h"

namespace athena {

AthenaAppBrowserTest::AthenaAppBrowserTest() {
}

AthenaAppBrowserTest::~AthenaAppBrowserTest() {
}

void AthenaAppBrowserTest::SetUpCommandLine(base::CommandLine* command_line) {
  // The NaCl sandbox won't work in our browser tests.
  command_line->AppendSwitch(switches::kNoSandbox);
  extensions::PlatformAppBrowserTest::SetUpCommandLine(command_line);
}

Activity* AthenaAppBrowserTest::CreateTestAppActivity(
    const std::string app_id) {
  ActivityLifetimeTracker tracker;

  test_util::WaitUntilIdle();
  content::WindowedNotificationObserver observer(
      content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
      content::NotificationService::AllSources());

  ExtensionsDelegate::Get(GetBrowserContext())->LaunchApp(app_id);

  observer.Wait();
  return tracker.GetNewActivityAndReset();
}

const std::string& AthenaAppBrowserTest::GetTestAppID() {
  if (!app_id_.empty())
    return app_id_;

  const extensions::Extension* extension = InstallPlatformApp("minimal");
  app_id_ = extension->id();
  if (app_id_.empty())
    NOTREACHED() << "A test application should have been registered!";

  return app_id_;
}

void AthenaAppBrowserTest::SetUpOnMainThread() {
  // Set the memory pressure to low and turning off undeterministic resource
  // observer events.
  test_util::SendTestMemoryPressureEvent(ResourceManager::MEMORY_PRESSURE_LOW);
}

content::BrowserContext* AthenaAppBrowserTest::GetBrowserContext() {
  return ProfileManager::GetActiveUserProfile();
}

}  // namespace athena
