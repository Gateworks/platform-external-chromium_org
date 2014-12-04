// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/preferences/pref_service_bridge.h"

#include <jni.h>

#include "base/android/build_info.h"
#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/jni_weak_ref.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "chrome/browser/browsing_data/browsing_data_remover.h"
#include "chrome/browser/net/prediction_options.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/android/android_about_app_info.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/locale_settings.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/translate/core/browser/translate_prefs.h"
#include "components/translate/core/common/translate_pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/user_agent.h"
#include "jni/PrefServiceBridge_jni.h"
#include "ui/base/l10n/l10n_util.h"

using base::android::AttachCurrentThread;
using base::android::CheckException;
using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::ScopedJavaLocalRef;
using content::BrowserThread;

namespace {

enum NetworkPredictionOptions {
  NETWORK_PREDICTION_ALWAYS,
  NETWORK_PREDICTION_WIFI_ONLY,
  NETWORK_PREDICTION_NEVER,
};

Profile* GetOriginalProfile() {
  return ProfileManager::GetActiveUserProfile()->GetOriginalProfile();
}

bool GetBooleanForContentSetting(HostContentSettingsMap* content_settings,
                                 ContentSettingsType type) {
  switch (content_settings->GetDefaultContentSetting(type, NULL)) {
    case CONTENT_SETTING_BLOCK:
      return false;
    case CONTENT_SETTING_ALLOW:
      return true;
    case CONTENT_SETTING_ASK:
    default:
      return true;
  }
}

std::string GetStringForContentSettingsType(
    ContentSetting content_setting) {
  switch (content_setting) {
    case CONTENT_SETTING_BLOCK:
      return "block";
    case CONTENT_SETTING_ALLOW:
      return "allow";
    case CONTENT_SETTING_ASK:
      return "ask";
    case CONTENT_SETTING_SESSION_ONLY:
      return "session";
    case CONTENT_SETTING_NUM_SETTINGS:
      return "num_settings";
    case CONTENT_SETTING_DEFAULT:
    default:
      return "default";
  }
}

bool IsContentSettingManaged(HostContentSettingsMap* content_settings,
                             ContentSettingsType content_settings_type) {
  std::string source;
  content_settings->GetDefaultContentSetting(content_settings_type, &source);
  HostContentSettingsMap::ProviderType provider =
      content_settings->GetProviderTypeFromSource(source);
  return provider == HostContentSettingsMap::POLICY_PROVIDER;
}

void ReturnAbsoluteProfilePathValue(std::string path_value) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jstring> j_path_value =
      ConvertUTF8ToJavaString(env, path_value);
  Java_PrefServiceBridge_setProfilePathValue(env, j_path_value.obj());
}

void GetAbsolutePath(Profile* profile) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  if (profile) {
    base::FilePath profile_path = profile->GetPath();
    profile_path = base::MakeAbsoluteFilePath(profile_path);
    if (!profile_path.empty()) {
      BrowserThread::PostTask(
          BrowserThread::UI, FROM_HERE,
          base::Bind(&ReturnAbsoluteProfilePathValue, profile_path.value()));
    }
  }
}

PrefService* GetPrefService() {
  return GetOriginalProfile()->GetPrefs();
}

static void EnsureConsistentGeolocationPreferences(Profile* profile) {
  // On Android, we use the kGeolocationEnabled flag to control geolocation on a
  // global basis, rather than the default geolocation host content setting,
  // which is only used if no previously stored more specific host exception
  // cannot be found.
  //
  // On Android, there is currently no UI to change this default setting, so it
  // needs to default to ASK. Additionally, for users that have previously set
  // the default to BLOCK, we set the preference to disable geolocation
  // globally.

  ContentSetting defaultSetting = profile->GetHostContentSettingsMap()->
      GetDefaultContentSetting(CONTENT_SETTINGS_TYPE_GEOLOCATION, NULL);

  if (defaultSetting == CONTENT_SETTING_ASK)
    return;

  profile->GetHostContentSettingsMap()->SetDefaultContentSetting(
      CONTENT_SETTINGS_TYPE_GEOLOCATION, CONTENT_SETTING_ASK);

  if (defaultSetting == CONTENT_SETTING_BLOCK) {
    profile->GetPrefs()->SetBoolean(prefs::kGeolocationEnabled, false);
  }
}

