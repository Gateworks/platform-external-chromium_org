// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/shell/browser/default_shell_browser_main_delegate.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_tokenizer.h"
#include "extensions/shell/browser/shell_desktop_controller.h"
#include "extensions/shell/browser/shell_extension_system.h"
#include "extensions/shell/common/switches.h"

namespace extensions {

DefaultShellBrowserMainDelegate::DefaultShellBrowserMainDelegate() {
}

DefaultShellBrowserMainDelegate::~DefaultShellBrowserMainDelegate() {
}

void DefaultShellBrowserMainDelegate::Start(
    content::BrowserContext* browser_context) {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kAppShellAppPath)) {
    ShellExtensionSystem* extension_system = static_cast<ShellExtensionSystem*>(
        ExtensionSystem::Get(browser_context));
    extension_system->Init();

    CommandLine::StringType path_list =
        command_line->GetSwitchValueNative(switches::kAppShellAppPath);

    base::StringTokenizerT<CommandLine::StringType,
                           CommandLine::StringType::const_iterator>
        tokenizer(path_list, FILE_PATH_LITERAL(","));
    while (tokenizer.GetNext()) {
      base::FilePath app_absolute_dir =
          base::MakeAbsoluteFilePath(base::FilePath(tokenizer.token()));

      const Extension* extension = extension_system->LoadApp(app_absolute_dir);
      if (!extension)
        continue;
      extension_system->LaunchApp(extension->id());
    }
  } else {
    LOG(ERROR) << "--" << switches::kAppShellAppPath
               << " unset; boredom is in your future";
  }
}

void DefaultShellBrowserMainDelegate::Shutdown() {
}

DesktopController* DefaultShellBrowserMainDelegate::CreateDesktopController() {
  return new ShellDesktopController();
}

}  // namespace extensions
