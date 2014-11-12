// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_IME_IME_KEYBOARD_X11_H_
#define CHROMEOS_IME_IME_KEYBOARD_X11_H_

#include "chromeos/ime/ime_keyboard.h"

#include <cstdlib>
#include <cstring>
#include <queue>
#include <set>
#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/process/process_handle.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/sys_info.h"
#include "base/threading/thread_checker.h"
#include "ui/gfx/x/x11_types.h"

// These includes conflict with base/tracked_objects.h so must come last.
#include <X11/XKBlib.h>
#include <X11/Xlib.h>


namespace chromeos {
namespace input_method {

class CHROMEOS_EXPORT ImeKeyboardX11 : public ImeKeyboard {
 public:
  ImeKeyboardX11();
  virtual ~ImeKeyboardX11();

  // ImeKeyboard:
  virtual bool SetCurrentKeyboardLayoutByName(
      const std::string& layout_name) override;
  virtual bool ReapplyCurrentKeyboardLayout() override;
  virtual void ReapplyCurrentModifierLockStatus() override;
  virtual void DisableNumLock() override;
  virtual void SetCapsLockEnabled(bool enable_caps_lock) override;
  virtual bool CapsLockIsEnabled() override;
  virtual bool SetAutoRepeatEnabled(bool enabled) override;
  virtual bool SetAutoRepeatRate(const AutoRepeatRate& rate) override;

 private:
  // Returns a mask for Num Lock (e.g. 1U << 4). Returns 0 on error.
  unsigned int GetNumLockMask();

  // Sets the caps-lock status. Note that calling this function always disables
  // the num-lock.
  void SetLockedModifiers();

  // This function is used by SetLayout() and RemapModifierKeys(). Calls
  // setxkbmap command if needed, and updates the last_full_layout_name_ cache.
  bool SetLayoutInternal(const std::string& layout_name, bool force);

  // Executes 'setxkbmap -layout ...' command asynchronously using a layout name
  // in the |execute_queue_|. Do nothing if the queue is empty.
  // TODO(yusukes): Use libxkbfile.so instead of the command (crosbug.com/13105)
  void MaybeExecuteSetLayoutCommand();

  // Polls to see setxkbmap process exits.
  void PollUntilChildFinish(const base::ProcessHandle handle);

  // Called when execve'd setxkbmap process exits.
  void OnSetLayoutFinish();

  const bool is_running_on_chrome_os_;
  unsigned int num_lock_mask_;

  // A queue for executing setxkbmap one by one.
  std::queue<std::string> execute_queue_;

  base::ThreadChecker thread_checker_;

  base::WeakPtrFactory<ImeKeyboardX11> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ImeKeyboardX11);
};


} // namespace input_method
} // namespace chromeos

#endif  // CHROMEOS_IME_IME_KEYBOARD_X11_H_
