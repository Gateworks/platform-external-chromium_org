// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>
#include <shlwapi.h>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/debug/trace_event.h"
#include "base/environment.h"
#include "base/file_version_info.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string16.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/version.h"
#include "base/win/windows_version.h"
#include "chrome/app/chrome_crash_reporter_client.h"
#include "chrome/app/client_util.h"
#include "chrome/app/image_pre_reader_win.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_result_codes.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/env_vars.h"
#include "chrome/installer/util/google_update_constants.h"
#include "chrome/installer/util/google_update_settings.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/util_constants.h"
#include "components/crash/app/breakpad_win.h"
#include "components/crash/app/crash_reporter_client.h"
#include "components/metrics/client_info.h"
#include "content/public/app/startup_helper_win.h"
#include "sandbox/win/src/sandbox.h"

namespace {
// The entry point signature of chrome.dll.
typedef int (*DLL_MAIN)(HINSTANCE, sandbox::SandboxInterfaceInfo*);

typedef void (*RelaunchChromeBrowserWithNewCommandLineIfNeededFunc)();

base::LazyInstance<chrome::ChromeCrashReporterClient>::Leaky
    g_chrome_crash_client = LAZY_INSTANCE_INITIALIZER;

// Expects that |dir| has a trailing backslash. |dir| is modified so it
// contains the full path that was tried. Caller must check for the return
// value not being null to determine if this path contains a valid dll.
HMODULE LoadModuleWithDirectory(base::string16* dir,
                                const wchar_t* dll_name,
                                bool pre_read) {
  ::SetCurrentDirectoryW(dir->c_str());
  dir->append(dll_name);

  if (pre_read) {
    // We pre-read the binary to warm the memory caches (fewer hard faults to
    // page parts of the binary in).
    const size_t kStepSize = 1024 * 1024;
    size_t percent = 100;
    ImagePreReader::PartialPreReadImage(dir->c_str(), percent, kStepSize);
  }

  return ::LoadLibraryExW(dir->c_str(), NULL,
                          LOAD_WITH_ALTERED_SEARCH_PATH);
}

void RecordDidRun(const base::string16& dll_path) {
  bool system_level = !InstallUtil::IsPerUserInstall(dll_path.c_str());
  GoogleUpdateSettings::UpdateDidRunState(true, system_level);
}

void ClearDidRun(const base::string16& dll_path) {
  bool system_level = !InstallUtil::IsPerUserInstall(dll_path.c_str());
  GoogleUpdateSettings::UpdateDidRunState(false, system_level);
}

bool InMetroMode() {
  return (wcsstr(
      ::GetCommandLineW(), L" -ServerName:DefaultBrowserServer") != NULL);
}

typedef int (*InitMetro)();

}  // namespace

base::string16 GetExecutablePath() {
  wchar_t path[MAX_PATH];
  ::GetModuleFileNameW(NULL, path, MAX_PATH);
  if (!::PathRemoveFileSpecW(path))
    return base::string16();
  base::string16 exe_path(path);
  return exe_path.append(1, L'\\');
}

base::string16 GetCurrentModuleVersion() {
  scoped_ptr<FileVersionInfo> file_version_info(
      FileVersionInfo::CreateFileVersionInfoForCurrentModule());
  if (file_version_info.get()) {
    base::string16 version_string(file_version_info->file_version());
    if (Version(base::UTF16ToASCII(version_string)).IsValid())
      return version_string;
  }
  return base::string16();
}

//=============================================================================

MainDllLoader::MainDllLoader()
  : dll_(NULL), metro_mode_(InMetroMode()) {
}

MainDllLoader::~MainDllLoader() {
}

// Loading chrome is an interesting affair. First we try loading from the
// current directory to support run-what-you-compile and other development
// scenarios.
// If that fails then we look at the version resource in the current
// module. This is the expected path for chrome.exe browser instances in an
// installed build.
HMODULE MainDllLoader::Load(base::string16* version,
                            base::string16* out_file) {
  const base::string16 executable_dir(GetExecutablePath());
  *out_file = executable_dir;

  const wchar_t* dll_name = metro_mode_ ?
      installer::kChromeMetroDll :
#if !defined(CHROME_MULTIPLE_DLL)
      installer::kChromeDll;
#else
      (process_type_ == "service")  || process_type_.empty() ?
          installer::kChromeDll :
          installer::kChromeChildDll;
#endif
  const bool pre_read = !metro_mode_;
  HMODULE dll = LoadModuleWithDirectory(out_file, dll_name, pre_read);
  if (!dll) {
    base::string16 version_string(GetCurrentModuleVersion());
    if (version_string.empty()) {
      LOG(ERROR) << "No valid Chrome version found";
      return NULL;
    }
    *out_file = executable_dir;
    *version = version_string;
    out_file->append(version_string).append(1, L'\\');
    dll = LoadModuleWithDirectory(out_file, dll_name, pre_read);
    if (!dll) {
      PLOG(ERROR) << "Failed to load Chrome DLL from " << *out_file;
      return NULL;
    }
  }

  DCHECK(dll);
  return dll;
}

