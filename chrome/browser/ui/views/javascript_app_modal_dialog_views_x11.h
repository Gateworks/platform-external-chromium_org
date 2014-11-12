// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_JAVASCRIPT_APP_MODAL_DIALOG_VIEWS_X11_H_
#define CHROME_BROWSER_UI_VIEWS_JAVASCRIPT_APP_MODAL_DIALOG_VIEWS_X11_H_

#include "base/memory/scoped_ptr.h"
#include "components/app_modal_dialogs/views/javascript_app_modal_dialog_views.h"

class JavascriptAppModalEventBlockerX11;

// JavaScriptAppModalDialog implmentation for linux desktop.
class JavaScriptAppModalDialogViewsX11 : public JavaScriptAppModalDialogViews {
 public:
  explicit JavaScriptAppModalDialogViewsX11(JavaScriptAppModalDialog* parent);
  virtual ~JavaScriptAppModalDialogViewsX11();

  // JavaScriptAppModalDialogViews:
  virtual void ShowAppModalDialog() override;

  // views::DialogDelegate:
  virtual void WindowClosing() override;

 private:
  // Blocks events to other browser windows while the dialog is open.
  scoped_ptr<JavascriptAppModalEventBlockerX11> event_blocker_x11_;

  DISALLOW_COPY_AND_ASSIGN(JavaScriptAppModalDialogViewsX11);
};

#endif  // CHROME_BROWSER_UI_VIEWS_JAVASCRIPT_APP_MODAL_DIALOG_VIEWS_X11_H_
