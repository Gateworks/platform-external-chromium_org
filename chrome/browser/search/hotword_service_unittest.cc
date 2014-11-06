// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/metrics/field_trial.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/extension_service_test_base.h"
#include "chrome/browser/extensions/test_extension_service.h"
#include "chrome/browser/search/hotword_service.h"
#include "chrome/browser/search/hotword_service_factory.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/manifest.h"
#include "extensions/common/one_shot_event.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class MockHotwordService : public HotwordService {
 public:
  explicit MockHotwordService(Profile* profile)
      : HotwordService(profile),
        uninstall_count_(0) {
  }

  bool UninstallHotwordExtension(ExtensionService* extension_service) override {
    uninstall_count_++;
    return HotwordService::UninstallHotwordExtension(extension_service);
  }

  void InstallHotwordExtensionFromWebstore() override {
    scoped_ptr<base::DictionaryValue> manifest =
        extensions::DictionaryBuilder()
        .Set("name", "Hotword Test Extension")
        .Set("version", "1.0")
        .Set("manifest_version", 2)
        .Build();
    scoped_refptr<extensions::Extension> extension =
        extensions::ExtensionBuilder().SetManifest(manifest.Pass())
        .AddFlags(extensions::Extension::FROM_WEBSTORE
                  | extensions::Extension::WAS_INSTALLED_BY_DEFAULT)
        .SetID(extension_id_)
        .SetLocation(extensions::Manifest::EXTERNAL_COMPONENT)
        .Build();
    ASSERT_TRUE(extension.get());
    service_->OnExtensionInstalled(extension.get(), syncer::StringOrdinal());
  }


  int uninstall_count() { return uninstall_count_; }

  void SetExtensionService(ExtensionService* service) { service_ = service; }
  void SetExtensionId(const std::string& extension_id) {
    extension_id_ = extension_id;
  }

  ExtensionService* extension_service() { return service_; }

 private:
  ExtensionService* service_;
  int uninstall_count_;
  std::string extension_id_;
};

KeyedService* BuildMockHotwordService(content::BrowserContext* context) {
  return new MockHotwordService(static_cast<Profile*>(context));
}

}  // namespace

class HotwordServiceTest :
  public extensions::ExtensionServiceTestBase,
  public ::testing::WithParamInterface<const char*> {
 protected:
  HotwordServiceTest() : field_trial_list_(NULL) {}
  virtual ~HotwordServiceTest() {}

  void SetApplicationLocale(Profile* profile, const std::string& new_locale) {
#if defined(OS_CHROMEOS)
        // On ChromeOS locale is per-profile.
    profile->GetPrefs()->SetString(prefs::kApplicationLocale, new_locale);
#else
    g_browser_process->SetApplicationLocale(new_locale);
#endif
  }

  void SetUp() override {
    extension_id_ = GetParam();
    if (extension_id_ == extension_misc::kHotwordSharedModuleId) {
      base::CommandLine::ForCurrentProcess()->AppendSwitch(
          switches::kEnableExperimentalHotwording);
    }

    extensions::ExtensionServiceTestBase::SetUp();
  }

  base::FieldTrialList field_trial_list_;
  std::string extension_id_;
};

INSTANTIATE_TEST_CASE_P(HotwordServiceTests,
                        HotwordServiceTest,
                        ::testing::Values(
                            extension_misc::kHotwordExtensionId,
                            extension_misc::kHotwordSharedModuleId));

TEST_P(HotwordServiceTest, IsHotwordAllowedBadFieldTrial) {
  TestingProfile::Builder profile_builder;
  scoped_ptr<TestingProfile> profile = profile_builder.Build();

  HotwordServiceFactory* hotword_service_factory =
      HotwordServiceFactory::GetInstance();

  // Check that the service exists so that a NULL service be ruled out in
  // following tests.
  HotwordService* hotword_service =
      hotword_service_factory->GetForProfile(profile.get());
  EXPECT_TRUE(hotword_service != NULL);

  // When the field trial is empty or Disabled, it should not be allowed.
  std::string group = base::FieldTrialList::FindFullName(
      hotword_internal::kHotwordFieldTrialName);
  EXPECT_TRUE(group.empty());
  EXPECT_FALSE(HotwordServiceFactory::IsHotwordAllowed(profile.get()));

  ASSERT_TRUE(base::FieldTrialList::CreateFieldTrial(
     hotword_internal::kHotwordFieldTrialName,
     hotword_internal::kHotwordFieldTrialDisabledGroupName));
  group = base::FieldTrialList::FindFullName(
      hotword_internal::kHotwordFieldTrialName);
  EXPECT_TRUE(group ==hotword_internal::kHotwordFieldTrialDisabledGroupName);
  EXPECT_FALSE(HotwordServiceFactory::IsHotwordAllowed(profile.get()));

  // Set a valid locale with invalid field trial to be sure it is
  // still false.
  SetApplicationLocale(static_cast<Profile*>(profile.get()), "en");
  EXPECT_FALSE(HotwordServiceFactory::IsHotwordAllowed(profile.get()));

  // Test that incognito returns false as well.
  EXPECT_FALSE(HotwordServiceFactory::IsHotwordAllowed(
      profile->GetOffTheRecordProfile()));
}

