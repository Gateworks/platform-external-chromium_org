// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "base/command_line.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/signin/core/common/profile_management_switches.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/l10n/l10n_util.h"

class UserManagerUIBrowserTest : public InProcessBrowserTest,
                                 public testing::WithParamInterface<bool> {
 public:
  UserManagerUIBrowserTest() {}

 protected:
  virtual void SetUp() override {
    InProcessBrowserTest::SetUp();
    DCHECK(switches::IsNewAvatarMenu());
  }

  void SetUpCommandLine(CommandLine* command_line) override {
    switches::EnableNewAvatarMenuForTesting(command_line);
  }
};

IN_PROC_BROWSER_TEST_F(UserManagerUIBrowserTest, PageLoads) {
  ui_test_utils::NavigateToURLBlockUntilNavigationsComplete(
      browser(), GURL(chrome::kChromeUIUserManagerURL), 1);
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  base::string16 title = web_contents->GetTitle();
  EXPECT_EQ(l10n_util::GetStringUTF16(IDS_PRODUCT_NAME), title);

  // If the page has loaded correctly, then there should be an account picker.
  int num_account_pickers = -1;
  ASSERT_TRUE(content::ExecuteScriptAndExtractInt(
      web_contents,
      "domAutomationController.send("
      "document.getElementsByClassName('account-picker').length)",
      &num_account_pickers));
  EXPECT_EQ(1, num_account_pickers);

  int num_pods = -1;
  ASSERT_TRUE(content::ExecuteScriptAndExtractInt(
      web_contents,
      "domAutomationController.send("
      "parseInt(document.getElementById('pod-row').getAttribute('ncolumns')))",
      &num_pods));

  // There should be one user pod for each profile.
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  EXPECT_EQ(num_pods, static_cast<int>(profile_manager->GetNumberOfProfiles()));
}
