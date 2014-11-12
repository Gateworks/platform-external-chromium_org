// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/shell/browser/shell_browser_main_parts.h"

#include "base/command_line.h"
#include "base/run_loop.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/omaha_query_params/omaha_query_params.h"
#include "components/storage_monitor/storage_monitor.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/context_factory.h"
#include "content/public/browser/devtools_http_handler.h"
#include "content/public/common/result_codes.h"
#include "content/shell/browser/shell_devtools_manager_delegate.h"
#include "content/shell/browser/shell_net_log.h"
#include "extensions/browser/app_window/app_window_client.h"
#include "extensions/browser/browser_context_keyed_service_factories.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/updater/update_service.h"
#include "extensions/common/constants.h"
#include "extensions/common/switches.h"
#include "extensions/shell/browser/shell_browser_context.h"
#include "extensions/shell/browser/shell_browser_context_keyed_service_factories.h"
#include "extensions/shell/browser/shell_browser_main_delegate.h"
#include "extensions/shell/browser/shell_desktop_controller.h"
#include "extensions/shell/browser/shell_device_client.h"
#include "extensions/shell/browser/shell_extension_system.h"
#include "extensions/shell/browser/shell_extension_system_factory.h"
#include "extensions/shell/browser/shell_extensions_browser_client.h"
#include "extensions/shell/browser/shell_oauth2_token_service.h"
#include "extensions/shell/browser/shell_omaha_query_params_delegate.h"
#include "extensions/shell/common/shell_extensions_client.h"
#include "extensions/shell/common/switches.h"
#include "ui/aura/env.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/ime/input_method_initializer.h"
#include "ui/base/resource/resource_bundle.h"

#if defined(OS_CHROMEOS)
#include "chromeos/audio/cras_audio_handler.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/network/network_handler.h"
#include "extensions/shell/browser/shell_audio_controller_chromeos.h"
#include "extensions/shell/browser/shell_network_controller_chromeos.h"
#endif

#if !defined(DISABLE_NACL)
#include "components/nacl/browser/nacl_browser.h"
#include "components/nacl/browser/nacl_process_host.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/shell/browser/shell_nacl_browser_delegate.h"
#endif

using base::CommandLine;
using content::BrowserContext;

#if !defined(DISABLE_NACL)
using content::BrowserThread;
#endif

namespace extensions {

namespace {

void CrxInstallComplete(bool success) {
  VLOG(1) << "CRX download complete. Success: " << success;
}
}

ShellBrowserMainParts::ShellBrowserMainParts(
    const content::MainFunctionParams& parameters,
    ShellBrowserMainDelegate* browser_main_delegate)
    : devtools_http_handler_(nullptr),
      extension_system_(nullptr),
      parameters_(parameters),
      run_message_loop_(true),
      browser_main_delegate_(browser_main_delegate) {
}

ShellBrowserMainParts::~ShellBrowserMainParts() {
  if (devtools_http_handler_) {
    // Note that Stop destroys devtools_http_handler_.
    devtools_http_handler_->Stop();
  }
}

void ShellBrowserMainParts::PreMainMessageLoopStart() {
  // TODO(jamescook): Initialize touch here?
}

void ShellBrowserMainParts::PostMainMessageLoopStart() {
#if defined(OS_CHROMEOS)
  // Perform initialization of D-Bus objects here rather than in the below
  // helper classes so those classes' tests can initialize stub versions of the
  // D-Bus objects.
  chromeos::DBusThreadManager::Initialize();

  chromeos::NetworkHandler::Initialize();
  network_controller_.reset(new ShellNetworkController(
      CommandLine::ForCurrentProcess()->GetSwitchValueNative(
          switches::kAppShellPreferredNetwork)));

  chromeos::CrasAudioHandler::Initialize(
      new ShellAudioController::PrefHandler());
  audio_controller_.reset(new ShellAudioController());
#else
  // Non-Chrome OS platforms are for developer convenience, so use a test IME.
  ui::InitializeInputMethodForTesting();
#endif
}

void ShellBrowserMainParts::PreEarlyInitialization() {
}

int ShellBrowserMainParts::PreCreateThreads() {
  // TODO(jamescook): Initialize chromeos::CrosSettings here?

  content::ChildProcessSecurityPolicy::GetInstance()->RegisterWebSafeScheme(
      kExtensionScheme);
  content::ChildProcessSecurityPolicy::GetInstance()->RegisterWebSafeScheme(
      kExtensionResourceScheme);

  // Return no error.
  return 0;
}

void ShellBrowserMainParts::PreMainMessageLoopRun() {
  // Initialize our "profile" equivalent.
  browser_context_.reset(new ShellBrowserContext(net_log_.get()));

  aura::Env::GetInstance()->set_context_factory(content::GetContextFactory());

  storage_monitor::StorageMonitor::Create();

  desktop_controller_.reset(browser_main_delegate_->CreateDesktopController());

  // NOTE: Much of this is culled from chrome/test/base/chrome_test_suite.cc
  // TODO(jamescook): Initialize user_manager::UserManager.
  net_log_.reset(new content::ShellNetLog("app_shell"));

  device_client_.reset(new ShellDeviceClient);

  extensions_client_.reset(new ShellExtensionsClient());
  ExtensionsClient::Set(extensions_client_.get());

  extensions_browser_client_.reset(
      new ShellExtensionsBrowserClient(browser_context_.get()));
  ExtensionsBrowserClient::Set(extensions_browser_client_.get());

  omaha_query_params_delegate_.reset(new ShellOmahaQueryParamsDelegate);
  omaha_query_params::OmahaQueryParams::SetDelegate(
      omaha_query_params_delegate_.get());

  // Create our custom ExtensionSystem first because other
  // KeyedServices depend on it.
  // TODO(yoz): Move this after EnsureBrowserContextKeyedServiceFactoriesBuilt.
  CreateExtensionSystem();

  // Register additional KeyedService factories here. See
  // ChromeBrowserMainExtraPartsProfiles for details.
  EnsureBrowserContextKeyedServiceFactoriesBuilt();
  ShellExtensionSystemFactory::GetInstance();

  BrowserContextDependencyManager::GetInstance()->CreateBrowserContextServices(
      browser_context_.get());

  // Initialize OAuth2 support from command line.
  CommandLine* cmd = CommandLine::ForCurrentProcess();
  oauth2_token_service_.reset(new ShellOAuth2TokenService(
      browser_context_.get(),
      cmd->GetSwitchValueASCII(switches::kAppShellUser),
      cmd->GetSwitchValueASCII(switches::kAppShellRefreshToken)));

#if !defined(DISABLE_NACL)
  // Takes ownership.
  nacl::NaClBrowser::SetDelegate(
      new ShellNaClBrowserDelegate(browser_context_.get()));
  // Track the task so it can be canceled if app_shell shuts down very quickly,
  // such as in browser tests.
  task_tracker_.PostTask(
      BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO).get(),
      FROM_HERE,
      base::Bind(nacl::NaClProcessHost::EarlyStartup));
#endif