static void EnsureConsistentProtectedMediaIdentifierPreferences(
    Profile* profile) {
  // We use the kProtectedMediaIdentifierEnabled flag to control protected media
  // identifier on a global basis.
  //
  // On Android, there is currently no UI to change this default setting, so it
  // needs to default to ASK. Additionally, for users that have previously set
  // the default to BLOCK, we set the preference to disable protected media
  // identifier globally.
  ContentSetting defaultSetting =
      profile->GetHostContentSettingsMap()->
      GetDefaultContentSetting(CONTENT_SETTINGS_TYPE_PROTECTED_MEDIA_IDENTIFIER,
                               NULL);

  if (defaultSetting == CONTENT_SETTING_ASK)
    return;

  profile->GetHostContentSettingsMap()->SetDefaultContentSetting(
      CONTENT_SETTINGS_TYPE_PROTECTED_MEDIA_IDENTIFIER, CONTENT_SETTING_ASK);

  if (defaultSetting == CONTENT_SETTING_BLOCK) {
    profile->GetPrefs()->SetBoolean(prefs::kProtectedMediaIdentifierEnabled,
                                    false);
  }
}

}  // namespace

// ----------------------------------------------------------------------------
// Native JNI methods
// ----------------------------------------------------------------------------

static jboolean GetAcceptCookiesEnabled(JNIEnv* env, jobject obj) {
  return GetBooleanForContentSetting(
      GetOriginalProfile()->GetHostContentSettingsMap(),
      CONTENT_SETTINGS_TYPE_COOKIES);
}

static jboolean GetAcceptCookiesManaged(JNIEnv* env, jobject obj) {
  return IsContentSettingManaged(
      GetOriginalProfile()->GetHostContentSettingsMap(),
      CONTENT_SETTINGS_TYPE_COOKIES);
}

static jboolean GetBlockThirdPartyCookiesEnabled(JNIEnv* env, jobject obj) {
  return GetPrefService()->GetBoolean(prefs::kBlockThirdPartyCookies);
}

static jboolean GetBlockThirdPartyCookiesManaged(JNIEnv* env, jobject obj) {
  return GetPrefService()->IsManagedPreference(prefs::kBlockThirdPartyCookies);
}

static jboolean GetRememberPasswordsEnabled(JNIEnv* env, jobject obj) {
  return GetPrefService()->GetBoolean(
      password_manager::prefs::kPasswordManagerSavingEnabled);
}

static jboolean GetRememberPasswordsManaged(JNIEnv* env, jobject obj) {
  return GetPrefService()->IsManagedPreference(
      password_manager::prefs::kPasswordManagerSavingEnabled);
}

static jboolean GetDoNotTrackEnabled(JNIEnv* env, jobject obj) {
  return GetPrefService()->GetBoolean(prefs::kEnableDoNotTrack);
}

static jint GetNetworkPredictionOptions(JNIEnv* env, jobject obj) {
  return GetPrefService()->GetInteger(prefs::kNetworkPredictionOptions);
}

static jboolean GetNetworkPredictionManaged(JNIEnv* env, jobject obj) {
  return GetPrefService()->IsManagedPreference(
      prefs::kNetworkPredictionOptions);
}

static jboolean GetPasswordEchoEnabled(JNIEnv* env, jobject obj) {
  return GetPrefService()->GetBoolean(prefs::kWebKitPasswordEchoEnabled);
}

static jboolean GetPrintingEnabled(JNIEnv* env, jobject obj) {
  return GetPrefService()->GetBoolean(prefs::kPrintingEnabled);
}

static jboolean GetPrintingManaged(JNIEnv* env, jobject obj) {
  return GetPrefService()->IsManagedPreference(prefs::kPrintingEnabled);
}

