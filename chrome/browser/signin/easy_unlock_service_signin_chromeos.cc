// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/easy_unlock_service_signin_chromeos.h"

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/sys_info.h"
#include "base/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/login/easy_unlock/easy_unlock_key_manager.h"
#include "chrome/browser/chromeos/login/easy_unlock/easy_unlock_metrics.h"
#include "chrome/browser/chromeos/login/session/user_session_manager.h"
#include "chromeos/login/auth/user_context.h"

namespace {

// The maximum allowed backoff interval when waiting for cryptohome to start.
uint32 kMaxCryptohomeBackoffIntervalMs = 10000u;

// If the data load fails, the initial interval after which the load will be
// retried. Further intervals will exponentially increas by factor 2.
uint32 kInitialCryptohomeBackoffIntervalMs = 200u;

// Calculates the backoff interval that should be used next.
// |backoff| The last backoff interval used.
uint32 GetNextBackoffInterval(uint32 backoff) {
  if (backoff == 0u)
    return kInitialCryptohomeBackoffIntervalMs;
  return backoff * 2;
}

void LoadDataForUser(
    const std::string& user_id,
    uint32 backoff_ms,
    const chromeos::EasyUnlockKeyManager::GetDeviceDataListCallback& callback);

// Callback passed to |LoadDataForUser()|.
// If |LoadDataForUser| function succeeded, it invokes |callback| with the
// results.
// If |LoadDataForUser| failed and further retries are allowed, schedules new
// |LoadDataForUser| call with some backoff. If no further retires are allowed,
// it invokes |callback| with the |LoadDataForUser| results.
void RetryDataLoadOnError(
    const std::string& user_id,
    uint32 backoff_ms,
    const chromeos::EasyUnlockKeyManager::GetDeviceDataListCallback& callback,
    bool success,
    const chromeos::EasyUnlockDeviceKeyDataList& data_list) {
  if (success) {
    callback.Run(success, data_list);
    return;
  }

  uint32 next_backoff_ms = GetNextBackoffInterval(backoff_ms);
  if (next_backoff_ms > kMaxCryptohomeBackoffIntervalMs) {
    callback.Run(false, data_list);
    return;
  }

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&LoadDataForUser, user_id, next_backoff_ms, callback),
      base::TimeDelta::FromMilliseconds(next_backoff_ms));
}

// Loads device data list associated with the user's Easy unlock keys.
void LoadDataForUser(
    const std::string& user_id,
    uint32 backoff_ms,
    const chromeos::EasyUnlockKeyManager::GetDeviceDataListCallback& callback) {
  chromeos::EasyUnlockKeyManager* key_manager =
      chromeos::UserSessionManager::GetInstance()->GetEasyUnlockKeyManager();
  DCHECK(key_manager);

  key_manager->GetDeviceDataList(
      chromeos::UserContext(user_id),
      base::Bind(&RetryDataLoadOnError, user_id, backoff_ms, callback));
}

}  // namespace

EasyUnlockServiceSignin::UserData::UserData()
    : state(EasyUnlockServiceSignin::USER_DATA_STATE_INITIAL) {
}

EasyUnlockServiceSignin::UserData::~UserData() {}

EasyUnlockServiceSignin::EasyUnlockServiceSignin(Profile* profile)
    : EasyUnlockService(profile),
      allow_cryptohome_backoff_(true),
      service_active_(false),
      weak_ptr_factory_(this) {
}

EasyUnlockServiceSignin::~EasyUnlockServiceSignin() {
}

EasyUnlockService::Type EasyUnlockServiceSignin::GetType() const {
  return EasyUnlockService::TYPE_SIGNIN;
}

std::string EasyUnlockServiceSignin::GetUserEmail() const {
  return user_id_;
}

void EasyUnlockServiceSignin::LaunchSetup() {
  NOTREACHED();
}

const base::DictionaryValue* EasyUnlockServiceSignin::GetPermitAccess() const {
  return NULL;
}

void EasyUnlockServiceSignin::SetPermitAccess(
    const base::DictionaryValue& permit) {
  NOTREACHED();
}

void EasyUnlockServiceSignin::ClearPermitAccess() {
  NOTREACHED();
}

const base::ListValue* EasyUnlockServiceSignin::GetRemoteDevices() const {
  const UserData* data = FindLoadedDataForCurrentUser();
  if (!data)
    return NULL;
  return &data->remote_devices_value;
}

void EasyUnlockServiceSignin::SetRemoteDevices(
    const base::ListValue& devices) {
  NOTREACHED();
}

void EasyUnlockServiceSignin::ClearRemoteDevices() {
  NOTREACHED();
}

void EasyUnlockServiceSignin::RunTurnOffFlow() {
  NOTREACHED();
}