  // TODO(rockot): Remove this temporary hack test.
  std::string install_crx_id =
      cmd->GetSwitchValueASCII(switches::kAppShellInstallCrx);
  if (install_crx_id.size() != 0) {
    CHECK(install_crx_id.size() == 32)
        << "Extension ID must be exactly 32 characters long.";
    UpdateService* update_service = UpdateService::Get(browser_context_.get());
    update_service->DownloadAndInstall(install_crx_id,
                                       base::Bind(CrxInstallComplete));
  }

  // CreateHttpHandler retains ownership over DevToolsHttpHandler.
  devtools_http_handler_ =
      content::ShellDevToolsManagerDelegate::CreateHttpHandler(
          browser_context_.get());
  if (parameters_.ui_task) {
    // For running browser tests.
    parameters_.ui_task->Run();
    delete parameters_.ui_task;
    run_message_loop_ = false;
  } else {
    browser_main_delegate_->Start(browser_context_.get());
  }
}

bool ShellBrowserMainParts::MainMessageLoopRun(int* result_code) {
  if (!run_message_loop_)
    return true;
  // TODO(yoz): just return false here?
  base::RunLoop run_loop;
  run_loop.Run();
  *result_code = content::RESULT_CODE_NORMAL_EXIT;
  return true;
}

void ShellBrowserMainParts::PostMainMessageLoopRun() {
  browser_main_delegate_->Shutdown();

#if !defined(DISABLE_NACL)
  task_tracker_.TryCancelAll();
  nacl::NaClBrowser::SetDelegate(nullptr);
#endif

  oauth2_token_service_.reset();
  BrowserContextDependencyManager::GetInstance()->DestroyBrowserContextServices(
      browser_context_.get());
  extension_system_ = NULL;
  ExtensionsBrowserClient::Set(NULL);
  extensions_browser_client_.reset();
  browser_context_.reset();

  desktop_controller_.reset();

  storage_monitor::StorageMonitor::Destroy();
}

void ShellBrowserMainParts::PostDestroyThreads() {
#if defined(OS_CHROMEOS)
  audio_controller_.reset();
  chromeos::CrasAudioHandler::Shutdown();
  network_controller_.reset();
  chromeos::NetworkHandler::Shutdown();
  chromeos::DBusThreadManager::Shutdown();
#endif
}

void ShellBrowserMainParts::CreateExtensionSystem() {
  DCHECK(browser_context_);
  extension_system_ = static_cast<ShellExtensionSystem*>(
      ExtensionSystem::Get(browser_context_.get()));
  extension_system_->InitForRegularProfile(true);
}

}  // namespace extensions
