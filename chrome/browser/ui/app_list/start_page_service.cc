// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/start_page_service.h"

#include <string>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/memory/singleton.h"
#include "base/metrics/user_metrics.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/media/media_stream_infobar_delegate.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/hotword_service.h"
#include "chrome/browser/search/hotword_service_factory.h"
#include "chrome/browser/ui/app_list/recommended_apps.h"
#include "chrome/browser/ui/app_list/speech_recognizer.h"
#include "chrome/browser/ui/app_list/start_page_observer.h"
#include "chrome/browser/ui/app_list/start_page_service_factory.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "extensions/browser/extension_system_provider.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/common/extension.h"
#include "ui/app_list/app_list_switches.h"

using base::RecordAction;
using base::UserMetricsAction;

namespace app_list {

namespace {

bool InSpeechRecognition(SpeechRecognitionState state) {
  return state == SPEECH_RECOGNITION_RECOGNIZING ||
      state == SPEECH_RECOGNITION_IN_SPEECH;
}

}

class StartPageService::ProfileDestroyObserver
    : public content::NotificationObserver {
 public:
  explicit ProfileDestroyObserver(StartPageService* service)
      : service_(service) {
    registrar_.Add(this,
                   chrome::NOTIFICATION_PROFILE_DESTROYED,
                   content::Source<Profile>(service_->profile()));
  }
  ~ProfileDestroyObserver() override {}

 private:
  // content::NotificationObserver
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override {
    DCHECK_EQ(chrome::NOTIFICATION_PROFILE_DESTROYED, type);
    DCHECK_EQ(service_->profile(), content::Source<Profile>(source).ptr());
    service_->Shutdown();
  }

  StartPageService* service_;  // Owner of this class.
  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(ProfileDestroyObserver);
};

class StartPageService::StartPageWebContentsDelegate
    : public content::WebContentsDelegate {
 public:
  StartPageWebContentsDelegate() {}
  ~StartPageWebContentsDelegate() override {}

  void RequestMediaAccessPermission(
      content::WebContents* web_contents,
      const content::MediaStreamRequest& request,
      const content::MediaResponseCallback& callback) override {
    if (MediaStreamInfoBarDelegate::Create(web_contents, request, callback))
      NOTREACHED() << "Media stream not allowed for WebUI";
  }

  bool CheckMediaAccessPermission(content::WebContents* web_contents,
                                  const GURL& security_origin,
                                  content::MediaStreamType type) override {
    return MediaCaptureDevicesDispatcher::GetInstance()
        ->CheckMediaAccessPermission(web_contents, security_origin, type);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(StartPageWebContentsDelegate);
};

// static
StartPageService* StartPageService::Get(Profile* profile) {
  return StartPageServiceFactory::GetForProfile(profile);
}

StartPageService::StartPageService(Profile* profile)
    : profile_(profile),
      profile_destroy_observer_(new ProfileDestroyObserver(this)),
      recommended_apps_(new RecommendedApps(profile)),
      state_(app_list::SPEECH_RECOGNITION_OFF),
      speech_button_toggled_manually_(false),
      speech_result_obtained_(false),
      webui_finished_loading_(false),
      weak_factory_(this) {
  // If experimental hotwording is enabled, then we're always "ready".
  // Transitioning into the "hotword recognizing" state is handled by the
  // hotword extension.
  if (HotwordService::IsExperimentalHotwordingEnabled())
    state_ = app_list::SPEECH_RECOGNITION_READY;

  if (app_list::switches::IsExperimentalAppListEnabled())
    LoadContents();
}

StartPageService::~StartPageService() {}

void StartPageService::AddObserver(StartPageObserver* observer) {
  observers_.AddObserver(observer);
}

void StartPageService::RemoveObserver(StartPageObserver* observer) {
  observers_.RemoveObserver(observer);
}

void StartPageService::AppListShown() {
  if (!contents_) {
    LoadContents();
  } else if (contents_->GetWebUI() &&
             !HotwordService::IsExperimentalHotwordingEnabled()) {
    // If experimental hotwording is enabled, don't call onAppListShown.
    // onAppListShown() initializes the web speech API, which is not used with
    // experimental hotwording.
    contents_->GetWebUI()->CallJavascriptFunction(
        "appList.startPage.onAppListShown",
        base::FundamentalValue(HotwordEnabled()));
  }
}

void StartPageService::AppListHidden() {
  if (contents_->GetWebUI()) {
    contents_->GetWebUI()->CallJavascriptFunction(
        "appList.startPage.onAppListHidden");
  }
  if (!app_list::switches::IsExperimentalAppListEnabled())
    UnloadContents();

  if (HotwordService::IsExperimentalHotwordingEnabled() &&
      speech_recognizer_) {
    speech_recognizer_->Stop();
  }
}

void StartPageService::ToggleSpeechRecognition() {
  DCHECK(contents_);
  speech_button_toggled_manually_ = true;
  if (!contents_->GetWebUI())
    return;

  if (!webui_finished_loading_) {
    pending_webui_callbacks_.push_back(
        base::Bind(&StartPageService::ToggleSpeechRecognition,
                   base::Unretained(this)));
    return;
  }

  if (HotwordService::IsExperimentalHotwordingEnabled()) {
    if (!speech_recognizer_) {
      std::string profile_locale;
#if defined(OS_CHROMEOS)
      profile_locale = profile_->GetPrefs()->GetString(
          prefs::kApplicationLocale);
#endif
      if (profile_locale.empty())
        profile_locale = g_browser_process->GetApplicationLocale();

      speech_recognizer_.reset(
          new SpeechRecognizer(weak_factory_.GetWeakPtr(),
                               profile_->GetRequestContext(),
                               profile_locale));
    }

    speech_recognizer_->Start();
    return;
  }

  contents_->GetWebUI()->CallJavascriptFunction(
      "appList.startPage.toggleSpeechRecognition");
}

bool StartPageService::HotwordEnabled() {
  if (HotwordService::IsExperimentalHotwordingEnabled()) {
    auto prefs = profile_->GetPrefs();
    return HotwordServiceFactory::IsServiceAvailable(profile_) &&
        (prefs->GetBoolean(prefs::kHotwordSearchEnabled) ||
         prefs->GetBoolean(prefs::kHotwordAlwaysOnSearchEnabled));
  }
#if defined(OS_CHROMEOS)
  return HotwordServiceFactory::IsServiceAvailable(profile_) &&
      profile_->GetPrefs()->GetBoolean(prefs::kHotwordSearchEnabled);
#else
  return false;
#endif
}

content::WebContents* StartPageService::GetStartPageContents() {
  return app_list::switches::IsExperimentalAppListEnabled() ? contents_.get()
                                                            : NULL;
}

content::WebContents* StartPageService::GetSpeechRecognitionContents() {
  if (app_list::switches::IsVoiceSearchEnabled()) {
    if (!contents_)
      LoadContents();
    return contents_.get();
  }
  return NULL;
}

void StartPageService::OnSpeechResult(
    const base::string16& query, bool is_final) {
  if (is_final) {
    speech_result_obtained_ = true;
    RecordAction(UserMetricsAction("AppList_SearchedBySpeech"));
  }
  FOR_EACH_OBSERVER(StartPageObserver,
                    observers_,
                    OnSpeechResult(query, is_final));
}

void StartPageService::OnSpeechSoundLevelChanged(int16_t level) {
  FOR_EACH_OBSERVER(StartPageObserver,
                    observers_,
                    OnSpeechSoundLevelChanged(level));
}

void StartPageService::OnSpeechRecognitionStateChanged(
    SpeechRecognitionState new_state) {

  if (HotwordService::IsExperimentalHotwordingEnabled() &&
      new_state == SPEECH_RECOGNITION_READY &&
      speech_recognizer_) {
    speech_recognizer_->Stop();
  }

  if (!InSpeechRecognition(state_) && InSpeechRecognition(new_state)) {
    if (!speech_button_toggled_manually_ &&
        state_ == SPEECH_RECOGNITION_HOTWORD_LISTENING) {
      RecordAction(UserMetricsAction("AppList_HotwordRecognized"));
    } else {
      RecordAction(UserMetricsAction("AppList_VoiceSearchStartedManually"));
    }
  } else if (InSpeechRecognition(state_) && !InSpeechRecognition(new_state) &&
             !speech_result_obtained_) {
    RecordAction(UserMetricsAction("AppList_VoiceSearchCanceled"));
  }
  speech_button_toggled_manually_ = false;
  speech_result_obtained_ = false;
  state_ = new_state;
  FOR_EACH_OBSERVER(StartPageObserver,
                    observers_,
                    OnSpeechRecognitionStateChanged(new_state));
}

content::WebContents* StartPageService::GetSpeechContents() {
  return GetSpeechRecognitionContents();
}

void StartPageService::Shutdown() {
  UnloadContents();
}

void StartPageService::WebUILoaded() {
  // There's a race condition between the WebUI loading, and calling its JS
  // functions. Specifically, calling LoadContents() doesn't mean that the page
  // has loaded, but several code paths make this assumption. This function
  // allows us to defer calling JS functions until after the page has finished
  // loading.
  webui_finished_loading_ = true;
  for (const auto& cb : pending_webui_callbacks_)
    cb.Run();
  pending_webui_callbacks_.clear();
}

void StartPageService::LoadContents() {
  contents_.reset(content::WebContents::Create(
      content::WebContents::CreateParams(profile_)));
  contents_delegate_.reset(new StartPageWebContentsDelegate());
  contents_->SetDelegate(contents_delegate_.get());

  GURL url(chrome::kChromeUIAppListStartPageURL);
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(::switches::kAppListStartPageURL)) {
    url = GURL(
        command_line->GetSwitchValueASCII(::switches::kAppListStartPageURL));
  }

  contents_->GetController().LoadURL(
      url,
      content::Referrer(),
      ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
      std::string());
}

void StartPageService::UnloadContents() {
  contents_.reset();
  webui_finished_loading_ = false;
}

}  // namespace app_list