void EasyUnlockServiceSignin::ResetTurnOffFlow() {
  NOTREACHED();
}

EasyUnlockService::TurnOffFlowStatus
    EasyUnlockServiceSignin::GetTurnOffFlowStatus() const {
  return EasyUnlockService::IDLE;
}

std::string EasyUnlockServiceSignin::GetChallenge() const {
  const UserData* data = FindLoadedDataForCurrentUser();
  // TODO(xiyuan): Use correct remote device instead of hard coded first one.
  uint32 device_index = 0;
  if (!data || data->devices.size() <= device_index)
    return std::string();
  return data->devices[device_index].challenge;
}

std::string EasyUnlockServiceSignin::GetWrappedSecret() const {
  const UserData* data = FindLoadedDataForCurrentUser();
  // TODO(xiyuan): Use correct remote device instead of hard coded first one.
  uint32 device_index = 0;
  if (!data || data->devices.size() <= device_index)
    return std::string();
  return data->devices[device_index].wrapped_secret;
}

void EasyUnlockServiceSignin::RecordEasySignInOutcome(
    const std::string& user_id,
    bool success) const {
  DCHECK_EQ(GetUserEmail(), user_id);

  chromeos::RecordEasyUnlockLoginEvent(success
                                           ? chromeos::EASY_SIGN_IN_SUCCESS
                                           : chromeos::EASY_SIGN_IN_FAILURE);
  DVLOG(1) << "Easy sign-in " << (success ? "success" : "failure");
}

void EasyUnlockServiceSignin::RecordPasswordLoginEvent(
    const std::string& user_id) const {
  // This happens during tests where user could login without pod focusing.
  if (GetUserEmail() != user_id)
    return;

  chromeos::EasyUnlockLoginEvent event =
      chromeos::EASY_SIGN_IN_LOGIN_EVENT_COUNT;
  if (!GetRemoteDevices() ||
      GetHardlockState() == EasyUnlockScreenlockStateHandler::NO_PAIRING) {
    event = chromeos::PASSWORD_SIGN_IN_NO_PAIRING;
  } else if (GetHardlockState() ==
             EasyUnlockScreenlockStateHandler::PAIRING_CHANGED) {
    event = chromeos::PASSWORD_SIGN_IN_PAIRING_CHANGED;
  } else if (GetHardlockState() ==
             EasyUnlockScreenlockStateHandler::USER_HARDLOCK) {
    event = chromeos::PASSWORD_SIGN_IN_USER_HARDLOCK;
  } else if (!screenlock_state_handler()) {
    event = chromeos::PASSWORD_SIGN_IN_SERVICE_NOT_ACTIVE;
  } else {
    switch (screenlock_state_handler()->state()) {
      case EasyUnlockScreenlockStateHandler::STATE_INACTIVE:
        event = chromeos::PASSWORD_SIGN_IN_SERVICE_NOT_ACTIVE;
        break;
      case EasyUnlockScreenlockStateHandler::STATE_NO_BLUETOOTH:
        event = chromeos::PASSWORD_SIGN_IN_NO_BLUETOOTH;
        break;
      case EasyUnlockScreenlockStateHandler::STATE_BLUETOOTH_CONNECTING:
        event = chromeos::PASSWORD_SIGN_IN_BLUETOOTH_CONNECTING;
        break;
      case EasyUnlockScreenlockStateHandler::STATE_NO_PHONE:
        event = chromeos::PASSWORD_SIGN_IN_NO_PHONE;
        break;
      case EasyUnlockScreenlockStateHandler::STATE_PHONE_NOT_AUTHENTICATED:
        event = chromeos::PASSWORD_SIGN_IN_PHONE_NOT_AUTHENTICATED;
        break;
      case EasyUnlockScreenlockStateHandler::STATE_PHONE_LOCKED:
        event = chromeos::PASSWORD_SIGN_IN_PHONE_LOCKED;
        break;
      case EasyUnlockScreenlockStateHandler::STATE_PHONE_UNLOCKABLE:
        event = chromeos::PASSWORD_SIGN_IN_PHONE_NOT_LOCKABLE;
        break;
      case EasyUnlockScreenlockStateHandler::STATE_PHONE_UNSUPPORTED:
        event = chromeos::PASSWORD_SIGN_IN_PHONE_UNSUPPORTED;
        break;
      case EasyUnlockScreenlockStateHandler::STATE_RSSI_TOO_LOW:
        event = chromeos::PASSWORD_SIGN_IN_RSSI_TOO_LOW;
        break;
      case EasyUnlockScreenlockStateHandler::STATE_TX_POWER_TOO_HIGH:
        event = chromeos::PASSWORD_SIGN_IN_TX_POWER_TOO_HIGH;
        break;
      case EasyUnlockScreenlockStateHandler::STATE_AUTHENTICATED:
        event = chromeos::PASSWORD_SIGN_IN_WITH_AUTHENTICATED_PHONE;
        break;
    }
  }

  chromeos::RecordEasyUnlockLoginEvent(event);
  DVLOG(1) << "EasySignIn password login event, event=" << event;
}

