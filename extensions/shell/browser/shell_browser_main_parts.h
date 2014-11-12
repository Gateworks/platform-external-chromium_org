// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_SHELL_BROWSER_SHELL_BROWSER_MAIN_PARTS_H_
#define EXTENSIONS_SHELL_BROWSER_SHELL_BROWSER_MAIN_PARTS_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "content/public/browser/browser_main_parts.h"
#include "content/public/common/main_function_params.h"
#include "ui/aura/window_tree_host_observer.h"

namespace content {
class DevToolsHttpHandler;
struct MainFunctionParams;
}

namespace views {
class Widget;
}

namespace net {
class NetLog;
}

namespace extensions {

class AppWindowClient;
class DesktopController;
class ShellBrowserContext;
class ShellBrowserMainDelegate;
class ShellDeviceClient;
class ShellExtensionsBrowserClient;
class ShellExtensionsClient;
class ShellExtensionSystem;
class ShellOAuth2TokenService;
class ShellOmahaQueryParamsDelegate;

#if defined(OS_CHROMEOS)
class ShellAudioController;
class ShellNetworkController;
#endif

// Handles initialization of AppShell.
class ShellBrowserMainParts : public content::BrowserMainParts {
 public:
  ShellBrowserMainParts(const content::MainFunctionParams& parameters,
                        ShellBrowserMainDelegate* browser_main_delegate);
  ~ShellBrowserMainParts() override;

  ShellBrowserContext* browser_context() { return browser_context_.get(); }

  ShellExtensionSystem* extension_system() { return extension_system_; }

  // BrowserMainParts overrides.
  void PreEarlyInitialization() override;
  void PreMainMessageLoopStart() override;
  void PostMainMessageLoopStart() override;
  int PreCreateThreads() override;
  void PreMainMessageLoopRun() override;
  bool MainMessageLoopRun(int* result_code) override;
  void PostMainMessageLoopRun() override;
  void PostDestroyThreads() override;

 private:
  // Creates and initializes the ExtensionSystem.
  void CreateExtensionSystem();

#if defined(OS_CHROMEOS)
  scoped_ptr<ShellNetworkController> network_controller_;
  scoped_ptr<ShellAudioController> audio_controller_;
#endif
  scoped_ptr<DesktopController> desktop_controller_;
  scoped_ptr<ShellBrowserContext> browser_context_;
  scoped_ptr<ShellDeviceClient> device_client_;
  scoped_ptr<AppWindowClient> app_window_client_;
  scoped_ptr<ShellExtensionsClient> extensions_client_;
  scoped_ptr<ShellExtensionsBrowserClient> extensions_browser_client_;
  scoped_ptr<net::NetLog> net_log_;
  content::DevToolsHttpHandler* devtools_http_handler_;
  scoped_ptr<ShellOmahaQueryParamsDelegate> omaha_query_params_delegate_;
  scoped_ptr<ShellOAuth2TokenService> oauth2_token_service_;

  // Owned by the KeyedService system.
  ShellExtensionSystem* extension_system_;

  // For running app browsertests.
  const content::MainFunctionParams parameters_;

  // If true, indicates the main message loop should be run
  // in MainMessageLoopRun. If false, it has already been run.
  bool run_message_loop_;

  scoped_ptr<ShellBrowserMainDelegate> browser_main_delegate_;

#if !defined(DISABLE_NACL)
  base::CancelableTaskTracker task_tracker_;
#endif

  DISALLOW_COPY_AND_ASSIGN(ShellBrowserMainParts);
};

}  // namespace extensions

#endif  // EXTENSIONS_SHELL_BROWSER_SHELL_BROWSER_MAIN_PARTS_H_