static jboolean GetTranslateEnabled(JNIEnv* env, jobject obj) {
  return GetPrefService()->GetBoolean(prefs::kEnableTranslate);
}

static jboolean GetTranslateManaged(JNIEnv* env, jobject obj) {
  return GetPrefService()->IsManagedPreference(prefs::kEnableTranslate);
}

static jboolean GetSearchSuggestEnabled(JNIEnv* env, jobject obj) {
  return GetPrefService()->GetBoolean(prefs::kSearchSuggestEnabled);
}

static jboolean GetSearchSuggestManaged(JNIEnv* env, jobject obj) {
  return GetPrefService()->IsManagedPreference(prefs::kSearchSuggestEnabled);
}

static jboolean GetProtectedMediaIdentifierEnabled(JNIEnv* env, jobject obj) {
  Profile* profile = GetOriginalProfile();
  EnsureConsistentProtectedMediaIdentifierPreferences(profile);
  return GetPrefService()->GetBoolean(prefs::kProtectedMediaIdentifierEnabled);
}

static jboolean GetAllowLocationEnabled(JNIEnv* env, jobject obj) {
  Profile* profile = GetOriginalProfile();
  EnsureConsistentGeolocationPreferences(profile);
  return GetPrefService()->GetBoolean(prefs::kGeolocationEnabled);
}

static jboolean GetAllowLocationManaged(JNIEnv* env, jobject obj) {
  return IsContentSettingManaged(
      GetOriginalProfile()->GetHostContentSettingsMap(),
      CONTENT_SETTINGS_TYPE_GEOLOCATION);
}

static jboolean GetResolveNavigationErrorEnabled(JNIEnv* env, jobject obj) {
  return GetPrefService()->GetBoolean(prefs::kAlternateErrorPagesEnabled);
}

static jboolean GetResolveNavigationErrorManaged(JNIEnv* env, jobject obj) {
  return GetPrefService()->IsManagedPreference(
      prefs::kAlternateErrorPagesEnabled);
}

static jboolean GetCrashReportManaged(JNIEnv* env, jobject obj) {
  return GetPrefService()->IsManagedPreference(
      prefs::kCrashReportingEnabled);
}

static jboolean GetForceSafeSearch(JNIEnv* env, jobject obj) {
  return GetPrefService()->GetBoolean(prefs::kForceSafeSearch);
}

static jint GetDefaultSupervisedUserFilteringBehavior(JNIEnv* env,
                                                      jobject obj) {
  return GetPrefService()->GetInteger(
      prefs::kDefaultSupervisedUserFilteringBehavior);
}

static jboolean GetIncognitoModeEnabled(JNIEnv* env, jobject obj) {
  PrefService* prefs = GetPrefService();
  IncognitoModePrefs::Availability incognito_pref =
      IncognitoModePrefs::GetAvailability(prefs);
  DCHECK(incognito_pref == IncognitoModePrefs::ENABLED ||
         incognito_pref == IncognitoModePrefs::DISABLED) <<
             "Unsupported incognito mode preference: " << incognito_pref;
  return incognito_pref != IncognitoModePrefs::DISABLED;
}

static jboolean GetIncognitoModeManaged(JNIEnv* env, jobject obj) {
  return GetPrefService()->IsManagedPreference(
      prefs::kIncognitoModeAvailability);
}

namespace {

// Redirects a BrowsingDataRemover completion callback back into Java.
class ClearBrowsingDataObserver : public BrowsingDataRemover::Observer {
 public:
  // |obj| is expected to be the object passed into ClearBrowsingData(); e.g. a
  // ChromePreference.
  ClearBrowsingDataObserver(JNIEnv* env, jobject obj)
      : weak_chrome_native_preferences_(env, obj) {
  }

  virtual void OnBrowsingDataRemoverDone() override {
    // Just as a BrowsingDataRemover deletes itself when done, we delete ourself
    // when done.  No need to remove ourself as an observer given the lifetime
    // of BrowsingDataRemover.
    scoped_ptr<ClearBrowsingDataObserver> auto_delete(this);

    JNIEnv* env = AttachCurrentThread();
    if (weak_chrome_native_preferences_.get(env).is_null())
      return;

    Java_PrefServiceBridge_browsingDataCleared(
        env, weak_chrome_native_preferences_.get(env).obj());
  }

