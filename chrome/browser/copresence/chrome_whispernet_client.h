// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_COPRESENCE_CHROME_WHISPERNET_CLIENT_H_
#define CHROME_BROWSER_COPRESENCE_CHROME_WHISPERNET_CLIENT_H_

#include <string>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "components/copresence/public/copresence_constants.h"
#include "components/copresence/public/whispernet_client.h"

namespace content {
class BrowserContext;
}

namespace extensions {
namespace api {
namespace copresence_private {
struct AudioParameters;
}
}
}

namespace media {
class AudioBusRefCounted;
}

// This class is responsible for communication with our ledger_proxy extension
// that talks to the whispernet audio library.
class ChromeWhispernetClient final : public copresence::WhispernetClient {
 public:
  // The browser context needs to outlive this class.
  explicit ChromeWhispernetClient(content::BrowserContext* browser_context);
  ~ChromeWhispernetClient() override;

  // WhispernetClient overrides:
  void Initialize(const SuccessCallback& init_callback) override;
  void Shutdown() override;

  void EncodeToken(const std::string& token,
                   copresence::AudioType type) override;
  void DecodeSamples(copresence::AudioType type,
                     const std::string& samples) override;
  void DetectBroadcast() override;

  void RegisterTokensCallback(const TokensCallback& tokens_callback) override;
  void RegisterSamplesCallback(
      const SamplesCallback& samples_callback) override;
  void RegisterDetectBroadcastCallback(
      const SuccessCallback& db_callback) override;

  TokensCallback GetTokensCallback() override;
  SamplesCallback GetSamplesCallback() override;
  SuccessCallback GetDetectBroadcastCallback() override;
  SuccessCallback GetInitializedCallback() override;

  static const char kWhispernetProxyExtensionId[];

 private:
  // Fire an event to initialize whispernet with the given parameters.
  void InitializeWhispernet(
      const extensions::api::copresence_private::AudioParameters& params);

  // This gets called twice; once when the proxy extension loads, the second
  // time when we have initialized the proxy extension's encoder and decoder.
  void OnExtensionLoaded(bool success);

  content::BrowserContext* browser_context_;

  SuccessCallback extension_loaded_callback_;
  SuccessCallback init_callback_;

  TokensCallback tokens_callback_;
  SamplesCallback samples_callback_;
  SuccessCallback db_callback_;

  bool extension_loaded_;

  DISALLOW_COPY_AND_ASSIGN(ChromeWhispernetClient);
};

#endif  // CHROME_BROWSER_COPRESENCE_CHROME_WHISPERNET_CLIENT_H_
