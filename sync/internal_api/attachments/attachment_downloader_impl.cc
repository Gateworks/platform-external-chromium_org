// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/internal_api/public/attachments/attachment_downloader_impl.h"

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "net/base/load_flags.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/http/http_util.h"
#include "net/url_request/url_fetcher.h"
#include "sync/internal_api/public/attachments/attachment_uploader_impl.h"
#include "sync/protocol/sync.pb.h"
#include "url/gurl.h"

namespace syncer {

struct AttachmentDownloaderImpl::DownloadState {
 public:
  DownloadState(const AttachmentId& attachment_id,
                const AttachmentUrl& attachment_url);

  AttachmentId attachment_id;
  AttachmentUrl attachment_url;
  // |access_token| needed to invalidate if downloading attachment fails with
  // HTTP_UNAUTHORIZED.
  std::string access_token;
  scoped_ptr<net::URLFetcher> url_fetcher;
  std::vector<DownloadCallback> user_callbacks;
};

AttachmentDownloaderImpl::DownloadState::DownloadState(
    const AttachmentId& attachment_id,
    const AttachmentUrl& attachment_url)
    : attachment_id(attachment_id), attachment_url(attachment_url) {
}

AttachmentDownloaderImpl::AttachmentDownloaderImpl(
    const GURL& sync_service_url,
    const scoped_refptr<net::URLRequestContextGetter>&
        url_request_context_getter,
    const std::string& account_id,
    const OAuth2TokenService::ScopeSet& scopes,
    const scoped_refptr<OAuth2TokenServiceRequest::TokenServiceProvider>&
        token_service_provider)
    : OAuth2TokenService::Consumer("attachment-downloader-impl"),
      sync_service_url_(sync_service_url),
      url_request_context_getter_(url_request_context_getter),
      account_id_(account_id),
      oauth2_scopes_(scopes),
      token_service_provider_(token_service_provider) {
  DCHECK(!account_id.empty());
  DCHECK(!scopes.empty());
  DCHECK(token_service_provider_.get());
  DCHECK(url_request_context_getter_.get());
}

AttachmentDownloaderImpl::~AttachmentDownloaderImpl() {
}

void AttachmentDownloaderImpl::DownloadAttachment(
    const AttachmentId& attachment_id,
    const DownloadCallback& callback) {
  DCHECK(CalledOnValidThread());

  AttachmentUrl url = AttachmentUploaderImpl::GetURLForAttachmentId(
                          sync_service_url_, attachment_id).spec();

  StateMap::iterator iter = state_map_.find(url);
  if (iter == state_map_.end()) {
    // There is no request started for this attachment id. Let's create
    // DownloadState and request access token for it.
    scoped_ptr<DownloadState> new_download_state(
        new DownloadState(attachment_id, url));
    iter = state_map_.add(url, new_download_state.Pass()).first;
    RequestAccessToken(iter->second);
  }
  DownloadState* download_state = iter->second;
  DCHECK(download_state->attachment_id == attachment_id);
  download_state->user_callbacks.push_back(callback);
}

void AttachmentDownloaderImpl::OnGetTokenSuccess(
    const OAuth2TokenService::Request* request,
    const std::string& access_token,
    const base::Time& expiration_time) {
  DCHECK(CalledOnValidThread());
  DCHECK(request == access_token_request_.get());
  access_token_request_.reset();
  StateList::const_iterator iter;
  // Start downloads for all download requests waiting for access token.
  for (iter = requests_waiting_for_access_token_.begin();
       iter != requests_waiting_for_access_token_.end();
       ++iter) {
    DownloadState* download_state = *iter;
    download_state->access_token = access_token;
    download_state->url_fetcher =
        CreateFetcher(download_state->attachment_url, access_token).Pass();
    download_state->url_fetcher->Start();
  }
  requests_waiting_for_access_token_.clear();
}

void AttachmentDownloaderImpl::OnGetTokenFailure(
    const OAuth2TokenService::Request* request,
    const GoogleServiceAuthError& error) {
  DCHECK(CalledOnValidThread());
  DCHECK(request == access_token_request_.get());
  access_token_request_.reset();
  StateList::const_iterator iter;
  // Without access token all downloads fail.
  for (iter = requests_waiting_for_access_token_.begin();
       iter != requests_waiting_for_access_token_.end();
       ++iter) {
    DownloadState* download_state = *iter;
    scoped_refptr<base::RefCountedString> null_attachment_data;
    ReportResult(
        *download_state, DOWNLOAD_TRANSIENT_ERROR, null_attachment_data);
    DCHECK(state_map_.find(download_state->attachment_url) != state_map_.end());
    state_map_.erase(download_state->attachment_url);
  }
  requests_waiting_for_access_token_.clear();
}

void AttachmentDownloaderImpl::OnURLFetchComplete(
    const net::URLFetcher* source) {
  DCHECK(CalledOnValidThread());

  // Find DownloadState by url.
  AttachmentUrl url = source->GetOriginalURL().spec();
  StateMap::iterator iter = state_map_.find(url);
  DCHECK(iter != state_map_.end());
  const DownloadState& download_state = *iter->second;
  DCHECK(source == download_state.url_fetcher.get());

  DownloadResult result = DOWNLOAD_TRANSIENT_ERROR;
  scoped_refptr<base::RefCountedString> attachment_data;

  const int response_code = source->GetResponseCode();
  if (response_code == net::HTTP_OK) {
    std::string data_as_string;
    source->GetResponseAsString(&data_as_string);
    if (VerifyHashIfPresent(*source, data_as_string)) {
      result = DOWNLOAD_SUCCESS;
      attachment_data = base::RefCountedString::TakeString(&data_as_string);
    } else {
      // TODO(maniscalco): Test me!
      result = DOWNLOAD_TRANSIENT_ERROR;
    }
  } else if (response_code == net::HTTP_UNAUTHORIZED) {
    // Server tells us we've got a bad token so invalidate it.
    OAuth2TokenServiceRequest::InvalidateToken(token_service_provider_.get(),
                                               account_id_,
                                               oauth2_scopes_,
                                               download_state.access_token);
    // Fail the request, but indicate that it may be successful if retried.
    result = DOWNLOAD_TRANSIENT_ERROR;
  } else if (response_code == net::HTTP_FORBIDDEN) {
    // User is not allowed to use attachments.  Retrying won't help.
    result = DOWNLOAD_UNSPECIFIED_ERROR;
  } else if (response_code == net::URLFetcher::RESPONSE_CODE_INVALID) {
    result = DOWNLOAD_TRANSIENT_ERROR;
  }
  ReportResult(download_state, result, attachment_data);
  state_map_.erase(iter);
}

scoped_ptr<net::URLFetcher> AttachmentDownloaderImpl::CreateFetcher(
    const AttachmentUrl& url,
    const std::string& access_token) {
  scoped_ptr<net::URLFetcher> url_fetcher(
      net::URLFetcher::Create(GURL(url), net::URLFetcher::GET, this));
  url_fetcher->SetAutomaticallyRetryOn5xx(false);
  const std::string auth_header("Authorization: Bearer " + access_token);
  url_fetcher->AddExtraRequestHeader(auth_header);
  url_fetcher->SetRequestContext(url_request_context_getter_.get());
  url_fetcher->SetLoadFlags(net::LOAD_DO_NOT_SAVE_COOKIES |
                            net::LOAD_DO_NOT_SEND_COOKIES |
                            net::LOAD_DISABLE_CACHE);
  // TODO(maniscalco): Set an appropriate headers (User-Agent, what else?) on
  // the request (bug 371521).
  return url_fetcher.Pass();
}

void AttachmentDownloaderImpl::RequestAccessToken(
    DownloadState* download_state) {
  requests_waiting_for_access_token_.push_back(download_state);
  // Start access token request if there is no active one.
  if (access_token_request_ == NULL) {
    access_token_request_ = OAuth2TokenServiceRequest::CreateAndStart(
        token_service_provider_.get(), account_id_, oauth2_scopes_, this);
  }
}

void AttachmentDownloaderImpl::ReportResult(
    const DownloadState& download_state,
    const DownloadResult& result,
    const scoped_refptr<base::RefCountedString>& attachment_data) {
  std::vector<DownloadCallback>::const_iterator iter;
  for (iter = download_state.user_callbacks.begin();
       iter != download_state.user_callbacks.end();
       ++iter) {
    scoped_ptr<Attachment> attachment;
    if (result == DOWNLOAD_SUCCESS) {
      attachment.reset(new Attachment(Attachment::CreateWithId(
          download_state.attachment_id, attachment_data)));
    }

    base::MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(*iter, result, base::Passed(&attachment)));
  }
}

