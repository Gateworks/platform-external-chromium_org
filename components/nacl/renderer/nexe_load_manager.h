// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_NACL_RENDERER_NEXE_LOAD_MANAGER_H_
#define COMPONENTS_NACL_RENDERER_NEXE_LOAD_MANAGER_H_

#include <map>
#include <string>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ppapi/c/private/ppb_nacl_private.h"
#include "url/gurl.h"

namespace content {
class PepperPluginInstance;
}

namespace nacl {

class ManifestServiceChannel;
class TrustedPluginChannel;

// NexeLoadManager provides methods for reporting the progress of loading a
// nexe.
class NexeLoadManager {
 public:
  explicit NexeLoadManager(PP_Instance instance);
  ~NexeLoadManager();

  void NexeFileDidOpen(int32_t pp_error,
                       int32_t fd,
                       int32_t http_status,
                       int64_t nexe_bytes_read,
                       const std::string& url,
                       int64_t time_since_open);
  void ReportLoadSuccess(const std::string& url,
                         uint64_t loaded_bytes,
                         uint64_t total_bytes);
  void ReportLoadError(PP_NaClError error,
                       const std::string& error_message);
  void ReportLoadError(PP_NaClError error,
                       const std::string& error_message,
                       const std::string& console_message);
  void ReportLoadAbort();
  void NexeDidCrash(const char* crash_log);

  // TODO(dmichael): Everything below this comment should eventually be made
  // private, when ppb_nacl_private_impl.cc is no longer using them directly.
  // The intent is for this class to only expose functions for reporting a
  // load state transition (e.g., ReportLoadError, ReportProgress,
  // ReportLoadAbort, etc.)
  struct ProgressEvent {
    explicit ProgressEvent(PP_NaClEventType event_type_param)
        : event_type(event_type_param),
          length_is_computable(false),
          loaded_bytes(0),
          total_bytes(0) {
    }
    ProgressEvent(PP_Instance instance, PP_NaClEventType event_type,
                  const std::string& resource_url, bool length_is_computable,
                  uint64_t loaded_bytes, uint64_t total_bytes)
        : instance(instance),
          event_type(event_type),
          resource_url(resource_url),
          length_is_computable(length_is_computable),
          loaded_bytes(loaded_bytes),
          total_bytes(total_bytes) {
    }
    PP_Instance instance;
    PP_NaClEventType event_type;
    std::string resource_url;
    bool length_is_computable;
    uint64_t loaded_bytes;
    uint64_t total_bytes;
  };
  void DispatchEvent(const ProgressEvent &event);
  void set_trusted_plugin_channel(scoped_ptr<TrustedPluginChannel> channel);
  void set_manifest_service_channel(
      scoped_ptr<ManifestServiceChannel> channel);

  PP_NaClReadyState nacl_ready_state();
  void set_nacl_ready_state(PP_NaClReadyState ready_state);

  void SetReadOnlyProperty(PP_Var key, PP_Var value);
  void SetLastError(const std::string& error);
  void LogToConsole(const std::string& message);

  bool is_installed() const { return is_installed_; }

  int32_t exit_status() const { return exit_status_; }
  void set_exit_status(int32_t exit_status);

  void InitializePlugin(uint32_t argc, const char* argn[], const char* argv[]);

  void ReportStartupOverhead() const;

  int64_t nexe_size() const { return nexe_size_; }

  bool RequestNaClManifest(const std::string& url, bool* is_data_uri);
  void ProcessNaClManifest(const std::string& program_url);

  // URL resolution support.
  // plugin_base_url is the URL used for resolving relative URLs used in
  // src="...".
  const GURL& plugin_base_url() const { return plugin_base_url_; }

  // manifest_base_url is the URL used for resolving relative URLs mentioned
  // in manifest files.  If the manifest is a data URI, this is an empty string
  const GURL& manifest_base_url() const { return manifest_base_url_; }

  // Returns the manifest URL passed as an argument for this plugin instance.
  std::string GetManifestURLArgument() const;

  // Returns true if the MIME type for this plugin matches the type for PNaCl,
  // false otherwise.
  bool IsPNaCl() const;

  // Returns true if dev interfaces are enabled for this plugin.
  bool DevInterfacesEnabled() const;

 private:
  DISALLOW_COPY_AND_ASSIGN(NexeLoadManager);

  void ReportDeadNexe();

  // Copies a crash log to the console, one line at a time.
  void CopyCrashLogToJsConsole(const std::string& crash_log);

  PP_Instance pp_instance_;
  PP_NaClReadyState nacl_ready_state_;
  bool nexe_error_reported_;

  // A flag indicating if the NaCl executable is being loaded from an installed
  // application.  This flag is used to bucket UMA statistics more precisely to
  // help determine whether nexe loading problems are caused by networking
  // issues.  (Installed applications will be loaded from disk.)
  // Unfortunately, the definition of what it means to be part of an installed
  // application is a little murky - for example an installed application can
  // register a mime handler that loads NaCl executables into an arbitrary web
  // page.  As such, the flag actually means "our best guess, based on the URLs
  // for NaCl resources that we have seen so far".
  bool is_installed_;

  // Time of a successful nexe load.
  base::Time ready_time_;

  // Time of plugin initialization.
  base::Time init_time_;

  // Time of the start of loading a NaCl module.
  base::Time load_start_;

  // The exit status of the plugin process.
  // This will have a value in the range (0x00-0xff) if the exit status is set,
  // or -1 if set_exit_status() has never been called.
  int32_t exit_status_;

  // Size of the downloaded nexe, in bytes.
  int64_t nexe_size_;

  // Non-owning.
  content::PepperPluginInstance* plugin_instance_;

  // The URL for the document corresponding to this plugin instance.
  GURL plugin_base_url_;

  GURL manifest_base_url_;

  // Arguments passed to this plugin instance from the DOM.
  std::map<std::string, std::string> args_;

  // We store mime_type_ outside of args_ explicitly because we change it to be
  // lowercase.
  std::string mime_type_;

  scoped_ptr<TrustedPluginChannel> trusted_plugin_channel_;
  scoped_ptr<ManifestServiceChannel> manifest_service_channel_;
  base::WeakPtrFactory<NexeLoadManager> weak_factory_;
};

}  // namespace nacl

#endif  // COMPONENTS_NACL_RENDERER_NEXE_LOAD_MANAGER_H_
