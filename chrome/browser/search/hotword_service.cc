// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/search/hotword_service.h"

#include "base/command_line.h"
#include "base/i18n/case_conversion.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/hotword_private/hotword_private_api.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/pending_extension_manager.h"
#include "chrome/browser/extensions/updater/extension_updater.h"
#include "chrome/browser/extensions/webstore_startup_installer.h"
#include "chrome/browser/plugins/plugin_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/hotword_audio_history_handler.h"
#include "chrome/browser/search/hotword_service_factory.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/common/webplugininfo.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"
#include "extensions/common/one_shot_event.h"
#include "ui/base/l10n/l10n_util.h"

using extensions::BrowserContextKeyedAPIFactory;
using extensions::HotwordPrivateEventService;

namespace {

// Allowed languages for hotwording.
static const char* kSupportedLocales[] = {
  "en",
  "de",
  "fr",
  "ru"
};

// Enum describing the state of the hotword preference.
// This is used for UMA stats -- do not reorder or delete items; only add to
// the end.
enum HotwordEnabled {
  UNSET = 0,  // The hotword preference has not been set.
  ENABLED,    // The hotword preference is enabled.
  DISABLED,   // The hotword preference is disabled.
  NUM_HOTWORD_ENABLED_METRICS
};

// Enum describing the availability state of the hotword extension.
// This is used for UMA stats -- do not reorder or delete items; only add to
// the end.
enum HotwordExtensionAvailability {
  UNAVAILABLE = 0,
  AVAILABLE,
  PENDING_DOWNLOAD,
  DISABLED_EXTENSION,
  NUM_HOTWORD_EXTENSION_AVAILABILITY_METRICS
};

// Enum describing the types of errors that can arise when determining
// if hotwording can be used. NO_ERROR is used so it can be seen how often
// errors arise relative to when they do not.
// This is used for UMA stats -- do not reorder or delete items; only add to
// the end.
enum HotwordError {
  NO_HOTWORD_ERROR = 0,
  GENERIC_HOTWORD_ERROR,
  NACL_HOTWORD_ERROR,
  MICROPHONE_HOTWORD_ERROR,
  NUM_HOTWORD_ERROR_METRICS
};

void RecordExtensionAvailabilityMetrics(
    ExtensionService* service,
    const extensions::Extension* extension) {
  HotwordExtensionAvailability availability_state = UNAVAILABLE;
  if (extension) {
    availability_state = AVAILABLE;
  } else if (service->pending_extension_manager() &&
             service->pending_extension_manager()->IsIdPending(
                 extension_misc::kHotwordExtensionId)) {
    availability_state = PENDING_DOWNLOAD;
  } else if (!service->IsExtensionEnabled(
      extension_misc::kHotwordExtensionId)) {
    availability_state = DISABLED_EXTENSION;
  }
  UMA_HISTOGRAM_ENUMERATION("Hotword.HotwordExtensionAvailability",
                            availability_state,
                            NUM_HOTWORD_EXTENSION_AVAILABILITY_METRICS);
}

void RecordLoggingMetrics(Profile* profile) {
  // If the user is not opted in to hotword voice search, the audio logging
  // metric is not valid so it is not recorded.
  if (!profile->GetPrefs()->GetBoolean(prefs::kHotwordSearchEnabled))
    return;

  UMA_HISTOGRAM_BOOLEAN(
      "Hotword.HotwordAudioLogging",
      profile->GetPrefs()->GetBoolean(prefs::kHotwordAudioLoggingEnabled));
}

void RecordErrorMetrics(int error_message) {
  HotwordError error = NO_HOTWORD_ERROR;
  switch (error_message) {
    case IDS_HOTWORD_GENERIC_ERROR_MESSAGE:
      error = GENERIC_HOTWORD_ERROR;
      break;
    case IDS_HOTWORD_NACL_DISABLED_ERROR_MESSAGE:
      error = NACL_HOTWORD_ERROR;
      break;
    case IDS_HOTWORD_MICROPHONE_ERROR_MESSAGE:
      error = MICROPHONE_HOTWORD_ERROR;
      break;
    default:
      error = NO_HOTWORD_ERROR;
  }

  UMA_HISTOGRAM_ENUMERATION("Hotword.HotwordError",
                            error,
                            NUM_HOTWORD_ERROR_METRICS);
}

ExtensionService* GetExtensionService(Profile* profile) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  extensions::ExtensionSystem* extension_system =
      extensions::ExtensionSystem::Get(profile);
  return extension_system ?  extension_system->extension_service() : NULL;
}

