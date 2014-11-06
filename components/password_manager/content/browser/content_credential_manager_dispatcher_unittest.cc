// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/content/browser/content_credential_manager_dispatcher.h"

#include "base/command_line.h"
#include "base/run_loop.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "components/password_manager/content/browser/credential_manager_password_form_manager.h"
#include "components/password_manager/content/common/credential_manager_messages.h"
#include "components/password_manager/content/common/credential_manager_types.h"
#include "components/password_manager/core/browser/stub_password_manager_client.h"
#include "components/password_manager/core/browser/stub_password_manager_driver.h"
#include "components/password_manager/core/browser/test_password_store.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/test_renderer_host.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserContext;
using content::WebContents;

namespace {

// Chosen by fair dice roll. Guaranteed to be random.
const int kRequestId = 4;

class TestPasswordManagerClient
    : public password_manager::StubPasswordManagerClient {
 public:
  TestPasswordManagerClient(password_manager::PasswordStore* store)
      : did_prompt_user_to_save_(false), store_(store) {}
  ~TestPasswordManagerClient() override {}

  password_manager::PasswordStore* GetPasswordStore() override {
    return store_;
  }

  password_manager::PasswordManagerDriver* GetDriver() override {
    return &driver_;
  }

  bool PromptUserToSavePassword(
      scoped_ptr<password_manager::PasswordFormManager> manager) override {
    did_prompt_user_to_save_ = true;
    manager_.reset(manager.release());
    return true;
  }

  bool did_prompt_user_to_save() const { return did_prompt_user_to_save_; }

  password_manager::PasswordFormManager* pending_manager() const {
    return manager_.get();
  }

 private:
  bool did_prompt_user_to_save_;
  password_manager::PasswordStore* store_;
  password_manager::StubPasswordManagerDriver driver_;
  scoped_ptr<password_manager::PasswordFormManager> manager_;
};

void RunAllPendingTasks() {
  base::RunLoop run_loop;
  base::MessageLoop::current()->PostTask(
      FROM_HERE, base::MessageLoop::QuitWhenIdleClosure());
  run_loop.Run();
}

}  // namespace

namespace password_manager {

class ContentCredentialManagerDispatcherTest
    : public content::RenderViewHostTestHarness {
 public:
  ContentCredentialManagerDispatcherTest() {}

  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    store_ = new TestPasswordStore;
    client_.reset(new TestPasswordManagerClient(store_.get()));
    dispatcher_.reset(
        new ContentCredentialManagerDispatcher(web_contents(), client_.get()));

    NavigateAndCommit(GURL("https://example.com/test.html"));

    form_.username_value = base::ASCIIToUTF16("Username");
    form_.display_name = base::ASCIIToUTF16("Display Name");
    form_.password_value = base::ASCIIToUTF16("Password");
    form_.origin = web_contents()->GetLastCommittedURL().GetOrigin();
    form_.signon_realm = form_.origin.spec();
    form_.scheme = autofill::PasswordForm::SCHEME_HTML;

    store_->Clear();
    EXPECT_TRUE(store_->IsEmpty());
  }

  void TearDown() override {
    store_->Shutdown();
    content::RenderViewHostTestHarness::TearDown();
  }

  ContentCredentialManagerDispatcher* dispatcher() { return dispatcher_.get(); }

 protected:
  autofill::PasswordForm form_;
  scoped_refptr<TestPasswordStore> store_;
  scoped_ptr<ContentCredentialManagerDispatcher> dispatcher_;
  scoped_ptr<TestPasswordManagerClient> client_;
};

TEST_F(ContentCredentialManagerDispatcherTest,
       CredentialManagerOnNotifyFailedSignIn) {
  CredentialInfo info;
  info.type = CREDENTIAL_TYPE_LOCAL;
  dispatcher()->OnNotifyFailedSignIn(kRequestId, info);

  const uint32 kMsgID = CredentialManagerMsg_AcknowledgeFailedSignIn::ID;
  const IPC::Message* message =
      process()->sink().GetFirstMessageMatching(kMsgID);
  EXPECT_TRUE(message);
  process()->sink().ClearMessages();
}

