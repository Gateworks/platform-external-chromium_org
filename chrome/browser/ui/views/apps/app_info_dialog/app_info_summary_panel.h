// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_APPS_APP_INFO_DIALOG_APP_INFO_SUMMARY_PANEL_H_
#define CHROME_BROWSER_UI_VIEWS_APPS_APP_INFO_DIALOG_APP_INFO_SUMMARY_PANEL_H_

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/apps/app_info_dialog/app_info_panel.h"
#include "chrome/common/extensions/extension_constants.h"
#include "ui/views/controls/combobox/combobox_listener.h"

class LaunchOptionsComboboxModel;
class Profile;

namespace extensions {
class Extension;
}

namespace views {
class Combobox;
class Label;
}

// The summary panel of the app info dialog, which provides basic information
// and controls related to the app.
class AppInfoSummaryPanel : public AppInfoPanel,
                            public views::ComboboxListener,
                            public base::SupportsWeakPtr<AppInfoSummaryPanel> {
 public:
  AppInfoSummaryPanel(Profile* profile, const extensions::Extension* app);

  virtual ~AppInfoSummaryPanel();

 private:
  // Internal initialisation methods.
  void AddDescriptionControl(views::View* vertical_stack);
  void AddDetailsControl(views::View* vertical_stack);
  void AddLaunchOptionControl(views::View* vertical_stack);
  void AddSubviews();

  // Overridden from views::ComboboxListener:
  virtual void OnPerformAction(views::Combobox* combobox) override;

  // Called asynchronously to calculate and update the size of the app displayed
  // in the dialog.
  void StartCalculatingAppSize();
  void OnAppSizeCalculated(int64 app_size_in_bytes);

  // Returns the launch type of the app (e.g. pinned tab, fullscreen, etc).
  extensions::LaunchType GetLaunchType() const;

  // Sets the launch type of the app to the given type. Must only be called if
  // CanSetLaunchType() returns true.
  void SetLaunchType(extensions::LaunchType) const;
  bool CanSetLaunchType() const;

  // UI elements on the dialog.
  views::Label* size_value_;

  scoped_ptr<LaunchOptionsComboboxModel> launch_options_combobox_model_;
  views::Combobox* launch_options_combobox_;

  base::WeakPtrFactory<AppInfoSummaryPanel> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(AppInfoSummaryPanel);
};

#endif  // CHROME_BROWSER_UI_VIEWS_APPS_APP_INFO_DIALOG_APP_INFO_SUMMARY_PANEL_H_
