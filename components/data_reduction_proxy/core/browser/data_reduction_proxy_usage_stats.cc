// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_usage_stats.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/metrics/histogram.h"
#include "base/metrics/sparse_histogram.h"
#include "base/prefs/pref_member.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_headers.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/proxy/proxy_retry_info.h"
#include "net/proxy/proxy_server.h"
#include "net/proxy/proxy_service.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"

using base::MessageLoopProxy;
using net::HostPortPair;
using net::ProxyServer;
using net::ProxyService;
using net::NetworkChangeNotifier;
using net::URLRequest;

namespace data_reduction_proxy {

namespace {

const int kMinFailedRequestsWhenUnavailable = 1;
const int kMaxSuccessfulRequestsWhenUnavailable = 0;
const int kMaxFailedRequestsBeforeReset = 3;

// Records a net error code that resulted in bypassing the data reduction
// proxy (|is_primary| is true) or the data reduction proxy fallback.
void RecordDataReductionProxyBypassOnNetworkError(
    bool is_primary,
    const ProxyServer& proxy_server,
    int net_error) {
  if (is_primary) {
    UMA_HISTOGRAM_SPARSE_SLOWLY(
        "DataReductionProxy.BypassOnNetworkErrorPrimary",
        std::abs(net_error));
    return;
  }
  UMA_HISTOGRAM_SPARSE_SLOWLY(
      "DataReductionProxy.BypassOnNetworkErrorFallback",
      std::abs(net_error));
}

}  // namespace

// static
void DataReductionProxyUsageStats::RecordDataReductionProxyBypassInfo(
    bool is_primary,
    bool bypass_all,
    const net::ProxyServer& proxy_server,
    DataReductionProxyBypassType bypass_type) {
  if (bypass_all) {
    if (is_primary) {
      UMA_HISTOGRAM_ENUMERATION("DataReductionProxy.BlockTypePrimary",
                                bypass_type, BYPASS_EVENT_TYPE_MAX);
    } else {
      UMA_HISTOGRAM_ENUMERATION("DataReductionProxy.BlockTypeFallback",
                                bypass_type, BYPASS_EVENT_TYPE_MAX);
    }
  } else {
    if (is_primary) {
      UMA_HISTOGRAM_ENUMERATION("DataReductionProxy.BypassTypePrimary",
                                bypass_type, BYPASS_EVENT_TYPE_MAX);
    } else {
      UMA_HISTOGRAM_ENUMERATION("DataReductionProxy.BypassTypeFallback",
                                bypass_type, BYPASS_EVENT_TYPE_MAX);
    }
  }
}

// static
void DataReductionProxyUsageStats::DetectAndRecordMissingViaHeaderResponseCode(
      bool is_primary,
      const net::HttpResponseHeaders* headers) {
  if (HasDataReductionProxyViaHeader(headers, NULL)) {
    // The data reduction proxy via header is present, so don't record anything.
    return;
  }

  if (is_primary) {
    UMA_HISTOGRAM_SPARSE_SLOWLY(
        "DataReductionProxy.MissingViaHeader.ResponseCode.Primary",
        headers->response_code());
  } else {
    UMA_HISTOGRAM_SPARSE_SLOWLY(
        "DataReductionProxy.MissingViaHeader.ResponseCode.Fallback",
        headers->response_code());
  }
}

DataReductionProxyUsageStats::DataReductionProxyUsageStats(
    DataReductionProxyParams* params,
    const scoped_refptr<MessageLoopProxy>& ui_thread_proxy)
    : data_reduction_proxy_params_(params),
      last_bypass_type_(BYPASS_EVENT_TYPE_MAX),
      triggering_request_(true),
      ui_thread_proxy_(ui_thread_proxy),
      successful_requests_through_proxy_count_(0),
      proxy_net_errors_count_(0),
      unavailable_(false) {
  DCHECK(params);

  NetworkChangeNotifier::AddNetworkChangeObserver(this);
};

DataReductionProxyUsageStats::~DataReductionProxyUsageStats() {
  NetworkChangeNotifier::RemoveNetworkChangeObserver(this);
};

void DataReductionProxyUsageStats::OnUrlRequestCompleted(
    const net::URLRequest* request, bool started) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (request->status().status() == net::URLRequestStatus::SUCCESS &&
      data_reduction_proxy_params_->WasDataReductionProxyUsed(request, NULL)) {
    successful_requests_through_proxy_count_++;
    NotifyUnavailabilityIfChanged();
  }
}

