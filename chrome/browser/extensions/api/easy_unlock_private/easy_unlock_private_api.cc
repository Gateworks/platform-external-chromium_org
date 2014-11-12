// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/easy_unlock_private/easy_unlock_private_api.h"

#include <vector>

#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/memory/linked_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/easy_unlock_private/easy_unlock_private_crypto_delegate.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/easy_unlock_screenlock_state_handler.h"
#include "chrome/browser/signin/easy_unlock_service.h"
#include "chrome/browser/signin/screenlock_bridge.h"
#include "chrome/common/extensions/api/easy_unlock_private.h"
#include "chrome/grit/generated_resources.h"
#include "components/proximity_auth/bluetooth_util.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/chromeos_utils.h"
#include "chrome/browser/ui/webui/options/chromeos/user_image_source.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#endif

namespace extensions {
namespace api {

namespace {

static base::LazyInstance<BrowserContextKeyedAPIFactory<EasyUnlockPrivateAPI> >
    g_factory = LAZY_INSTANCE_INITIALIZER;

// Utility method for getting the API's crypto delegate.
EasyUnlockPrivateCryptoDelegate* GetCryptoDelegate(
    content::BrowserContext* context) {
  return BrowserContextKeyedAPIFactory<EasyUnlockPrivateAPI>::Get(context)
             ->crypto_delegate();
}

EasyUnlockScreenlockStateHandler::State ToScreenlockStateHandlerState(
    easy_unlock_private::State state) {
  switch (state) {
    case easy_unlock_private::STATE_NO_BLUETOOTH:
      return EasyUnlockScreenlockStateHandler::STATE_NO_BLUETOOTH;
    case easy_unlock_private::STATE_BLUETOOTH_CONNECTING:
      return EasyUnlockScreenlockStateHandler::STATE_BLUETOOTH_CONNECTING;
    case easy_unlock_private::STATE_NO_PHONE:
      return EasyUnlockScreenlockStateHandler::STATE_NO_PHONE;
    case easy_unlock_private::STATE_PHONE_NOT_AUTHENTICATED:
      return EasyUnlockScreenlockStateHandler::STATE_PHONE_NOT_AUTHENTICATED;
    case easy_unlock_private::STATE_PHONE_LOCKED:
      return EasyUnlockScreenlockStateHandler::STATE_PHONE_LOCKED;
    case easy_unlock_private::STATE_PHONE_UNLOCKABLE:
      return EasyUnlockScreenlockStateHandler::STATE_PHONE_UNLOCKABLE;
    case easy_unlock_private::STATE_PHONE_UNSUPPORTED:
      return EasyUnlockScreenlockStateHandler::STATE_PHONE_UNSUPPORTED;
    case easy_unlock_private::STATE_RSSI_TOO_LOW:
      return EasyUnlockScreenlockStateHandler::STATE_RSSI_TOO_LOW;
    case easy_unlock_private::STATE_TX_POWER_TOO_HIGH:
      return EasyUnlockScreenlockStateHandler::STATE_TX_POWER_TOO_HIGH;
    case easy_unlock_private::STATE_AUTHENTICATED:
      return EasyUnlockScreenlockStateHandler::STATE_AUTHENTICATED;
    default:
      return EasyUnlockScreenlockStateHandler::STATE_INACTIVE;
  }
}

}  // namespace

// static
BrowserContextKeyedAPIFactory<EasyUnlockPrivateAPI>*
    EasyUnlockPrivateAPI::GetFactoryInstance() {
  return g_factory.Pointer();
}

EasyUnlockPrivateAPI::EasyUnlockPrivateAPI(content::BrowserContext* context)
    : crypto_delegate_(EasyUnlockPrivateCryptoDelegate::Create()) {
}

EasyUnlockPrivateAPI::~EasyUnlockPrivateAPI() {}

EasyUnlockPrivateGetStringsFunction::EasyUnlockPrivateGetStringsFunction() {
}
EasyUnlockPrivateGetStringsFunction::~EasyUnlockPrivateGetStringsFunction() {
}

bool EasyUnlockPrivateGetStringsFunction::RunSync() {
  scoped_ptr<base::DictionaryValue> strings(new base::DictionaryValue);

#if defined(OS_CHROMEOS)
  const base::string16 device_type = chromeos::GetChromeDeviceType();
#else
  // TODO(isherman): Set an appropriate device name for non-ChromeOS devices.
  const base::string16 device_type = base::ASCIIToUTF16("Chromeschnozzle");
#endif  // defined(OS_CHROMEOS)

#if defined(OS_CHROMEOS)
  const user_manager::UserManager* manager = user_manager::UserManager::Get();
  const user_manager::User* user = manager ? manager->GetActiveUser() : NULL;
  const std::string user_email_utf8 =
      user ? user->display_email() : std::string();
  const base::string16 user_email = base::UTF8ToUTF16(user_email_utf8);
#else
  // TODO(isherman): Set an appropriate user display email for non-ChromeOS
  // platforms.
  const base::string16 user_email = base::UTF8ToUTF16("superman@example.com");
#endif  // defined(OS_CHROMEOS)

  // Common strings.
  strings->SetString(
      "learnMoreLinkTitle",
      l10n_util::GetStringUTF16(IDS_EASY_UNLOCK_LEARN_MORE_LINK_TITLE));
  strings->SetString("deviceType", device_type);

  // Setup notification strings.
  strings->SetString(
      "setupNotificationTitle",
      l10n_util::GetStringFUTF16(IDS_EASY_UNLOCK_SETUP_NOTIFICATION_TITLE,
                                 device_type));
  strings->SetString(
      "setupNotificationMessage",
      l10n_util::GetStringFUTF16(IDS_EASY_UNLOCK_SETUP_NOTIFICATION_MESSAGE,
                                 device_type));
  strings->SetString(
      "setupNotificationButtonTitle",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_NOTIFICATION_BUTTON_TITLE));

