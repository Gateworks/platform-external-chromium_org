// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_GCM_DRIVER_FAKE_GCM_CLIENT_H_
#define COMPONENTS_GCM_DRIVER_FAKE_GCM_CLIENT_H_

#include "base/compiler_specific.h"
#include "base/memory/weak_ptr.h"
#include "components/gcm_driver/gcm_client.h"

namespace base {
class SequencedTaskRunner;
}

namespace gcm {

class FakeGCMClient : public GCMClient {
 public:
  enum Status {
    UNINITIALIZED,
    STARTED,
    STOPPED,
    CHECKED_OUT
  };

  enum StartMode {
    NO_DELAY_START,
    DELAY_START,
  };

  FakeGCMClient(StartMode start_mode,
                const scoped_refptr<base::SequencedTaskRunner>& ui_thread,
                const scoped_refptr<base::SequencedTaskRunner>& io_thread);
  ~FakeGCMClient() override;

  // Overridden from GCMClient:
  // Called on IO thread.
  void Initialize(
      const ChromeBuildInfo& chrome_build_info,
      const base::FilePath& store_path,
      const scoped_refptr<base::SequencedTaskRunner>& blocking_task_runner,
      const scoped_refptr<net::URLRequestContextGetter>&
          url_request_context_getter,
      scoped_ptr<Encryptor> encryptor,
      Delegate* delegate) override;
  void Start() override;
  void Stop() override;
  void CheckOut() override;
  void Register(const std::string& app_id,
                const std::vector<std::string>& sender_ids) override;
  void Unregister(const std::string& app_id) override;
  void Send(const std::string& app_id,
            const std::string& receiver_id,
            const OutgoingMessage& message) override;
  void SetRecording(bool recording) override;
  void ClearActivityLogs() override;
  GCMStatistics GetStatistics() const override;
  void SetAccountTokens(
      const std::vector<AccountTokenInfo>& account_tokens) override;
  void UpdateAccountMapping(const AccountMapping& account_mapping) override;
  void RemoveAccountMapping(const std::string& account_id) override;
  void SetLastTokenFetchTime(const base::Time& time) override;

  // Initiate the loading that has been delayed.
  // Called on UI thread.
  void PerformDelayedLoading();

  // Simulate receiving something from the server.
  // Called on UI thread.
  void ReceiveMessage(const std::string& app_id,
                      const IncomingMessage& message);
  void DeleteMessages(const std::string& app_id);

  std::string GetRegistrationIdFromSenderIds(
      const std::vector<std::string>& sender_ids) const;

  Status status() const { return status_; }

 private:
  // Called on IO thread.
  void DoLoading();
  void CheckinFinished();
  void RegisterFinished(const std::string& app_id,
                        const std::string& registrion_id);
  void UnregisterFinished(const std::string& app_id);
  void SendFinished(const std::string& app_id, const OutgoingMessage& message);
  void MessageReceived(const std::string& app_id,
                       const IncomingMessage& message);
  void MessagesDeleted(const std::string& app_id);
  void MessageSendError(const std::string& app_id,
                        const SendErrorDetails& send_error_details);
  void SendAcknowledgement(const std::string& app_id,
                           const std::string& message_id);

  Delegate* delegate_;
  // Increased at checkout in order to produce a different registration ID
  // after checkout and re-checkin.
  int sequence_id_;
  Status status_;
  StartMode start_mode_;
  scoped_refptr<base::SequencedTaskRunner> ui_thread_;
  scoped_refptr<base::SequencedTaskRunner> io_thread_;
  base::WeakPtrFactory<FakeGCMClient> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(FakeGCMClient);
};

}  // namespace gcm

#endif  // COMPONENTS_GCM_DRIVER_FAKE_GCM_CLIENT_H_
