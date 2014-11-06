// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api/cast_channel/cast_socket.h"

#include <stdlib.h>
#include <string.h>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/format_macros.h"
#include "base/lazy_instance.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/sys_byteorder.h"
#include "extensions/browser/api/cast_channel/cast_auth_util.h"
#include "extensions/browser/api/cast_channel/cast_framer.h"
#include "extensions/browser/api/cast_channel/cast_message_util.h"
#include "extensions/browser/api/cast_channel/logger.h"
#include "extensions/browser/api/cast_channel/logger_util.h"
#include "extensions/common/api/cast_channel/cast_channel.pb.h"
#include "net/base/address_list.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_errors.h"
#include "net/base/net_util.h"
#include "net/cert/cert_verifier.h"
#include "net/cert/x509_certificate.h"
#include "net/http/transport_security_state.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_client_socket.h"
#include "net/ssl/ssl_config_service.h"
#include "net/ssl/ssl_info.h"

// Assumes |ip_endpoint_| of type net::IPEndPoint and |channel_auth_| of enum
// type ChannelAuthType are available in the current scope.
#define VLOG_WITH_CONNECTION(level) VLOG(level) << "[" << \
    ip_endpoint_.ToString() << ", auth=" << channel_auth_ << "] "

namespace {

// The default keepalive delay.  On Linux, keepalives probes will be sent after
// the socket is idle for this length of time, and the socket will be closed
// after 9 failed probes.  So the total idle time before close is 10 *
// kTcpKeepAliveDelaySecs.
const int kTcpKeepAliveDelaySecs = 10;
}  // namespace