 private:
  JavaObjectWeakGlobalRef weak_chrome_native_preferences_;
};
}  // namespace

static void ClearBrowsingData(JNIEnv* env, jobject obj, jboolean history,
    jboolean cache, jboolean cookies_and_site_data, jboolean passwords,
    jboolean form_data) {
  // BrowsingDataRemover deletes itself.
  BrowsingDataRemover* browsing_data_remover =
      BrowsingDataRemover::CreateForPeriod(
          GetOriginalProfile(),
          static_cast<BrowsingDataRemover::TimePeriod>(
              BrowsingDataRemover::EVERYTHING));
  browsing_data_remover->AddObserver(new ClearBrowsingDataObserver(env, obj));

  int remove_mask = 0;
  if (history)
    remove_mask |= BrowsingDataRemover::REMOVE_HISTORY;
  if (cache)
    remove_mask |= BrowsingDataRemover::REMOVE_CACHE;
  if (cookies_and_site_data) {
    remove_mask |= BrowsingDataRemover::REMOVE_COOKIES;
    remove_mask |= BrowsingDataRemover::REMOVE_SITE_DATA;
  }
  if (passwords)
    remove_mask |= BrowsingDataRemover::REMOVE_PASSWORDS;
  if (form_data)
    remove_mask |= BrowsingDataRemover::REMOVE_FORM_DATA;
  browsing_data_remover->Remove(remove_mask,
                                BrowsingDataHelper::UNPROTECTED_WEB);
}

static void SetAllowCookiesEnabled(JNIEnv* env, jobject obj, jboolean allow) {
  HostContentSettingsMap* host_content_settings_map =
      GetOriginalProfile()->GetHostContentSettingsMap();
  host_content_settings_map->SetDefaultContentSetting(
      CONTENT_SETTINGS_TYPE_COOKIES,
      allow ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_BLOCK);
}

static void SetBlockThirdPartyCookiesEnabled(JNIEnv* env, jobject obj,
                                             jboolean enabled) {
  GetPrefService()->SetBoolean(prefs::kBlockThirdPartyCookies, enabled);
}

static void SetRememberPasswordsEnabled(JNIEnv* env, jobject obj,
                                        jboolean allow) {
  GetPrefService()->SetBoolean(
      password_manager::prefs::kPasswordManagerSavingEnabled, allow);
}

static void SetProtectedMediaIdentifierEnabled(JNIEnv* env,
                                               jobject obj,
                                               jboolean is_enabled) {
  GetPrefService()->SetBoolean(prefs::kProtectedMediaIdentifierEnabled,
                               is_enabled);
}

static void SetAllowLocationEnabled(JNIEnv* env, jobject obj, jboolean allow) {
  GetPrefService()->SetBoolean(prefs::kGeolocationEnabled, allow);
}

static void SetCrashReporting(JNIEnv* env, jobject obj, jboolean reporting) {
  PrefService* local_state = g_browser_process->local_state();
  local_state->SetBoolean(prefs::kCrashReportingEnabled, reporting);
}

static jboolean CanPredictNetworkActions(JNIEnv* env, jobject obj) {
  return chrome_browser_net::CanPrefetchAndPrerenderUI(GetPrefService());
}

static void SetDoNotTrackEnabled(JNIEnv* env, jobject obj, jboolean allow) {
  GetPrefService()->SetBoolean(prefs::kEnableDoNotTrack, allow);
}

static jstring GetSyncLastAccountName(JNIEnv* env, jobject obj) {
  return ConvertUTF8ToJavaString(
      env, GetPrefService()->GetString(prefs::kGoogleServicesLastUsername))
      .Release();
}

static void SetTranslateEnabled(JNIEnv* env, jobject obj, jboolean enabled) {
  GetPrefService()->SetBoolean(prefs::kEnableTranslate, enabled);
}

