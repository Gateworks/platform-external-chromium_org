// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/app_modal_dialogs/app_modal_dialog.h"

#include "base/logging.h"
#include "base/run_loop.h"
#include "components/app_modal_dialogs/app_modal_dialog_queue.h"
#include "components/app_modal_dialogs/native_app_modal_dialog.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"

using content::WebContents;

namespace {

AppModalDialogObserver* app_modal_dialog_observer = NULL;

}  // namespace

AppModalDialogObserver::AppModalDialogObserver() {
  DCHECK(!app_modal_dialog_observer);
  app_modal_dialog_observer = this;
}

AppModalDialogObserver::~AppModalDialogObserver() {
  DCHECK(app_modal_dialog_observer);
  app_modal_dialog_observer = NULL;
}

AppModalDialog::AppModalDialog(WebContents* web_contents,
                               const base::string16& title)
    : title_(title),
      completed_(false),
      valid_(true),
      native_dialog_(NULL),
      web_contents_(web_contents) {
}

AppModalDialog::~AppModalDialog() {
  CompleteDialog();
}

void AppModalDialog::ShowModalDialog() {
  web_contents_->GetDelegate()->ActivateContents(web_contents_);
  CreateAndShowDialog();
  if (app_modal_dialog_observer)
    app_modal_dialog_observer->Notify(this);
}

void AppModalDialog::CreateAndShowDialog() {
  native_dialog_ = CreateNativeDialog();
  native_dialog_->ShowAppModalDialog();
}

bool AppModalDialog::IsValid() {
  return valid_;
}

void AppModalDialog::Invalidate() {
  valid_ = false;
}

bool AppModalDialog::IsJavaScriptModalDialog() {
  return false;
}

void AppModalDialog::ActivateModalDialog() {
  DCHECK(native_dialog_);
  native_dialog_->ActivateAppModalDialog();
}

void AppModalDialog::CloseModalDialog() {
  DCHECK(native_dialog_);
  native_dialog_->CloseAppModalDialog();
}

void AppModalDialog::CompleteDialog() {
  if (!completed_) {
    completed_ = true;
    AppModalDialogQueue::GetInstance()->ShowNextDialog();
  }
}
