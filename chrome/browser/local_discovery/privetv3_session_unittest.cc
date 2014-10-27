// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/local_discovery/privetv3_session.h"

#include "chrome/browser/local_discovery/privet_http.h"
#include "content/public/test/test_utils.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace local_discovery {

namespace {

using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::StrictMock;
using testing::_;

class MockDelegate : public PrivetV3Session::Delegate {
 public:
  MOCK_METHOD2(OnSetupConfirmationNeeded,
               void(const std::string&,
                    extensions::api::gcd_private::ConfirmationType));
  MOCK_METHOD1(OnSessionStatus, void(extensions::api::gcd_private::Status));
};

class PrivetV3SessionTest : public testing::Test {
 public:
  PrivetV3SessionTest()
      : session_(scoped_ptr<PrivetHTTPClient>(), &delegate_) {}

  virtual ~PrivetV3SessionTest() {}

  void QuitLoop() {
    base::MessageLoop::current()->PostTask(FROM_HERE, quit_closure_);
  }

  void ConfirmCode(const std::string& code,
                   extensions::api::gcd_private::ConfirmationType type) {
    session_.ConfirmCode(code);
  }

 protected:
  virtual void SetUp() override {
    quit_closure_ = run_loop_.QuitClosure();
    EXPECT_CALL(delegate_, OnSetupConfirmationNeeded(_, _)).Times(0);
    EXPECT_CALL(delegate_, OnSessionStatus(_)).Times(0);
  }

  StrictMock<MockDelegate> delegate_;
  PrivetV3Session session_;
  base::MessageLoop loop_;
  base::RunLoop run_loop_;
  base::Closure quit_closure_;
};

TEST_F(PrivetV3SessionTest, NotConfirmed) {
  EXPECT_CALL(delegate_, OnSetupConfirmationNeeded(_, _)).Times(1).WillOnce(
      InvokeWithoutArgs(this, &PrivetV3SessionTest::QuitLoop));
  session_.Start();
  run_loop_.Run();
}

TEST_F(PrivetV3SessionTest, Confirmed) {
  EXPECT_CALL(delegate_,
              OnSessionStatus(extensions::api::gcd_private::STATUS_SUCCESS))
      .Times(1)
      .WillOnce(InvokeWithoutArgs(this, &PrivetV3SessionTest::QuitLoop));
  EXPECT_CALL(delegate_, OnSetupConfirmationNeeded(_, _)).Times(1).WillOnce(
      Invoke(this, &PrivetV3SessionTest::ConfirmCode));
  session_.Start();
  run_loop_.Run();
}

}  // namespace

}  // namespace local_discovery