TEST_P(HotwordServiceTest, IsHotwordAllowedLocale) {
  TestingProfile::Builder profile_builder;
  scoped_ptr<TestingProfile> profile = profile_builder.Build();

  HotwordServiceFactory* hotword_service_factory =
      HotwordServiceFactory::GetInstance();

  // Check that the service exists so that a NULL service be ruled out in
  // following tests.
  HotwordService* hotword_service =
      hotword_service_factory->GetForProfile(profile.get());
  EXPECT_TRUE(hotword_service != NULL);

  // Set the field trial to a valid one.
  ASSERT_TRUE(base::FieldTrialList::CreateFieldTrial(
      hotword_internal::kHotwordFieldTrialName, "Good"));

  // Set the language to an invalid one.
  SetApplicationLocale(static_cast<Profile*>(profile.get()), "non-valid");
  EXPECT_FALSE(HotwordServiceFactory::IsHotwordAllowed(profile.get()));

  // Now with valid locales it should be fine.
  SetApplicationLocale(static_cast<Profile*>(profile.get()), "en");
  EXPECT_TRUE(HotwordServiceFactory::IsHotwordAllowed(profile.get()));
  SetApplicationLocale(static_cast<Profile*>(profile.get()), "en-US");
  EXPECT_TRUE(HotwordServiceFactory::IsHotwordAllowed(profile.get()));
  SetApplicationLocale(static_cast<Profile*>(profile.get()), "en_us");
  EXPECT_TRUE(HotwordServiceFactory::IsHotwordAllowed(profile.get()));
  SetApplicationLocale(static_cast<Profile*>(profile.get()), "de_DE");
  EXPECT_TRUE(HotwordServiceFactory::IsHotwordAllowed(profile.get()));
  SetApplicationLocale(static_cast<Profile*>(profile.get()), "fr_fr");
  EXPECT_TRUE(HotwordServiceFactory::IsHotwordAllowed(profile.get()));

  // Test that incognito even with a valid locale and valid field trial
  // still returns false.
  Profile* otr_profile = profile->GetOffTheRecordProfile();
  SetApplicationLocale(otr_profile, "en");
  EXPECT_FALSE(HotwordServiceFactory::IsHotwordAllowed(otr_profile));
}

TEST_P(HotwordServiceTest, AudioLoggingPrefSetCorrectly) {
  TestingProfile::Builder profile_builder;
  scoped_ptr<TestingProfile> profile = profile_builder.Build();

  HotwordServiceFactory* hotword_service_factory =
      HotwordServiceFactory::GetInstance();
  HotwordService* hotword_service =
      hotword_service_factory->GetForProfile(profile.get());
  EXPECT_TRUE(hotword_service != NULL);

  // If it's a fresh profile, although the default value is true,
  // it should return false if the preference has never been set.
  EXPECT_FALSE(hotword_service->IsOptedIntoAudioLogging());
}