static void ResetTranslateDefaults(JNIEnv* env, jobject obj) {
  scoped_ptr<translate::TranslatePrefs> translate_prefs =
      ChromeTranslateClient::CreateTranslatePrefs(GetPrefService());
  translate_prefs->ResetToDefaults();
}

static jboolean GetJavaScriptManaged(JNIEnv* env, jobject obj) {
  HostContentSettingsMap* content_settings =
      GetOriginalProfile()->GetHostContentSettingsMap();
  return IsContentSettingManaged(
      content_settings, CONTENT_SETTINGS_TYPE_JAVASCRIPT);
}

static void SetJavaScriptEnabled(JNIEnv* env, jobject obj, jboolean enabled) {
  GetPrefService()->SetBoolean(prefs::kWebKitJavascriptEnabled, enabled);
}

static jboolean GetJavaScriptEnabled(JNIEnv* env, jobject obj) {
  // The user pref for Javascript is stored in kWebKitJavascriptEnabled for
  // historical reasons, but the content setting is where a possibly managed
  // value will be enforced.
  HostContentSettingsMap* content_settings =
      GetOriginalProfile()->GetHostContentSettingsMap();

  jboolean javascript_enabled = GetBooleanForContentSetting(
      content_settings, CONTENT_SETTINGS_TYPE_JAVASCRIPT);
  if (!GetJavaScriptManaged(env, obj)) {
    javascript_enabled &= GetPrefService()->GetBoolean(
        prefs::kWebKitJavascriptEnabled);
  }
  return javascript_enabled;
}

static void SetPasswordEchoEnabled(JNIEnv* env,
                                   jobject obj,
                                   jboolean passwordEchoEnabled) {
  GetPrefService()->SetBoolean(prefs::kWebKitPasswordEchoEnabled,
                               passwordEchoEnabled);
}

static jboolean GetAllowPopupsEnabled(JNIEnv* env, jobject obj) {
  HostContentSettingsMap* content_settings =
      GetOriginalProfile()->GetHostContentSettingsMap();
  return GetBooleanForContentSetting(content_settings,
      CONTENT_SETTINGS_TYPE_POPUPS);
}

static jboolean GetAllowPopupsManaged(JNIEnv* env, jobject obj) {
  HostContentSettingsMap* content_settings =
      GetOriginalProfile()->GetHostContentSettingsMap();
  return IsContentSettingManaged(content_settings,
                                 CONTENT_SETTINGS_TYPE_POPUPS);
}

static void SetAllowPopupsEnabled(JNIEnv* env, jobject obj, jboolean allow) {
  HostContentSettingsMap* host_content_settings_map =
      GetOriginalProfile()->GetHostContentSettingsMap();
  host_content_settings_map->SetDefaultContentSetting(
      CONTENT_SETTINGS_TYPE_POPUPS,
      allow ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_BLOCK);
}

static jboolean GetAutologinEnabled(JNIEnv* env, jobject obj) {
  return GetPrefService()->GetBoolean(prefs::kAutologinEnabled);
}

static void SetAutologinEnabled(JNIEnv* env, jobject obj,
                                jboolean autologinEnabled) {
  GetPrefService()->SetBoolean(prefs::kAutologinEnabled, autologinEnabled);
}

static void SetPopupException(JNIEnv* env, jobject obj, jstring pattern,
                              jboolean allow) {
  HostContentSettingsMap* host_content_settings_map =
      GetOriginalProfile()->GetHostContentSettingsMap();
  host_content_settings_map->SetContentSetting(
      ContentSettingsPattern::FromString(ConvertJavaStringToUTF8(env, pattern)),
      ContentSettingsPattern::Wildcard(),
      CONTENT_SETTINGS_TYPE_POPUPS,
      "",
      allow ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_BLOCK);
}