void DataReductionProxyUsageStats::OnNetworkChanged(
    NetworkChangeNotifier::ConnectionType type) {
  DCHECK(thread_checker_.CalledOnValidThread());
  ClearRequestCounts();
}

void DataReductionProxyUsageStats::ClearRequestCounts() {
  DCHECK(thread_checker_.CalledOnValidThread());
  successful_requests_through_proxy_count_ = 0;
  proxy_net_errors_count_ = 0;
}

void DataReductionProxyUsageStats::NotifyUnavailabilityIfChanged() {
  bool prev_unavailable = unavailable_;
  unavailable_ =
      (proxy_net_errors_count_ >= kMinFailedRequestsWhenUnavailable &&
          successful_requests_through_proxy_count_ <=
              kMaxSuccessfulRequestsWhenUnavailable);
  if (prev_unavailable != unavailable_) {
    ui_thread_proxy_->PostTask(FROM_HERE, base::Bind(
        &DataReductionProxyUsageStats::NotifyUnavailabilityOnUIThread,
        base::Unretained(this),
        unavailable_));
  }
}

void DataReductionProxyUsageStats::NotifyUnavailabilityOnUIThread(
    bool unavailable) {
  DCHECK(ui_thread_proxy_->BelongsToCurrentThread());
  if (!unavailable_callback_.is_null())
    unavailable_callback_.Run(unavailable);
}

void DataReductionProxyUsageStats::SetBypassType(
    DataReductionProxyBypassType type) {
  last_bypass_type_ = type;
  triggering_request_ = true;
}

DataReductionProxyBypassType
DataReductionProxyUsageStats::GetBypassType() const {
  return last_bypass_type_;
}

void DataReductionProxyUsageStats::RecordBytesHistograms(
    net::URLRequest* request,
    const BooleanPrefMember& data_reduction_proxy_enabled,
    const net::ProxyConfig& data_reduction_proxy_config) {
  RecordBypassedBytesHistograms(request, data_reduction_proxy_enabled,
                                data_reduction_proxy_config);
  RecordMissingViaHeaderBytes(request);
}

void DataReductionProxyUsageStats::RecordBypassedBytesHistograms(
    net::URLRequest* request,
    const BooleanPrefMember& data_reduction_proxy_enabled,
    const net::ProxyConfig& data_reduction_proxy_config) {
  int64 content_length = request->received_response_content_length();

  if (data_reduction_proxy_enabled.GetValue() &&
      !data_reduction_proxy_config.Equals(
          request->context()->proxy_service()->config())) {
    RecordBypassedBytes(last_bypass_type_,
                        DataReductionProxyUsageStats::MANAGED_PROXY_CONFIG,
                        content_length);
    return;
  }

  if (data_reduction_proxy_params_->WasDataReductionProxyUsed(request, NULL)) {
    RecordBypassedBytes(last_bypass_type_,
                        DataReductionProxyUsageStats::NOT_BYPASSED,
                        content_length);
    return;
  }

  if (data_reduction_proxy_enabled.GetValue() &&
      request->url().SchemeIs(url::kHttpsScheme)) {
    RecordBypassedBytes(last_bypass_type_,
                        DataReductionProxyUsageStats::SSL,
                        content_length);
    return;
  }

  if (data_reduction_proxy_enabled.GetValue() &&
      data_reduction_proxy_params_->IsBypassedByDataReductionProxyLocalRules(
          *request, data_reduction_proxy_config)) {
    RecordBypassedBytes(last_bypass_type_,
                        DataReductionProxyUsageStats::LOCAL_BYPASS_RULES,
                        content_length);
    return;
  }

  // Only record separate triggering request UMA for short, medium, and long
  // bypass events.
  if (triggering_request_ &&
     (last_bypass_type_ ==  BYPASS_EVENT_TYPE_SHORT ||
      last_bypass_type_ ==  BYPASS_EVENT_TYPE_MEDIUM ||
      last_bypass_type_ ==  BYPASS_EVENT_TYPE_LONG)) {
    std::string mime_type;
    request->GetMimeType(&mime_type);
    // MIME types are named by <media-type>/<subtype>. Check to see if the
    // media type is audio or video. Only record when triggered by short bypass,
    // there isn't an audio or video bucket for medium or long bypasses.
    if (last_bypass_type_ ==  BYPASS_EVENT_TYPE_SHORT &&
       (mime_type.compare(0, 6, "audio/") == 0  ||
        mime_type.compare(0, 6, "video/") == 0)) {
      RecordBypassedBytes(last_bypass_type_,
                          DataReductionProxyUsageStats::AUDIO_VIDEO,
                          content_length);
      return;
    }

    RecordBypassedBytes(last_bypass_type_,
                        DataReductionProxyUsageStats::TRIGGERING_REQUEST,
                        content_length);
    triggering_request_ = false;
    return;
  }

  if (last_bypass_type_ != BYPASS_EVENT_TYPE_MAX) {
    RecordBypassedBytes(last_bypass_type_,
                        DataReductionProxyUsageStats::BYPASSED_BYTES_TYPE_MAX,
                        content_length);
    return;
  }

  if (data_reduction_proxy_enabled.GetValue() &&
      data_reduction_proxy_params_->AreDataReductionProxiesBypassed(*request,
                                                                    NULL)) {
    RecordBypassedBytes(last_bypass_type_,
                        DataReductionProxyUsageStats::NETWORK_ERROR,
                        content_length);
  }
}

