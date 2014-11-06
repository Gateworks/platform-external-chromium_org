// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/api/networking_private/networking_private_credentials_getter.h"
#include "chrome/browser/extensions/api/networking_private/networking_private_service_client.h"
#include "chrome/browser/extensions/api/networking_private/networking_private_service_client_factory.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#include "components/wifi/fake_wifi_service.h"
#include "content/public/test/test_utils.h"
#include "extensions/common/switches.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::Return;
using testing::_;

using extensions::NetworkingPrivateServiceClient;
using extensions::NetworkingPrivateServiceClientFactory;

// This tests the Windows / Mac implementation of the networkingPrivate API.
// Note: the expectations in test/data/extensions/api_test/networking/test.js
// are shared between this and the Chrome OS tests. TODO(stevenjb): Develop
// a mechanism to specify the test expectations from here to eliminate that
// dependency.

namespace {

// Stub Verify* methods implementation to satisfy expectations of
// networking_private_apitest.
class CryptoVerifyStub
    : public extensions::NetworkingPrivateServiceClient::CryptoVerify {
  void VerifyDestination(const Credentials& verification_properties,
                         bool* verified,
                         std::string* error) override {
    *verified = true;
  }

  void VerifyAndEncryptCredentials(
      const std::string& network_guid,
      const Credentials& credentials,
      const VerifyAndEncryptCredentialsCallback& callback) override {
    callback.Run("encrypted_credentials", "");
  }

  void VerifyAndEncryptData(const Credentials& verification_properties,
                            const std::string& data,
                            std::string* base64_encoded_ciphertext,
                            std::string* error) override {
    *base64_encoded_ciphertext = "encrypted_data";
  }
};

class NetworkingPrivateServiceClientApiTest : public ExtensionApiTest {
 public:
  NetworkingPrivateServiceClientApiTest() {}

  bool RunNetworkingSubtest(const std::string& subtest) {
    return RunExtensionSubtest("networking",
                               "main.html?" + subtest,
                               kFlagEnableFileAccess | kFlagLoadAsComponent);
  }

  void SetUpInProcessBrowserTestFixture() override {
    ExtensionApiTest::SetUpInProcessBrowserTestFixture();
  }

  void SetUpCommandLine(CommandLine* command_line) override {
    ExtensionApiTest::SetUpCommandLine(command_line);
    // Whitelist the extension ID of the test extension.
    command_line->AppendSwitchASCII(
        extensions::switches::kWhitelistedExtensionID,
        "epcifkihnkjgphfkloaaleeakhpmgdmn");
  }

  static KeyedService* CreateNetworkingPrivateServiceClient(
      content::BrowserContext* profile) {
    return new NetworkingPrivateServiceClient(new wifi::FakeWiFiService(),
                                              new CryptoVerifyStub());
  }

  void SetUpOnMainThread() override {
    ExtensionApiTest::SetUpOnMainThread();
    content::RunAllPendingInMessageLoop();
    NetworkingPrivateServiceClientFactory::GetInstance()->SetTestingFactory(
        profile(), &CreateNetworkingPrivateServiceClient);
  }

 protected:
  DISALLOW_COPY_AND_ASSIGN(NetworkingPrivateServiceClientApiTest);
};

// Place each subtest into a separate browser test so that the stub networking
// library state is reset for each subtest run. This way they won't affect each
// other.

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest, StartConnect) {
  EXPECT_TRUE(RunNetworkingSubtest("startConnect")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest, StartDisconnect) {
  EXPECT_TRUE(RunNetworkingSubtest("startDisconnect")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       StartConnectNonexistent) {
  EXPECT_TRUE(RunNetworkingSubtest("startConnectNonexistent")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       StartDisconnectNonexistent) {
  EXPECT_TRUE(RunNetworkingSubtest("startDisconnectNonexistent")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       StartGetPropertiesNonexistent) {
  EXPECT_TRUE(RunNetworkingSubtest("startGetPropertiesNonexistent"))
      << message_;
}

// TODO(stevenjb/mef): Fix these, crbug.com/371442.
IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       DISABLED_GetNetworks) {
  EXPECT_TRUE(RunNetworkingSubtest("getNetworks")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       DISABLED_GetVisibleNetworks) {
  EXPECT_TRUE(RunNetworkingSubtest("getVisibleNetworks")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       GetVisibleNetworksWifi) {
  EXPECT_TRUE(RunNetworkingSubtest("getVisibleNetworksWifi")) << message_;
}

// TODO(stevenjb/mef): Fix this, crbug.com/371442.
IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       DISABLED_RequestNetworkScan) {
  EXPECT_TRUE(RunNetworkingSubtest("requestNetworkScan")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest, GetProperties) {
  EXPECT_TRUE(RunNetworkingSubtest("getProperties")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest, GetState) {
  EXPECT_TRUE(RunNetworkingSubtest("getState")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       GetStateNonExistent) {
  EXPECT_TRUE(RunNetworkingSubtest("getStateNonExistent")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest, SetProperties) {
  EXPECT_TRUE(RunNetworkingSubtest("setProperties")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest, CreateNetwork) {
  EXPECT_TRUE(RunNetworkingSubtest("createNetwork")) << message_;
}

// TODO(stevenjb/mef): Fix this, crbug.com/371442.
IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       DISABLED_GetManagedProperties) {
  EXPECT_TRUE(RunNetworkingSubtest("getManagedProperties")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       OnNetworksChangedEventConnect) {
  EXPECT_TRUE(RunNetworkingSubtest("onNetworksChangedEventConnect"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       OnNetworksChangedEventDisconnect) {
  EXPECT_TRUE(RunNetworkingSubtest("onNetworksChangedEventDisconnect"))
      << message_;
}

// TODO(stevenjb/mef): Fix this, crbug.com/371442.
IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       DISABLED_OnNetworkListChangedEvent) {
  EXPECT_TRUE(RunNetworkingSubtest("onNetworkListChangedEvent")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       VerifyDestination) {
  EXPECT_TRUE(RunNetworkingSubtest("verifyDestination")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       VerifyAndEncryptCredentials) {
  EXPECT_TRUE(RunNetworkingSubtest("verifyAndEncryptCredentials")) << message_;
}

IN_PROC_BROWSER_TEST_F(NetworkingPrivateServiceClientApiTest,
                       VerifyAndEncryptData) {
  EXPECT_TRUE(RunNetworkingSubtest("verifyAndEncryptData")) << message_;
}

}  // namespace
