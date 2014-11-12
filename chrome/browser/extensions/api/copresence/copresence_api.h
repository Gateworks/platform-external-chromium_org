// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_COPRESENCE_COPRESENCE_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_COPRESENCE_COPRESENCE_API_H_

#include <map>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/extensions/api/copresence/copresence_translations.h"
#include "chrome/browser/extensions/chrome_extension_function.h"
#include "chrome/common/extensions/api/copresence.h"
#include "components/copresence/public/copresence_delegate.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"

class ChromeWhispernetClient;

namespace copresence {
class CopresenceManager;
class WhispernetClient;
}

namespace gcm {
class GCMDriver;
}

namespace extensions {

class CopresenceService : public BrowserContextKeyedAPI,
                          public copresence::CopresenceDelegate {
 public:
  explicit CopresenceService(content::BrowserContext* context);
  ~CopresenceService() override;

  // BrowserContextKeyedAPI implementation.
  void Shutdown() override;

  // These accessors will always return an object (except during shutdown).
  // If the object doesn't exist, they will create one first.
  copresence::CopresenceManager* manager();
  copresence::WhispernetClient* whispernet_client();

  // A registry containing the app id's associated with every subscription.
  SubscriptionToAppMap& apps_by_subscription_id() {
    return apps_by_subscription_id_;
  }

  void set_api_key(const std::string& app_id,
                   const std::string& api_key);

  std::string auth_token() const {
    return auth_token_;
  }

  void set_auth_token(const std::string& token);

  // Manager override for testing.
  void set_manager_for_testing(
      scoped_ptr<copresence::CopresenceManager> manager);

  // BrowserContextKeyedAPI implementation.
  static BrowserContextKeyedAPIFactory<CopresenceService>* GetFactoryInstance();

 private:
  friend class BrowserContextKeyedAPIFactory<CopresenceService>;

  // CopresenceDelegate implementation
  void HandleMessages(const std::string& app_id,
                      const std::string& subscription_id,
                      const std::vector<copresence::Message>& message) override;
  void HandleStatusUpdate(copresence::CopresenceStatus status) override;
  net::URLRequestContextGetter* GetRequestContext() const override;
  const std::string GetPlatformVersionString() const override;
  const std::string GetAPIKey(const std::string& app_id) const override;
  copresence::WhispernetClient* GetWhispernetClient() override;
  gcm::GCMDriver* GetGCMDriver() override;

  // BrowserContextKeyedAPI implementation.
  static const char* service_name() { return "CopresenceService"; }

  bool is_shutting_down_;
  std::map<std::string, std::string> apps_by_subscription_id_;

  content::BrowserContext* const browser_context_;
  std::map<std::string, std::string> api_keys_by_app_;

  // TODO(ckehoe): This is a temporary hack.
  // Auth tokens from different apps needs to be separated properly.
  std::string auth_token_;

  scoped_ptr<copresence::CopresenceManager> manager_;
  scoped_ptr<ChromeWhispernetClient> whispernet_client_;

  DISALLOW_COPY_AND_ASSIGN(CopresenceService);
};

template <>
void BrowserContextKeyedAPIFactory<
    CopresenceService>::DeclareFactoryDependencies();

class CopresenceExecuteFunction : public ChromeUIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("copresence.execute", COPRESENCE_EXECUTE);

 protected:
  ~CopresenceExecuteFunction() override {}
  ExtensionFunction::ResponseAction Run() override;

 private:
  void SendResult(copresence::CopresenceStatus status);
};

class CopresenceSetApiKeyFunction : public ChromeUIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("copresence.setApiKey", COPRESENCE_SETAPIKEY);

 protected:
  ~CopresenceSetApiKeyFunction() override {}
  ExtensionFunction::ResponseAction Run() override;
};

class CopresenceSetAuthTokenFunction : public ChromeUIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("copresence.setAuthToken",
                             COPRESENCE_SETAUTHTOKEN);

 protected:
  virtual ~CopresenceSetAuthTokenFunction() {}
  ExtensionFunction::ResponseAction Run() override;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_COPRESENCE_COPRESENCE_API_H_