  // Chromebook added to Easy Unlock notification strings.
  strings->SetString(
      "chromebookAddedNotificationTitle",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_CHROMEBOOK_ADDED_NOTIFICATION_TITLE));
  strings->SetString(
      "chromebookAddedNotificationMessage",
      l10n_util::GetStringFUTF16(
          IDS_EASY_UNLOCK_CHROMEBOOK_ADDED_NOTIFICATION_MESSAGE,
          device_type));
  strings->SetString(
      "chromebookAddedNotificationAboutButton",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_CHROMEBOOK_ADDED_NOTIFICATION_ABOUT_BUTTON));

  // Shared "Learn more" button for the pairing changed and pairing change
  // applied notification.
  strings->SetString(
      "phoneChangedNotificationLearnMoreButton",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_NOTIFICATION_LEARN_MORE_BUTTON));

  // Pairing changed notification strings.
  strings->SetString(
      "phoneChangedNotificationTitle",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_PAIRING_CHANGED_NOTIFICATION_TITLE));
  strings->SetString(
      "phoneChangedNotificationMessage",
      l10n_util::GetStringFUTF16(
          IDS_EASY_UNLOCK_PAIRING_CHANGED_NOTIFICATION_MESSAGE,
          device_type));
  strings->SetString(
      "phoneChangedNotificationUpdateButton",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_PAIRING_CHANGED_NOTIFICATION_UPDATE_BUTTON));

  // Phone change applied notification strings.
  strings->SetString(
      "phoneChangeAppliedNotificationTitle",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_PAIRING_CHANGE_APPLIED_NOTIFICATION_TITLE));
  strings->SetString(
      "phoneChangeAppliedNotificationMessage",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_PAIRING_CHANGE_APPLIED_NOTIFICATION_MESSAGE));

  // Setup dialog strings.
  // Step 1: Intro.
  strings->SetString(
      "setupIntroHeaderTitle",
      l10n_util::GetStringUTF16(IDS_EASY_UNLOCK_SETUP_INTRO_HEADER_TITLE));
  strings->SetString(
      "setupIntroHeaderText",
      l10n_util::GetStringFUTF16(IDS_EASY_UNLOCK_SETUP_INTRO_HEADER_TEXT,
                                 device_type,
                                 user_email));
  strings->SetString(
      "setupIntroFindPhoneButtonLabel",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_INTRO_FIND_PHONE_BUTTON_LABEL));
  strings->SetString(
      "setupIntroFindingPhoneButtonLabel",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_INTRO_FINDING_PHONE_BUTTON_LABEL));
  strings->SetString(
      "setupIntroRetryFindPhoneButtonLabel",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_INTRO_RETRY_FIND_PHONE_BUTTON_LABEL));
  strings->SetString(
      "setupIntroHowIsThisSecureLinkText",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_INTRO_HOW_IS_THIS_SECURE_LINK_TEXT));
  // Step 1.5: Phone found but is not secured with lock screen
  strings->SetString("setupSecurePhoneHeaderTitle",
                     l10n_util::GetStringUTF16(
                         IDS_EASY_UNLOCK_SETUP_SECURE_PHONE_HEADER_TITLE));
  strings->SetString(
      "setupSecurePhoneHeaderText",
      l10n_util::GetStringFUTF16(IDS_EASY_UNLOCK_SETUP_SECURE_PHONE_HEADER_TEXT,
                                 device_type));
  // Step 2: Found a viable phone.
  strings->SetString(
      "setupFoundPhoneHeaderTitle",
      l10n_util::GetStringFUTF16(
          IDS_EASY_UNLOCK_SETUP_FOUND_PHONE_HEADER_TITLE, device_type));
  strings->SetString(
      "setupFoundPhoneHeaderText",
      l10n_util::GetStringFUTF16(
          IDS_EASY_UNLOCK_SETUP_FOUND_PHONE_HEADER_TEXT, device_type));
  strings->SetString(
      "setupFoundPhoneUseThisPhoneButtonLabel",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_FOUND_PHONE_USE_THIS_PHONE_BUTTON_LABEL));
  strings->SetString("setupFoundPhoneDeviceFormattedButtonLabel",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_FOUND_PHONE_DEVICE_FORMATTED_BUTTON_LABEL));
  strings->SetString(
      "setupFoundPhoneSwitchPhoneLinkLabel",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_FOUND_PHONE_SWITCH_PHONE_LINK_LABEL));
  strings->SetString(
      "setupPairingPhoneFailedButtonLabel",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_PAIRING_PHONE_FAILED_BUTTON_LABEL));
  // Step 2.5: Recommend user to set up Android Smart Lock
  strings->SetString(
      "setupAndroidSmartLockHeaderTitle",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_ANDROID_SMART_LOCK_HEADER_TITLE));
  strings->SetString("setupAndroidSmartLockHeaderText",
                     l10n_util::GetStringUTF16(
                         IDS_EASY_UNLOCK_SETUP_ANDROID_SMART_LOCK_HEADER_TEXT));
  strings->SetString(
      "setupAndroidSmartLockDoneButtonText",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_ANDROID_SMART_LOCK_DONE_BUTTON_LABEL));
  strings->SetString(
      "setupAndroidSmartLockAboutLinkText",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_ANDROID_SMART_LOCK_ABOUT_LINK_TEXT));
  // Step 3: Setup completed successfully.
  strings->SetString(
      "setupCompleteHeaderTitle",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_COMPLETE_HEADER_TITLE));
  strings->SetString(
      "setupCompleteHeaderText",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_COMPLETE_HEADER_TEXT));
  strings->SetString(
      "setupCompleteTryItOutButtonLabel",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_COMPLETE_TRY_IT_OUT_BUTTON_LABEL));
  strings->SetString(
      "setupCompleteSettingsLinkText",
      l10n_util::GetStringUTF16(
          IDS_EASY_UNLOCK_SETUP_COMPLETE_SETTINGS_LINK_TEXT));
  // Step 4: Post lockscreen confirmation.
  strings->SetString(
      "setupPostLockHeaderTitle",
      l10n_util::GetStringUTF16(IDS_EASY_UNLOCK_SETUP_POST_LOCK_HEADER_TITLE));
  strings->SetString(
      "setupPostLockHeaderText",
      l10n_util::GetStringUTF16(IDS_EASY_UNLOCK_SETUP_POST_LOCK_HEADER_TEXT));
  strings->SetString("setupPostLockTryItOutButtonLabel",
                     l10n_util::GetStringUTF16(
                         IDS_EASY_UNLOCK_SETUP_POST_LOCK_DISMISS_BUTTON_LABEL));

  // Error strings.
  strings->SetString(
      "setupErrorBluetoothUnavailable",
      l10n_util::GetStringFUTF16(
          IDS_EASY_UNLOCK_SETUP_ERROR_BLUETOOTH_UNAVAILBLE, device_type));
  strings->SetString(
      "setupErrorOffline",
      l10n_util::GetStringFUTF16(
          IDS_EASY_UNLOCK_SETUP_ERROR_OFFLINE, device_type));
  strings->SetString(
      "setupErrorFindingPhone",
      l10n_util::GetStringUTF16(IDS_EASY_UNLOCK_SETUP_ERROR_FINDING_PHONE));
  strings->SetString(
      "setupErrorBluetoothConnectionFailed",
      l10n_util::GetStringFUTF16(
          IDS_EASY_UNLOCK_SETUP_ERROR_BLUETOOTH_CONNECTION_FAILED,
          device_type));
  strings->SetString(
      "setupErrorConnectionToPhoneTimeout",
       l10n_util::GetStringFUTF16(
           IDS_EASY_UNLOCK_SETUP_ERROR_CONNECT_TO_PHONE_TIMEOUT,
           device_type));
  strings->SetString(
      "setupErrorSyncPhoneState",
       l10n_util::GetStringUTF16(
           IDS_EASY_UNLOCK_SETUP_ERROR_SYNC_PHONE_STATE_FAILED));
  strings->SetString(
      "setupErrorConnectingToPhone",
      l10n_util::GetStringFUTF16(
          IDS_EASY_UNLOCK_SETUP_ERROR_CONNECTING_TO_PHONE, device_type));

  // TODO(isherman): Remove this string once the app has been updated.
  strings->SetString("setupIntroHeaderFootnote", base::string16());

  SetResult(strings.release());
  return true;
}

