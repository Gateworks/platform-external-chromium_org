// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_UI_WEBUI_LOGIN_DISPLAY_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_UI_WEBUI_LOGIN_DISPLAY_H_

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "chrome/browser/chromeos/login/screens/gaia_screen.h"
#include "chrome/browser/chromeos/login/screens/user_selection_screen.h"
#include "chrome/browser/chromeos/login/signin_specifics.h"
#include "chrome/browser/chromeos/login/ui/login_display.h"
#include "chrome/browser/ui/webui/chromeos/login/native_window_delegate.h"
#include "chrome/browser/ui/webui/chromeos/login/signin_screen_handler.h"
#include "components/user_manager/user.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/user_activity_observer.h"

namespace chromeos {
// WebUI-based login UI implementation.
class WebUILoginDisplay : public LoginDisplay,
                          public NativeWindowDelegate,
                          public SigninScreenHandlerDelegate,
                          public wm::UserActivityObserver {
 public:
  explicit WebUILoginDisplay(LoginDisplay::Delegate* delegate);
  virtual ~WebUILoginDisplay();

  // LoginDisplay implementation:
  virtual void ClearAndEnablePassword() override;
  virtual void Init(const user_manager::UserList& users,
                    bool show_guest,
                    bool show_users,
                    bool show_new_user) override;
  virtual void OnPreferencesChanged() override;
  virtual void OnBeforeUserRemoved(const std::string& username) override;
  virtual void OnUserImageChanged(const user_manager::User& user) override;
  virtual void OnUserRemoved(const std::string& username) override;
  virtual void SetUIEnabled(bool is_enabled) override;
  virtual void ShowError(int error_msg_id,
                         int login_attempts,
                         HelpAppLauncher::HelpTopic help_topic_id) override;
  virtual void ShowErrorScreen(LoginDisplay::SigninError error_id) override;
  virtual void ShowGaiaPasswordChanged(const std::string& username) override;
  virtual void ShowPasswordChangedDialog(bool show_password_error) override;
  virtual void ShowSigninUI(const std::string& email) override;

  // NativeWindowDelegate implementation:
  virtual gfx::NativeWindow GetNativeWindow() const override;

  // SigninScreenHandlerDelegate implementation:
  virtual void CancelPasswordChangedFlow() override;
  virtual void ResyncUserData() override;
  virtual void MigrateUserData(const std::string& old_password) override;

  virtual void Login(const UserContext& user_context,
                     const SigninSpecifics& specifics) override;
  virtual bool IsSigninInProgress() const override;
  virtual void Signout() override;
  virtual void CreateAccount() override;
  virtual void CompleteLogin(const UserContext& user_context) override;

  virtual void OnSigninScreenReady() override;
  virtual void CancelUserAdding() override;
  virtual void LoadWallpaper(const std::string& username) override;
  virtual void LoadSigninWallpaper() override;
  virtual void RemoveUser(const std::string& username) override;
  virtual void ShowEnterpriseEnrollmentScreen() override;
  virtual void ShowKioskEnableScreen() override;
  virtual void ShowKioskAutolaunchScreen() override;
  virtual void ShowWrongHWIDScreen() override;
  virtual void SetWebUIHandler(
      LoginDisplayWebUIHandler* webui_handler) override;
  virtual void ShowSigninScreenForCreds(const std::string& username,
                                        const std::string& password);
  virtual const user_manager::UserList& GetUsers() const override;
  virtual bool IsShowGuest() const override;
  virtual bool IsShowUsers() const override;
  virtual bool IsUserSigninCompleted() const override;
  virtual void SetDisplayEmail(const std::string& email) override;
  virtual void HandleGetUsers() override;
  virtual void SetAuthType(
      const std::string& username,
      ScreenlockBridge::LockHandler::AuthType auth_type) override;
  virtual ScreenlockBridge::LockHandler::AuthType GetAuthType(
      const std::string& username) const override;

  // wm::UserActivityDetector implementation:
  virtual void OnUserActivity(const ui::Event* event) override;

 private:

  // Whether to show guest login.
  bool show_guest_;

  // Weather to show the user pods or a GAIA sign in.
  // Public sessions are always shown.
  bool show_users_;

  // Whether to show add new user.
  bool show_new_user_;

  // Reference to the WebUI handling layer for the login screen
  LoginDisplayWebUIHandler* webui_handler_;

  scoped_ptr<GaiaScreen> gaia_screen_;
  scoped_ptr<UserSelectionScreen> user_selection_screen_;

  DISALLOW_COPY_AND_ASSIGN(WebUILoginDisplay);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_UI_WEBUI_LOGIN_DISPLAY_H_
