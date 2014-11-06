// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PASSWORD_MANAGER_CONTENT_BROWSER_CONTENT_PASSWORD_MANAGER_DRIVER_H_
#define COMPONENTS_PASSWORD_MANAGER_CONTENT_BROWSER_CONTENT_PASSWORD_MANAGER_DRIVER_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "components/password_manager/core/browser/password_autofill_manager.h"
#include "components/password_manager/core/browser/password_generation_manager.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "components/password_manager/core/browser/password_manager_driver.h"
#include "content/public/browser/web_contents_observer.h"

namespace autofill {
class AutofillManager;
struct PasswordForm;
}

namespace content {
class WebContents;
}

namespace password_manager {

class ContentPasswordManagerDriver : public PasswordManagerDriver,
                                     public content::WebContentsObserver {
 public:
  ContentPasswordManagerDriver(content::WebContents* web_contents,
                               PasswordManagerClient* client,
                               autofill::AutofillClient* autofill_client);
  ~ContentPasswordManagerDriver() override;

  // PasswordManagerDriver implementation.
  void FillPasswordForm(
      const autofill::PasswordFormFillData& form_data) override;
  bool DidLastPageLoadEncounterSSLErrors() override;
  bool IsOffTheRecord() override;
  void AllowPasswordGenerationForForm(
      const autofill::PasswordForm& form) override;
  void AccountCreationFormsFound(
      const std::vector<autofill::FormData>& forms) override;
  void FillSuggestion(const base::string16& username,
                      const base::string16& password) override;
  void PreviewSuggestion(const base::string16& username,
                         const base::string16& password) override;
  void ClearPreviewedForm() override;

  PasswordGenerationManager* GetPasswordGenerationManager() override;
  PasswordManager* GetPasswordManager() override;
  autofill::AutofillManager* GetAutofillManager() override;
  PasswordAutofillManager* GetPasswordAutofillManager() override;

  // content::WebContentsObserver overrides.
  bool OnMessageReceived(const IPC::Message& message) override;
  void DidNavigateMainFrame(
      const content::LoadCommittedDetails& details,
      const content::FrameNavigateParams& params) override;

 private:
  PasswordManager password_manager_;
  PasswordGenerationManager password_generation_manager_;
  PasswordAutofillManager password_autofill_manager_;

  // Every instance of PasswordFormFillData created by |*this| and sent to
  // PasswordAutofillManager and PasswordAutofillAgent is given an ID, so that
  // the latter two classes can reference to the same instance without sending
  // it to each other over IPC. The counter below is used to generate new IDs.
  int next_free_key_;

  DISALLOW_COPY_AND_ASSIGN(ContentPasswordManagerDriver);
};

}  // namespace password_manager

#endif  // COMPONENTS_PASSWORD_MANAGER_CONTENT_BROWSER_CONTENT_PASSWORD_MANAGER_DRIVER_H_