TEST_P(HotwordServiceTest, ShouldReinstallExtension) {
  // Set the field trial to a valid one.
  ASSERT_TRUE(base::FieldTrialList::CreateFieldTrial(
      hotword_internal::kHotwordFieldTrialName, "Install"));

  InitializeEmptyExtensionService();

  HotwordServiceFactory* hotword_service_factory =
      HotwordServiceFactory::GetInstance();

  MockHotwordService* hotword_service = static_cast<MockHotwordService*>(
      hotword_service_factory->SetTestingFactoryAndUse(
          profile(), BuildMockHotwordService));
  EXPECT_TRUE(hotword_service != NULL);
  hotword_service->SetExtensionId(extension_id_);

  // If no locale has been set, no reason to uninstall.
  EXPECT_FALSE(hotword_service->ShouldReinstallHotwordExtension());

  SetApplicationLocale(profile(), "en");
  hotword_service->SetPreviousLanguagePref();

  // Now a locale is set, but it hasn't changed.
  EXPECT_FALSE(hotword_service->ShouldReinstallHotwordExtension());

  SetApplicationLocale(profile(), "fr_fr");

  // Now it's a different locale so it should uninstall.
  EXPECT_TRUE(hotword_service->ShouldReinstallHotwordExtension());
}

TEST_P(HotwordServiceTest, PreviousLanguageSetOnInstall) {
  // Set the field trial to a valid one.
  ASSERT_TRUE(base::FieldTrialList::CreateFieldTrial(
      hotword_internal::kHotwordFieldTrialName, "Install"));

  InitializeEmptyExtensionService();
  service_->Init();

  HotwordServiceFactory* hotword_service_factory =
      HotwordServiceFactory::GetInstance();

  MockHotwordService* hotword_service = static_cast<MockHotwordService*>(
      hotword_service_factory->SetTestingFactoryAndUse(
          profile(), BuildMockHotwordService));
  EXPECT_TRUE(hotword_service != NULL);
  hotword_service->SetExtensionService(service());
  hotword_service->SetExtensionId(extension_id_);

  // If no locale has been set, no reason to uninstall.
  EXPECT_FALSE(hotword_service->ShouldReinstallHotwordExtension());

  SetApplicationLocale(profile(), "test_locale");

  hotword_service->InstallHotwordExtensionFromWebstore();
  base::MessageLoop::current()->RunUntilIdle();

  EXPECT_EQ("test_locale",
            profile()->GetPrefs()->GetString(prefs::kHotwordPreviousLanguage));
}

TEST_P(HotwordServiceTest, UninstallReinstallTriggeredCorrectly) {
  // Set the field trial to a valid one.
  ASSERT_TRUE(base::FieldTrialList::CreateFieldTrial(
      hotword_internal::kHotwordFieldTrialName, "Install"));

  InitializeEmptyExtensionService();
  service_->Init();

  HotwordServiceFactory* hotword_service_factory =
      HotwordServiceFactory::GetInstance();

  MockHotwordService* hotword_service = static_cast<MockHotwordService*>(
      hotword_service_factory->SetTestingFactoryAndUse(
          profile(), BuildMockHotwordService));
  EXPECT_TRUE(hotword_service != NULL);
  hotword_service->SetExtensionService(service());
  hotword_service->SetExtensionId(extension_id_);

  // Initialize the locale to "en".
  SetApplicationLocale(profile(), "en");

  // The previous locale should not be set. No reason to uninstall.
  EXPECT_FALSE(hotword_service->MaybeReinstallHotwordExtension());

   // Do an initial installation.
  hotword_service->InstallHotwordExtensionFromWebstore();
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_EQ("en",
            profile()->GetPrefs()->GetString(prefs::kHotwordPreviousLanguage));

  if (extension_id_ == extension_misc::kHotwordSharedModuleId) {
    // Shared module is installed and enabled.
    EXPECT_EQ(0U, registry()->disabled_extensions().size());
    EXPECT_TRUE(registry()->enabled_extensions().Contains(extension_id_));
  } else {
    // Verify the extension is installed but disabled.
    EXPECT_EQ(1U, registry()->disabled_extensions().size());
    EXPECT_TRUE(registry()->disabled_extensions().Contains(extension_id_));
  }

   // The previous locale should be set but should match the current
  // locale. No reason to uninstall.
  EXPECT_FALSE(hotword_service->MaybeReinstallHotwordExtension());

  // Switch the locale to a valid but different one.
  SetApplicationLocale(profile(), "fr_fr");
  EXPECT_TRUE(HotwordServiceFactory::IsHotwordAllowed(profile()));

   // Different but valid locale so expect uninstall.
  EXPECT_TRUE(hotword_service->MaybeReinstallHotwordExtension());
  EXPECT_EQ(1, hotword_service->uninstall_count());
  EXPECT_EQ("fr_fr",
            profile()->GetPrefs()->GetString(prefs::kHotwordPreviousLanguage));

  if (extension_id_ == extension_misc::kHotwordSharedModuleId) {
    // Shared module is installed and enabled.
    EXPECT_TRUE(registry()->enabled_extensions().Contains(extension_id_));
  } else {
    // Verify the extension is installed. It's still disabled.
    EXPECT_TRUE(registry()->disabled_extensions().Contains(extension_id_));
  }

  // Switch the locale to an invalid one.
  SetApplicationLocale(profile(), "invalid");
  EXPECT_FALSE(HotwordServiceFactory::IsHotwordAllowed(profile()));
  EXPECT_FALSE(hotword_service->MaybeReinstallHotwordExtension());
  EXPECT_EQ("fr_fr",
            profile()->GetPrefs()->GetString(prefs::kHotwordPreviousLanguage));

  // If the locale is set back to the last valid one, then an uninstall-install
  // shouldn't be needed.
  SetApplicationLocale(profile(), "fr_fr");
  EXPECT_TRUE(HotwordServiceFactory::IsHotwordAllowed(profile()));
  EXPECT_FALSE(hotword_service->MaybeReinstallHotwordExtension());
  EXPECT_EQ(1, hotword_service->uninstall_count());  // no change
}