void DataReductionProxyUsageStats::OnProxyFallback(
    const net::ProxyServer& bypassed_proxy,
    int net_error) {
  DataReductionProxyTypeInfo data_reduction_proxy_info;
  if (bypassed_proxy.is_valid() && !bypassed_proxy.is_direct() &&
      data_reduction_proxy_params_->IsDataReductionProxy(
      bypassed_proxy.host_port_pair(), &data_reduction_proxy_info)) {
    if (data_reduction_proxy_info.is_ssl)
      return;

    proxy_net_errors_count_++;

    // To account for the case when the proxy is reachable for sometime, and
    // then gets blocked, we reset counts when number of errors exceed
    // the threshold.
    if (proxy_net_errors_count_ >= kMaxFailedRequestsBeforeReset &&
        successful_requests_through_proxy_count_ >
            kMaxSuccessfulRequestsWhenUnavailable) {
      ClearRequestCounts();
    } else {
      NotifyUnavailabilityIfChanged();
    }

    if (!data_reduction_proxy_info.is_fallback) {
      RecordDataReductionProxyBypassInfo(
          true, false, bypassed_proxy, BYPASS_EVENT_TYPE_NETWORK_ERROR);
      RecordDataReductionProxyBypassOnNetworkError(
          true, bypassed_proxy, net_error);
    } else {
      RecordDataReductionProxyBypassInfo(
          false, false, bypassed_proxy, BYPASS_EVENT_TYPE_NETWORK_ERROR);
      RecordDataReductionProxyBypassOnNetworkError(
          false, bypassed_proxy, net_error);
    }
  }
}

void DataReductionProxyUsageStats::OnConnectComplete(
    const net::HostPortPair& proxy_server,
    int net_error) {
  if (data_reduction_proxy_params_->IsDataReductionProxy(proxy_server, NULL)) {
    UMA_HISTOGRAM_SPARSE_SLOWLY(
      "DataReductionProxy.HTTPConnectCompleted",
      std::abs(net_error));
  }
}

