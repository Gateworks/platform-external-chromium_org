// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_SESSION_USER_SESSION_MANAGER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_SESSION_USER_SESSION_MANAGER_H_

#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/chromeos/base/locale_util.h"
#include "chrome/browser/chromeos/login/signin/oauth2_login_manager.h"
#include "chromeos/dbus/session_manager_client.h"
#include "chromeos/ime/input_method_manager.h"
#include "chromeos/login/auth/authenticator.h"
#include "chromeos/login/auth/user_context.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#include "net/base/network_change_notifier.h"

class GURL;
class PrefRegistrySimple;
class PrefService;
class Profile;

namespace user_manager {

class User;

}  // namespace user_manager

namespace chromeos {

class EasyUnlockKeyManager;

class UserSessionManagerDelegate {
 public:
  // Called after profile is loaded and prepared for the session.
  // |browser_launched| will be true is browser has been launched, otherwise
  // it will return false and client is responsible on launching browser.
  virtual void OnProfilePrepared(Profile* profile,
                                 bool browser_launched) = 0;

#if defined(ENABLE_RLZ)
  // Called after post-profile RLZ initialization.
  virtual void OnRlzInitialized();
#endif
 protected:
  virtual ~UserSessionManagerDelegate();
};

class UserSessionStateObserver {
 public:
  // Called when UserManager finishes restoring user sessions after crash.
  virtual void PendingUserSessionsRestoreFinished();

