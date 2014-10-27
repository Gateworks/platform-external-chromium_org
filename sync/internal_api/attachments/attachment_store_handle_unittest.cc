// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/internal_api/public/attachments/attachment_store_handle.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/thread_task_runner_handle.h"
#include "sync/api/attachments/attachment.h"
#include "sync/api/attachments/attachment_id.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

class MockAttachmentStore : public AttachmentStoreBase {
 public:
  MockAttachmentStore(const base::Closure& read_called,
                      const base::Closure& write_called,
                      const base::Closure& drop_called,
                      const base::Closure& dtor_called)
      : read_called_(read_called),
        write_called_(write_called),
        drop_called_(drop_called),
        dtor_called_(dtor_called) {}

  ~MockAttachmentStore() override { dtor_called_.Run(); }

  void Read(const AttachmentIdList& ids,
            const ReadCallback& callback) override {
    read_called_.Run();
  }

  void Write(const AttachmentList& attachments,
             const WriteCallback& callback) override {
    write_called_.Run();
  }

  void Drop(const AttachmentIdList& ids,
            const DropCallback& callback) override {
    drop_called_.Run();
  }

  base::Closure read_called_;
  base::Closure write_called_;
  base::Closure drop_called_;
  base::Closure dtor_called_;
};

}  // namespace

class AttachmentStoreHandleTest : public testing::Test {
 protected:
  AttachmentStoreHandleTest()
      : read_call_count_(0),
        write_call_count_(0),
        drop_call_count_(0),
        dtor_call_count_(0) {}

  virtual void SetUp() {
    scoped_ptr<AttachmentStoreBase> backend(new MockAttachmentStore(
        base::Bind(&AttachmentStoreHandleTest::ReadCalled,
                   base::Unretained(this)),
        base::Bind(&AttachmentStoreHandleTest::WriteCalled,
                   base::Unretained(this)),
        base::Bind(&AttachmentStoreHandleTest::DropCalled,
                   base::Unretained(this)),
        base::Bind(&AttachmentStoreHandleTest::DtorCalled,
                   base::Unretained(this))));
    attachment_store_handle_ = new AttachmentStoreHandle(
        backend.Pass(), base::ThreadTaskRunnerHandle::Get());
  }

  static void DoneWithResult(const AttachmentStore::Result& result) {
    NOTREACHED();
  }

  static void ReadDone(const AttachmentStore::Result& result,
                       scoped_ptr<AttachmentMap> attachments,
                       scoped_ptr<AttachmentIdList> unavailable_attachments) {
    NOTREACHED();
  }

  void ReadCalled() { ++read_call_count_; }

  void WriteCalled() { ++write_call_count_; }

  void DropCalled() { ++drop_call_count_; }

  void DtorCalled() { ++dtor_call_count_; }

  void RunMessageLoop() {
    base::RunLoop run_loop;
    run_loop.RunUntilIdle();
  }

  base::MessageLoop message_loop_;
  scoped_refptr<AttachmentStoreHandle> attachment_store_handle_;
  int read_call_count_;
  int write_call_count_;
  int drop_call_count_;
  int dtor_call_count_;
};

// Test that method calls are forwarded to backend loop
TEST_F(AttachmentStoreHandleTest, MethodsCalled) {
  AttachmentIdList ids;
  AttachmentList attachments;

  attachment_store_handle_->Read(
      ids, base::Bind(&AttachmentStoreHandleTest::ReadDone));
  EXPECT_EQ(read_call_count_, 0);
  RunMessageLoop();
  EXPECT_EQ(read_call_count_, 1);

  attachment_store_handle_->Write(
      attachments, base::Bind(&AttachmentStoreHandleTest::DoneWithResult));
  EXPECT_EQ(write_call_count_, 0);
  RunMessageLoop();
  EXPECT_EQ(write_call_count_, 1);

  attachment_store_handle_->Drop(
      ids, base::Bind(&AttachmentStoreHandleTest::DoneWithResult));
  EXPECT_EQ(drop_call_count_, 0);
  RunMessageLoop();
  EXPECT_EQ(drop_call_count_, 1);

  // Releasing referehce to AttachmentStoreHandle should result in
  // MockAttachmentStore being deleted on backend loop.
  attachment_store_handle_ = NULL;
  EXPECT_EQ(dtor_call_count_, 0);
  RunMessageLoop();
  EXPECT_EQ(dtor_call_count_, 1);
}

}  // namespace syncer
