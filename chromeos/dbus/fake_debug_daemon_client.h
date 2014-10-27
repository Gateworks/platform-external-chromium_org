// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_FAKE_DEBUG_DAEMON_CLIENT_H_
#define CHROMEOS_DBUS_FAKE_DEBUG_DAEMON_CLIENT_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chromeos/dbus/debug_daemon_client.h"

namespace chromeos {

// The DebugDaemonClient implementation used on Linux desktop,
// which does nothing.
class CHROMEOS_EXPORT FakeDebugDaemonClient : public DebugDaemonClient {
 public:
  FakeDebugDaemonClient();
  virtual ~FakeDebugDaemonClient();

  virtual void Init(dbus::Bus* bus) override;
  virtual void DumpDebugLogs(bool is_compressed,
                             base::File file,
                             scoped_refptr<base::TaskRunner> task_runner,
                             const GetDebugLogsCallback& callback) override;
  virtual void SetDebugMode(const std::string& subsystem,
                            const SetDebugModeCallback& callback) override;
  virtual void StartSystemTracing() override;
  virtual bool RequestStopSystemTracing(
      scoped_refptr<base::TaskRunner> task_runner,
      const StopSystemTracingCallback& callback) override;
  virtual void GetRoutes(bool numeric,
                         bool ipv6,
                         const GetRoutesCallback& callback) override;
  virtual void GetNetworkStatus(const GetNetworkStatusCallback& callback)
      override;
  virtual void GetModemStatus(const GetModemStatusCallback& callback) override;
  virtual void GetWiMaxStatus(const GetWiMaxStatusCallback& callback) override;
  virtual void GetNetworkInterfaces(
      const GetNetworkInterfacesCallback& callback) override;
  virtual void GetPerfData(uint32_t duration,
                           const GetPerfDataCallback& callback) override;
  virtual void GetScrubbedLogs(const GetLogsCallback& callback) override;
  virtual void GetAllLogs(const GetLogsCallback& callback) override;
  virtual void GetUserLogFiles(const GetLogsCallback& callback) override;
  virtual void TestICMP(const std::string& ip_address,
                        const TestICMPCallback& callback) override;
  virtual void TestICMPWithOptions(
      const std::string& ip_address,
      const std::map<std::string, std::string>& options,
      const TestICMPCallback& callback) override;
  virtual void UploadCrashes() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeDebugDaemonClient);
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_FAKE_DEBUG_DAEMON_CLIENT_H_
