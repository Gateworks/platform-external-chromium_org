// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/login_utils.h"

#include <algorithm>
#include <set>
#include <vector>

#include "base/base_paths.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/memory/weak_ptr.h"
#include "base/prefs/pref_member.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/sys_info.h"
#include "base/task_runner_util.h"
#include "base/threading/worker_pool.h"
#include "base/time/time.h"
#include "chrome/browser/about_flags.h"
#include "chrome/browser/app_mode/app_mode_utils.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_shutdown.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/boot_times_loader.h"
#include "chrome/browser/chromeos/login/auth/chrome_cryptohome_authenticator.h"
#include "chrome/browser/chromeos/login/demo_mode/demo_app_launcher.h"
#include "chrome/browser/chromeos/login/existing_user_controller.h"
#include "chrome/browser/chromeos/login/lock/screen_locker.h"
#include "chrome/browser/chromeos/login/profile_auth_data.h"
#include "chrome/browser/chromeos/login/saml/saml_offline_signin_limiter.h"
#include "chrome/browser/chromeos/login/saml/saml_offline_signin_limiter_factory.h"
#include "chrome/browser/chromeos/login/session/user_session_manager.h"
#include "chrome/browser/chromeos/login/signin/oauth2_login_manager.h"
#include "chrome/browser/chromeos/login/signin/oauth2_login_manager_factory.h"
#include "chrome/browser/chromeos/login/ui/input_events_blocker.h"
#include "chrome/browser/chromeos/login/ui/login_display_host.h"
#include "chrome/browser/chromeos/login/ui/user_adding_screen.h"
#include "chrome/browser/chromeos/login/user_flow.h"
#include "chrome/browser/chromeos/login/users/chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/supervised_user_manager.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/google/google_brand_chromeos.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/pref_service_flags_storage.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/rlz/rlz.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/app_list/start_page_service.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/logging_chrome.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/cryptohome/cryptohome_util.h"
#include "chromeos/dbus/cryptohome_client.h"
#include "chromeos/dbus/dbus_method_call_status.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/session_manager_client.h"
#include "chromeos/login/auth/user_context.h"
#include "chromeos/settings/cros_settings_names.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "google_apis/gaia/gaia_auth_consumer.h"
#include "net/base/network_change_notifier.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"

#if defined(USE_ATHENA)
#include "athena/main/public/athena_launcher.h"
#endif

using content::BrowserThread;

namespace {

void LogCustomSwitches(const std::set<std::string>& switches) {
  if (!VLOG_IS_ON(1))
    return;
  for (std::set<std::string>::const_iterator it = switches.begin();
       it != switches.end();
       ++it) {
    VLOG(1) << "Switch leading to restart: '" << *it << "'";
  }
}

}  // anonymous namespace

