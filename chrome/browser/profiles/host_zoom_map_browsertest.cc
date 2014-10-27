// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/host_zoom_map.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/prefs/pref_service.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/chrome_page_zoom.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/zoom/chrome_zoom_level_prefs.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"

namespace {

class ZoomLevelChangeObserver {
 public:
  explicit ZoomLevelChangeObserver(Profile* profile)
      : message_loop_runner_(new content::MessageLoopRunner) {
    content::HostZoomMap* host_zoom_map = static_cast<content::HostZoomMap*>(
        content::HostZoomMap::GetDefaultForBrowserContext(profile));
    subscription_ = host_zoom_map->AddZoomLevelChangedCallback(base::Bind(
        &ZoomLevelChangeObserver::OnZoomLevelChanged, base::Unretained(this)));
  }

  void BlockUntilZoomLevelForHostHasChanged(const std::string& host) {
    while (!std::count(changed_hosts_.begin(), changed_hosts_.end(), host)) {
      message_loop_runner_->Run();
      message_loop_runner_ = new content::MessageLoopRunner;
    }
    changed_hosts_.clear();
  }

 private:
  void OnZoomLevelChanged(const content::HostZoomMap::ZoomLevelChange& change) {
    changed_hosts_.push_back(change.host);
    message_loop_runner_->Quit();
  }

  scoped_refptr<content::MessageLoopRunner> message_loop_runner_;
  std::vector<std::string> changed_hosts_;
  scoped_ptr<content::HostZoomMap::Subscription> subscription_;

  DISALLOW_COPY_AND_ASSIGN(ZoomLevelChangeObserver);
};

}  // namespace

class HostZoomMapBrowserTest : public InProcessBrowserTest {
 public:
  HostZoomMapBrowserTest() {}

 protected:
  void SetDefaultZoomLevel(double level) {
    browser()->profile()->GetZoomLevelPrefs()->SetDefaultZoomLevelPref(level);
  }

  double GetZoomLevel(const GURL& url) {
    content::HostZoomMap* host_zoom_map = static_cast<content::HostZoomMap*>(
        content::HostZoomMap::GetDefaultForBrowserContext(
            browser()->profile()));
    return host_zoom_map->GetZoomLevelForHostAndScheme(url.scheme(),
                                                       url.host());
  }

  std::vector<std::string> GetHostsWithZoomLevels() {
    typedef content::HostZoomMap::ZoomLevelVector ZoomLevelVector;
    content::HostZoomMap* host_zoom_map = static_cast<content::HostZoomMap*>(
        content::HostZoomMap::GetDefaultForBrowserContext(
            browser()->profile()));
    content::HostZoomMap::ZoomLevelVector zoom_levels =
        host_zoom_map->GetAllZoomLevels();
    std::vector<std::string> results;
    for (ZoomLevelVector::const_iterator it = zoom_levels.begin();
         it != zoom_levels.end(); ++it)
      results.push_back(it->host);
    return results;
  }

  std::vector<std::string> GetHostsWithZoomLevelsFromPrefs() {
    PrefService* prefs = browser()->profile()->GetPrefs();
    const base::DictionaryValue* dictionaries =
        prefs->GetDictionary(prefs::kPartitionPerHostZoomLevels);
    const base::DictionaryValue* values = NULL;
    std::string partition_key =
        chrome::ChromeZoomLevelPrefs::GetHashForTesting(base::FilePath());
    dictionaries->GetDictionary(partition_key, &values);
    std::vector<std::string> results;
    if (values) {
      for (base::DictionaryValue::Iterator it(*values);
           !it.IsAtEnd(); it.Advance())
        results.push_back(it.key());
    }
    return results;
  }

  GURL ConstructTestServerURL(const char* url_template) {
    return GURL(base::StringPrintf(
        url_template, embedded_test_server()->port()));
  }

 private:
  scoped_ptr<net::test_server::HttpResponse> HandleRequest(
      const net::test_server::HttpRequest& request) {
    return scoped_ptr<net::test_server::HttpResponse>(
        new net::test_server::BasicHttpResponse);
  }

  // BrowserTestBase:
  void SetUpOnMainThread() override {
    ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
    embedded_test_server()->RegisterRequestHandler(base::Bind(
        &HostZoomMapBrowserTest::HandleRequest, base::Unretained(this)));
    host_resolver()->AddRule("*", "127.0.0.1");
  }

  DISALLOW_COPY_AND_ASSIGN(HostZoomMapBrowserTest);
};

#define PARTITION_KEY_PLACEHOLDER "NNN"

class HostZoomMapBrowserTestWithPrefs : public HostZoomMapBrowserTest {
 public:
  explicit HostZoomMapBrowserTestWithPrefs(const std::string& prefs_data)
      : prefs_data_(prefs_data) {}