TEST_P(HotwordServiceTest, DisableAlwaysOnOnLanguageChange) {
  // Bypass test for old hotwording.
  if (extension_id_ != extension_misc::kHotwordSharedModuleId)
    return;

  // Set the field trial to a valid one.
  ASSERT_TRUE(base::FieldTrialList::CreateFieldTrial(
      hotword_internal::kHotwordFieldTrialName, "Install"));

  InitializeEmptyExtensionService();
  service_->Init();

  // Enable always-on.
  profile()->GetPrefs()->SetBoolean(prefs::kHotwordAlwaysOnSearchEnabled, true);

  HotwordServiceFactory* hotword_service_factory =
      HotwordServiceFactory::GetInstance();

  MockHotwordService* hotword_service = static_cast<MockHotwordService*>(
      hotword_service_factory->SetTestingFactoryAndUse(
          profile(), BuildMockHotwordService));
  EXPECT_TRUE(hotword_service != NULL);
  hotword_service->SetExtensionService(service());
  hotword_service->SetExtensionId(extension_id_);

  // Initialize the locale to "en".
  SetApplicationLocale(profile(), "en");

  // The previous locale should not be set. No reason to uninstall.
  EXPECT_FALSE(hotword_service->MaybeReinstallHotwordExtension());
  EXPECT_TRUE(hotword_service->IsAlwaysOnEnabled());

  // Do an initial installation.
  hotword_service->InstallHotwordExtensionFromWebstore();
  base::MessageLoop::current()->RunUntilIdle();

  // The previous locale should be set but should match the current
  // locale. No reason to uninstall.
  EXPECT_FALSE(hotword_service->MaybeReinstallHotwordExtension());
  EXPECT_TRUE(hotword_service->IsAlwaysOnEnabled());

  // Switch the locale to a valid but different one.
  SetApplicationLocale(profile(), "fr_fr");
  EXPECT_TRUE(HotwordServiceFactory::IsHotwordAllowed(profile()));

  // Different but valid locale so expect uninstall.
  EXPECT_TRUE(hotword_service->MaybeReinstallHotwordExtension());
  EXPECT_FALSE(hotword_service->IsAlwaysOnEnabled());

  // Re-enable always-on.
  profile()->GetPrefs()->SetBoolean(prefs::kHotwordAlwaysOnSearchEnabled, true);

  // Switch the locale to an invalid one.
  SetApplicationLocale(profile(), "invalid");
  EXPECT_FALSE(HotwordServiceFactory::IsHotwordAllowed(profile()));
  EXPECT_FALSE(hotword_service->MaybeReinstallHotwordExtension());
  EXPECT_TRUE(hotword_service->IsAlwaysOnEnabled());

  // If the locale is set back to the last valid one, then an uninstall-install
  // shouldn't be needed.
  SetApplicationLocale(profile(), "fr_fr");
  EXPECT_TRUE(HotwordServiceFactory::IsHotwordAllowed(profile()));
  EXPECT_FALSE(hotword_service->MaybeReinstallHotwordExtension());
  EXPECT_TRUE(hotword_service->IsAlwaysOnEnabled());
}
