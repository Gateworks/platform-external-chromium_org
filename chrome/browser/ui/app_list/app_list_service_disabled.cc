// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "base/memory/singleton.h"
#include "chrome/browser/ui/app_list/app_list_service.h"

namespace {

class AppListServiceDisabled : public AppListService {
 public:
  static AppListServiceDisabled* GetInstance() {
    return Singleton<AppListServiceDisabled,
                     LeakySingletonTraits<AppListServiceDisabled> >::get();
  }

 private:
  friend struct DefaultSingletonTraits<AppListServiceDisabled>;

  AppListServiceDisabled() {}

  // AppListService overrides:
  virtual void SetAppListNextPaintCallback(void (*callback)()) override {}
  virtual void HandleFirstRun() override {}
  virtual void Init(Profile* initial_profile) override {}

  virtual base::FilePath GetProfilePath(
      const base::FilePath& user_data_dir) override {
    return base::FilePath();
  }
  virtual void SetProfilePath(const base::FilePath& profile_path) override {}

  virtual void Show() override {}
  virtual void ShowForProfile(Profile* profile) override {}
  virtual void ShowForVoiceSearch(Profile* profile) override {}
  virtual void ShowForAppInstall(Profile* profile,
                                 const std::string& extension_id,
                                 bool start_discovery_tracking) override {}
  virtual void DismissAppList() override {}

  virtual Profile* GetCurrentAppListProfile() override { return NULL; }
  virtual bool IsAppListVisible() const override { return false; }
  virtual void EnableAppList(Profile* initial_profile,
                             AppListEnableSource enable_source) override {}
  virtual gfx::NativeWindow GetAppListWindow() override { return NULL; }
  virtual AppListControllerDelegate* GetControllerDelegate() override {
    return NULL;
  }
  virtual void CreateShortcut() override {}

  DISALLOW_COPY_AND_ASSIGN(AppListServiceDisabled);
};

}  // namespace

// static
AppListService* AppListService::Get(chrome::HostDesktopType desktop_type) {
  return AppListServiceDisabled::GetInstance();
}

// static
void AppListService::InitAll(Profile* initial_profile) {}

// static
void AppListService::RegisterPrefs(PrefRegistrySimple* registry) {}

// static
bool AppListService::HandleLaunchCommandLine(
    const base::CommandLine& command_line,
    Profile* launch_profile) {
  return false;
}
