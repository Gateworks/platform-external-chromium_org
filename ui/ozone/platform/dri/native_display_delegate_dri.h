// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_DRI_NATIVE_DISPLAY_DELEGATE_DRI_H_
#define UI_OZONE_PLATFORM_DRI_NATIVE_DISPLAY_DELEGATE_DRI_H_

#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/observer_list.h"
#include "ui/display/types/native_display_delegate.h"
#include "ui/events/ozone/device/device_event_observer.h"

namespace ui {

class DeviceManager;
class DisplaySnapshotDri;
class DriConsoleBuffer;
class DriWrapper;
class ScreenManager;

class NativeDisplayDelegateDri : public NativeDisplayDelegate,
                                 DeviceEventObserver {
 public:
  NativeDisplayDelegateDri(DriWrapper* dri,
                           ScreenManager* screen_manager,
                           DeviceManager* device_manager);
  virtual ~NativeDisplayDelegateDri();

  DisplaySnapshot* FindDisplaySnapshot(int64_t id);
  const DisplayMode* FindDisplayMode(const gfx::Size& size,
                                     bool is_interlaced,
                                     float refresh_rate);

  // NativeDisplayDelegate overrides:
  virtual void Initialize() override;
  virtual void GrabServer() override;
  virtual void UngrabServer() override;
  virtual void SyncWithServer() override;
  virtual void SetBackgroundColor(uint32_t color_argb) override;
  virtual void ForceDPMSOn() override;
  virtual std::vector<DisplaySnapshot*> GetDisplays() override;
  virtual void AddMode(const DisplaySnapshot& output,
                       const DisplayMode* mode) override;
  virtual bool Configure(const DisplaySnapshot& output,
                         const DisplayMode* mode,
                         const gfx::Point& origin) override;
  virtual void CreateFrameBuffer(const gfx::Size& size) override;
  virtual bool GetHDCPState(const DisplaySnapshot& output,
                            HDCPState* state) override;
  virtual bool SetHDCPState(const DisplaySnapshot& output,
                            HDCPState state) override;
  virtual std::vector<ui::ColorCalibrationProfile>
      GetAvailableColorCalibrationProfiles(
          const ui::DisplaySnapshot& output) override;
  virtual bool SetColorCalibrationProfile(
      const ui::DisplaySnapshot& output,
      ui::ColorCalibrationProfile new_profile) override;
  virtual void AddObserver(NativeDisplayObserver* observer) override;
  virtual void RemoveObserver(NativeDisplayObserver* observer) override;

  // DeviceEventObserver overrides:
  virtual void OnDeviceEvent(const DeviceEvent& event) override;

 private:
  // Notify ScreenManager of all the displays that were present before the
  // update but are gone after the update.
  void NotifyScreenManager(
      const std::vector<DisplaySnapshotDri*>& new_displays,
      const std::vector<DisplaySnapshotDri*>& old_displays) const;

  DriWrapper* dri_;                // Not owned.
  ScreenManager* screen_manager_;  // Not owned.
  DeviceManager* device_manager_;  // Not owned.
  scoped_ptr<DriConsoleBuffer> console_buffer_;
  // Modes can be shared between different displays, so we need to keep track
  // of them independently for cleanup.
  ScopedVector<const DisplayMode> cached_modes_;
  ScopedVector<DisplaySnapshotDri> cached_displays_;
  ObserverList<NativeDisplayObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(NativeDisplayDelegateDri);
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_DRI_NATIVE_DISPLAY_DELEGATE_DRI_H_