EasyUnlockPrivatePerformECDHKeyAgreementFunction::
EasyUnlockPrivatePerformECDHKeyAgreementFunction() {}

EasyUnlockPrivatePerformECDHKeyAgreementFunction::
~EasyUnlockPrivatePerformECDHKeyAgreementFunction() {}

bool EasyUnlockPrivatePerformECDHKeyAgreementFunction::RunAsync() {
  scoped_ptr<easy_unlock_private::PerformECDHKeyAgreement::Params> params =
      easy_unlock_private::PerformECDHKeyAgreement::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params);

  GetCryptoDelegate(browser_context())->PerformECDHKeyAgreement(
      *params,
      base::Bind(&EasyUnlockPrivatePerformECDHKeyAgreementFunction::OnData,
                 this));
  return true;
}

void EasyUnlockPrivatePerformECDHKeyAgreementFunction::OnData(
    const std::string& secret_key) {
  // TODO(tbarzic): Improve error handling.
  if (!secret_key.empty()) {
    results_ = easy_unlock_private::PerformECDHKeyAgreement::Results::Create(
        secret_key);
  }
  SendResponse(true);
}

EasyUnlockPrivateGenerateEcP256KeyPairFunction::
EasyUnlockPrivateGenerateEcP256KeyPairFunction() {}

