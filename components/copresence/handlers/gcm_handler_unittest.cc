// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/copresence/handlers/gcm_handler.h"

#include "base/base64.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_util.h"
#include "components/copresence/proto/push_message.pb.h"
#include "components/copresence/test/fake_directive_handler.h"
#include "components/gcm_driver/fake_gcm_driver.h"
#include "components/gcm_driver/gcm_client.h"
#include "testing/gmock/include/gmock/gmock.h"

using gcm::GCMClient;

namespace {

// TODO(ckehoe): Move this to a central place.
std::string ToUrlSafe(std::string token) {
  base::ReplaceChars(token, "+", "-", &token);
  base::ReplaceChars(token, "/", "_", &token);
  return token;
}

}  // namespace


namespace copresence {

class GCMHandlerTest : public testing::Test {
 public:
  GCMHandlerTest()
    : driver_(new gcm::FakeGCMDriver),
      directive_handler_(new FakeDirectiveHandler),
      gcm_handler_(driver_.get(), directive_handler_.get()) {
  }

 protected:
  scoped_ptr<gcm::GCMDriver> driver_;
  scoped_ptr<FakeDirectiveHandler> directive_handler_;
  GCMHandler gcm_handler_;
};

TEST_F(GCMHandlerTest, OnMessage) {
  // Create a PushMessage.
  PushMessage push_message;
  push_message.set_type(PushMessage::REPORT);
  Report* report = push_message.mutable_report();
  report->add_directive()->set_subscription_id("subscription 1");
  report->add_directive()->set_subscription_id("subscription 2");

  // Encode it.
  std::string serialized_proto;
  std::string encoded_proto;
  push_message.SerializeToString(&serialized_proto);
  base::Base64Encode(serialized_proto, &encoded_proto);

  // Send it in a GCM message.
  GCMClient::IncomingMessage gcm_message;
  gcm_message.data[GCMHandler::kGcmMessageKey] = ToUrlSafe(encoded_proto);
  gcm_handler_.OnMessage(GCMHandler::kCopresenceAppId, gcm_message);

  // Check that the correct directives were passed along.
  EXPECT_THAT(directive_handler_->added_directives(),
              testing::ElementsAre("subscription 1", "subscription 2"));
}

}  // namespace copresence

