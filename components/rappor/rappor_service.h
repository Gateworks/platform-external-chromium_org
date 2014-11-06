// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_RAPPOR_RAPPOR_SERVICE_H_
#define COMPONENTS_RAPPOR_RAPPOR_SERVICE_H_

#include <map>
#include <string>

#include "base/basictypes.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/timer/timer.h"
#include "components/metrics/daily_event.h"
#include "components/rappor/rappor_parameters.h"

class PrefRegistrySimple;
class PrefService;

namespace net {
class URLRequestContextGetter;
}

namespace rappor {

class LogUploader;
class RapporMetric;
class RapporReports;

// The type of data stored in a metric.
enum RapporType {
  // For sampling the eTLD+1 of a URL.
  ETLD_PLUS_ONE_RAPPOR_TYPE = 0,
  COARSE_RAPPOR_TYPE,
  NUM_RAPPOR_TYPES
};

// This class provides an interface for recording samples for rappor metrics,
// and periodically generates and uploads reports based on the collected data.
class RapporService {
 public:
  // Constructs a RapporService.
  // Calling code is responsible for ensuring that the lifetime of
  // |pref_service| is longer than the lifetime of RapporService.
  explicit RapporService(PrefService* pref_service);
  virtual ~RapporService();

  // Add an observer for collecting daily metrics.
  void AddDailyObserver(scoped_ptr<metrics::DailyEvent::Observer> observer);

  // Starts the periodic generation of reports and upload attempts.
  // |metrics enabled| should be true if UMA users have opted in.
  void Start(net::URLRequestContextGetter* context,
             bool metrics_enabled);

  // Records a sample of the rappor metric specified by |metric_name|.
  // Creates and initializes the metric, if it doesn't yet exist.
  void RecordSample(const std::string& metric_name,
                    RapporType type,
                    const std::string& sample);

  // Registers the names of all of the preferences used by RapporService in the
  // provided PrefRegistry. This should be called before calling Start().
  static void RegisterPrefs(PrefRegistrySimple* registry);

 protected:
  // Initializes the state of the RapporService.
  void Initialize(int32_t cohort,
                  const std::string& secret,
                  const ReportingLevel& reporting_level);

  // Retrieves the cohort number this client was assigned to, generating it if
  // doesn't already exist. The cohort should be persistent.
  int32_t LoadCohort();

  // Retrieves the value for secret_ from preferences, generating it if doesn't
  // already exist. The secret should be persistent, so that additional bits
  // from the client do not get exposed over time.
  std::string LoadSecret();

  // Logs all of the collected metrics to the reports proto message and clears
  // the internal map. Exposed for tests. Returns true if any metrics were
  // recorded.
  bool ExportMetrics(RapporReports* reports);

  // Records a sample of the rappor metric specified by |parameters|.
  // Creates and initializes the metric, if it doesn't yet exist.
  // Exposed for tests.
  void RecordSampleInternal(const std::string& metric_name,
                            const RapporParameters& parameters,
                            const std::string& sample);

 private:
  // Check if the service has been started successfully.
  bool IsInitialized() const;

  // Called whenever the logging interval elapses to generate a new log of
  // reports and pass it to the uploader.
  void OnLogInterval();

  // Finds a metric in the metrics_map_, creating it if it doesn't already
  // exist.
  RapporMetric* LookUpMetric(const std::string& metric_name,
                             const RapporParameters& parameters);

  // A weak pointer to the PrefService used to read and write preferences.
  PrefService* pref_service_;

  // Client-side secret used to generate fake bits.
  std::string secret_;

  // The cohort this client is assigned to. -1 is uninitialized.
  int32_t cohort_;

  // Timer which schedules calls to OnLogInterval().
  base::OneShotTimer<RapporService> log_rotation_timer_;

  // A daily event for collecting metrics once a day.
  metrics::DailyEvent daily_event_;

  // A private LogUploader instance for sending reports to the server.
  scoped_ptr<LogUploader> uploader_;

  // What reporting level of metrics are being reported.
  ReportingLevel reporting_level_;

  // We keep all registered metrics in a map, from name to metric.
  // The map owns the metrics it contains.
  std::map<std::string, RapporMetric*> metrics_map_;

  DISALLOW_COPY_AND_ASSIGN(RapporService);
};

}  // namespace rappor

#endif  // COMPONENTS_RAPPOR_RAPPOR_SERVICE_H_