 private:
  // InProcessBrowserTest:
  bool SetUpUserDataDirectory() override {
    std::replace(prefs_data_.begin(), prefs_data_.end(), '\'', '\"');
    // It seems the hash functions on different platforms can return different
    // values for the same input, so make sure we test with the hash appropriate
    // for the platform.
    std::string hash_string =
        chrome::ChromeZoomLevelPrefs::GetHashForTesting(base::FilePath());
    std::string partition_key_placeholder(PARTITION_KEY_PLACEHOLDER);
    size_t start_index;
    while ((start_index = prefs_data_.find(partition_key_placeholder)) !=
           std::string::npos) {
      prefs_data_.replace(
          start_index, partition_key_placeholder.size(), hash_string);
    }

    base::FilePath user_data_directory, path_to_prefs;
    PathService::Get(chrome::DIR_USER_DATA, &user_data_directory);
    path_to_prefs = user_data_directory
        .AppendASCII(TestingProfile::kTestUserProfileDir)
        .Append(chrome::kPreferencesFilename);
    base::CreateDirectory(path_to_prefs.DirName());
    base::WriteFile(
        path_to_prefs, prefs_data_.c_str(), prefs_data_.size());
    return true;
  }

  std::string prefs_data_;

  DISALLOW_COPY_AND_ASSIGN(HostZoomMapBrowserTestWithPrefs);
};

// Zoom-related preferences demonstrating the two problems that
// could be caused by the bug. They incorrectly contain a per-host
// zoom level for the empty host; and a value for 'host1' that only
// differs from the default by epsilon. Neither should have been
// persisted.
const char kSanitizationTestPrefs[] =
    "{'partition': {"
    "   'default_zoom_level': { '" PARTITION_KEY_PLACEHOLDER "': 1.2 },"
    "   'per_host_zoom_levels': {"
    "     '" PARTITION_KEY_PLACEHOLDER "': {"
    "       '': 1.1, 'host1': 1.20001, 'host2': 1.3 }"
    "   }"
    "}}";

#undef PARTITION_KEY_PLACEHOLDER

class HostZoomMapSanitizationBrowserTest
    : public HostZoomMapBrowserTestWithPrefs {
 public:
  HostZoomMapSanitizationBrowserTest()
      : HostZoomMapBrowserTestWithPrefs(kSanitizationTestPrefs) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(HostZoomMapSanitizationBrowserTest);
};

// Regression test for crbug.com/364399.
IN_PROC_BROWSER_TEST_F(HostZoomMapBrowserTest, ToggleDefaultZoomLevel) {
  const double default_zoom_level = content::ZoomFactorToZoomLevel(1.5);

  const char kTestURLTemplate1[] = "http://host1:%d/";
  const char kTestURLTemplate2[] = "http://host2:%d/";

  ZoomLevelChangeObserver observer(browser()->profile());

  GURL test_url1 = ConstructTestServerURL(kTestURLTemplate1);
  ui_test_utils::NavigateToURL(browser(), test_url1);

  SetDefaultZoomLevel(default_zoom_level);
  observer.BlockUntilZoomLevelForHostHasChanged(test_url1.host());
  EXPECT_TRUE(
      content::ZoomValuesEqual(default_zoom_level, GetZoomLevel(test_url1)));

  GURL test_url2 = ConstructTestServerURL(kTestURLTemplate2);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), test_url2, NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  EXPECT_TRUE(
      content::ZoomValuesEqual(default_zoom_level, GetZoomLevel(test_url2)));

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  chrome_page_zoom::Zoom(web_contents, content::PAGE_ZOOM_OUT);
  observer.BlockUntilZoomLevelForHostHasChanged(test_url2.host());
  EXPECT_FALSE(
      content::ZoomValuesEqual(default_zoom_level, GetZoomLevel(test_url2)));

  chrome_page_zoom::Zoom(web_contents, content::PAGE_ZOOM_IN);
  observer.BlockUntilZoomLevelForHostHasChanged(test_url2.host());
  EXPECT_TRUE(
      content::ZoomValuesEqual(default_zoom_level, GetZoomLevel(test_url2)));

  // Now both tabs should be at the default zoom level, so there should not be
  // any per-host values saved either to Pref, or internally in HostZoomMap.
  EXPECT_TRUE(GetHostsWithZoomLevels().empty());
  EXPECT_TRUE(GetHostsWithZoomLevelsFromPrefs().empty());
}

