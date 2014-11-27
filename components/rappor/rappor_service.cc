// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/rappor/rappor_service.h"

#include "base/base64.h"
#include "base/metrics/field_trial.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/pref_service.h"
#include "base/rand_util.h"
#include "base/stl_util.h"
#include "base/time/time.h"
#include "components/metrics/metrics_hashes.h"
#include "components/rappor/log_uploader.h"
#include "components/rappor/proto/rappor_metric.pb.h"
#include "components/rappor/rappor_metric.h"
#include "components/rappor/rappor_pref_names.h"
#include "components/variations/variations_associated_data.h"

namespace rappor {

namespace {

// Seconds before the initial log is generated.
const int kInitialLogIntervalSeconds = 15;
// Interval between ongoing logs.
const int kLogIntervalSeconds = 30 * 60;

const char kMimeType[] = "application/vnd.chrome.rappor";

const char kRapporDailyEventHistogram[] = "Rappor.DailyEvent.IntervalType";

// Constants for the RAPPOR rollout field trial.
const char kRapporRolloutFieldTrialName[] = "RapporRollout";

// Constant for the finch parameter name for the server URL
const char kRapporRolloutServerUrlParam[] = "ServerUrl";

// The rappor server's URL.
const char kDefaultServerUrl[] = "https://clients4.google.com/rappor";

GURL GetServerUrl() {
  std::string server_url = variations::GetVariationParamValue(
      kRapporRolloutFieldTrialName,
      kRapporRolloutServerUrlParam);
  if (!server_url.empty())
    return GURL(server_url);
  else
    return GURL(kDefaultServerUrl);
}

const RapporParameters kRapporParametersForType[NUM_RAPPOR_TYPES] = {
    // ETLD_PLUS_ONE_RAPPOR_TYPE
    {128 /* Num cohorts */,
     16 /* Bloom filter size bytes */,
     2 /* Bloom filter hash count */,
     rappor::PROBABILITY_50 /* Fake data probability */,
     rappor::PROBABILITY_50 /* Fake one probability */,
     rappor::PROBABILITY_75 /* One coin probability */,
     rappor::PROBABILITY_25 /* Zero coin probability */,
     FINE_LEVEL /* Reporting level */},
    // COARSE_RAPPOR_TYPE
    {128 /* Num cohorts */,
     1 /* Bloom filter size bytes */,
     2 /* Bloom filter hash count */,
     rappor::PROBABILITY_50 /* Fake data probability */,
     rappor::PROBABILITY_50 /* Fake one probability */,
     rappor::PROBABILITY_75 /* One coin probability */,
     rappor::PROBABILITY_25 /* Zero coin probability */,
     COARSE_LEVEL /* Reporting level */},
};

}  // namespace

RapporService::RapporService(PrefService* pref_service)
    : pref_service_(pref_service),
      cohort_(-1),
      daily_event_(pref_service,
                   prefs::kRapporLastDailySample,
                   kRapporDailyEventHistogram),
      reporting_level_(REPORTING_DISABLED) {
}

RapporService::~RapporService() {
  STLDeleteValues(&metrics_map_);
}

void RapporService::AddDailyObserver(
    scoped_ptr<metrics::DailyEvent::Observer> observer) {
  daily_event_.AddObserver(observer.Pass());
}

void RapporService::Start(net::URLRequestContextGetter* request_context,
                          bool metrics_enabled) {
  const GURL server_url = GetServerUrl();
  if (!server_url.is_valid()) {
    DVLOG(1) << server_url.spec() << " is invalid. "
             << "RapporService not started.";
    return;
  }
  // TODO(holte): Consider moving this logic once we've determined the
  // conditions for COARSE metrics.
  ReportingLevel reporting_level = metrics_enabled ?
                                   FINE_LEVEL : REPORTING_DISABLED;
  DVLOG(1) << "RapporService reporting_level_? " << reporting_level;
  if (reporting_level <= REPORTING_DISABLED)
    return;
  DVLOG(1) << "RapporService started. Reporting to " << server_url.spec();
  DCHECK(!uploader_);
  Initialize(LoadCohort(), LoadSecret(), reporting_level);
  uploader_.reset(new LogUploader(server_url, kMimeType, request_context));
  log_rotation_timer_.Start(
      FROM_HERE,
      base::TimeDelta::FromSeconds(kInitialLogIntervalSeconds),
      this,
      &RapporService::OnLogInterval);
}

void RapporService::Initialize(int32_t cohort,
                               const std::string& secret,
                               const ReportingLevel& reporting_level) {
  DCHECK(!IsInitialized());
  DCHECK(secret_.empty());
  cohort_ = cohort;
  secret_ = secret;
  reporting_level_ = reporting_level;
}

void RapporService::OnLogInterval() {
  DCHECK(uploader_);
  DVLOG(2) << "RapporService::OnLogInterval";
  daily_event_.CheckInterval();
  RapporReports reports;
  if (ExportMetrics(&reports)) {
    std::string log_text;
    bool success = reports.SerializeToString(&log_text);
    DCHECK(success);
    DVLOG(1) << "RapporService sending a report of "
             << reports.report_size() << " value(s).";
    uploader_->QueueLog(log_text);
  }
  log_rotation_timer_.Start(FROM_HERE,
                            base::TimeDelta::FromSeconds(kLogIntervalSeconds),
                            this,
                            &RapporService::OnLogInterval);
}

// static
void RapporService::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(prefs::kRapporSecret, std::string());
  registry->RegisterIntegerPref(prefs::kRapporCohortDeprecated, -1);
  registry->RegisterIntegerPref(prefs::kRapporCohortSeed, -1);
  metrics::DailyEvent::RegisterPref(registry,
                                       prefs::kRapporLastDailySample);
}