void EasyUnlockServiceSignin::InitializeInternal() {
  if (chromeos::LoginState::Get()->IsUserLoggedIn())
    return;

  service_active_ = true;

  chromeos::LoginState::Get()->AddObserver(this);
  ScreenlockBridge* screenlock_bridge = ScreenlockBridge::Get();
  screenlock_bridge->AddObserver(this);
  if (!screenlock_bridge->focused_user_id().empty())
    OnFocusedUserChanged(screenlock_bridge->focused_user_id());
}

void EasyUnlockServiceSignin::ShutdownInternal() {
  if (!service_active_)
    return;
  service_active_ = false;

  weak_ptr_factory_.InvalidateWeakPtrs();
  ScreenlockBridge::Get()->RemoveObserver(this);
  chromeos::LoginState::Get()->RemoveObserver(this);
  STLDeleteContainerPairSecondPointers(user_data_.begin(), user_data_.end());
  user_data_.clear();
}

bool EasyUnlockServiceSignin::IsAllowedInternal() {
  return service_active_ &&
         !user_id_.empty() &&
         !chromeos::LoginState::Get()->IsUserLoggedIn();
}

void EasyUnlockServiceSignin::OnScreenDidLock() {
  // Update initial UI is when the account picker on login screen is ready.
  ShowInitialUserState();
}

void EasyUnlockServiceSignin::OnScreenDidUnlock() {
  Shutdown();
}

void EasyUnlockServiceSignin::OnFocusedUserChanged(const std::string& user_id) {
  if (user_id_ == user_id)
    return;

  // Setting or clearing the user_id may changed |IsAllowed| value, so in these
  // cases update the app state. Otherwise, it's enough to notify the app the
  // user data has been updated.
  bool should_update_app_state = user_id_.empty() != user_id.empty();
  user_id_ = user_id;

  ResetScreenlockState();
  ShowInitialUserState();

  if (should_update_app_state) {
    UpdateAppState();
  } else {
    NotifyUserUpdated();
  }

  LoadCurrentUserDataIfNeeded();
}

void EasyUnlockServiceSignin::LoggedInStateChanged() {
  if (!chromeos::LoginState::Get()->IsUserLoggedIn())
    return;
  UnloadApp();
}

void EasyUnlockServiceSignin::LoadCurrentUserDataIfNeeded() {
  // TODO(xiyuan): Revisit this when adding tests.
  if (!base::SysInfo::IsRunningOnChromeOS())
    return;

  if (user_id_.empty() || !service_active_)
    return;

  std::map<std::string, UserData*>::iterator it = user_data_.find(user_id_);
  if (it == user_data_.end())
    user_data_.insert(std::make_pair(user_id_, new UserData()));

  UserData* data = user_data_[user_id_];

  if (data->state != USER_DATA_STATE_INITIAL)
    return;
  data->state = USER_DATA_STATE_LOADING;

  LoadDataForUser(
      user_id_,
      allow_cryptohome_backoff_ ? 0u : kMaxCryptohomeBackoffIntervalMs,
      base::Bind(&EasyUnlockServiceSignin::OnUserDataLoaded,
                 weak_ptr_factory_.GetWeakPtr(),
                 user_id_));
}

void EasyUnlockServiceSignin::OnUserDataLoaded(
    const std::string& user_id,
    bool success,
    const chromeos::EasyUnlockDeviceKeyDataList& devices) {
  allow_cryptohome_backoff_ = false;

  UserData* data = user_data_[user_id_];
  data->state = USER_DATA_STATE_LOADED;
  if (success) {
    data->devices = devices;
    chromeos::EasyUnlockKeyManager::DeviceDataListToRemoteDeviceList(
        user_id, devices, &data->remote_devices_value);
  }

  // If the fetched data belongs to the currently focused user, notify the app
  // that it has to refresh it's user data.
  if (user_id == user_id_)
    NotifyUserUpdated();
}

const EasyUnlockServiceSignin::UserData*
    EasyUnlockServiceSignin::FindLoadedDataForCurrentUser() const {
  if (user_id_.empty())
    return NULL;

  std::map<std::string, UserData*>::const_iterator it =
      user_data_.find(user_id_);
  if (it == user_data_.end())
    return NULL;
  if (it->second->state != USER_DATA_STATE_LOADED)
    return NULL;
  return it->second;
}