EasyUnlockPrivateGenerateEcP256KeyPairFunction::
~EasyUnlockPrivateGenerateEcP256KeyPairFunction() {}

bool EasyUnlockPrivateGenerateEcP256KeyPairFunction::RunAsync() {
  GetCryptoDelegate(browser_context())->GenerateEcP256KeyPair(
      base::Bind(&EasyUnlockPrivateGenerateEcP256KeyPairFunction::OnData,
                 this));
  return true;
}

void EasyUnlockPrivateGenerateEcP256KeyPairFunction::OnData(
    const std::string& private_key,
    const std::string& public_key) {
  // TODO(tbarzic): Improve error handling.
  if (!public_key.empty() && !private_key.empty()) {
    results_ = easy_unlock_private::GenerateEcP256KeyPair::Results::Create(
        public_key, private_key);
  }
  SendResponse(true);
}

EasyUnlockPrivateCreateSecureMessageFunction::
EasyUnlockPrivateCreateSecureMessageFunction() {}

EasyUnlockPrivateCreateSecureMessageFunction::
~EasyUnlockPrivateCreateSecureMessageFunction() {}

bool EasyUnlockPrivateCreateSecureMessageFunction::RunAsync() {
  scoped_ptr<easy_unlock_private::CreateSecureMessage::Params> params =
      easy_unlock_private::CreateSecureMessage::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params);

  GetCryptoDelegate(browser_context())->CreateSecureMessage(
      *params,
      base::Bind(&EasyUnlockPrivateCreateSecureMessageFunction::OnData,
                 this));
  return true;
}

