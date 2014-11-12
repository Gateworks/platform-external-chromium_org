// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/ime/ime_keyboard_ozone.h"

namespace chromeos {
namespace input_method {


ImeKeyboardOzone::ImeKeyboardOzone() {
}


ImeKeyboardOzone::~ImeKeyboardOzone() {
}

bool ImeKeyboardOzone::SetCurrentKeyboardLayoutByName(
    const std::string& layout_name) {
  // Call SetKeyMapping here.
  // TODO: parse out layout name and variation.
  last_layout_ = layout_name;
  return true;
}

bool ImeKeyboardOzone::ReapplyCurrentKeyboardLayout() {
  return SetCurrentKeyboardLayoutByName(last_layout_);
}

void ImeKeyboardOzone::SetCapsLockEnabled(bool enable_caps_lock) {
  // Call SetModifierStates here.
  ImeKeyboard::SetCapsLockEnabled(enable_caps_lock);
}

bool ImeKeyboardOzone::CapsLockIsEnabled() {
  // Call getModifierStates here.
  return ImeKeyboard::CapsLockIsEnabled();
}

void ImeKeyboardOzone::ReapplyCurrentModifierLockStatus() {
  // call SetModifierStates here.
}

void ImeKeyboardOzone::DisableNumLock() {
}

bool ImeKeyboardOzone::SetAutoRepeatRate(const AutoRepeatRate& rate) {
  return true;
}

bool ImeKeyboardOzone::SetAutoRepeatEnabled(bool enabled) {
  return true;
}

// static
ImeKeyboard* ImeKeyboard::Create() { return new ImeKeyboardOzone(); }

}  // namespace input_method
}  // namespace chromeos