TEST_F(ContentCredentialManagerDispatcherTest,
       CredentialManagerOnNotifySignedIn) {
  CredentialInfo info(form_);
  dispatcher()->OnNotifySignedIn(kRequestId, info);

  const uint32 kMsgID = CredentialManagerMsg_AcknowledgeSignedIn::ID;
  const IPC::Message* message =
      process()->sink().GetFirstMessageMatching(kMsgID);
  EXPECT_TRUE(message);
  process()->sink().ClearMessages();

  // Allow the PasswordFormManager to talk to the password store, determine
  // that the form is new, and set it as pending.
  RunAllPendingTasks();

  EXPECT_TRUE(client_->did_prompt_user_to_save());
  EXPECT_TRUE(client_->pending_manager()->HasCompletedMatching());

  autofill::PasswordForm new_form =
      client_->pending_manager()->pending_credentials();
  EXPECT_EQ(form_.username_value, new_form.username_value);
  EXPECT_EQ(form_.display_name, new_form.display_name);
  EXPECT_EQ(form_.password_value, new_form.password_value);
  EXPECT_EQ(form_.origin, new_form.origin);
  EXPECT_EQ(form_.signon_realm, new_form.signon_realm);
  EXPECT_EQ(autofill::PasswordForm::SCHEME_HTML, new_form.scheme);
}

TEST_F(ContentCredentialManagerDispatcherTest,
       CredentialManagerOnNotifySignedOut) {
  dispatcher()->OnNotifySignedOut(kRequestId);

  const uint32 kMsgID = CredentialManagerMsg_AcknowledgeSignedOut::ID;
  const IPC::Message* message =
      process()->sink().GetFirstMessageMatching(kMsgID);
  EXPECT_TRUE(message);
  process()->sink().ClearMessages();
}

TEST_F(ContentCredentialManagerDispatcherTest,
       CredentialManagerOnRequestCredentialWithEmptyPasswordStore) {
  std::vector<GURL> federations;
  dispatcher()->OnRequestCredential(kRequestId, false, federations);

  RunAllPendingTasks();

  const uint32 kMsgID = CredentialManagerMsg_SendCredential::ID;
  const IPC::Message* message =
      process()->sink().GetFirstMessageMatching(kMsgID);
  EXPECT_TRUE(message);
  CredentialManagerMsg_SendCredential::Param param;
  CredentialManagerMsg_SendCredential::Read(message, &param);
  EXPECT_EQ(CREDENTIAL_TYPE_EMPTY, param.b.type);
  process()->sink().ClearMessages();
}

TEST_F(ContentCredentialManagerDispatcherTest,
       CredentialManagerOnRequestCredentialWithFullPasswordStore) {
  store_->AddLogin(form_);

  std::vector<GURL> federations;
  dispatcher()->OnRequestCredential(kRequestId, false, federations);

  RunAllPendingTasks();

  const uint32 kMsgID = CredentialManagerMsg_SendCredential::ID;
  const IPC::Message* message =
      process()->sink().GetFirstMessageMatching(kMsgID);
  EXPECT_TRUE(message);
  process()->sink().ClearMessages();
}

TEST_F(ContentCredentialManagerDispatcherTest,
       CredentialManagerOnRequestCredentialWhileRequestPending) {
  store_->AddLogin(form_);

  std::vector<GURL> federations;
  dispatcher()->OnRequestCredential(kRequestId, false, federations);
  dispatcher()->OnRequestCredential(kRequestId, false, federations);

  // Check that the second request triggered a rejection.
  uint32 kMsgID = CredentialManagerMsg_RejectCredentialRequest::ID;
  const IPC::Message* message =
      process()->sink().GetFirstMessageMatching(kMsgID);
  EXPECT_TRUE(message);
  CredentialManagerMsg_RejectCredentialRequest::Param reject_param;
  CredentialManagerMsg_RejectCredentialRequest::Read(message, &reject_param);
  EXPECT_EQ(blink::WebCredentialManagerError::ErrorTypePendingRequest,
            reject_param.b);

  process()->sink().ClearMessages();

  // Execute the PasswordStore asynchronousness.
  RunAllPendingTasks();

  // Check that the first request resolves.
  kMsgID = CredentialManagerMsg_SendCredential::ID;
  message = process()->sink().GetFirstMessageMatching(kMsgID);
  EXPECT_TRUE(message);
  CredentialManagerMsg_SendCredential::Param send_param;
  CredentialManagerMsg_SendCredential::Read(message, &send_param);
  CredentialManagerMsg_SendCredential::Read(message, &send_param);
  EXPECT_NE(CREDENTIAL_TYPE_EMPTY, send_param.b.type);
  process()->sink().ClearMessages();
}

}  // namespace password_manager