void EasyUnlockPrivateCreateSecureMessageFunction::OnData(
    const std::string& message) {
  // TODO(tbarzic): Improve error handling.
  if (!message.empty()) {
    results_ = easy_unlock_private::CreateSecureMessage::Results::Create(
        message);
  }
  SendResponse(true);
}

EasyUnlockPrivateUnwrapSecureMessageFunction::
EasyUnlockPrivateUnwrapSecureMessageFunction() {}

EasyUnlockPrivateUnwrapSecureMessageFunction::
~EasyUnlockPrivateUnwrapSecureMessageFunction() {}

bool EasyUnlockPrivateUnwrapSecureMessageFunction::RunAsync() {
  scoped_ptr<easy_unlock_private::UnwrapSecureMessage::Params> params =
      easy_unlock_private::UnwrapSecureMessage::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params);

  GetCryptoDelegate(browser_context())->UnwrapSecureMessage(
      *params,
      base::Bind(&EasyUnlockPrivateUnwrapSecureMessageFunction::OnData,
                 this));
  return true;
}

void EasyUnlockPrivateUnwrapSecureMessageFunction::OnData(
    const std::string& data) {
  // TODO(tbarzic): Improve error handling.
  if (!data.empty())
    results_ = easy_unlock_private::UnwrapSecureMessage::Results::Create(data);
  SendResponse(true);
}

EasyUnlockPrivateSeekBluetoothDeviceByAddressFunction::
    EasyUnlockPrivateSeekBluetoothDeviceByAddressFunction() {}

EasyUnlockPrivateSeekBluetoothDeviceByAddressFunction::
    ~EasyUnlockPrivateSeekBluetoothDeviceByAddressFunction() {}