std::string GetCurrentLocale(Profile* profile) {
#if defined(OS_CHROMEOS)
  std::string profile_locale =
      profile->GetPrefs()->GetString(prefs::kApplicationLocale);
  if (!profile_locale.empty()) {
    // On ChromeOS locale is per-profile, but only if set.
    return profile_locale;
  }
#endif
  return g_browser_process->GetApplicationLocale();
}

}  // namespace

namespace hotword_internal {
// Constants for the hotword field trial.
const char kHotwordFieldTrialName[] = "VoiceTrigger";
const char kHotwordFieldTrialDisabledGroupName[] = "Disabled";
const char kHotwordFieldTrialExperimentalGroupName[] = "Experimental";
// Old preference constant.
const char kHotwordUnusablePrefName[] = "hotword.search_enabled";
// String passed to indicate the training state has changed.
const char kHotwordTrainingEnabled[] = "hotword_training_enabled";
}  // namespace hotword_internal

// static
bool HotwordService::DoesHotwordSupportLanguage(Profile* profile) {
  std::string normalized_locale =
      l10n_util::NormalizeLocale(GetCurrentLocale(profile));
  base::StringToLowerASCII(&normalized_locale);

  for (size_t i = 0; i < arraysize(kSupportedLocales); i++) {
    if (normalized_locale.compare(0, 2, kSupportedLocales[i]) == 0)
      return true;
  }
  return false;
}

// static
bool HotwordService::IsExperimentalHotwordingEnabled() {
  std::string group = base::FieldTrialList::FindFullName(
      hotword_internal::kHotwordFieldTrialName);
  if (!group.empty() &&
      group == hotword_internal::kHotwordFieldTrialExperimentalGroupName) {
    return true;
  }

  CommandLine* command_line = CommandLine::ForCurrentProcess();
  return command_line->HasSwitch(switches::kEnableExperimentalHotwording);
}

HotwordService::HotwordService(Profile* profile)
    : profile_(profile),
      extension_registry_observer_(this),
      client_(NULL),
      error_message_(0),
      reinstall_pending_(false),
      training_(false),
      weak_factory_(this) {
  extension_registry_observer_.Add(extensions::ExtensionRegistry::Get(profile));
  // This will be called during profile initialization which is a good time
  // to check the user's hotword state.
  HotwordEnabled enabled_state = UNSET;
  if (IsExperimentalHotwordingEnabled()) {
    // Disable the old extension so it doesn't interfere with the new stuff.
    DisableHotwordExtension(GetExtensionService(profile_));
  } else {
    if (profile_->GetPrefs()->HasPrefPath(prefs::kHotwordSearchEnabled)) {
      if (profile_->GetPrefs()->GetBoolean(prefs::kHotwordSearchEnabled))
        enabled_state = ENABLED;
      else
        enabled_state = DISABLED;
    } else {
      // If the preference has not been set the hotword extension should
      // not be running. However, this should only be done if auto-install
      // is enabled which is gated through the IsHotwordAllowed check.
      if (IsHotwordAllowed())
        DisableHotwordExtension(GetExtensionService(profile_));
    }
  }
  UMA_HISTOGRAM_ENUMERATION("Hotword.Enabled", enabled_state,
                            NUM_HOTWORD_ENABLED_METRICS);

  pref_registrar_.Init(profile_->GetPrefs());
  pref_registrar_.Add(
      prefs::kHotwordSearchEnabled,
      base::Bind(&HotwordService::OnHotwordSearchEnabledChanged,
                 base::Unretained(this)));

  extensions::ExtensionSystem::Get(profile_)->ready().Post(
      FROM_HERE,
      base::Bind(base::IgnoreResult(
          &HotwordService::MaybeReinstallHotwordExtension),
                 weak_factory_.GetWeakPtr()));

  // Clear the old user pref because it became unusable.
  // TODO(rlp): Remove this code per crbug.com/358789.
  if (profile_->GetPrefs()->HasPrefPath(
          hotword_internal::kHotwordUnusablePrefName)) {
    profile_->GetPrefs()->ClearPref(hotword_internal::kHotwordUnusablePrefName);
  }

  audio_history_handler_.reset(new HotwordAudioHistoryHandler(profile_));
}