static void RemovePopupException(JNIEnv* env, jobject obj, jstring pattern) {
  HostContentSettingsMap* host_content_settings_map =
      GetOriginalProfile()->GetHostContentSettingsMap();
  host_content_settings_map->SetContentSetting(
      ContentSettingsPattern::FromString(ConvertJavaStringToUTF8(env, pattern)),
      ContentSettingsPattern::Wildcard(),
      CONTENT_SETTINGS_TYPE_POPUPS,
      "",
      CONTENT_SETTING_DEFAULT);
}

static void GetPopupExceptions(JNIEnv* env, jobject obj, jobject list) {
  HostContentSettingsMap* host_content_settings_map =
      GetOriginalProfile()->GetHostContentSettingsMap();
  ContentSettingsForOneType entries;
  host_content_settings_map->GetSettingsForOneType(
      CONTENT_SETTINGS_TYPE_POPUPS, "", &entries);
  for (size_t i = 0; i < entries.size(); ++i) {
    Java_PrefServiceBridge_insertPopupExceptionToList(
        env, list,
        ConvertUTF8ToJavaString(
            env, entries[i].primary_pattern.ToString()).obj(),
        ConvertUTF8ToJavaString(
            env, GetStringForContentSettingsType(entries[i].setting)).obj(),
        ConvertUTF8ToJavaString(env, entries[i].source).obj());
  }
}

static void SetSearchSuggestEnabled(JNIEnv* env, jobject obj,
                                    jboolean enabled) {
  GetPrefService()->SetBoolean(prefs::kSearchSuggestEnabled, enabled);
}

static jstring GetContextualSearchPreference(JNIEnv* env, jobject obj) {
  return ConvertUTF8ToJavaString(
      env, GetPrefService()->GetString(prefs::kContextualSearchEnabled)).
          Release();
}

static jboolean GetContextualSearchPreferenceIsManaged(JNIEnv* env,
                                                       jobject obj) {
  return GetPrefService()->IsManagedPreference(prefs::kContextualSearchEnabled);
}

static void SetContextualSearchPreference(JNIEnv* env, jobject obj,
                                          jstring pref) {
  GetPrefService()->SetString(prefs::kContextualSearchEnabled,
      ConvertJavaStringToUTF8(env, pref));
}

static void SetNetworkPredictionOptions(JNIEnv* env, jobject obj, int option) {
  GetPrefService()->SetInteger(prefs::kNetworkPredictionOptions, option);
}

static jboolean NetworkPredictionEnabledHasUserSetting(JNIEnv* env,
                                                       jobject obj) {
  return GetPrefService()->GetUserPrefValue(
      prefs::kNetworkPredictionEnabled) != NULL;
}

static jboolean NetworkPredictionOptionsHasUserSetting(JNIEnv* env,
                                                       jobject obj) {
  return GetPrefService()->GetUserPrefValue(
      prefs::kNetworkPredictionOptions) != NULL;
}

static jboolean GetNetworkPredictionEnabledUserPrefValue(JNIEnv* env,
                                                         jobject obj) {
  const base::Value* network_prediction_enabled =
      GetPrefService()->GetUserPrefValue(prefs::kNetworkPredictionEnabled);
  DCHECK(network_prediction_enabled);
  bool value = false;
  DCHECK(network_prediction_enabled->GetAsBoolean(&value));
  return value;
}

static void SetResolveNavigationErrorEnabled(JNIEnv* env, jobject obj,
                                             jboolean enabled) {
  GetPrefService()->SetBoolean(prefs::kAlternateErrorPagesEnabled, enabled);
}

static jboolean GetFirstRunEulaAccepted(JNIEnv* env, jobject obj) {
  return g_browser_process->local_state()->GetBoolean(prefs::kEulaAccepted);
}

static void SetEulaAccepted(JNIEnv* env, jobject obj) {
  g_browser_process->local_state()->SetBoolean(prefs::kEulaAccepted, true);
}

static void ResetAcceptLanguages(JNIEnv* env,
                                 jobject obj,
                                 jstring default_locale) {
  std::string accept_languages(l10n_util::GetStringUTF8(IDS_ACCEPT_LANGUAGES));
  std::string locale_string(ConvertJavaStringToUTF8(env, default_locale));

  PrependToAcceptLanguagesIfNecessary(locale_string, &accept_languages);
  GetPrefService()->SetString(prefs::kAcceptLanguages, accept_languages);
}

