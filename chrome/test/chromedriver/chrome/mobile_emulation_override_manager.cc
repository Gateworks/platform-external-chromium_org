// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/chromedriver/chrome/mobile_emulation_override_manager.h"

#include "base/values.h"
#include "chrome/test/chromedriver/chrome/browser_info.h"
#include "chrome/test/chromedriver/chrome/device_metrics.h"
#include "chrome/test/chromedriver/chrome/devtools_client.h"
#include "chrome/test/chromedriver/chrome/status.h"

MobileEmulationOverrideManager::MobileEmulationOverrideManager(
    DevToolsClient* client,
    const DeviceMetrics* device_metrics,
    const BrowserInfo* browser_info)
    : client_(client),
      overridden_device_metrics_(device_metrics),
      browser_info_(browser_info) {
  if (overridden_device_metrics_)
    client_->AddListener(this);
}

MobileEmulationOverrideManager::~MobileEmulationOverrideManager() {
}

Status MobileEmulationOverrideManager::OnConnected(DevToolsClient* client) {
  return ApplyOverrideIfNeeded();
}

Status MobileEmulationOverrideManager::OnEvent(
    DevToolsClient* client,
    const std::string& method,
    const base::DictionaryValue& params) {
  if (method == "Page.frameNavigated") {
    const base::Value* unused_value;
    if (!params.Get("frame.parentId", &unused_value))
      return ApplyOverrideIfNeeded();
  }
  return Status(kOk);
}

Status MobileEmulationOverrideManager::ApplyOverrideIfNeeded() {
  if (overridden_device_metrics_ == NULL)
    return Status(kOk);

  // Old revisions of Blink expect a parameter named |emulateViewport| but in
  // Blink revision 177367 (Chromium revision 281046, build number 2081) this
  // was renamed to |mobile|.
  std::string mobile_param_name = "mobile";
  if (browser_info_->browser_name == "chrome") {
    if (browser_info_->build_no <= 2081)
      mobile_param_name = "emulateViewport";
  } else {
    if (browser_info_->blink_revision < 177367)
      mobile_param_name = "emulateViewport";
  }

  base::DictionaryValue params;
  params.SetInteger("width", overridden_device_metrics_->width);
  params.SetInteger("height", overridden_device_metrics_->height);
  params.SetDouble("deviceScaleFactor",
                   overridden_device_metrics_->device_scale_factor);
  params.SetBoolean(mobile_param_name, overridden_device_metrics_->mobile);
  params.SetBoolean("fitWindow", overridden_device_metrics_->fit_window);
  params.SetBoolean("textAutosizing",
                    overridden_device_metrics_->text_autosizing);
  params.SetDouble("fontScaleFactor",
                   overridden_device_metrics_->font_scale_factor);
  Status status = client_->SendCommand("Page.setDeviceMetricsOverride", params);
  if (status.IsError())
    return status;

  // Always emulate touch.
  base::DictionaryValue emulate_touch_params;
  emulate_touch_params.SetBoolean("enabled", true);
  return client_->SendCommand(
      "Page.setTouchEmulationEnabled", emulate_touch_params);
}