HotwordService::~HotwordService() {
}

void HotwordService::OnExtensionUninstalled(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UninstallReason reason) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  if ((extension->id() != extension_misc::kHotwordExtensionId &&
       extension->id() != extension_misc::kHotwordSharedModuleId) ||
       profile_ != Profile::FromBrowserContext(browser_context) ||
      !GetExtensionService(profile_))
    return;

  // If the extension wasn't uninstalled due to language change, don't try to
  // reinstall it.
  if (!reinstall_pending_)
    return;

  InstallHotwordExtensionFromWebstore();
  SetPreviousLanguagePref();
}

std::string HotwordService::ReinstalledExtensionId() {
  if (IsExperimentalHotwordingEnabled())
    return extension_misc::kHotwordSharedModuleId;

  return extension_misc::kHotwordExtensionId;
}

void HotwordService::InstallHotwordExtensionFromWebstore() {
  installer_ = new extensions::WebstoreStartupInstaller(
      ReinstalledExtensionId(),
      profile_,
      false,
      extensions::WebstoreStandaloneInstaller::Callback());
  installer_->BeginInstall();
}

void HotwordService::OnExtensionInstalled(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    bool is_update) {

  if ((extension->id() != extension_misc::kHotwordExtensionId &&
       extension->id() != extension_misc::kHotwordSharedModuleId) ||
       profile_ != Profile::FromBrowserContext(browser_context))
    return;

  // If the previous locale pref has never been set, set it now since
  // the extension has been installed.
  if (!profile_->GetPrefs()->HasPrefPath(prefs::kHotwordPreviousLanguage))
    SetPreviousLanguagePref();

  // If MaybeReinstallHotwordExtension already triggered an uninstall, we
  // don't want to loop and trigger another uninstall-install cycle.
  // However, if we arrived here via an uninstall-triggered-install (and in
  // that case |reinstall_pending_| will be true) then we know install
  // has completed and we can reset |reinstall_pending_|.
  if (!reinstall_pending_)
    MaybeReinstallHotwordExtension();
  else
    reinstall_pending_ = false;

  // Now that the extension is installed, if the user has not selected
  // the preference on, make sure it is turned off.
  //
  // Disabling the extension automatically on install should only occur
  // if the user is in the field trial for auto-install which is gated
  // by the IsHotwordAllowed check. The check for IsHotwordAllowed() here
  // can be removed once it's known that few people have manually
  // installed extension.
  if (IsHotwordAllowed() &&
      !profile_->GetPrefs()->GetBoolean(prefs::kHotwordSearchEnabled)) {
    DisableHotwordExtension(GetExtensionService(profile_));
  }
}

bool HotwordService::MaybeReinstallHotwordExtension() {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  ExtensionService* extension_service = GetExtensionService(profile_);
  if (!extension_service)
    return false;

  const extensions::Extension* extension = extension_service->GetExtensionById(
      ReinstalledExtensionId(), true);
  if (!extension)
    return false;

  // If the extension is currently pending, return and we'll check again
  // after the install is finished.
  extensions::PendingExtensionManager* pending_manager =
      extension_service->pending_extension_manager();
  if (pending_manager->IsIdPending(extension->id()))
    return false;

  // If there is already a pending request from HotwordService, don't try
  // to uninstall either.
  if (reinstall_pending_)
    return false;

  // Check if the current locale matches the previous. If they don't match,
  // uninstall the extension.
  if (!ShouldReinstallHotwordExtension())
    return false;

  // Ensure the call to OnExtensionUninstalled was triggered by a language
  // change so it's okay to reinstall.
  reinstall_pending_ = true;

  // Disable always-on on a language change. We do this because the speaker-id
  // model needs to be re-trained.
  if (IsAlwaysOnEnabled()) {
    profile_->GetPrefs()->SetBoolean(prefs::kHotwordAlwaysOnSearchEnabled,
                                     false);
  }

  return UninstallHotwordExtension(extension_service);
}