void PrependToAcceptLanguagesIfNecessary(std::string locale,
                                         std::string* accept_languages) {
  if (locale.size() != 5u || locale[2] != '_')  // not well-formed
    return;

  std::string language(locale.substr(0, 2));
  std::string region(locale.substr(3, 2));

  // Java mostly follows ISO-639-1 and ICU, except for the following three.
  // See documentation on java.util.Locale constructor for more.
  if (language == "iw") {
    language = "he";
  } else if (language == "ji") {
    language = "yi";
  } else if (language == "in") {
    language = "id";
  }

  std::string language_region(language + "-" + region);

  if (accept_languages->find(language_region) == std::string::npos) {
    std::vector<std::string> parts;
    parts.push_back(language_region);
    // If language is not in the accept languages list, also add language code.
    if (accept_languages->find(language + ",") == std::string::npos &&
        !std::equal(language.rbegin(), language.rend(),
                    accept_languages->rbegin()))
      parts.push_back(language);
    parts.push_back(*accept_languages);
    *accept_languages = JoinString(parts, ',');
  }
}

// Sends all information about the different versions to Java.
// From browser_about_handler.cc
static jobject GetAboutVersionStrings(JNIEnv* env, jobject obj) {
  chrome::VersionInfo version_info;
  std::string os_version = version_info.OSType();
  os_version += " " + AndroidAboutAppInfo::GetOsInfo();

  base::android::BuildInfo* android_build_info =
        base::android::BuildInfo::GetInstance();
  std::string application(android_build_info->package_label());
  application.append(" ");
  application.append(version_info.Version());

  // OK to release, returning to Java.
  return Java_PrefServiceBridge_createAboutVersionStrings(
      env,
      ConvertUTF8ToJavaString(env, application).obj(),
      ConvertUTF8ToJavaString(env, content::GetWebKitVersion()).obj(),
      ConvertUTF8ToJavaString(
          env, AndroidAboutAppInfo::GetJavaScriptVersion()).obj(),
      ConvertUTF8ToJavaString(env, os_version).obj()).Release();
}

static void SetPathValuesForAboutChrome(JNIEnv* env, jobject obj) {
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                          base::Bind(&GetAbsolutePath, GetOriginalProfile()));
}

static jstring GetSupervisedUserCustodianName(JNIEnv* env, jobject obj) {
  return ConvertUTF8ToJavaString(
      env, GetPrefService()->GetString(prefs::kSupervisedUserCustodianName))
      .Release();
}

static jstring GetSupervisedUserCustodianEmail(JNIEnv* env, jobject obj) {
  return ConvertUTF8ToJavaString(
      env, GetPrefService()->GetString(prefs::kSupervisedUserCustodianEmail))
      .Release();
}

static jstring GetSupervisedUserCustodianProfileImageURL(JNIEnv* env,
                                                         jobject obj) {
  return ConvertUTF8ToJavaString(
      env,
      GetPrefService()->GetString(
          prefs::kSupervisedUserCustodianProfileImageURL)).Release();
}

static jstring GetSupervisedUserSecondCustodianName(JNIEnv* env, jobject obj) {
  return ConvertUTF8ToJavaString(
      env,
      GetPrefService()->GetString(prefs::kSupervisedUserSecondCustodianName))
      .Release();
}

static jstring GetSupervisedUserSecondCustodianEmail(JNIEnv* env, jobject obj) {
  return ConvertUTF8ToJavaString(
      env,
      GetPrefService()->GetString(prefs::kSupervisedUserSecondCustodianEmail))
      .Release();
}

static jstring GetSupervisedUserSecondCustodianProfileImageURL(JNIEnv* env,
                                                               jobject obj) {
  return ConvertUTF8ToJavaString(
      env,
      GetPrefService()->GetString(
          prefs::kSupervisedUserSecondCustodianProfileImageURL)).Release();
}

bool RegisterPrefServiceBridge(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
