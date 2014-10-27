// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_SHELL_BROWSER_SHELL_AUDIO_CONTROLLER_CHROMEOS_H_
#define EXTENSIONS_SHELL_BROWSER_SHELL_AUDIO_CONTROLLER_CHROMEOS_H_

#include "base/macros.h"
#include "chromeos/audio/audio_devices_pref_handler.h"
#include "chromeos/audio/cras_audio_handler.h"

namespace chromeos {
class AudioDevicesPrefHandler;
}

namespace extensions {

// Ensures that the "best" input and output audio devices are always active.
class ShellAudioController : public chromeos::CrasAudioHandler::AudioObserver {
 public:
  // Requests max volume for all devices and doesn't bother saving prefs.
  class PrefHandler : public chromeos::AudioDevicesPrefHandler {
   public:
    PrefHandler();

    // chromeos::AudioDevicesPrefHandler implementation:
    virtual double GetOutputVolumeValue(
        const chromeos::AudioDevice* device) override;
    virtual double GetInputGainValue(
        const chromeos::AudioDevice* device) override;
    virtual void SetVolumeGainValue(const chromeos::AudioDevice& device,
                                    double value) override;
    virtual bool GetMuteValue(const chromeos::AudioDevice& device) override;
    virtual void SetMuteValue(const chromeos::AudioDevice& device,
                              bool mute_on) override;
    virtual bool GetAudioCaptureAllowedValue() override;
    virtual bool GetAudioOutputAllowedValue() override;
    virtual void AddAudioPrefObserver(
        chromeos::AudioPrefObserver* observer) override;
    virtual void RemoveAudioPrefObserver(
        chromeos::AudioPrefObserver* observer) override;

   protected:
    virtual ~PrefHandler();

   private:
    DISALLOW_COPY_AND_ASSIGN(PrefHandler);
  };

  ShellAudioController();
  virtual ~ShellAudioController();

  // chromeos::CrasAudioHandler::Observer implementation:
  virtual void OnOutputVolumeChanged() override;
  virtual void OnOutputMuteChanged() override;
  virtual void OnInputGainChanged() override;
  virtual void OnInputMuteChanged() override;
  virtual void OnAudioNodesChanged() override;
  virtual void OnActiveOutputNodeChanged() override;
  virtual void OnActiveInputNodeChanged() override;

 private:
  // Gets the current device list from CRAS, chooses the best input and output
  // device, and activates them if they aren't already active.
  void ActivateDevices();

  DISALLOW_COPY_AND_ASSIGN(ShellAudioController);
};

}  // namespace extensions

#endif  // EXTENSIONS_SHELL_BROWSER_SHELL_AUDIO_CONTROLLER_CHROMEOS_H_
