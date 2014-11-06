// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/string16.h"
#include "chrome/browser/extensions/api/screenlock_private/screenlock_private_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/signin/easy_unlock_service.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/signin/core/common/profile_management_switches.h"
#include "content/public/browser/notification_service.h"
#include "extensions/browser/api/test/test_api.h"
#include "extensions/browser/notification_types.h"
#include "extensions/common/switches.h"

namespace extensions {

namespace {

const char kAttemptClickAuthMessage[] = "attemptClickAuth";
const char kTestExtensionId[] = "lkegkdgachcnekllcdfkijonogckdnjo";
const char kTestUser[] = "testuser@gmail.com";

}  // namespace

class ScreenlockPrivateApiTest : public ExtensionApiTest,
                                 public content::NotificationObserver {
 public:
  ScreenlockPrivateApiTest() {}

  ~ScreenlockPrivateApiTest() override {}

  // ExtensionApiTest
  void SetUpCommandLine(CommandLine* command_line) override {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(
        extensions::switches::kWhitelistedExtensionID, kTestExtensionId);

#if !defined(OS_CHROMEOS)
    // New profile management needs to be on for non-ChromeOS lock.
    ::switches::EnableNewProfileManagementForTesting(command_line);
#endif
  }

  void SetUpOnMainThread() override {
    SigninManagerFactory::GetForProfile(profile())
        ->SetAuthenticatedUsername(kTestUser);
    ExtensionApiTest::SetUpOnMainThread();
  }

 protected:
  // ExtensionApiTest override:
  void RunTestOnMainThreadLoop() override {
    registrar_.Add(this,
                   extensions::NOTIFICATION_EXTENSION_TEST_MESSAGE,
                   content::NotificationService::AllSources());
    ExtensionApiTest::RunTestOnMainThreadLoop();
    registrar_.RemoveAll();
  }

  // content::NotificationObserver override:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override {
    const std::string& content = *content::Details<std::string>(details).ptr();
    if (content == kAttemptClickAuthMessage) {
      ScreenlockBridge::Get()->lock_handler()->SetAuthType(
          kTestUser,
          ScreenlockBridge::LockHandler::USER_CLICK,
          base::string16());
      EasyUnlockService::Get(profile())->AttemptAuth(kTestUser);
    }
  }

  // Loads |extension_name| as appropriate for the platform and waits for a
  // pass / fail notification.
  void RunTest(const std::string& extension_name) {
#if defined(OS_CHROMEOS)
    ASSERT_TRUE(RunComponentExtensionTest(extension_name)) << message_;
#else
    ASSERT_TRUE(RunExtensionTest(extension_name)) << message_;
#endif
  }

 private:
  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(ScreenlockPrivateApiTest);
};

IN_PROC_BROWSER_TEST_F(ScreenlockPrivateApiTest, LockUnlock) {
  RunTest("screenlock_private/lock_unlock");
}

IN_PROC_BROWSER_TEST_F(ScreenlockPrivateApiTest, AuthType) {
  RunTest("screenlock_private/auth_type");
}

}  // namespace extensions