void DataReductionProxyUsageStats::RecordBypassedBytes(
    DataReductionProxyBypassType bypass_type,
    DataReductionProxyUsageStats::BypassedBytesType bypassed_bytes_type,
    int64 content_length) {
  // Individual histograms are needed to count the bypassed bytes for each
  // bypass type so that we can see the size of requests. This helps us
  // remove outliers that would skew the sum of bypassed bytes for each type.
  switch (bypassed_bytes_type) {
    case DataReductionProxyUsageStats::NOT_BYPASSED:
      UMA_HISTOGRAM_COUNTS(
          "DataReductionProxy.BypassedBytes.NotBypassed", content_length);
      break;
    case DataReductionProxyUsageStats::SSL:
      UMA_HISTOGRAM_COUNTS(
          "DataReductionProxy.BypassedBytes.SSL", content_length);
      break;
    case DataReductionProxyUsageStats::LOCAL_BYPASS_RULES:
      UMA_HISTOGRAM_COUNTS(
          "DataReductionProxy.BypassedBytes.LocalBypassRules",
          content_length);
      break;
    case DataReductionProxyUsageStats::MANAGED_PROXY_CONFIG:
      UMA_HISTOGRAM_COUNTS(
          "DataReductionProxy.BypassedBytes.ManagedProxyConfig",
          content_length);
      break;
    case DataReductionProxyUsageStats::AUDIO_VIDEO:
      if (last_bypass_type_ == BYPASS_EVENT_TYPE_SHORT) {
        UMA_HISTOGRAM_COUNTS(
            "DataReductionProxy.BypassedBytes.ShortAudioVideo",
            content_length);
      }
      break;
    case DataReductionProxyUsageStats::TRIGGERING_REQUEST:
      switch (bypass_type) {
        case BYPASS_EVENT_TYPE_SHORT:
          UMA_HISTOGRAM_COUNTS(
              "DataReductionProxy.BypassedBytes.ShortTriggeringRequest",
              content_length);
          break;
        case BYPASS_EVENT_TYPE_MEDIUM:
          UMA_HISTOGRAM_COUNTS(
              "DataReductionProxy.BypassedBytes.MediumTriggeringRequest",
              content_length);
          break;
        case BYPASS_EVENT_TYPE_LONG:
          UMA_HISTOGRAM_COUNTS(
              "DataReductionProxy.BypassedBytes.LongTriggeringRequest",
              content_length);
          break;
        default:
          break;
      }
      break;
    case DataReductionProxyUsageStats::NETWORK_ERROR:
      UMA_HISTOGRAM_COUNTS(
          "DataReductionProxy.BypassedBytes.NetworkErrorOther",
          content_length);
      break;
    case DataReductionProxyUsageStats::BYPASSED_BYTES_TYPE_MAX:
      switch (bypass_type) {
        case BYPASS_EVENT_TYPE_CURRENT:
          UMA_HISTOGRAM_COUNTS("DataReductionProxy.BypassedBytes.Current",
                               content_length);
          break;
        case BYPASS_EVENT_TYPE_SHORT:
          UMA_HISTOGRAM_COUNTS("DataReductionProxy.BypassedBytes.ShortAll",
                               content_length);
          break;
        case BYPASS_EVENT_TYPE_MEDIUM:
          UMA_HISTOGRAM_COUNTS("DataReductionProxy.BypassedBytes.MediumAll",
                               content_length);
          break;
        case BYPASS_EVENT_TYPE_LONG:
          UMA_HISTOGRAM_COUNTS("DataReductionProxy.BypassedBytes.LongAll",
                               content_length);
          break;
        case BYPASS_EVENT_TYPE_MISSING_VIA_HEADER_4XX:
          UMA_HISTOGRAM_COUNTS(
              "DataReductionProxy.BypassedBytes.MissingViaHeader4xx",
              content_length);
          break;
        case BYPASS_EVENT_TYPE_MISSING_VIA_HEADER_OTHER:
          UMA_HISTOGRAM_COUNTS(
              "DataReductionProxy.BypassedBytes.MissingViaHeaderOther",
              content_length);
          break;
        case BYPASS_EVENT_TYPE_MALFORMED_407:
          UMA_HISTOGRAM_COUNTS("DataReductionProxy.BypassedBytes.Malformed407",
                               content_length);
         break;
        case BYPASS_EVENT_TYPE_STATUS_500_HTTP_INTERNAL_SERVER_ERROR:
          UMA_HISTOGRAM_COUNTS(
              "DataReductionProxy.BypassedBytes."
              "Status500HttpInternalServerError",
              content_length);
          break;
        case BYPASS_EVENT_TYPE_STATUS_502_HTTP_BAD_GATEWAY:
          UMA_HISTOGRAM_COUNTS(
              "DataReductionProxy.BypassedBytes.Status502HttpBadGateway",
              content_length);
          break;
        case BYPASS_EVENT_TYPE_STATUS_503_HTTP_SERVICE_UNAVAILABLE:
          UMA_HISTOGRAM_COUNTS(
              "DataReductionProxy.BypassedBytes."
              "Status503HttpServiceUnavailable",
              content_length);
          break;
        default:
          break;
      }
      break;
  }
}

void DataReductionProxyUsageStats::RecordMissingViaHeaderBytes(
    URLRequest* request) {
  // Responses that were served from cache should have been filtered out
  // already.
  DCHECK(!request->was_cached());

  if (!data_reduction_proxy_params_->WasDataReductionProxyUsed(request, NULL) ||
      HasDataReductionProxyViaHeader(request->response_headers(), NULL)) {
    // Only track requests that used the data reduction proxy and had responses
    // that were missing the data reduction proxy via header.
    return;
  }

  if (request->GetResponseCode() >= net::HTTP_BAD_REQUEST &&
      request->GetResponseCode() < net::HTTP_INTERNAL_SERVER_ERROR) {
    // Track 4xx responses that are missing via headers separately.
    UMA_HISTOGRAM_COUNTS("DataReductionProxy.MissingViaHeader.Bytes.4xx",
                         request->received_response_content_length());
  } else {
    UMA_HISTOGRAM_COUNTS("DataReductionProxy.MissingViaHeader.Bytes.Other",
                         request->received_response_content_length());
  }
}

}  // namespace data_reduction_proxy