bool HotwordService::UninstallHotwordExtension(
    ExtensionService* extension_service) {
  base::string16 error;
  std::string extension_id = ReinstalledExtensionId();
  if (!extension_service->UninstallExtension(
          extension_id,
          extensions::UNINSTALL_REASON_INTERNAL_MANAGEMENT,
          base::Bind(&base::DoNothing),
          &error)) {
    LOG(WARNING) << "Cannot uninstall extension with id "
                 << extension_id
                 << ": " << error;
    reinstall_pending_ = false;
    return false;
  }
  return true;
}

bool HotwordService::IsServiceAvailable() {
  error_message_ = 0;

  // Determine if the extension is available.
  extensions::ExtensionSystem* system =
      extensions::ExtensionSystem::Get(profile_);
  ExtensionService* service = system->extension_service();
  // Include disabled extensions (true parameter) since it may not be enabled
  // if the user opted out.
  const extensions::Extension* extension =
      service->GetExtensionById(ReinstalledExtensionId(), true);
  if (!extension)
    error_message_ = IDS_HOTWORD_GENERIC_ERROR_MESSAGE;

  RecordExtensionAvailabilityMetrics(service, extension);
  RecordLoggingMetrics(profile_);

  // Determine if NaCl is available.
  bool nacl_enabled = false;
  base::FilePath path;
  if (PathService::Get(chrome::FILE_NACL_PLUGIN, &path)) {
    content::WebPluginInfo info;
    PluginPrefs* plugin_prefs = PluginPrefs::GetForProfile(profile_).get();
    if (content::PluginService::GetInstance()->GetPluginInfoByPath(path, &info))
      nacl_enabled = plugin_prefs->IsPluginEnabled(info);
  }
  if (!nacl_enabled)
    error_message_ = IDS_HOTWORD_NACL_DISABLED_ERROR_MESSAGE;

  RecordErrorMetrics(error_message_);

  // Determine if the proper audio capabilities exist.
  // The first time this is called, it probably won't return in time, but that's
  // why it won't be included in the error calculation (i.e., the call to
  // IsAudioDeviceStateUpdated()). However, this use case is rare and typically
  // the devices will be initialized by the time a user goes to settings.
  bool audio_device_state_updated =
      HotwordServiceFactory::IsAudioDeviceStateUpdated();
  HotwordServiceFactory::GetInstance()->UpdateMicrophoneState();
  if (audio_device_state_updated) {
    bool audio_capture_allowed =
        profile_->GetPrefs()->GetBoolean(prefs::kAudioCaptureAllowed);
    if (!audio_capture_allowed ||
        !HotwordServiceFactory::IsMicrophoneAvailable())
      error_message_ = IDS_HOTWORD_MICROPHONE_ERROR_MESSAGE;
  }

  return (error_message_ == 0) && IsHotwordAllowed();
}

bool HotwordService::IsHotwordAllowed() {
  std::string group = base::FieldTrialList::FindFullName(
      hotword_internal::kHotwordFieldTrialName);
  return !group.empty() &&
      group != hotword_internal::kHotwordFieldTrialDisabledGroupName &&
      DoesHotwordSupportLanguage(profile_);
}

bool HotwordService::IsOptedIntoAudioLogging() {
  // Do not opt the user in if the preference has not been set.
  return
      profile_->GetPrefs()->HasPrefPath(prefs::kHotwordAudioLoggingEnabled) &&
      profile_->GetPrefs()->GetBoolean(prefs::kHotwordAudioLoggingEnabled);
}

bool HotwordService::IsAlwaysOnEnabled() {
  return
      profile_->GetPrefs()->HasPrefPath(prefs::kHotwordAlwaysOnSearchEnabled) &&
      profile_->GetPrefs()->GetBoolean(prefs::kHotwordAlwaysOnSearchEnabled);
}

void HotwordService::EnableHotwordExtension(
    ExtensionService* extension_service) {
  if (extension_service && !IsExperimentalHotwordingEnabled())
    extension_service->EnableExtension(extension_misc::kHotwordExtensionId);
}

void HotwordService::DisableHotwordExtension(
    ExtensionService* extension_service) {
  if (extension_service) {
    extension_service->DisableExtension(
        extension_misc::kHotwordExtensionId,
        extensions::Extension::DISABLE_USER_ACTION);
  }
}

