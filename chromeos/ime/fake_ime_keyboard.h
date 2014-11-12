// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_IME_FAKE_IME_KEYBOARD_H_
#define CHROMEOS_IME_FAKE_IME_KEYBOARD_H_

#include "chromeos/ime/ime_keyboard.h"

#include <string>

#include "base/compiler_specific.h"
#include "chromeos/chromeos_export.h"

namespace chromeos {
namespace input_method {

class CHROMEOS_EXPORT FakeImeKeyboard : public ImeKeyboard {
 public:
  FakeImeKeyboard();
  virtual ~FakeImeKeyboard();

  virtual bool SetCurrentKeyboardLayoutByName(const std::string& layout_name)
      override;
  virtual bool SetAutoRepeatRate(const AutoRepeatRate& rate) override;
  virtual bool SetAutoRepeatEnabled(bool enabled) override;
  virtual bool ReapplyCurrentKeyboardLayout() override;
  virtual void ReapplyCurrentModifierLockStatus() override;
  virtual void DisableNumLock() override;
  virtual bool IsISOLevel5ShiftAvailable() const override;
  virtual bool IsAltGrAvailable() const override;

  int set_current_keyboard_layout_by_name_count_;
  AutoRepeatRate last_auto_repeat_rate_;
  // TODO(yusukes): Add more variables for counting the numbers of the API calls
  bool auto_repeat_is_enabled_;

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeImeKeyboard);
};

}  // namespace input_method
}  // namespace chromeos

#endif  // CHROMEOS_IME_FAKE_IME_KEYBOARD_H_