namespace extensions {

static base::LazyInstance<BrowserContextKeyedAPIFactory<
    ApiResourceManager<core_api::cast_channel::CastSocket> > > g_factory =
    LAZY_INSTANCE_INITIALIZER;

// static
template <>
BrowserContextKeyedAPIFactory<
    ApiResourceManager<core_api::cast_channel::CastSocket> >*
ApiResourceManager<core_api::cast_channel::CastSocket>::GetFactoryInstance() {
  return g_factory.Pointer();
}

namespace core_api {
namespace cast_channel {
CastSocket::CastSocket(const std::string& owner_extension_id,
                       const net::IPEndPoint& ip_endpoint,
                       ChannelAuthType channel_auth,
                       CastSocket::Delegate* delegate,
                       net::NetLog* net_log,
                       const base::TimeDelta& timeout,
                       const scoped_refptr<Logger>& logger)
    : ApiResource(owner_extension_id),
      channel_id_(0),
      ip_endpoint_(ip_endpoint),
      channel_auth_(channel_auth),
      delegate_(delegate),
      net_log_(net_log),
      logger_(logger),
      connect_timeout_(timeout),
      connect_timeout_timer_(new base::OneShotTimer<CastSocket>),
      is_canceled_(false),
      connect_state_(proto::CONN_STATE_NONE),
      write_state_(proto::WRITE_STATE_NONE),
      read_state_(proto::READ_STATE_NONE),
      error_state_(CHANNEL_ERROR_NONE),
      ready_state_(READY_STATE_NONE) {
  DCHECK(net_log_);
  DCHECK(channel_auth_ == CHANNEL_AUTH_TYPE_SSL ||
         channel_auth_ == CHANNEL_AUTH_TYPE_SSL_VERIFIED);
  net_log_source_.type = net::NetLog::SOURCE_SOCKET;
  net_log_source_.id = net_log_->NextID();

  // Buffer is reused across messages.
  read_buffer_ = new net::GrowableIOBuffer();
  read_buffer_->SetCapacity(MessageFramer::MessageHeader::max_message_size());
  framer_.reset(new MessageFramer(read_buffer_));
}

CastSocket::~CastSocket() {
  // Ensure that resources are freed but do not run pending callbacks to avoid
  // any re-entrancy.
  CloseInternal();
}

ReadyState CastSocket::ready_state() const {
  return ready_state_;
}

ChannelError CastSocket::error_state() const {
  return error_state_;
}

scoped_ptr<net::TCPClientSocket> CastSocket::CreateTcpSocket() {
  net::AddressList addresses(ip_endpoint_);
  return scoped_ptr<net::TCPClientSocket>(
      new net::TCPClientSocket(addresses, net_log_, net_log_source_));
  // Options cannot be set on the TCPClientSocket yet, because the
  // underlying platform socket will not be created until Bind()
  // or Connect() is called.
}

scoped_ptr<net::SSLClientSocket> CastSocket::CreateSslSocket(
    scoped_ptr<net::StreamSocket> socket) {
  net::SSLConfig ssl_config;
  // If a peer cert was extracted in a previous attempt to connect, then
  // whitelist that cert.
  if (!peer_cert_.empty()) {
    net::SSLConfig::CertAndStatus cert_and_status;
    cert_and_status.cert_status = net::CERT_STATUS_AUTHORITY_INVALID;
    cert_and_status.der_cert = peer_cert_;
    ssl_config.allowed_bad_certs.push_back(cert_and_status);
    logger_->LogSocketEvent(channel_id_, proto::SSL_CERT_WHITELISTED);
  }

  cert_verifier_.reset(net::CertVerifier::CreateDefault());
  transport_security_state_.reset(new net::TransportSecurityState);
  net::SSLClientSocketContext context;
  // CertVerifier and TransportSecurityState are owned by us, not the
  // context object.
  context.cert_verifier = cert_verifier_.get();
  context.transport_security_state = transport_security_state_.get();

  scoped_ptr<net::ClientSocketHandle> connection(new net::ClientSocketHandle);
  connection->SetSocket(socket.Pass());
  net::HostPortPair host_and_port = net::HostPortPair::FromIPEndPoint(
      ip_endpoint_);

  return net::ClientSocketFactory::GetDefaultFactory()->CreateSSLClientSocket(
      connection.Pass(), host_and_port, ssl_config, context);
}

bool CastSocket::ExtractPeerCert(std::string* cert) {
  DCHECK(cert);
  DCHECK(peer_cert_.empty());
  net::SSLInfo ssl_info;
  if (!socket_->GetSSLInfo(&ssl_info) || !ssl_info.cert.get()) {
    return false;
  }

  logger_->LogSocketEvent(channel_id_, proto::SSL_INFO_OBTAINED);

  bool result = net::X509Certificate::GetDEREncoded(
     ssl_info.cert->os_cert_handle(), cert);
  if (result) {
    VLOG_WITH_CONNECTION(1) << "Successfully extracted peer certificate: "
                            << *cert;
  }

  logger_->LogSocketEventWithRv(
      channel_id_, proto::DER_ENCODED_CERT_OBTAIN, result ? 1 : 0);
  return result;
}

bool CastSocket::VerifyChallengeReply() {
  AuthResult result = AuthenticateChallengeReply(*challenge_reply_, peer_cert_);
  if (result.success()) {
    VLOG(1) << result.error_message;
  }
  logger_->LogSocketChallengeReplyEvent(channel_id_, result);
  return result.success();
}

void CastSocket::Connect(const net::CompletionCallback& callback) {
  DCHECK(CalledOnValidThread());
  VLOG_WITH_CONNECTION(1) << "Connect readyState = " << ready_state_;
  if (ready_state_ != READY_STATE_NONE) {
    logger_->LogSocketEventWithDetails(
        channel_id_, proto::CONNECT_FAILED, "ReadyState not NONE");
    callback.Run(net::ERR_CONNECTION_FAILED);
    return;
  }

  connect_callback_ = callback;
  SetReadyState(READY_STATE_CONNECTING);
  SetConnectState(proto::CONN_STATE_TCP_CONNECT);

  if (connect_timeout_.InMicroseconds() > 0) {
    DCHECK(connect_timeout_callback_.IsCancelled());
    connect_timeout_callback_.Reset(
        base::Bind(&CastSocket::OnConnectTimeout, base::Unretained(this)));
    GetTimer()->Start(FROM_HERE,
                      connect_timeout_,
                      connect_timeout_callback_.callback());
  }
  DoConnectLoop(net::OK);
}

void CastSocket::PostTaskToStartConnectLoop(int result) {
  DCHECK(CalledOnValidThread());
  DCHECK(connect_loop_callback_.IsCancelled());
  connect_loop_callback_.Reset(
      base::Bind(&CastSocket::DoConnectLoop, base::Unretained(this), result));
  base::MessageLoop::current()->PostTask(FROM_HERE,
                                         connect_loop_callback_.callback());
}

void CastSocket::OnConnectTimeout() {
  DCHECK(CalledOnValidThread());
  // Stop all pending connection setup tasks and report back to the client.
  is_canceled_ = true;
  logger_->LogSocketEvent(channel_id_, proto::CONNECT_TIMED_OUT);
  VLOG_WITH_CONNECTION(1) << "Timeout while establishing a connection.";
  DoConnectCallback(net::ERR_TIMED_OUT);
}

// This method performs the state machine transitions for connection flow.
// There are two entry points to this method:
// 1. Connect method: this starts the flow
// 2. Callback from network operations that finish asynchronously
void CastSocket::DoConnectLoop(int result) {
  connect_loop_callback_.Cancel();
  if (is_canceled_) {
    LOG(ERROR) << "CANCELLED - Aborting DoConnectLoop.";
    return;
  }
  // Network operations can either finish synchronously or asynchronously.
  // This method executes the state machine transitions in a loop so that
  // correct state transitions happen even when network operations finish
  // synchronously.
  int rv = result;
  do {
    proto::ConnectionState state = connect_state_;
    // Default to CONN_STATE_NONE, which breaks the processing loop if any
    // handler fails to transition to another state to continue processing.
    connect_state_ = proto::CONN_STATE_NONE;
    switch (state) {
      case proto::CONN_STATE_TCP_CONNECT:
        rv = DoTcpConnect();
        break;
      case proto::CONN_STATE_TCP_CONNECT_COMPLETE:
        rv = DoTcpConnectComplete(rv);
        break;
      case proto::CONN_STATE_SSL_CONNECT:
        DCHECK_EQ(net::OK, rv);
        rv = DoSslConnect();
        break;
      case proto::CONN_STATE_SSL_CONNECT_COMPLETE:
        rv = DoSslConnectComplete(rv);
        break;
      case proto::CONN_STATE_AUTH_CHALLENGE_SEND:
        rv = DoAuthChallengeSend();
        break;
      case proto::CONN_STATE_AUTH_CHALLENGE_SEND_COMPLETE:
        rv = DoAuthChallengeSendComplete(rv);
        break;
      case proto::CONN_STATE_AUTH_CHALLENGE_REPLY_COMPLETE:
        rv = DoAuthChallengeReplyComplete(rv);
        break;
      default:
        NOTREACHED() << "BUG in connect flow. Unknown state: " << state;
        break;
    }
  } while (rv != net::ERR_IO_PENDING &&
           connect_state_ != proto::CONN_STATE_NONE);
  // Get out of the loop either when: // a. A network operation is pending, OR
  // b. The Do* method called did not change state

  // No state change occurred in do-while loop above. This means state has
  // transitioned to NONE.
  if (connect_state_ == proto::CONN_STATE_NONE) {
    logger_->LogSocketConnectState(channel_id_, connect_state_);
  }

  // Connect loop is finished: if there is no pending IO invoke the callback.
  if (rv != net::ERR_IO_PENDING) {
    GetTimer()->Stop();
    DoConnectCallback(rv);
  }
}

int CastSocket::DoTcpConnect() {
  DCHECK(connect_loop_callback_.IsCancelled());
  VLOG_WITH_CONNECTION(1) << "DoTcpConnect";
  SetConnectState(proto::CONN_STATE_TCP_CONNECT_COMPLETE);
  tcp_socket_ = CreateTcpSocket();

  int rv = tcp_socket_->Connect(
      base::Bind(&CastSocket::DoConnectLoop, base::Unretained(this)));
  logger_->LogSocketEventWithRv(channel_id_, proto::TCP_SOCKET_CONNECT, rv);
  return rv;
}

int CastSocket::DoTcpConnectComplete(int result) {
  VLOG_WITH_CONNECTION(1) << "DoTcpConnectComplete: " << result;
  if (result == net::OK) {
    // Enable TCP protocol-level keep-alive.
    bool result = tcp_socket_->SetKeepAlive(true, kTcpKeepAliveDelaySecs);
    LOG_IF(WARNING, !result) << "Failed to SetKeepAlive.";
    logger_->LogSocketEventWithRv(
        channel_id_, proto::TCP_SOCKET_SET_KEEP_ALIVE, result ? 1 : 0);
    SetConnectState(proto::CONN_STATE_SSL_CONNECT);
  }
  return result;
}

int CastSocket::DoSslConnect() {
  DCHECK(connect_loop_callback_.IsCancelled());
  VLOG_WITH_CONNECTION(1) << "DoSslConnect";
  SetConnectState(proto::CONN_STATE_SSL_CONNECT_COMPLETE);
  socket_ = CreateSslSocket(tcp_socket_.Pass());

  int rv = socket_->Connect(
      base::Bind(&CastSocket::DoConnectLoop, base::Unretained(this)));
  logger_->LogSocketEventWithRv(channel_id_, proto::SSL_SOCKET_CONNECT, rv);
  return rv;
}

int CastSocket::DoSslConnectComplete(int result) {
  VLOG_WITH_CONNECTION(1) << "DoSslConnectComplete: " << result;
  if (result == net::ERR_CERT_AUTHORITY_INVALID &&
      peer_cert_.empty() && ExtractPeerCert(&peer_cert_)) {
    SetConnectState(proto::CONN_STATE_TCP_CONNECT);
  } else if (result == net::OK &&
             channel_auth_ == CHANNEL_AUTH_TYPE_SSL_VERIFIED) {
    SetConnectState(proto::CONN_STATE_AUTH_CHALLENGE_SEND);
  }
  return result;
}

int CastSocket::DoAuthChallengeSend() {
  VLOG_WITH_CONNECTION(1) << "DoAuthChallengeSend";
  SetConnectState(proto::CONN_STATE_AUTH_CHALLENGE_SEND_COMPLETE);

  CastMessage challenge_message;
  CreateAuthChallengeMessage(&challenge_message);
  VLOG_WITH_CONNECTION(1) << "Sending challenge: "
                          << CastMessageToString(challenge_message);
  // Post a task to send auth challenge so that DoWriteLoop is not nested inside
  // DoConnectLoop. This is not strictly necessary but keeps the write loop
  // code decoupled from connect loop code.
  DCHECK(send_auth_challenge_callback_.IsCancelled());
  send_auth_challenge_callback_.Reset(
      base::Bind(&CastSocket::SendCastMessageInternal,
                 base::Unretained(this),
                 challenge_message,
                 base::Bind(&CastSocket::DoAuthChallengeSendWriteComplete,
                            base::Unretained(this))));
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      send_auth_challenge_callback_.callback());
  // Always return IO_PENDING since the result is always asynchronous.
  return net::ERR_IO_PENDING;
}

void CastSocket::DoAuthChallengeSendWriteComplete(int result) {
  send_auth_challenge_callback_.Cancel();
  VLOG_WITH_CONNECTION(2) << "DoAuthChallengeSendWriteComplete: " << result;
  DCHECK_GT(result, 0);
  DCHECK_EQ(write_queue_.size(), 1UL);
  PostTaskToStartConnectLoop(result);
}

int CastSocket::DoAuthChallengeSendComplete(int result) {
  VLOG_WITH_CONNECTION(1) << "DoAuthChallengeSendComplete: " << result;
  if (result < 0) {
    return result;
  }
  SetConnectState(proto::CONN_STATE_AUTH_CHALLENGE_REPLY_COMPLETE);

  // Post a task to start read loop so that DoReadLoop is not nested inside
  // DoConnectLoop. This is not strictly necessary but keeps the read loop
  // code decoupled from connect loop code.
  PostTaskToStartReadLoop();
  // Always return IO_PENDING since the result is always asynchronous.
  return net::ERR_IO_PENDING;
}

int CastSocket::DoAuthChallengeReplyComplete(int result) {
  VLOG_WITH_CONNECTION(1) << "DoAuthChallengeReplyComplete: " << result;
  if (result < 0) {
    return result;
  }
  if (!VerifyChallengeReply()) {
    return net::ERR_FAILED;
  }
  VLOG_WITH_CONNECTION(1) << "Auth challenge verification succeeded";
  return net::OK;
}

void CastSocket::DoConnectCallback(int result) {
  SetReadyState((result == net::OK) ? READY_STATE_OPEN : READY_STATE_CLOSED);
  if (result == net::OK) {
    SetErrorState(CHANNEL_ERROR_NONE);
    PostTaskToStartReadLoop();
    VLOG_WITH_CONNECTION(1) << "Calling Connect_Callback";
    base::ResetAndReturn(&connect_callback_).Run(result);
    return;
  } else if (result == net::ERR_TIMED_OUT) {
    SetErrorState(CHANNEL_ERROR_CONNECT_TIMEOUT);
  } else {
    SetErrorState(CHANNEL_ERROR_CONNECT_ERROR);
  }
  // Calls the connect callback.
  CloseWithError();
}

void CastSocket::Close(const net::CompletionCallback& callback) {
  CloseInternal();
  RunPendingCallbacksOnClose();
  // Run this callback last.  It may delete the socket.
  callback.Run(net::OK);
}

void CastSocket::CloseInternal() {
  // TODO(mfoltz): Enforce this when CastChannelAPITest is rewritten to create
  // and free sockets on the same thread.  crbug.com/398242
  // DCHECK(CalledOnValidThread());
  if (ready_state_ == READY_STATE_CLOSED) {
    return;
  }

  VLOG_WITH_CONNECTION(1) << "Close ReadyState = " << ready_state_;
  tcp_socket_.reset();
  socket_.reset();
  cert_verifier_.reset();
  transport_security_state_.reset();
  GetTimer()->Stop();

  // Cancel callbacks that we queued ourselves to re-enter the connect or read
  // loops.
  connect_loop_callback_.Cancel();
  send_auth_challenge_callback_.Cancel();
  read_loop_callback_.Cancel();
  connect_timeout_callback_.Cancel();
  SetReadyState(READY_STATE_CLOSED);
  logger_->LogSocketEvent(channel_id_, proto::SOCKET_CLOSED);
}

void CastSocket::RunPendingCallbacksOnClose() {
  DCHECK_EQ(ready_state_, READY_STATE_CLOSED);
  if (!connect_callback_.is_null()) {
    connect_callback_.Run(net::ERR_CONNECTION_FAILED);
    connect_callback_.Reset();
  }
  for (; !write_queue_.empty(); write_queue_.pop()) {
    net::CompletionCallback& callback = write_queue_.front().callback;
    callback.Run(net::ERR_FAILED);
    callback.Reset();
  }
}

void CastSocket::SendMessage(const MessageInfo& message,
                             const net::CompletionCallback& callback) {
  DCHECK(CalledOnValidThread());
  if (ready_state_ != READY_STATE_OPEN) {
    logger_->LogSocketEventForMessage(channel_id_,
                                      proto::SEND_MESSAGE_FAILED,
                                      message.namespace_,
                                      "Ready state not OPEN");
    callback.Run(net::ERR_FAILED);
    return;
  }
  CastMessage message_proto;
  if (!MessageInfoToCastMessage(message, &message_proto)) {
    logger_->LogSocketEventForMessage(channel_id_,
                                      proto::SEND_MESSAGE_FAILED,
                                      message.namespace_,
                                      "Failed to convert to CastMessage");
    callback.Run(net::ERR_FAILED);
    return;
  }
  SendCastMessageInternal(message_proto, callback);
}

void CastSocket::SendCastMessageInternal(
    const CastMessage& message,
    const net::CompletionCallback& callback) {
  WriteRequest write_request(callback);
  if (!write_request.SetContent(message)) {
    logger_->LogSocketEventForMessage(channel_id_,
                                      proto::SEND_MESSAGE_FAILED,
                                      message.namespace_(),
                                      "SetContent failed");
    callback.Run(net::ERR_FAILED);
    return;
  }

  write_queue_.push(write_request);
  logger_->LogSocketEventForMessage(
      channel_id_,
      proto::MESSAGE_ENQUEUED,
      message.namespace_(),
      base::StringPrintf("Queue size: %" PRIuS, write_queue_.size()));
  if (write_state_ == proto::WRITE_STATE_NONE) {
    SetWriteState(proto::WRITE_STATE_WRITE);
    DoWriteLoop(net::OK);
  }
}

void CastSocket::DoWriteLoop(int result) {
  DCHECK(CalledOnValidThread());
  VLOG_WITH_CONNECTION(1) << "DoWriteLoop queue size: " << write_queue_.size();

  if (write_queue_.empty()) {
    SetWriteState(proto::WRITE_STATE_NONE);
    return;
  }

  // Network operations can either finish synchronously or asynchronously.
  // This method executes the state machine transitions in a loop so that
  // write state transitions happen even when network operations finish
  // synchronously.
  int rv = result;
  do {
    proto::WriteState state = write_state_;
    write_state_ = proto::WRITE_STATE_NONE;
    switch (state) {
      case proto::WRITE_STATE_WRITE:
        rv = DoWrite();
        break;
      case proto::WRITE_STATE_WRITE_COMPLETE:
        rv = DoWriteComplete(rv);
        break;
      case proto::WRITE_STATE_DO_CALLBACK:
        rv = DoWriteCallback();
        break;
      case proto::WRITE_STATE_ERROR:
        rv = DoWriteError(rv);
        break;
      default:
        NOTREACHED() << "BUG in write flow. Unknown state: " << state;
        break;
    }
  } while (!write_queue_.empty() && rv != net::ERR_IO_PENDING &&
           write_state_ != proto::WRITE_STATE_NONE);

  // No state change occurred in do-while loop above. This means state has
  // transitioned to NONE.
  if (write_state_ == proto::WRITE_STATE_NONE) {
    logger_->LogSocketWriteState(channel_id_, write_state_);
  }

  // If write loop is done because the queue is empty then set write
  // state to NONE
  if (write_queue_.empty()) {
    SetWriteState(proto::WRITE_STATE_NONE);
  }

  // Write loop is done - if the result is ERR_FAILED then close with error.
  if (rv == net::ERR_FAILED) {
    CloseWithError();
  }
}

int CastSocket::DoWrite() {
  DCHECK(!write_queue_.empty());
  WriteRequest& request = write_queue_.front();

  VLOG_WITH_CONNECTION(2) << "WriteData byte_count = "
                          << request.io_buffer->size() << " bytes_written "
                          << request.io_buffer->BytesConsumed();

  SetWriteState(proto::WRITE_STATE_WRITE_COMPLETE);

  int rv = socket_->Write(
      request.io_buffer.get(),
      request.io_buffer->BytesRemaining(),
      base::Bind(&CastSocket::DoWriteLoop, base::Unretained(this)));
  logger_->LogSocketEventWithRv(channel_id_, proto::SOCKET_WRITE, rv);

  return rv;
}

int CastSocket::DoWriteComplete(int result) {
  DCHECK(!write_queue_.empty());
  if (result <= 0) {  // NOTE that 0 also indicates an error
    SetErrorState(CHANNEL_ERROR_SOCKET_ERROR);
    SetWriteState(proto::WRITE_STATE_ERROR);
    return result == 0 ? net::ERR_FAILED : result;
  }

  // Some bytes were successfully written
  WriteRequest& request = write_queue_.front();
  scoped_refptr<net::DrainableIOBuffer> io_buffer = request.io_buffer;
  io_buffer->DidConsume(result);
  if (io_buffer->BytesRemaining() == 0) {  // Message fully sent
    SetWriteState(proto::WRITE_STATE_DO_CALLBACK);
  } else {
    SetWriteState(proto::WRITE_STATE_WRITE);
  }

  return net::OK;
}

int CastSocket::DoWriteCallback() {
  DCHECK(!write_queue_.empty());

  SetWriteState(proto::WRITE_STATE_WRITE);

  WriteRequest& request = write_queue_.front();
  int bytes_consumed = request.io_buffer->BytesConsumed();
  logger_->LogSocketEventForMessage(
      channel_id_,
      proto::MESSAGE_WRITTEN,
      request.message_namespace,
      base::StringPrintf("Bytes: %d", bytes_consumed));
  request.callback.Run(bytes_consumed);
  write_queue_.pop();
  return net::OK;
}

int CastSocket::DoWriteError(int result) {
  DCHECK(!write_queue_.empty());
  DCHECK_LT(result, 0);

  // If inside connection flow, then there should be exactly one item in
  // the write queue.
  if (ready_state_ == READY_STATE_CONNECTING) {
    write_queue_.pop();
    DCHECK(write_queue_.empty());
    PostTaskToStartConnectLoop(result);
    // Connect loop will handle the error. Return net::OK so that write flow
    // does not try to report error also.
    return net::OK;
  }

  while (!write_queue_.empty()) {
    WriteRequest& request = write_queue_.front();
    request.callback.Run(result);
    write_queue_.pop();
  }
  return net::ERR_FAILED;
}

void CastSocket::PostTaskToStartReadLoop() {
  DCHECK(CalledOnValidThread());
  DCHECK(read_loop_callback_.IsCancelled());
  read_loop_callback_.Reset(
      base::Bind(&CastSocket::StartReadLoop, base::Unretained(this)));
  base::MessageLoop::current()->PostTask(FROM_HERE,
                                         read_loop_callback_.callback());
}

void CastSocket::StartReadLoop() {
  read_loop_callback_.Cancel();
  // Read loop would have already been started if read state is not NONE
  if (read_state_ == proto::READ_STATE_NONE) {
    SetReadState(proto::READ_STATE_READ);
    DoReadLoop(net::OK);
  }
}

void CastSocket::DoReadLoop(int result) {
  DCHECK(CalledOnValidThread());
  // Network operations can either finish synchronously or asynchronously.
  // This method executes the state machine transitions in a loop so that
  // write state transitions happen even when network operations finish
  // synchronously.
  int rv = result;
  do {
    proto::ReadState state = read_state_;
    read_state_ = proto::READ_STATE_NONE;

    switch (state) {
      case proto::READ_STATE_READ:
        rv = DoRead();
        break;
      case proto::READ_STATE_READ_COMPLETE:
        rv = DoReadComplete(rv);
        break;
      case proto::READ_STATE_DO_CALLBACK:
        rv = DoReadCallback();
        break;
      case proto::READ_STATE_ERROR:
        rv = DoReadError(rv);
        DCHECK_EQ(read_state_, proto::READ_STATE_NONE);
        break;
      default:
        NOTREACHED() << "BUG in read flow. Unknown state: " << state;
        break;
    }
  } while (rv != net::ERR_IO_PENDING && read_state_ != proto::READ_STATE_NONE);

  // No state change occurred in do-while loop above. This means state has
  // transitioned to NONE.
  if (read_state_ == proto::READ_STATE_NONE) {
    logger_->LogSocketReadState(channel_id_, read_state_);
  }

  if (rv == net::ERR_FAILED) {
    if (ready_state_ == READY_STATE_CONNECTING) {
      // Read errors during the handshake should notify the caller via the
      // connect callback.  This will also send error status via the OnError
      // delegate.
      PostTaskToStartConnectLoop(net::ERR_FAILED);
    } else {
      // Connection is already established.  Close and send error status via the
      // OnError delegate.
      CloseWithError();
    }
  }
}

int CastSocket::DoRead() {
  SetReadState(proto::READ_STATE_READ_COMPLETE);

  // Determine how many bytes need to be read.
  size_t num_bytes_to_read = framer_->BytesRequested();

  // Read up to num_bytes_to_read into |current_read_buffer_|.
  int rv = socket_->Read(
      read_buffer_.get(),
      base::checked_cast<uint32>(num_bytes_to_read),
      base::Bind(&CastSocket::DoReadLoop, base::Unretained(this)));
  logger_->LogSocketEventWithRv(channel_id_, proto::SOCKET_READ, rv);

  return rv;
}

int CastSocket::DoReadComplete(int result) {
  VLOG_WITH_CONNECTION(2) << "DoReadComplete result = " << result;

  if (result <= 0) {  // 0 means EOF: the peer closed the socket
    VLOG_WITH_CONNECTION(1) << "Read error, peer closed the socket";
    SetErrorState(CHANNEL_ERROR_SOCKET_ERROR);
    SetReadState(proto::READ_STATE_ERROR);
    return result == 0 ? net::ERR_FAILED : result;
  }

  size_t message_size;
  DCHECK(current_message_.get() == NULL);
  current_message_ = framer_->Ingest(result, &message_size, &error_state_);
  if (current_message_.get()) {
    DCHECK_EQ(error_state_, CHANNEL_ERROR_NONE);
    DCHECK_GT(message_size, static_cast<size_t>(0));
    logger_->LogSocketEventForMessage(
        channel_id_,
        proto::MESSAGE_READ,
        current_message_->namespace_(),
        base::StringPrintf("Message size: %u",
                           static_cast<uint32>(message_size)));
    SetReadState(proto::READ_STATE_DO_CALLBACK);
  } else if (error_state_ != CHANNEL_ERROR_NONE) {
    DCHECK(current_message_.get() == NULL);
    SetReadState(proto::READ_STATE_ERROR);
  } else {
    DCHECK(current_message_.get() == NULL);
    SetReadState(proto::READ_STATE_READ);
  }
  return net::OK;
}

int CastSocket::DoReadCallback() {
  SetReadState(proto::READ_STATE_READ);
  const CastMessage& message = *current_message_;
  if (ready_state_ == READY_STATE_CONNECTING) {
    if (IsAuthMessage(message)) {
      challenge_reply_.reset(new CastMessage(message));
      logger_->LogSocketEvent(channel_id_, proto::RECEIVED_CHALLENGE_REPLY);
      PostTaskToStartConnectLoop(net::OK);
      current_message_.reset();
      return net::OK;
    } else {
      SetReadState(proto::READ_STATE_ERROR);
      SetErrorState(CHANNEL_ERROR_INVALID_MESSAGE);
      current_message_.reset();
      return net::ERR_INVALID_RESPONSE;
    }
  }

  MessageInfo message_info;
  if (!CastMessageToMessageInfo(message, &message_info)) {
    current_message_.reset();
    SetReadState(proto::READ_STATE_ERROR);
    SetErrorState(CHANNEL_ERROR_INVALID_MESSAGE);
    return net::ERR_INVALID_RESPONSE;
  }

  logger_->LogSocketEventForMessage(channel_id_,
                                    proto::NOTIFY_ON_MESSAGE,
                                    message.namespace_(),
                                    std::string());
  delegate_->OnMessage(this, message_info);
  current_message_.reset();

  return net::OK;
}

int CastSocket::DoReadError(int result) {
  DCHECK_LE(result, 0);
  return net::ERR_FAILED;
}

void CastSocket::CloseWithError() {
  DCHECK(CalledOnValidThread());
  CloseInternal();
  RunPendingCallbacksOnClose();
  if (delegate_) {
    logger_->LogSocketEvent(channel_id_, proto::NOTIFY_ON_ERROR);
    delegate_->OnError(this, error_state_, logger_->GetLastErrors(channel_id_));
  }
}

std::string CastSocket::CastUrl() const {
  return ((channel_auth_ == CHANNEL_AUTH_TYPE_SSL_VERIFIED) ?
          "casts://" : "cast://") + ip_endpoint_.ToString();
}

bool CastSocket::CalledOnValidThread() const {
  return thread_checker_.CalledOnValidThread();
}

base::Timer* CastSocket::GetTimer() {
  return connect_timeout_timer_.get();
}

void CastSocket::SetConnectState(proto::ConnectionState connect_state) {
  if (connect_state_ != connect_state) {
    connect_state_ = connect_state;
    logger_->LogSocketConnectState(channel_id_, connect_state_);
  }
}

void CastSocket::SetReadyState(ReadyState ready_state) {
  if (ready_state_ != ready_state) {
    ready_state_ = ready_state;
    logger_->LogSocketReadyState(channel_id_, ReadyStateToProto(ready_state_));
  }
}

void CastSocket::SetErrorState(ChannelError error_state) {
  if (error_state_ != error_state) {
    error_state_ = error_state;
    logger_->LogSocketErrorState(channel_id_, ErrorStateToProto(error_state_));
  }
}

void CastSocket::SetReadState(proto::ReadState read_state) {
  if (read_state_ != read_state) {
    read_state_ = read_state;
    logger_->LogSocketReadState(channel_id_, read_state_);
  }
}

void CastSocket::SetWriteState(proto::WriteState write_state) {
  if (write_state_ != write_state) {
    write_state_ = write_state;
    logger_->LogSocketWriteState(channel_id_, write_state_);
  }
}

CastSocket::WriteRequest::WriteRequest(const net::CompletionCallback& callback)
    : callback(callback) {
}

bool CastSocket::WriteRequest::SetContent(const CastMessage& message_proto) {
  DCHECK(!io_buffer.get());
  std::string message_data;
  if (!MessageFramer::Serialize(message_proto, &message_data)) {
    return false;
  }
  message_namespace = message_proto.namespace_();
  io_buffer = new net::DrainableIOBuffer(new net::StringIOBuffer(message_data),
                                         message_data.size());
  return true;
}

CastSocket::WriteRequest::~WriteRequest() {
}

}  // namespace cast_channel
}  // namespace core_api
}  // namespace extensions

#undef VLOG_WITH_CONNECTION
