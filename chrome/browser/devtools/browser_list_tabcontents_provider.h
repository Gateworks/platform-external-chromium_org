// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DEVTOOLS_BROWSER_LIST_TABCONTENTS_PROVIDER_H_
#define CHROME_BROWSER_DEVTOOLS_BROWSER_LIST_TABCONTENTS_PROVIDER_H_

#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/ui/host_desktop.h"
#include "content/public/browser/devtools_http_handler_delegate.h"

class BrowserListTabContentsProvider
    : public content::DevToolsHttpHandlerDelegate {
 public:
  static void EnableTethering();

  explicit BrowserListTabContentsProvider(
      chrome::HostDesktopType host_desktop_type);
  ~BrowserListTabContentsProvider() override;

  // DevToolsHttpProtocolHandler::Delegate overrides.
  std::string GetDiscoveryPageHTML() override;
  bool BundlesFrontendResources() override;
  base::FilePath GetDebugFrontendDir() override;
  scoped_ptr<net::StreamListenSocket> CreateSocketForTethering(
      net::StreamListenSocket::Delegate* delegate,
      std::string* name) override;

 private:
  chrome::HostDesktopType host_desktop_type_;
  int last_tethering_port_;
  DISALLOW_COPY_AND_ASSIGN(BrowserListTabContentsProvider);
};

#endif  // CHROME_BROWSER_DEVTOOLS_BROWSER_LIST_TABCONTENTS_PROVIDER_H_