// Test that garbage data from crbug.com/364399 is cleared up on startup.
IN_PROC_BROWSER_TEST_F(HostZoomMapSanitizationBrowserTest, ClearOnStartup) {
  EXPECT_THAT(GetHostsWithZoomLevels(), testing::ElementsAre("host2"));
  EXPECT_THAT(GetHostsWithZoomLevelsFromPrefs(), testing::ElementsAre("host2"));
}

// In this case we migrate the zoom level data from the profile prefs.
const char kMigrationTestPrefs[] =
    "{'profile': {"
    "   'default_zoom_level': 1.2,"
    "   'per_host_zoom_levels': {'': 1.1, 'host1': 1.20001, 'host2': "
    "1.3}"
    "}}";

class HostZoomMapMigrationBrowserTest : public HostZoomMapBrowserTestWithPrefs {
 public:
  HostZoomMapMigrationBrowserTest()
      : HostZoomMapBrowserTestWithPrefs(kMigrationTestPrefs) {}

  static const double kOriginalDefaultZoomLevel;

 private:
  DISALLOW_COPY_AND_ASSIGN(HostZoomMapMigrationBrowserTest);
};

const double HostZoomMapMigrationBrowserTest::kOriginalDefaultZoomLevel = 1.2;

// This test is the same as HostZoomMapSanitizationBrowserTest, except that the
// zoom level data is loaded from the profile prefs, transfered to the
// zoom-level prefs, and we verify that the profile zoom level prefs are
// erased in the process. We also test that changes to the host zoom map and the
// default zoom level don't propagate back to the profile prefs.
IN_PROC_BROWSER_TEST_F(HostZoomMapMigrationBrowserTest,
                       MigrateProfileZoomPreferences) {
  EXPECT_THAT(GetHostsWithZoomLevels(), testing::ElementsAre("host2"));
  EXPECT_THAT(GetHostsWithZoomLevelsFromPrefs(), testing::ElementsAre("host2"));

  PrefService* profile_prefs =
      browser()->profile()->GetPrefs();
  chrome::ChromeZoomLevelPrefs* zoom_level_prefs =
      browser()->profile()->GetZoomLevelPrefs();
  // Make sure that the profile pref for default zoom level has been set to
  // its default value of 0.0.
  EXPECT_EQ(0.0, profile_prefs->GetDouble(prefs::kDefaultZoomLevelDeprecated));
  EXPECT_EQ(kOriginalDefaultZoomLevel,
            zoom_level_prefs->GetDefaultZoomLevelPref());

  // Make sure that the profile prefs for per-host zoom levels are erased.
  {
    const base::DictionaryValue* profile_host_zoom_dictionary =
        profile_prefs->GetDictionary(prefs::kPerHostZoomLevelsDeprecated);
    EXPECT_EQ(0UL, profile_host_zoom_dictionary->size());
  }

  ZoomLevelChangeObserver observer(browser()->profile());
  content::HostZoomMap* host_zoom_map = static_cast<content::HostZoomMap*>(
      content::HostZoomMap::GetDefaultForBrowserContext(
          browser()->profile()));

  // Make sure that a change to a host zoom level doesn't propagate to the
  // profile prefs.
  std::string host3("host3");
  host_zoom_map->SetZoomLevelForHost(host3, 1.3);
  observer.BlockUntilZoomLevelForHostHasChanged(host3);
  EXPECT_THAT(GetHostsWithZoomLevelsFromPrefs(),
              testing::ElementsAre("host2", host3));
  {
    const base::DictionaryValue* profile_host_zoom_dictionary =
        profile_prefs->GetDictionary(prefs::kPerHostZoomLevelsDeprecated);
    EXPECT_EQ(0UL, profile_host_zoom_dictionary->size());
  }

  // Make sure a change to the default zoom level doesn't propagate to the
  // profile prefs.

  // First, we need a host at the default zoom level to respond when the
  // default zoom level changes.
  const double kNewDefaultZoomLevel = 1.5;
  GURL test_url = ConstructTestServerURL("http://host4:%d/");
  ui_test_utils::NavigateToURL(browser(), test_url);
  EXPECT_TRUE(content::ZoomValuesEqual(kOriginalDefaultZoomLevel,
                                       GetZoomLevel(test_url)));

  // Change the default zoom level and observe.
  SetDefaultZoomLevel(kNewDefaultZoomLevel);
  observer.BlockUntilZoomLevelForHostHasChanged(test_url.host());
  EXPECT_TRUE(
      content::ZoomValuesEqual(kNewDefaultZoomLevel, GetZoomLevel(test_url)));
  EXPECT_EQ(kNewDefaultZoomLevel, zoom_level_prefs->GetDefaultZoomLevelPref());
  EXPECT_EQ(0.0, profile_prefs->GetDouble(prefs::kDefaultZoomLevelDeprecated));
}

