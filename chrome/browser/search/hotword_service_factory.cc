// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/search/hotword_service_factory.h"

#include "base/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/hotword_service.h"
#include "chrome/common/pref_names.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserContext;
using content::BrowserThread;

// static
HotwordService* HotwordServiceFactory::GetForProfile(BrowserContext* context) {
  return static_cast<HotwordService*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

// static
HotwordServiceFactory* HotwordServiceFactory::GetInstance() {
  return Singleton<HotwordServiceFactory>::get();
}

// static
bool HotwordServiceFactory::IsServiceAvailable(BrowserContext* context) {
  HotwordService* hotword_service = GetForProfile(context);
  return hotword_service && hotword_service->IsServiceAvailable();
}

// static
bool HotwordServiceFactory::IsHotwordAllowed(BrowserContext* context) {
  HotwordService* hotword_service = GetForProfile(context);
  return hotword_service && hotword_service->IsHotwordAllowed();
}

// static
bool HotwordServiceFactory::IsHotwordHardwareAvailable() {
  // TODO(rlp, dgreid): return has_hotword_hardware()
  // Fill in once the hardware has the correct interface implemented.
  // In the meantime, this function can be used to get other parts moving.
  return true;
}

// static
int HotwordServiceFactory::GetCurrentError(BrowserContext* context) {
  HotwordService* hotword_service = GetForProfile(context);
  if (!hotword_service)
    return 0;
  return hotword_service->error_message();
}

// static
bool HotwordServiceFactory::IsMicrophoneAvailable() {
  return GetInstance()->microphone_available();
}

// static
bool HotwordServiceFactory::IsAudioDeviceStateUpdated() {
  return GetInstance()->audio_device_state_updated();
}

HotwordServiceFactory::HotwordServiceFactory()
    : BrowserContextKeyedServiceFactory(
        "HotwordService",
        BrowserContextDependencyManager::GetInstance()),
      microphone_available_(false),
      audio_device_state_updated_(false) {
  // No dependencies.

  // Register with the device observer list to update the microphone
  // availability.
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&HotwordServiceFactory::InitializeMicrophoneObserver,
                 base::Unretained(this)));
}

HotwordServiceFactory::~HotwordServiceFactory() {
}

void HotwordServiceFactory::InitializeMicrophoneObserver() {
  MediaCaptureDevicesDispatcher::GetInstance()->AddObserver(this);
}

void HotwordServiceFactory::OnUpdateAudioDevices(
    const content::MediaStreamDevices& devices) {
  microphone_available_ = !devices.empty();
  audio_device_state_updated_ = true;
}

void HotwordServiceFactory::UpdateMicrophoneState() {
  // In order to trigger the monitor, just call getAudioCaptureDevices.
  content::MediaStreamDevices devices =
    MediaCaptureDevicesDispatcher::GetInstance()->GetAudioCaptureDevices();
}

void HotwordServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* prefs) {
  // Although this is default true, users will not send back information to
  // Google unless they have opted-in to Hotwording at which point they must
  // also confirm that they wish this preference to be true or opt out of it.
  prefs->RegisterBooleanPref(prefs::kHotwordAudioLoggingEnabled,
                             true,
                             user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  prefs->RegisterStringPref(prefs::kHotwordPreviousLanguage,
                            std::string(),
                            user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kHotwordAudioHistoryEnabled,
                             false,
                             user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  // Per-device settings (do not sync).
  prefs->RegisterBooleanPref(prefs::kHotwordSearchEnabled,
                             false,
                             user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kHotwordAlwaysOnSearchEnabled,
                             false,
                             user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

KeyedService* HotwordServiceFactory::BuildServiceInstanceFor(
    BrowserContext* context) const {
  return new HotwordService(Profile::FromBrowserContext(context));
}
