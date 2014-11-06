// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/app_list_service_views.h"

#include "chrome/browser/apps/scoped_keep_alive.h"
#include "chrome/browser/ui/app_list/app_list_controller_delegate.h"
#include "ui/app_list/views/app_list_view.h"

AppListServiceViews::AppListServiceViews(
    scoped_ptr<AppListControllerDelegate> controller_delegate)
    : shower_(this),
      can_dismiss_(true),
      controller_delegate_(controller_delegate.Pass()) {
}

AppListServiceViews::~AppListServiceViews() {}

void AppListServiceViews::OnViewBeingDestroyed() {
  can_dismiss_ = true;
  shower_.HandleViewBeingDestroyed();
}

void AppListServiceViews::Init(Profile* initial_profile) {
  PerformStartupChecks(initial_profile);
}

void AppListServiceViews::ShowForProfile(Profile* requested_profile) {
  DCHECK(requested_profile);

  ScopedKeepAlive keep_alive;

  CreateForProfile(requested_profile);
  shower_.ShowForCurrentProfile();
  RecordAppListLaunch();
}

void AppListServiceViews::DismissAppList() {
  if (!can_dismiss_)
    return;

  shower_.DismissAppList();
}

bool AppListServiceViews::IsAppListVisible() const {
  return shower_.IsAppListVisible();
}

gfx::NativeWindow AppListServiceViews::GetAppListWindow() {
  return shower_.GetWindow();
}

Profile* AppListServiceViews::GetCurrentAppListProfile() {
  return shower_.profile();
}

AppListControllerDelegate* AppListServiceViews::GetControllerDelegate() {
  return controller_delegate_.get();
}

void AppListServiceViews::CreateForProfile(Profile* requested_profile) {
  DCHECK(requested_profile);
  InvalidatePendingProfileLoads();
  shower_.CreateViewForProfile(requested_profile);
  SetProfilePath(shower_.profile()->GetPath());
}

void AppListServiceViews::DestroyAppList() {
  if (!shower_.HasView())
    return;

  // Use CloseNow(). This can't be asynchronous because the profile will be
  // deleted once this function returns.
  shower_.app_list()->GetWidget()->CloseNow();
  DCHECK(!shower_.HasView());
}

AppListViewDelegate* AppListServiceViews::GetViewDelegateForCreate() {
  return GetViewDelegate(shower_.profile());
}
