// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/local_discovery/privetv3_setup_flow.h"

#include "base/logging.h"
#include "chrome/browser/local_discovery/gcd_registration_ticket_request.h"

namespace local_discovery {

namespace {

const char kSsidJsonKeyName[] = "wifi.ssid";
const char kPasswordJsonKeyName[] = "wifi.passphrase";
const char kTicketJsonKeyName[] = "registration.ticketID";
const char kUserJsonKeyName[] = "registration.user";

class SetupRequest : public PrivetV3Session::Request {
 public:
  explicit SetupRequest(PrivetV3SetupFlow* setup_flow);
  ~SetupRequest() override;

  std::string GetName() override { return "/privet/v3/setup/start"; }
  const base::DictionaryValue& GetInput() override;

  void OnError() override;
  void OnParsedJson(const base::DictionaryValue& value,
                    bool has_error) override;

  void SetWiFiCridentials(const std::string& ssid, const std::string& password);

  void SetRegistrationTicket(const std::string& ticket_id,
                             const std::string& owner_email);

 private:
  base::DictionaryValue input_;
  PrivetV3SetupFlow* setup_flow_;
};

SetupRequest::SetupRequest(PrivetV3SetupFlow* setup_flow)
    : setup_flow_(setup_flow) {
}

SetupRequest::~SetupRequest() {
}

const base::DictionaryValue& SetupRequest::GetInput() {
  return input_;
}

void SetupRequest::OnError() {
  setup_flow_->OnSetupError();
}

void SetupRequest::OnParsedJson(const base::DictionaryValue& value,
                                bool has_error) {
  if (has_error)
    return setup_flow_->OnSetupError();
  setup_flow_->OnDeviceRegistered();
}

void SetupRequest::SetWiFiCridentials(const std::string& ssid,
                                      const std::string& password) {
  DCHECK(!ssid.empty());
  DCHECK(!password.empty());
  input_.SetString(kSsidJsonKeyName, ssid);
  input_.SetString(kPasswordJsonKeyName, password);
}

void SetupRequest::SetRegistrationTicket(const std::string& ticket_id,
                                         const std::string& owner_email) {
  DCHECK(!ticket_id.empty());
  DCHECK(!owner_email.empty());
  input_.SetString(kTicketJsonKeyName, ticket_id);
  input_.SetString(kUserJsonKeyName, owner_email);
}

}  // namespace

PrivetV3SetupFlow::Delegate::~Delegate() {
}

PrivetV3SetupFlow::PrivetV3SetupFlow(Delegate* delegate)
    : delegate_(delegate), weak_ptr_factory_(this) {
}

PrivetV3SetupFlow::~PrivetV3SetupFlow() {
}

void PrivetV3SetupFlow::Register(const std::string& service_name) {
  service_name_ = service_name;
  ticket_request_ = delegate_->CreateApiFlow();
  if (!ticket_request_) {
    OnSetupError();
    return;
  }
  scoped_ptr<GCDApiFlow::Request> ticket_request(
      new GCDRegistrationTicketRequest(
          base::Bind(&PrivetV3SetupFlow::OnTicketCreated,
                     weak_ptr_factory_.GetWeakPtr())));
  ticket_request_->Start(ticket_request.Pass());
}

#if defined(ENABLE_WIFI_BOOTSTRAPPING)
void PrivetV3SetupFlow::SetupWifiAndRegister(const std::string& device_ssid) {
  NOTIMPLEMENTED();
}
#endif  // ENABLE_WIFI_BOOTSTRAPPING

void PrivetV3SetupFlow::OnSetupConfirmationNeeded(
    const std::string& confirmation_code,
    extensions::api::gcd_private::ConfirmationType confirmation_type) {
  delegate_->ConfirmSecurityCode(confirmation_code,
                                 base::Bind(&PrivetV3SetupFlow::OnCodeConfirmed,
                                            weak_ptr_factory_.GetWeakPtr(),
                                            confirmation_code));
}

void PrivetV3SetupFlow::OnSessionStatus(
    extensions::api::gcd_private::Status status) {
  if (status == extensions::api::gcd_private::STATUS_SUCCESS) {
    DCHECK(setup_request_);
    session_->StartRequest(setup_request_.get());
  } else {
    OnSetupError();
  }
}

void PrivetV3SetupFlow::OnSetupError() {
  delegate_->OnSetupError();
}

void PrivetV3SetupFlow::OnDeviceRegistered() {
  delegate_->OnSetupDone();
}

void PrivetV3SetupFlow::OnTicketCreated(const std::string& ticket_id,
                                        const std::string& device_id) {
  if (ticket_id.empty() || device_id.empty()) {
    OnSetupError();
    return;
  }
  // TODO(vitalybuka): Implement success check by polling status of device_id_.
  device_id_ = device_id;
  SetupRequest* setup_request = new SetupRequest(this);
  setup_request_.reset(setup_request);
  setup_request->SetRegistrationTicket(ticket_id, "me");
  delegate_->CreatePrivetV3Client(
      service_name_,
      base::Bind(&PrivetV3SetupFlow::OnPrivetClientCreated,
                 weak_ptr_factory_.GetWeakPtr()));
}

void PrivetV3SetupFlow::OnPrivetClientCreated(
    scoped_ptr<PrivetHTTPClient> privet_http_client) {
  if (!privet_http_client) {
    OnSetupError();
    return;
  }
  session_.reset(new PrivetV3Session(privet_http_client.Pass(), this));
  session_->Start();
}

void PrivetV3SetupFlow::OnCodeConfirmed(const std::string& code, bool success) {
  if (!success)
    return OnSetupError();
  session_->ConfirmCode(code);
}

}  // namespace local_discovery