 protected:
  virtual ~UserSessionStateObserver();
};

// UserSessionManager is responsible for starting user session which includes:
// * load and initialize Profile (including custom Profile preferences),
// * mark user as logged in and notify observers,
// * initialize OAuth2 authentication session,
// * initialize and launch user session based on the user type.
// Also supports restoring active user sessions after browser crash:
// load profile, restore OAuth authentication session etc.
class UserSessionManager
    : public OAuth2LoginManager::Observer,
      public net::NetworkChangeNotifier::ConnectionTypeObserver,
      public base::SupportsWeakPtr<UserSessionManager>,
      public UserSessionManagerDelegate,
      public user_manager::UserManager::UserSessionStateObserver {
 public:
  // Context of StartSession calls.
  typedef enum {
    // Starting primary user session, through login UI.
    PRIMARY_USER_SESSION,

    // Starting secondary user session, through multi-profiles login UI.
    SECONDARY_USER_SESSION,

    // Starting primary user session after browser crash.
    PRIMARY_USER_SESSION_AFTER_CRASH,

    // Starting secondary user session after browser crash.
    SECONDARY_USER_SESSION_AFTER_CRASH,
  } StartSessionType;

  // Returns UserSessionManager instance.
  static UserSessionManager* GetInstance();

  // Called when user is logged in to override base::DIR_HOME path.
  static void OverrideHomedir();

  // Registers session related preferences.
  static void RegisterPrefs(PrefRegistrySimple* registry);

  // Invoked after the tmpfs is successfully mounted.
  // Asks session_manager to restart Chrome in Guest session mode.
  // |start_url| is an optional URL to be opened in Guest session browser.
  void CompleteGuestSessionLogin(const GURL& start_url);

  // Start user session given |user_context| and |authenticator| which holds
  // authentication context (profile).
  void StartSession(const UserContext& user_context,
                    StartSessionType start_session_type,
                    scoped_refptr<Authenticator> authenticator,
                    bool has_auth_cookies,
                    bool has_active_session,
                    UserSessionManagerDelegate* delegate);

  // Perform additional actions once system wide notification
  // "UserLoggedIn" has been sent.
  void PerformPostUserLoggedInActions();

  // Restores authentication session after crash.
  void RestoreAuthenticationSession(Profile* profile);

  // Usually is called when Chrome is restarted after a crash and there's an
  // active session. First user (one that is passed with --login-user) Chrome
  // session has been already restored at this point. This method asks session
  // manager for all active user sessions, marks them as logged in
  // and notifies observers.
  void RestoreActiveSessions();

  // Returns true iff browser has been restarted after crash and
  // UserSessionManager finished restoring user sessions.
  bool UserSessionsRestored() const;

  // Returns true iff browser has been restarted after crash and
  // user sessions restoration is in progress.
  bool UserSessionsRestoreInProgress() const;

  // Initialize RLZ.
  void InitRlz(Profile* profile);

  // Get the NSS cert database for the user represented with |profile|
  // and start certificate loader with it.
  void InitializeCerts(Profile* profile);

  // TODO(nkostylev): Drop these methods once LoginUtilsImpl::AttemptRestart()
  // is migrated.
  OAuth2LoginManager::SessionRestoreStrategy GetSigninSessionRestoreStrategy();
  bool exit_after_session_restore() { return exit_after_session_restore_; }
  void set_exit_after_session_restore(bool value) {
    exit_after_session_restore_ = value;
  }

  // Invoked when the user is logging in for the first time, or is logging in to
  // an ephemeral session type, such as guest or a public session.
  void SetFirstLoginPrefs(Profile* profile,
                          const std::string& public_session_locale,
                          const std::string& public_session_input_method);

  // Gets/sets Chrome OAuth client id and secret for kiosk app mode. The default
  // values can be overridden with kiosk auth file.
  bool GetAppModeChromeClientOAuthInfo(
      std::string* chrome_client_id,
      std::string* chrome_client_secret);
  void SetAppModeChromeClientOAuthInfo(
      const std::string& chrome_client_id,
      const std::string& chrome_client_secret);

  // Changes browser locale (selects best suitable locale from different
  // user settings). Returns true if callback will be called.
  bool RespectLocalePreference(
      Profile* profile,
      const user_manager::User* user,
      scoped_ptr<locale_util::SwitchLanguageCallback> callback) const;

  // Returns true if Easy unlock keys needs to be updated.
  bool NeedsToUpdateEasyUnlockKeys() const;

  // Returns true if there are pending Easy unlock key operations and
  // |callback| will be invoked when it is done.
  bool CheckEasyUnlockKeyOps(const base::Closure& callback);

  void AddSessionStateObserver(chromeos::UserSessionStateObserver* observer);
  void RemoveSessionStateObserver(chromeos::UserSessionStateObserver* observer);

  virtual void ActiveUserChanged(
      const user_manager::User* active_user) override;

  // Returns default IME state for user session.
  scoped_refptr<input_method::InputMethodManager::State> GetDefaultIMEState(
      Profile* profile);

  // Note this could return NULL if not enabled.
  EasyUnlockKeyManager* GetEasyUnlockKeyManager();

  // Update Easy unlock cryptohome keys for given user context.
  void UpdateEasyUnlockKeys(const UserContext& user_context);

 private:
  friend struct DefaultSingletonTraits<UserSessionManager>;

  typedef std::set<std::string> SigninSessionRestoreStateSet;

  UserSessionManager();
  virtual ~UserSessionManager();

  // OAuth2LoginManager::Observer overrides:
  virtual void OnSessionRestoreStateChanged(
      Profile* user_profile,
      OAuth2LoginManager::SessionRestoreState state) override;
  virtual void OnNewRefreshTokenAvaiable(Profile* user_profile) override;

  // net::NetworkChangeNotifier::ConnectionTypeObserver overrides:
  virtual void OnConnectionTypeChanged(
      net::NetworkChangeNotifier::ConnectionType type) override;

  // UserSessionManagerDelegate overrides:
  // Used when restoring user sessions after crash.
  virtual void OnProfilePrepared(Profile* profile,
                                 bool browser_launched) override;

  void CreateUserSession(const UserContext& user_context,
                         bool has_auth_cookies);
  void PreStartSession();
  void StartCrosSession();
  void NotifyUserLoggedIn();
  void PrepareProfile();

  // Callback for asynchronous profile creation.
  void OnProfileCreated(const UserContext& user_context,
                        bool is_incognito_profile,
                        Profile* profile,
                        Profile::CreateStatus status);

  // Callback for Profile::CREATE_STATUS_CREATED profile state.
  // Initializes basic preferences for newly created profile. Any other
  // early profile initialization that needs to happen before
  // ProfileManager::DoFinalInit() gets called is done here.
  void InitProfilePreferences(Profile* profile,
                              const UserContext& user_context);

  // Callback for Profile::CREATE_STATUS_INITIALIZED profile state.
  // Profile is created, extensions and promo resources are initialized.
  void UserProfileInitialized(Profile* profile,
                              bool is_incognito_profile,
                              const std::string& user_id);

  // Callback to resume profile creation after transferring auth data from
  // the authentication profile.
  void CompleteProfileCreateAfterAuthTransfer(Profile* profile);

  // Finalized profile preparation.
  void FinalizePrepareProfile(Profile* profile);

  // Starts out-of-box flow with the specified screen.
  void ActivateWizard(const std::string& screen_name);

  // Adds first-time login URLs.
  void InitializeStartUrls() const;

  // Perform session initialization and either move to additional login flows
  // such as TOS (public sessions), priority pref sync UI (new users) or
  // launch browser.
  // Returns true if browser has been launched or false otherwise.
  bool InitializeUserSession(Profile* profile);

  // Initializes member variables needed for session restore process via
  // OAuthLoginManager.
  void InitSessionRestoreStrategy();

  // Restores GAIA auth cookies for the created user profile from OAuth2 token.
  void RestoreAuthSessionImpl(Profile* profile,
                              bool restore_from_auth_cookies);

  // Initializes RLZ. If |disabled| is true, RLZ pings are disabled.
  void InitRlzImpl(Profile* profile, bool disabled);

  // Starts loading CRL set.
  void InitializeCRLSetFetcher(const user_manager::User* user);

  // Callback to process RetrieveActiveSessions() request results.
  void OnRestoreActiveSessions(
      const SessionManagerClient::ActiveSessionsMap& sessions,
      bool success);

  // Called by OnRestoreActiveSessions() when there're user sessions in
  // |pending_user_sessions_| that has to be restored one by one.
  // Also called after first user session from that list is restored and so on.
  // Process continues till |pending_user_sessions_| map is not empty.
  void RestorePendingUserSessions();

  // Notifies observers that user pending sessions restore has finished.
  void NotifyPendingUserSessionsRestoreFinished();

  // Callback invoked when Easy unlock key operations are finished.
  void OnEasyUnlockKeyOpsFinished(const std::string& user_id,
                                  bool success);

  UserSessionManagerDelegate* delegate_;

  // Authentication/user context.
  UserContext user_context_;
  scoped_refptr<Authenticator> authenticator_;
  StartSessionType start_session_type_;

  // True if the authentication context's cookie jar contains authentication
  // cookies from the authentication extension login flow.
  bool has_auth_cookies_;

  // Active user session restoration related members.

  // True if user sessions has been restored after crash.
  // On a normal boot then login into user sessions this will be false.
  bool user_sessions_restored_;

  // True if user sessions restoration after crash is in progress.
  bool user_sessions_restore_in_progress_;

  // User sessions that have to be restored after browser crash.
  // [user_id] > [user_id_hash]
  SessionManagerClient::ActiveSessionsMap pending_user_sessions_;

  ObserverList<chromeos::UserSessionStateObserver> session_state_observer_list_;

  // OAuth2 session related members.

  // True if we should restart chrome right after session restore.
  bool exit_after_session_restore_;

  // Sesion restore strategy.
  OAuth2LoginManager::SessionRestoreStrategy session_restore_strategy_;

  // OAuth2 refresh token for session restore.
  std::string oauth2_refresh_token_;

  // Set of user_id for those users that we should restore authentication
  // session when notified about online state change.
  SigninSessionRestoreStateSet pending_signin_restore_sessions_;

  // Kiosk mode related members.
  // Chrome oauth client id and secret - override values for kiosk mode.
  std::string chrome_client_id_;
  std::string chrome_client_secret_;

  // Per-user-session Input Methods states.
  std::map<Profile*, scoped_refptr<input_method::InputMethodManager::State>,
      ProfileCompare> default_ime_states_;

  // Manages Easy unlock cryptohome keys.
  scoped_ptr<EasyUnlockKeyManager> easy_unlock_key_manager_;
  bool running_easy_unlock_key_ops_;
  base::Closure easy_unlock_key_ops_finished_callback_;

  DISALLOW_COPY_AND_ASSIGN(UserSessionManager);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_SESSION_USER_SESSION_MANAGER_H_
