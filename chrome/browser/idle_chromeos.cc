// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/idle.h"

#include "ash/session/session_state_delegate.h"
#include "ash/shell.h"
#include "base/time/time.h"
#include "ui/wm/core/user_activity_detector.h"

void CalculateIdleTime(IdleTimeCallback notify) {
  base::TimeDelta idle_time = base::TimeTicks::Now() -
      wm::UserActivityDetector::Get()->last_activity_time();
  notify.Run(static_cast<int>(idle_time.InSeconds()));
}

bool CheckIdleStateIsLocked() {
#if defined(USE_ATHENA)
  return false;
#else
  return ash::Shell::GetInstance()->session_state_delegate()->IsScreenLocked();
#endif
}