void HotwordService::LaunchHotwordAudioVerificationApp(
    const LaunchMode& launch_mode) {
  hotword_audio_verification_launch_mode_ = launch_mode;

  ExtensionService* extension_service = GetExtensionService(profile_);
  if (!extension_service)
    return;
  const extensions::Extension* extension = extension_service->GetExtensionById(
      extension_misc::kHotwordAudioVerificationAppId, true);
  if (!extension)
    return;

  OpenApplication(AppLaunchParams(
      profile_, extension, extensions::LAUNCH_CONTAINER_WINDOW, NEW_WINDOW));
}

HotwordService::LaunchMode
HotwordService::GetHotwordAudioVerificationLaunchMode() {
  return hotword_audio_verification_launch_mode_;
}

void HotwordService::StartTraining() {
  training_ = true;

  if (!IsServiceAvailable())
    return;

  HotwordPrivateEventService* event_service =
      BrowserContextKeyedAPIFactory<HotwordPrivateEventService>::Get(profile_);
  if (event_service)
    event_service->OnEnabledChanged(hotword_internal::kHotwordTrainingEnabled);
}

void HotwordService::FinalizeSpeakerModel() {
  if (!IsServiceAvailable())
    return;

  HotwordPrivateEventService* event_service =
      BrowserContextKeyedAPIFactory<HotwordPrivateEventService>::Get(profile_);
  if (event_service)
    event_service->OnFinalizeSpeakerModel();
}

void HotwordService::StopTraining() {
  training_ = false;

  if (!IsServiceAvailable())
    return;

  HotwordPrivateEventService* event_service =
      BrowserContextKeyedAPIFactory<HotwordPrivateEventService>::Get(profile_);
  if (event_service)
    event_service->OnEnabledChanged(hotword_internal::kHotwordTrainingEnabled);
}

void HotwordService::NotifyHotwordTriggered() {
  if (!IsServiceAvailable())
    return;

  HotwordPrivateEventService* event_service =
      BrowserContextKeyedAPIFactory<HotwordPrivateEventService>::Get(profile_);
  if (event_service)
    event_service->OnHotwordTriggered();
}

bool HotwordService::IsTraining() {
  return training_;
}

void HotwordService::OnHotwordSearchEnabledChanged(
    const std::string& pref_name) {
  DCHECK_EQ(pref_name, std::string(prefs::kHotwordSearchEnabled));

  ExtensionService* extension_service = GetExtensionService(profile_);
  if (profile_->GetPrefs()->GetBoolean(prefs::kHotwordSearchEnabled))
    EnableHotwordExtension(extension_service);
  else
    DisableHotwordExtension(extension_service);
}

void HotwordService::RequestHotwordSession(HotwordClient* client) {
  if (!IsServiceAvailable() || (client_ && client_ != client))
    return;

  client_ = client;

  HotwordPrivateEventService* event_service =
      BrowserContextKeyedAPIFactory<HotwordPrivateEventService>::Get(profile_);
  if (event_service)
    event_service->OnHotwordSessionRequested();
}

void HotwordService::StopHotwordSession(HotwordClient* client) {
  if (!IsServiceAvailable())
    return;

  // Do nothing if there's no client.
  if (!client_)
    return;
  DCHECK(client_ == client);

  client_ = NULL;
  HotwordPrivateEventService* event_service =
      BrowserContextKeyedAPIFactory<HotwordPrivateEventService>::Get(profile_);
  if (event_service)
    event_service->OnHotwordSessionStopped();
}

void HotwordService::SetPreviousLanguagePref() {
  profile_->GetPrefs()->SetString(prefs::kHotwordPreviousLanguage,
                                  GetCurrentLocale(profile_));
}

bool HotwordService::ShouldReinstallHotwordExtension() {
  // If there is no previous locale pref, then this is the first install
  // so no need to uninstall first.
  if (!profile_->GetPrefs()->HasPrefPath(prefs::kHotwordPreviousLanguage))
    return false;

  std::string previous_locale =
      profile_->GetPrefs()->GetString(prefs::kHotwordPreviousLanguage);
  std::string locale = GetCurrentLocale(profile_);

  // If it's a new locale, then the old extension should be uninstalled.
  return locale != previous_locale &&
      HotwordService::DoesHotwordSupportLanguage(profile_);
}