namespace chromeos {

namespace {

// Returns new CommandLine with per-user flags.
CommandLine CreatePerSessionCommandLine(Profile* profile) {
  CommandLine user_flags(CommandLine::NO_PROGRAM);
  about_flags::PrefServiceFlagsStorage flags_storage_(profile->GetPrefs());
  about_flags::ConvertFlagsToSwitches(
      &flags_storage_, &user_flags, about_flags::kAddSentinels);
  return user_flags;
}

// Returns true if restart is needed to apply per-session flags.
bool NeedRestartToApplyPerSessionFlags(
    const CommandLine& user_flags,
    std::set<CommandLine::StringType>* out_command_line_difference) {
  // Don't restart browser if it is not first profile in session.
  if (user_manager::UserManager::Get()->GetLoggedInUsers().size() != 1)
    return false;

  // Only restart if needed and if not going into managed mode.
  if (user_manager::UserManager::Get()->IsLoggedInAsSupervisedUser())
    return false;

  if (about_flags::AreSwitchesIdenticalToCurrentCommandLine(
          user_flags,
          *CommandLine::ForCurrentProcess(),
          out_command_line_difference)) {
    return false;
  }

  return true;
}

bool CanPerformEarlyRestart() {
  // Desktop build is used for development only. Early restart is not supported.
  if (!base::SysInfo::IsRunningOnChromeOS())
    return false;

  if (!ChromeUserManager::Get()->GetCurrentUserFlow()->
          SupportsEarlyRestartToApplyFlags()) {
    return false;
  }

  const ExistingUserController* controller =
      ExistingUserController::current_controller();
  if (!controller)
    return true;

  // Early restart is possible only if OAuth token is up to date.

  if (controller->password_changed())
    return false;

  if (controller->auth_mode() != LoginPerformer::AUTH_MODE_INTERNAL)
    return false;

  // No early restart if Easy unlock key needs to be updated.
  if (UserSessionManager::GetInstance()->NeedsToUpdateEasyUnlockKeys())
    return false;

  return true;
}

}  // namespace

class LoginUtilsImpl : public LoginUtils,
                       public base::SupportsWeakPtr<LoginUtilsImpl>,
                       public UserSessionManagerDelegate {
 public:
  LoginUtilsImpl()
      : delegate_(NULL) {
  }

  virtual ~LoginUtilsImpl() {
  }

  // LoginUtils implementation:
  virtual void DoBrowserLaunch(Profile* profile,
                               LoginDisplayHost* login_host) override;
  virtual void PrepareProfile(
      const UserContext& user_context,
      bool has_auth_cookies,
      bool has_active_session,
      LoginUtils::Delegate* delegate) override;
  virtual void DelegateDeleted(LoginUtils::Delegate* delegate) override;
  virtual scoped_refptr<Authenticator> CreateAuthenticator(
      AuthStatusConsumer* consumer) override;
  virtual bool RestartToApplyPerSessionFlagsIfNeed(Profile* profile,
                                                   bool early_restart) override;

  // UserSessionManager::Delegate implementation:
  virtual void OnProfilePrepared(Profile* profile,
                                 bool browser_launched) override;
#if defined(ENABLE_RLZ)
  virtual void OnRlzInitialized() override;
#endif

 private:
  void DoBrowserLaunchInternal(Profile* profile,
                               LoginDisplayHost* login_host,
                               bool locale_pref_checked);

  // Switch to the locale that |profile| wishes to use and invoke |callback|.
  virtual void RespectLocalePreference(Profile* profile,
                                       const base::Closure& callback);

  static void RunCallbackOnLocaleLoaded(
      const base::Closure& callback,
      InputEventsBlocker* input_events_blocker,
      const std::string& locale,
      const std::string& loaded_locale,
      const bool success);

  // Attempts restarting the browser process and esures that this does
  // not happen while we are still fetching new OAuth refresh tokens.
  void AttemptRestart(Profile* profile);

  // Has to be scoped_refptr, see comment for CreateAuthenticator(...).
  scoped_refptr<Authenticator> authenticator_;

  // Delegate to be fired when the profile will be prepared.
  LoginUtils::Delegate* delegate_;

  DISALLOW_COPY_AND_ASSIGN(LoginUtilsImpl);
};

class LoginUtilsWrapper {
 public:
  static LoginUtilsWrapper* GetInstance() {
    return Singleton<LoginUtilsWrapper>::get();
  }

  LoginUtils* get() {
    base::AutoLock create(create_lock_);
    if (!ptr_.get())
      reset(new LoginUtilsImpl);
    return ptr_.get();
  }

  void reset(LoginUtils* ptr) {
    ptr_.reset(ptr);
  }

 private:
  friend struct DefaultSingletonTraits<LoginUtilsWrapper>;

  LoginUtilsWrapper() {}

  base::Lock create_lock_;
  scoped_ptr<LoginUtils> ptr_;