bool AttachmentDownloaderImpl::VerifyHashIfPresent(
    const net::URLFetcher& fetcher,
    const std::string& data) {
  const net::HttpResponseHeaders* headers = fetcher.GetResponseHeaders();
  if (!headers) {
    // No headers?  It passes.
    return true;
  }

  std::string value;
  if (!ExtractCrc32c(*headers, &value)) {
    // No crc32c?  It passes.
    return true;
  }

  if (value ==
      AttachmentUploaderImpl::ComputeCrc32cHash(data.data(), data.size())) {
    return true;
  } else {
    return false;
  }
}

bool AttachmentDownloaderImpl::ExtractCrc32c(
    const net::HttpResponseHeaders& headers,
    std::string* crc32c) {
  DCHECK(crc32c);
  std::string header_value;
  void* iter = NULL;
  // Iterate over all matching headers.
  while (headers.EnumerateHeader(&iter, "x-goog-hash", &header_value)) {
    // Because EnumerateHeader is smart about list values, header_value will
    // either be empty or a single name=value pair.
    net::HttpUtil::NameValuePairsIterator pair_iter(
        header_value.begin(), header_value.end(), ',');
    if (pair_iter.GetNext()) {
      if (pair_iter.name() == "crc32c") {
        *crc32c = pair_iter.value();
        DCHECK(!pair_iter.GetNext());
        return true;
      }
    }
  }

  return false;
}

}  // namespace syncer