int32_t RapporService::LoadCohort() {
  // Ignore and delete old cohort parameter.
  pref_service_->ClearPref(prefs::kRapporCohortDeprecated);

  int32_t cohort = pref_service_->GetInteger(prefs::kRapporCohortSeed);
  // If the user is already assigned to a valid cohort, we're done.
  if (cohort >= 0 && cohort < RapporParameters::kMaxCohorts)
    return cohort;

  // This is the first time the client has started the service (or their
  // preferences were corrupted).  Randomly assign them to a cohort.
  cohort = base::RandGenerator(RapporParameters::kMaxCohorts);
  DVLOG(2) << "Selected a new Rappor cohort: " << cohort;
  pref_service_->SetInteger(prefs::kRapporCohortSeed, cohort);
  return cohort;
}

std::string RapporService::LoadSecret() {
  std::string secret;
  std::string secret_base64 = pref_service_->GetString(prefs::kRapporSecret);
  if (!secret_base64.empty()) {
    bool decoded = base::Base64Decode(secret_base64, &secret);
    if (decoded && secret_.size() == HmacByteVectorGenerator::kEntropyInputSize)
      return secret;
    // If the preference fails to decode, or is the wrong size, it must be
    // corrupt, so continue as though it didn't exist yet and generate a new
    // one.
  }

  DVLOG(2) << "Generated a new Rappor secret.";
  secret = HmacByteVectorGenerator::GenerateEntropyInput();
  base::Base64Encode(secret, &secret_base64);
  pref_service_->SetString(prefs::kRapporSecret, secret_base64);
  return secret;
}

bool RapporService::ExportMetrics(RapporReports* reports) {
  if (metrics_map_.empty()) {
    DVLOG(2) << "metrics_map_ is empty.";
    return false;
  }

  DCHECK_GE(cohort_, 0);
  reports->set_cohort(cohort_);

  for (std::map<std::string, RapporMetric*>::const_iterator it =
           metrics_map_.begin();
       it != metrics_map_.end();
       ++it) {
    const RapporMetric* metric = it->second;
    RapporReports::Report* report = reports->add_report();
    report->set_name_hash(metrics::HashMetricName(it->first));
    ByteVector bytes = metric->GetReport(secret_);
    report->set_bits(std::string(bytes.begin(), bytes.end()));
  }
  STLDeleteValues(&metrics_map_);
  return true;
}

bool RapporService::IsInitialized() const {
  return cohort_ >= 0;
}

void RapporService::RecordSample(const std::string& metric_name,
                                 RapporType type,
                                 const std::string& sample) {
  // Ignore the sample if the service hasn't started yet.
  if (!IsInitialized())
    return;
  DCHECK_LT(type, NUM_RAPPOR_TYPES);
  const RapporParameters& parameters = kRapporParametersForType[type];
  DVLOG(2) << "Recording sample \"" << sample
           << "\" for metric \"" << metric_name
           << "\" of type: " << type;
  RecordSampleInternal(metric_name, parameters, sample);
}

void RapporService::RecordSampleInternal(const std::string& metric_name,
                                         const RapporParameters& parameters,
                                         const std::string& sample) {
  DCHECK(IsInitialized());
  // Skip this metric if it's reporting level is less than the enabled
  // reporting level.
  if (reporting_level_ < parameters.reporting_level) {
    DVLOG(2) << "Metric not logged due to reporting_level "
             << reporting_level_ << " < " << parameters.reporting_level;
    return;
  }
  RapporMetric* metric = LookUpMetric(metric_name, parameters);
  metric->AddSample(sample);
}

RapporMetric* RapporService::LookUpMetric(const std::string& metric_name,
                                          const RapporParameters& parameters) {
  DCHECK(IsInitialized());
  std::map<std::string, RapporMetric*>::const_iterator it =
      metrics_map_.find(metric_name);
  if (it != metrics_map_.end()) {
    RapporMetric* metric = it->second;
    DCHECK_EQ(parameters.ToString(), metric->parameters().ToString());
    return metric;
  }

  RapporMetric* new_metric = new RapporMetric(metric_name, parameters, cohort_);
  metrics_map_[metric_name] = new_metric;
  return new_metric;
}

}  // namespace rappor