  DISALLOW_COPY_AND_ASSIGN(LoginUtilsWrapper);
};

void LoginUtilsImpl::DoBrowserLaunchInternal(Profile* profile,
                                             LoginDisplayHost* login_host,
                                             bool locale_pref_checked) {
  if (browser_shutdown::IsTryingToQuit())
    return;

  if (!locale_pref_checked) {
    RespectLocalePreference(profile,
                            base::Bind(&LoginUtilsImpl::DoBrowserLaunchInternal,
                                       base::Unretained(this),
                                       profile,
                                       login_host,
                                       true /* locale_pref_checked */));
    return;
  }

  if (!ChromeUserManager::Get()->GetCurrentUserFlow()->ShouldLaunchBrowser()) {
    ChromeUserManager::Get()->GetCurrentUserFlow()->LaunchExtraSteps(profile);
    return;
  }

  if (RestartToApplyPerSessionFlagsIfNeed(profile, false))
    return;

  if (login_host) {
    login_host->SetStatusAreaVisible(true);
    login_host->BeforeSessionStart();
  }

  BootTimesLoader::Get()->AddLoginTimeMarker("BrowserLaunched", false);

  VLOG(1) << "Launching browser...";
  TRACE_EVENT0("login", "LaunchBrowser");

#if defined(USE_ATHENA)
  athena::StartAthenaSessionWithContext(profile);
#else
  StartupBrowserCreator browser_creator;
  int return_code;
  chrome::startup::IsFirstRun first_run = first_run::IsChromeFirstRun() ?
      chrome::startup::IS_FIRST_RUN : chrome::startup::IS_NOT_FIRST_RUN;

  browser_creator.LaunchBrowser(*CommandLine::ForCurrentProcess(),
                                profile,
                                base::FilePath(),
                                chrome::startup::IS_PROCESS_STARTUP,
                                first_run,
                                &return_code);

  // Triggers app launcher start page service to load start page web contents.
  app_list::StartPageService::Get(profile);
#endif

  // Mark login host for deletion after browser starts.  This
  // guarantees that the message loop will be referenced by the
  // browser before it is dereferenced by the login host.
  if (login_host)
    login_host->Finalize();
  user_manager::UserManager::Get()->SessionStarted();
  chromeos::BootTimesLoader::Get()->LoginDone(
      user_manager::UserManager::Get()->IsCurrentUserNew());
}

// static
void LoginUtilsImpl::RunCallbackOnLocaleLoaded(
    const base::Closure& callback,
    InputEventsBlocker* /* input_events_blocker */,
    const std::string& /* locale */,
    const std::string& /* loaded_locale */,
    const bool /* success */) {
  callback.Run();
}

void LoginUtilsImpl::RespectLocalePreference(Profile* profile,
                                             const base::Closure& callback) {
  if (browser_shutdown::IsTryingToQuit())
    return;

  user_manager::User* const user =
      ProfileHelper::Get()->GetUserByProfile(profile);
  scoped_ptr<locale_util::SwitchLanguageCallback> locale_switched_callback(
      new locale_util::SwitchLanguageCallback(base::Bind(
              &LoginUtilsImpl::RunCallbackOnLocaleLoaded,
              callback,
              base::Owned(new InputEventsBlocker))));  // Block UI events until
                                                       // the ResourceBundle is
                                                       // reloaded.
  if (!UserSessionManager::GetInstance()->RespectLocalePreference(
          profile,
          user,
          locale_switched_callback.Pass())) {
    callback.Run();
  }
}

void LoginUtilsImpl::DoBrowserLaunch(Profile* profile,
                                     LoginDisplayHost* login_host) {
  DoBrowserLaunchInternal(profile, login_host, false /* locale_pref_checked */);
}

void LoginUtilsImpl::PrepareProfile(
    const UserContext& user_context,
    bool has_auth_cookies,
    bool has_active_session,
    LoginUtils::Delegate* delegate) {
  // TODO(nkostylev): We have to initialize LoginUtils delegate as long
  // as it coexist with SessionManager.
  delegate_ = delegate;

  UserSessionManager::StartSessionType start_session_type =
      UserAddingScreen::Get()->IsRunning() ?
          UserSessionManager::SECONDARY_USER_SESSION :
          UserSessionManager::PRIMARY_USER_SESSION;

  // For the transition part LoginUtils will just delegate profile
  // creation and initialization to SessionManager. Later LoginUtils will be
  // removed and all LoginUtils clients will just work with SessionManager
  // directly.
  UserSessionManager::GetInstance()->StartSession(user_context,
                                                  start_session_type,
                                                  authenticator_,
                                                  has_auth_cookies,
                                                  has_active_session,
                                                  this);
}

void LoginUtilsImpl::DelegateDeleted(LoginUtils::Delegate* delegate) {
  if (delegate_ == delegate)
    delegate_ = NULL;
}

bool LoginUtilsImpl::RestartToApplyPerSessionFlagsIfNeed(Profile* profile,
                                                         bool early_restart) {
  if (ProfileHelper::IsSigninProfile(profile))
    return false;

  if (early_restart && !CanPerformEarlyRestart())
    return false;

  const CommandLine user_flags(CreatePerSessionCommandLine(profile));
  std::set<CommandLine::StringType> command_line_difference;
  if (!NeedRestartToApplyPerSessionFlags(user_flags, &command_line_difference))
    return false;

  LogCustomSwitches(command_line_difference);

  about_flags::ReportCustomFlags("Login.CustomFlags", command_line_difference);

  CommandLine::StringVector flags;
  // argv[0] is the program name |CommandLine::NO_PROGRAM|.
  flags.assign(user_flags.argv().begin() + 1, user_flags.argv().end());
  LOG(WARNING) << "Restarting to apply per-session flags...";
  DBusThreadManager::Get()->GetSessionManagerClient()->SetFlagsForUser(
      user_manager::UserManager::Get()->GetActiveUser()->email(), flags);
  AttemptRestart(profile);
  return true;
}

scoped_refptr<Authenticator> LoginUtilsImpl::CreateAuthenticator(
    AuthStatusConsumer* consumer) {
  // Screen locker needs new Authenticator instance each time.
  if (ScreenLocker::default_screen_locker()) {
    if (authenticator_.get())
      authenticator_->SetConsumer(NULL);
    authenticator_ = NULL;
  }

  if (authenticator_.get() == NULL) {
    authenticator_ = new ChromeCryptohomeAuthenticator(consumer);
  } else {
    // TODO(nkostylev): Fix this hack by improving Authenticator dependencies.
    authenticator_->SetConsumer(consumer);
  }
  return authenticator_;
}

void LoginUtilsImpl::OnProfilePrepared(Profile* profile,
                                       bool browser_launched) {
  if (delegate_)
    delegate_->OnProfilePrepared(profile, browser_launched);
}

#if defined(ENABLE_RLZ)
void LoginUtilsImpl::OnRlzInitialized() {
  if (delegate_)
    delegate_->OnRlzInitialized();
}
#endif

void LoginUtilsImpl::AttemptRestart(Profile* profile) {
  if (UserSessionManager::GetInstance()
          ->CheckEasyUnlockKeyOps(
              base::Bind(&LoginUtilsImpl::AttemptRestart,
                         base::Unretained(this),
                         profile))) {
    return;
  }

  if (UserSessionManager::GetInstance()->GetSigninSessionRestoreStrategy() !=
      OAuth2LoginManager::RESTORE_FROM_COOKIE_JAR) {
    chrome::AttemptRestart();
    return;
  }

  // We can't really quit if the session restore process that mints new
  // refresh token is still in progress.
  OAuth2LoginManager* login_manager =
      OAuth2LoginManagerFactory::GetInstance()->GetForProfile(profile);
  if (login_manager->state() !=
          OAuth2LoginManager::SESSION_RESTORE_PREPARING &&
      login_manager->state() !=
          OAuth2LoginManager::SESSION_RESTORE_IN_PROGRESS) {
    chrome::AttemptRestart();
    return;
  }

  LOG(WARNING) << "Attempting browser restart during session restore.";
  UserSessionManager::GetInstance()->set_exit_after_session_restore(true);
}

// static
LoginUtils* LoginUtils::Get() {
  return LoginUtilsWrapper::GetInstance()->get();
}

// static
void LoginUtils::Set(LoginUtils* mock) {
  LoginUtilsWrapper::GetInstance()->reset(mock);
}

// static
bool LoginUtils::IsWhitelisted(const std::string& username,
                               bool* wildcard_match) {
  // Skip whitelist check for tests.
  if (CommandLine::ForCurrentProcess()->HasSwitch(
      chromeos::switches::kOobeSkipPostLogin)) {
    return true;
  }

  CrosSettings* cros_settings = CrosSettings::Get();
  bool allow_new_user = false;
  cros_settings->GetBoolean(kAccountsPrefAllowNewUser, &allow_new_user);
  if (allow_new_user)
    return true;
  return cros_settings->FindEmailInList(
      kAccountsPrefUsers, username, wildcard_match);
}

}  // namespace chromeos