// Launching is a matter of loading the right dll, setting the CHROME_VERSION
// environment variable and just calling the entry point. Derived classes can
// add custom code in the OnBeforeLaunch callback.
int MainDllLoader::Launch(HINSTANCE instance) {
  const CommandLine& cmd_line = *CommandLine::ForCurrentProcess();
  process_type_ = cmd_line.GetSwitchValueASCII(switches::kProcessType);

  base::string16 version;
  base::string16 file;

  if (metro_mode_) {
    HMODULE metro_dll = Load(&version, &file);
    if (!metro_dll)
      return chrome::RESULT_CODE_MISSING_DATA;

    InitMetro chrome_metro_main =
        reinterpret_cast<InitMetro>(::GetProcAddress(metro_dll, "InitMetro"));
    return chrome_metro_main();
  }

  // Initialize the sandbox services.
  sandbox::SandboxInterfaceInfo sandbox_info = {0};
  content::InitializeSandboxInfo(&sandbox_info);

  crash_reporter::SetCrashReporterClient(g_chrome_crash_client.Pointer());
  bool exit_now = true;
  if (process_type_.empty()) {
    if (breakpad::ShowRestartDialogIfCrashed(&exit_now)) {
      // We restarted because of a previous crash. Ask user if we should
      // Relaunch. Only for the browser process. See crbug.com/132119.
      if (exit_now)
        return content::RESULT_CODE_NORMAL_EXIT;
    }
  }
  breakpad::InitCrashReporter(process_type_);

  dll_ = Load(&version, &file);
  if (!dll_)
    return chrome::RESULT_CODE_MISSING_DATA;

  scoped_ptr<base::Environment> env(base::Environment::Create());
  env->SetVar(chrome::kChromeVersionEnvVar, base::WideToUTF8(version));

  OnBeforeLaunch(file);
  DLL_MAIN chrome_main =
      reinterpret_cast<DLL_MAIN>(::GetProcAddress(dll_, "ChromeMain"));
  int rc = chrome_main(instance, &sandbox_info);
  return OnBeforeExit(rc, file);
}

void MainDllLoader::RelaunchChromeBrowserWithNewCommandLineIfNeeded() {
  if (!dll_)
    return;

  RelaunchChromeBrowserWithNewCommandLineIfNeededFunc relaunch_function =
      reinterpret_cast<RelaunchChromeBrowserWithNewCommandLineIfNeededFunc>(
          ::GetProcAddress(dll_,
                           "RelaunchChromeBrowserWithNewCommandLineIfNeeded"));
  if (!relaunch_function) {
    LOG(ERROR) << "Could not find exported function "
               << "RelaunchChromeBrowserWithNewCommandLineIfNeeded";
  } else {
    relaunch_function();
  }
}

//=============================================================================

class ChromeDllLoader : public MainDllLoader {
 protected:
  virtual void OnBeforeLaunch(const base::string16& dll_path) {
    RecordDidRun(dll_path);
  }

  virtual int OnBeforeExit(int return_code, const base::string16& dll_path) {
    // NORMAL_EXIT_CANCEL is used for experiments when the user cancels
    // so we need to reset the did_run signal so omaha does not count
    // this run as active usage.
    if (chrome::RESULT_CODE_NORMAL_EXIT_CANCEL == return_code) {
      ClearDidRun(dll_path);
    }
    return return_code;
  }
};

//=============================================================================

class ChromiumDllLoader : public MainDllLoader {
 protected:
  virtual void OnBeforeLaunch(const base::string16& dll_path) override {
  }
  virtual int OnBeforeExit(int return_code,
                           const base::string16& dll_path) override {
    return return_code;
  }
};

MainDllLoader* MakeMainDllLoader() {
#if defined(GOOGLE_CHROME_BUILD)
  return new ChromeDllLoader();
#else
  return new ChromiumDllLoader();
#endif
}