bool EasyUnlockPrivateSeekBluetoothDeviceByAddressFunction::RunAsync() {
  scoped_ptr<easy_unlock_private::SeekBluetoothDeviceByAddress::Params> params(
      easy_unlock_private::SeekBluetoothDeviceByAddress::Params::Create(
          *args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  proximity_auth::bluetooth_util::SeekDeviceByAddress(
      params->device_address,
      base::Bind(
          &EasyUnlockPrivateSeekBluetoothDeviceByAddressFunction::OnSeekSuccess,
          this),
      base::Bind(
          &EasyUnlockPrivateSeekBluetoothDeviceByAddressFunction::OnSeekFailure,
          this),
      content::BrowserThread::GetBlockingPool());
  return true;
}

void EasyUnlockPrivateSeekBluetoothDeviceByAddressFunction::OnSeekSuccess() {
  SendResponse(true);
}

void EasyUnlockPrivateSeekBluetoothDeviceByAddressFunction::OnSeekFailure(
    const std::string& error_message) {
  SetError(error_message);
  SendResponse(false);
}

EasyUnlockPrivateConnectToBluetoothServiceInsecurelyFunction::
    EasyUnlockPrivateConnectToBluetoothServiceInsecurelyFunction() {}

EasyUnlockPrivateConnectToBluetoothServiceInsecurelyFunction::
    ~EasyUnlockPrivateConnectToBluetoothServiceInsecurelyFunction() {}

void EasyUnlockPrivateConnectToBluetoothServiceInsecurelyFunction::
    ConnectToService(device::BluetoothDevice* device,
                     const device::BluetoothUUID& uuid) {
  device->ConnectToServiceInsecurely(
      uuid,
      base::Bind(&EasyUnlockPrivateConnectToBluetoothServiceInsecurelyFunction::
                     OnConnect,
                 this),
      base::Bind(&EasyUnlockPrivateConnectToBluetoothServiceInsecurelyFunction::
                     OnConnectError,
                 this));
}

EasyUnlockPrivateUpdateScreenlockStateFunction::
    EasyUnlockPrivateUpdateScreenlockStateFunction() {}

EasyUnlockPrivateUpdateScreenlockStateFunction::
    ~EasyUnlockPrivateUpdateScreenlockStateFunction() {}

bool EasyUnlockPrivateUpdateScreenlockStateFunction::RunSync() {
  scoped_ptr<easy_unlock_private::UpdateScreenlockState::Params> params(
      easy_unlock_private::UpdateScreenlockState::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  Profile* profile = Profile::FromBrowserContext(browser_context());
  if (EasyUnlockService::Get(profile)->UpdateScreenlockState(
          ToScreenlockStateHandlerState(params->state)))
    return true;

  SetError("Not allowed");
  return false;
}

EasyUnlockPrivateSetPermitAccessFunction::
    EasyUnlockPrivateSetPermitAccessFunction() {
}

EasyUnlockPrivateSetPermitAccessFunction::
    ~EasyUnlockPrivateSetPermitAccessFunction() {
}

bool EasyUnlockPrivateSetPermitAccessFunction::RunSync() {
  scoped_ptr<easy_unlock_private::SetPermitAccess::Params> params(
      easy_unlock_private::SetPermitAccess::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  Profile* profile = Profile::FromBrowserContext(browser_context());
  EasyUnlockService::Get(profile)
      ->SetPermitAccess(*params->permit_access.ToValue());

  return true;
}

EasyUnlockPrivateGetPermitAccessFunction::
    EasyUnlockPrivateGetPermitAccessFunction() {
}

EasyUnlockPrivateGetPermitAccessFunction::
    ~EasyUnlockPrivateGetPermitAccessFunction() {
}

bool EasyUnlockPrivateGetPermitAccessFunction::RunSync() {
  Profile* profile = Profile::FromBrowserContext(browser_context());
  const base::DictionaryValue* permit_value =
      EasyUnlockService::Get(profile)->GetPermitAccess();
  if (permit_value) {
    scoped_ptr<easy_unlock_private::PermitRecord> permit =
        easy_unlock_private::PermitRecord::FromValue(*permit_value);
    results_ = easy_unlock_private::GetPermitAccess::Results::Create(*permit);
  }

  return true;
}

EasyUnlockPrivateClearPermitAccessFunction::
    EasyUnlockPrivateClearPermitAccessFunction() {
}

EasyUnlockPrivateClearPermitAccessFunction::
    ~EasyUnlockPrivateClearPermitAccessFunction() {
}

bool EasyUnlockPrivateClearPermitAccessFunction::RunSync() {
  Profile* profile = Profile::FromBrowserContext(browser_context());
  EasyUnlockService::Get(profile)->ClearPermitAccess();
  return true;
}

EasyUnlockPrivateSetRemoteDevicesFunction::
    EasyUnlockPrivateSetRemoteDevicesFunction() {
}

EasyUnlockPrivateSetRemoteDevicesFunction::
    ~EasyUnlockPrivateSetRemoteDevicesFunction() {
}

bool EasyUnlockPrivateSetRemoteDevicesFunction::RunSync() {
  scoped_ptr<easy_unlock_private::SetRemoteDevices::Params> params(
      easy_unlock_private::SetRemoteDevices::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  Profile* profile = Profile::FromBrowserContext(browser_context());
  if (params->devices.empty()) {
    EasyUnlockService::Get(profile)->ClearRemoteDevices();
  } else {
    base::ListValue devices;
    for (size_t i = 0; i < params->devices.size(); ++i) {
      devices.Append(params->devices[i]->ToValue().release());
    }
    EasyUnlockService::Get(profile)->SetRemoteDevices(devices);
  }

  return true;
}

EasyUnlockPrivateGetRemoteDevicesFunction::
    EasyUnlockPrivateGetRemoteDevicesFunction() {
}

EasyUnlockPrivateGetRemoteDevicesFunction::
    ~EasyUnlockPrivateGetRemoteDevicesFunction() {
}

bool EasyUnlockPrivateGetRemoteDevicesFunction::RunSync() {
  Profile* profile = Profile::FromBrowserContext(browser_context());
  const base::ListValue* devices =
      EasyUnlockService::Get(profile)->GetRemoteDevices();
  SetResult(devices ? devices->DeepCopy() : new base::ListValue());
  return true;
}

EasyUnlockPrivateGetSignInChallengeFunction::
    EasyUnlockPrivateGetSignInChallengeFunction() {
}

EasyUnlockPrivateGetSignInChallengeFunction::
    ~EasyUnlockPrivateGetSignInChallengeFunction() {
}

bool EasyUnlockPrivateGetSignInChallengeFunction::RunSync() {
  Profile* profile = Profile::FromBrowserContext(browser_context());
  const std::string challenge =
      EasyUnlockService::Get(profile)->GetChallenge();
  if (!challenge.empty()) {
    results_ =
        easy_unlock_private::GetSignInChallenge::Results::Create(challenge);
  }
  return true;
}

EasyUnlockPrivateTrySignInSecretFunction::
    EasyUnlockPrivateTrySignInSecretFunction() {
}

EasyUnlockPrivateTrySignInSecretFunction::
    ~EasyUnlockPrivateTrySignInSecretFunction() {
}

bool EasyUnlockPrivateTrySignInSecretFunction::RunSync() {
  scoped_ptr<easy_unlock_private::TrySignInSecret::Params> params(
      easy_unlock_private::TrySignInSecret::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  Profile* profile = Profile::FromBrowserContext(browser_context());
  EasyUnlockService::Get(profile)->FinalizeSignin(params->sign_in_secret);
  return true;
}

EasyUnlockPrivateGetUserInfoFunction::EasyUnlockPrivateGetUserInfoFunction() {
}

EasyUnlockPrivateGetUserInfoFunction::~EasyUnlockPrivateGetUserInfoFunction() {
}

bool EasyUnlockPrivateGetUserInfoFunction::RunSync() {
  EasyUnlockService* service =
      EasyUnlockService::Get(Profile::FromBrowserContext(browser_context()));
  std::vector<linked_ptr<easy_unlock_private::UserInfo> > users;
  std::string user_id = service->GetUserEmail();
  if (!user_id.empty()) {
    users.push_back(
        linked_ptr<easy_unlock_private::UserInfo>(
            new easy_unlock_private::UserInfo()));
    users[0]->user_id = user_id;
    users[0]->logged_in = service->GetType() == EasyUnlockService::TYPE_REGULAR;
    users[0]->data_ready = users[0]->logged_in ||
                           service->GetRemoteDevices() != NULL;
  }
  results_ = easy_unlock_private::GetUserInfo::Results::Create(users);
  return true;
}

EasyUnlockPrivateGetUserImageFunction::EasyUnlockPrivateGetUserImageFunction() {
}

EasyUnlockPrivateGetUserImageFunction::
    ~EasyUnlockPrivateGetUserImageFunction() {
}

bool EasyUnlockPrivateGetUserImageFunction::RunSync() {
#if defined(OS_CHROMEOS)
  EasyUnlockService* service =
      EasyUnlockService::Get(Profile::FromBrowserContext(browser_context()));
  const std::vector<ui::ScaleFactor>& supported_scale_factors =
      ui::GetSupportedScaleFactors();

  base::RefCountedMemory* user_image =
      chromeos::options::UserImageSource::GetUserImage(
          service->GetUserEmail(), supported_scale_factors.back());

  results_ = easy_unlock_private::GetUserImage::Results::Create(std::string(
      reinterpret_cast<const char*>(user_image->front()), user_image->size()));
#else
  // TODO(tengs): Find a way to get the profile picture for non-ChromeOS
  // devices.
  results_ = easy_unlock_private::GetUserImage::Results::Create("");
  SetError("Not supported on non-ChromeOS platforms.");
#endif
  return true;
}

}  // namespace api
}  // namespace extensions
