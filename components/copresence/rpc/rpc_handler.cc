// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/copresence/rpc/rpc_handler.h"

#include <map>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/guid.h"
#include "base/logging.h"
#include "base/strings/string_util.h"

// TODO(ckehoe): time.h includes windows.h, which #defines DeviceCapabilities
// to DeviceCapabilitiesW. This breaks the pb.h headers below. For now,
// we fix this with an #undef.
#include "base/time/time.h"
#if defined(OS_WIN)
#undef DeviceCapabilities
#endif

#include "components/copresence/copresence_switches.h"
#include "components/copresence/handlers/directive_handler.h"
#include "components/copresence/proto/codes.pb.h"
#include "components/copresence/proto/data.pb.h"
#include "components/copresence/proto/rpcs.pb.h"
#include "components/copresence/public/copresence_constants.h"
#include "components/copresence/public/copresence_delegate.h"
#include "net/http/http_status_code.h"

// TODO(ckehoe): Return error messages for bad requests.

namespace copresence {

using google::protobuf::MessageLite;
using google::protobuf::RepeatedPtrField;

const char RpcHandler::kReportRequestRpcName[] = "report";

namespace {

// UrlSafe is defined as:
// '/' represented by a '_' and '+' represented by a '-'
// TODO(rkc): Move this to the wrapper.
std::string ToUrlSafe(std::string token) {
  base::ReplaceChars(token, "+", "-", &token);
  base::ReplaceChars(token, "/", "_", &token);
  return token;
}

const int kInvalidTokenExpiryTimeMs = 10 * 60 * 1000;  // 10 minutes.
const int kMaxInvalidTokens = 10000;
const char kRegisterDeviceRpcName[] = "registerdevice";
const char kDefaultCopresenceServer[] =
    "https://www.googleapis.com/copresence/v2/copresence";

// Logging

// Checks for a copresence error. If there is one, logs it and returns true.
bool CopresenceErrorLogged(const Status& status) {
  if (status.code() != OK) {
    LOG(ERROR) << "Copresence error code " << status.code()
               << (status.message().empty() ? std::string() :
                  ": " + status.message());
  }
  return status.code() != OK;
}

void LogIfErrorStatus(const util::error::Code& code,
                      const std::string& context) {
  LOG_IF(ERROR, code != util::error::OK)
      << context << " error " << code << ". See "
      << "cs/google3/util/task/codes.proto for more info.";
}

// If any errors occurred, logs them and returns true.
bool ReportErrorLogged(const ReportResponse& response) {
  bool result = CopresenceErrorLogged(response.header().status());

  // The Report fails or succeeds as a unit. If any responses had errors,
  // the header will too. Thus we don't need to propagate individual errors.
  if (response.has_update_signals_response())
    LogIfErrorStatus(response.update_signals_response().status(), "Update");
  if (response.has_manage_messages_response())
    LogIfErrorStatus(response.manage_messages_response().status(), "Publish");
  if (response.has_manage_subscriptions_response()) {
    LogIfErrorStatus(response.manage_subscriptions_response().status(),
                     "Subscribe");
  }

  return result;
}

// Request construction
// TODO(ckehoe): Move these into a separate file?

template <typename T>
BroadcastScanConfiguration GetBroadcastScanConfig(const T& msg) {
  if (msg.has_token_exchange_strategy() &&
      msg.token_exchange_strategy().has_broadcast_scan_configuration()) {
    return msg.token_exchange_strategy().broadcast_scan_configuration();
  }
  return BROADCAST_SCAN_CONFIGURATION_UNKNOWN;
}

scoped_ptr<DeviceState> GetDeviceCapabilities(const ReportRequest& request) {
  scoped_ptr<DeviceState> state(new DeviceState);

  TokenTechnology* ultrasound =
      state->mutable_capabilities()->add_token_technology();
  ultrasound->set_medium(AUDIO_ULTRASOUND_PASSBAND);
  ultrasound->add_instruction_type(TRANSMIT);
  ultrasound->add_instruction_type(RECEIVE);

  TokenTechnology* audible =
      state->mutable_capabilities()->add_token_technology();
  audible->set_medium(AUDIO_AUDIBLE_DTMF);
  audible->add_instruction_type(TRANSMIT);
  audible->add_instruction_type(RECEIVE);

  return state.Pass();
}

// TODO(ckehoe): We're keeping this code in a separate function for now
// because we get a version string from Chrome, but the proto expects
// an int64 version. We should probably change the version proto
// to handle a more detailed version.
ClientVersion* CreateVersion(const std::string& client,
                             const std::string& version_name) {
  ClientVersion* version = new ClientVersion;

  version->set_client(client);
  version->set_version_name(version_name);

  return version;
}

void AddTokenToRequest(ReportRequest* request, const AudioToken& token) {
  TokenObservation* token_observation =
      request->mutable_update_signals_request()->add_token_observation();
  token_observation->set_token_id(ToUrlSafe(token.token));

  TokenSignals* signals = token_observation->add_signals();
  signals->set_medium(token.audible ? AUDIO_AUDIBLE_DTMF
                                    : AUDIO_ULTRASOUND_PASSBAND);
  signals->set_observed_time_millis(base::Time::Now().ToJsTime());
}

}  // namespace

// Public methods

RpcHandler::RpcHandler(CopresenceDelegate* delegate)
    : delegate_(delegate),
      invalid_audio_token_cache_(
          base::TimeDelta::FromMilliseconds(kInvalidTokenExpiryTimeMs),
          kMaxInvalidTokens),
      server_post_callback_(base::Bind(&RpcHandler::SendHttpPost,
                                       base::Unretained(this))) {}

RpcHandler::~RpcHandler() {
  for (HttpPost* post : pending_posts_) {
    delete post;
  }

  if (delegate_ && delegate_->GetWhispernetClient()) {
    delegate_->GetWhispernetClient()->RegisterTokensCallback(
        WhispernetClient::TokensCallback());
    delegate_->GetWhispernetClient()->RegisterSamplesCallback(
        WhispernetClient::SamplesCallback());
  }
}

void RpcHandler::Initialize(const SuccessCallback& init_done_callback) {
  scoped_ptr<RegisterDeviceRequest> request(new RegisterDeviceRequest);
  DCHECK(device_id_.empty());

  request->mutable_push_service()->set_service(PUSH_SERVICE_NONE);
  Identity* identity =
      request->mutable_device_identifiers()->mutable_registrant();
  identity->set_type(CHROME);
  identity->set_chrome_id(base::GenerateGUID());
  SendServerRequest(
      kRegisterDeviceRpcName,
      std::string(),
      request.Pass(),
      base::Bind(&RpcHandler::RegisterResponseHandler,
                 // On destruction, this request will be cancelled.
                 base::Unretained(this),
                 init_done_callback));
}

void RpcHandler::SendReportRequest(scoped_ptr<ReportRequest> request) {
  SendReportRequest(request.Pass(), std::string(), StatusCallback());
}

void RpcHandler::SendReportRequest(scoped_ptr<ReportRequest> request,
                                   const std::string& app_id,
                                   const StatusCallback& status_callback) {
  DCHECK(request.get());
  DCHECK(!device_id_.empty())
      << "RpcHandler::Initialize() must complete successfully "
      << "before other RpcHandler methods are called.";

  DVLOG(3) << "Sending report request to server.";

  // If we are unpublishing or unsubscribing, we need to stop those publish or
  // subscribes right away, we don't need to wait for the server to tell us.
  ProcessRemovedOperations(*request);

  request->mutable_update_signals_request()->set_allocated_state(
      GetDeviceCapabilities(*request).release());

  AddPlayingTokens(request.get());

  SendServerRequest(kReportRequestRpcName,
                    app_id,
                    request.Pass(),
                    // On destruction, this request will be cancelled.
                    base::Bind(&RpcHandler::ReportResponseHandler,
                               base::Unretained(this),
                               status_callback));
}

void RpcHandler::ReportTokens(const std::vector<AudioToken>& tokens) {
  DCHECK(!tokens.empty());

  scoped_ptr<ReportRequest> request(new ReportRequest);
  for (const AudioToken& token : tokens) {
    if (invalid_audio_token_cache_.HasKey(ToUrlSafe(token.token)))
      continue;
    DVLOG(3) << "Sending token " << token.token << " to server.";
    AddTokenToRequest(request.get(), token);
  }
  SendReportRequest(request.Pass());
}

void RpcHandler::ConnectToWhispernet() {
  WhispernetClient* whispernet_client = delegate_->GetWhispernetClient();

  // |directive_handler_| will be destructed with us, so unretained is safe.
  directive_handler_.reset(new DirectiveHandler);
  directive_handler_->Initialize(
      base::Bind(&WhispernetClient::DecodeSamples,
                 base::Unretained(whispernet_client)),
      base::Bind(&RpcHandler::AudioDirectiveListToWhispernetConnector,
                 base::Unretained(this)));

  whispernet_client->RegisterTokensCallback(
      base::Bind(&RpcHandler::ReportTokens,
                 // On destruction, this callback will be disconnected.
                 base::Unretained(this)));
}

// Private methods

void RpcHandler::RegisterResponseHandler(
    const SuccessCallback& init_done_callback,
    HttpPost* completed_post,
    int http_status_code,
    const std::string& response_data) {
  if (completed_post) {
    int elements_erased = pending_posts_.erase(completed_post);
    DCHECK(elements_erased);
    delete completed_post;
  }

  if (http_status_code != net::HTTP_OK) {
    init_done_callback.Run(false);
    return;
  }

  RegisterDeviceResponse response;
  if (!response.ParseFromString(response_data)) {
    LOG(ERROR) << "Invalid RegisterDeviceResponse:\n" << response_data;
    init_done_callback.Run(false);
    return;
  }

  if (CopresenceErrorLogged(response.header().status()))
    return;
  device_id_ = response.registered_device_id();
  DCHECK(!device_id_.empty());
  DVLOG(2) << "Device registration successful: id " << device_id_;
  init_done_callback.Run(true);
}

void RpcHandler::ReportResponseHandler(const StatusCallback& status_callback,
                                       HttpPost* completed_post,
                                       int http_status_code,
                                       const std::string& response_data) {
  if (completed_post) {
    int elements_erased = pending_posts_.erase(completed_post);
    DCHECK(elements_erased);
    delete completed_post;
  }

  if (http_status_code != net::HTTP_OK) {
    if (!status_callback.is_null())
      status_callback.Run(FAIL);
    return;
  }

  DVLOG(3) << "Received ReportResponse.";
  ReportResponse response;
  if (!response.ParseFromString(response_data)) {
    LOG(ERROR) << "Invalid ReportResponse";
    if (!status_callback.is_null())
      status_callback.Run(FAIL);
    return;
  }

  if (ReportErrorLogged(response)) {
    if (!status_callback.is_null())
      status_callback.Run(FAIL);
    return;
  }

  for (const MessageResult& result :
      response.manage_messages_response().published_message_result()) {
    DVLOG(2) << "Published message with id " << result.published_message_id();
  }

  for (const SubscriptionResult& result :
      response.manage_subscriptions_response().subscription_result()) {
    DVLOG(2) << "Created subscription with id " << result.subscription_id();
  }

  if (response.has_update_signals_response()) {
    const UpdateSignalsResponse& update_response =
        response.update_signals_response();
    DispatchMessages(update_response.message());

    if (directive_handler_.get()) {
      for (const Directive& directive : update_response.directive())
        directive_handler_->AddDirective(directive);
    } else {
      DVLOG(1) << "No directive handler.";
    }

    for (const Token& token : update_response.token()) {
      switch (token.status()) {
        case VALID:
          // TODO(rkc/ckehoe): Store the token in a |valid_token_cache_| with a
          // short TTL (like 10s) and send it up with every report request.
          // Then we'll still get messages while we're waiting to hear it again.
          VLOG(1) << "Got valid token " << token.id();
          break;
        case INVALID:
          DVLOG(3) << "Discarding invalid token " << token.id();
          invalid_audio_token_cache_.Add(token.id(), true);
          break;
        default:
          DVLOG(2) << "Token " << token.id() << " has status code "
                   << token.status();
      }
    }
  }

  // TODO(ckehoe): Return a more detailed status response.
  if (!status_callback.is_null())
    status_callback.Run(SUCCESS);
}

void RpcHandler::ProcessRemovedOperations(const ReportRequest& request) {
  // Remove unpublishes.
  if (request.has_manage_messages_request()) {
    for (const std::string& unpublish :
        request.manage_messages_request().id_to_unpublish()) {
      directive_handler_->RemoveDirectives(unpublish);
    }
  }

  // Remove unsubscribes.
  if (request.has_manage_subscriptions_request()) {
    for (const std::string& unsubscribe :
        request.manage_subscriptions_request().id_to_unsubscribe()) {
      directive_handler_->RemoveDirectives(unsubscribe);
    }
  }
}

void RpcHandler::AddPlayingTokens(ReportRequest* request) {
  if (!directive_handler_)
    return;

  const std::string& audible_token =
      directive_handler_->GetCurrentAudioToken(AUDIBLE);
  const std::string& inaudible_token =
      directive_handler_->GetCurrentAudioToken(INAUDIBLE);

  if (!audible_token.empty())
    AddTokenToRequest(request, AudioToken(audible_token, true));
  if (!inaudible_token.empty())
    AddTokenToRequest(request, AudioToken(inaudible_token, false));
}

void RpcHandler::DispatchMessages(
    const RepeatedPtrField<SubscribedMessage>& messages) {
  if (messages.size() == 0)
    return;

  // Index the messages by subscription id.
  std::map<std::string, std::vector<Message>> messages_by_subscription;
  DVLOG(3) << "Dispatching " << messages.size() << " messages";
  for (const SubscribedMessage& message : messages) {
    for (const std::string& subscription_id : message.subscription_id()) {
      messages_by_subscription[subscription_id].push_back(
          message.published_message());
    }
  }

  // Send the messages for each subscription.
  for (const auto& map_entry : messages_by_subscription) {
    // TODO(ckehoe): Once we have the app ID from the server, we need to pass
    // it in here and get rid of the app id registry from the main API class.
    const std::string& subscription = map_entry.first;
    const std::vector<Message>& messages = map_entry.second;
    delegate_->HandleMessages("", subscription, messages);
  }
}

RequestHeader* RpcHandler::CreateRequestHeader(
    const std::string& client_name) const {
  RequestHeader* header = new RequestHeader;

  header->set_allocated_framework_version(CreateVersion(
      "Chrome", delegate_->GetPlatformVersionString()));
  if (!client_name.empty()) {
    header->set_allocated_client_version(
        CreateVersion(client_name, std::string()));
  }
  header->set_current_time_millis(base::Time::Now().ToJsTime());
  header->set_registered_device_id(device_id_);

  DeviceFingerprint* fingerprint = new DeviceFingerprint;
  fingerprint->set_platform_version(delegate_->GetPlatformVersionString());
  fingerprint->set_type(CHROME_PLATFORM_TYPE);
  header->set_allocated_device_fingerprint(fingerprint);

  return header;
}

template <class T>
void RpcHandler::SendServerRequest(
    const std::string& rpc_name,
    const std::string& app_id,
    scoped_ptr<T> request,
    const PostCleanupCallback& response_handler) {
  request->set_allocated_header(CreateRequestHeader(app_id));
  server_post_callback_.Run(delegate_->GetRequestContext(),
                            rpc_name,
                            make_scoped_ptr<MessageLite>(request.release()),
                            response_handler);
}

void RpcHandler::SendHttpPost(net::URLRequestContextGetter* url_context_getter,
                              const std::string& rpc_name,
                              scoped_ptr<MessageLite> request_proto,
                              const PostCleanupCallback& callback) {
  // Create the base URL to call.
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  const std::string copresence_server_host =
      command_line->HasSwitch(switches::kCopresenceServer) ?
      command_line->GetSwitchValueASCII(switches::kCopresenceServer) :
      kDefaultCopresenceServer;

  // Create the request and keep a pointer until it completes.
  HttpPost* http_post = new HttpPost(
      url_context_getter,
      copresence_server_host,
      rpc_name,
      command_line->GetSwitchValueASCII(switches::kCopresenceTracingToken),
      delegate_->GetAPIKey(),
      *request_proto);

  http_post->Start(base::Bind(callback, http_post));
  pending_posts_.insert(http_post);
}

void RpcHandler::AudioDirectiveListToWhispernetConnector(
    const std::string& token,
    AudioType type,
    const WhispernetClient::SamplesCallback& samples_callback) {
  DCHECK(type == AUDIBLE || type == INAUDIBLE);
  WhispernetClient* whispernet_client = delegate_->GetWhispernetClient();
  if (whispernet_client) {
    whispernet_client->RegisterSamplesCallback(samples_callback);
    whispernet_client->EncodeToken(token, type);
  }
}

}  // namespace copresence
