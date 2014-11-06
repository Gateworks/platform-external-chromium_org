// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/maximize_mode/maximize_mode_controller.h"

#include "ash/accelerators/accelerator_controller.h"
#include "ash/accelerators/accelerator_table.h"
#include "ash/accelerometer/accelerometer_controller.h"
#include "ash/ash_switches.h"
#include "ash/display/display_manager.h"
#include "ash/shell.h"
#include "ash/wm/maximize_mode/maximize_mode_window_manager.h"
#include "ash/wm/maximize_mode/scoped_disable_internal_mouse_and_keyboard.h"
#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/metrics/histogram.h"
#include "base/time/default_tick_clock.h"
#include "base/time/tick_clock.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/vector3d_f.h"

#if defined(USE_X11)
#include "ash/wm/maximize_mode/scoped_disable_internal_mouse_and_keyboard_x11.h"
#endif

#if defined(OS_CHROMEOS)
#include "chromeos/dbus/dbus_thread_manager.h"
#endif  // OS_CHROMEOS

namespace ash {

namespace {

// The hinge angle at which to enter maximize mode.
const float kEnterMaximizeModeAngle = 200.0f;

// The angle at which to exit maximize mode, this is specifically less than the
// angle to enter maximize mode to prevent rapid toggling when near the angle.
const float kExitMaximizeModeAngle = 160.0f;

// Defines a range for which accelerometer readings are considered accurate.
// When the lid is near open (or near closed) the accelerometer readings may be
// inaccurate and a lid that is fully open may appear to be near closed (and
// vice versa).
const float kMinStableAngle = 20.0f;
const float kMaxStableAngle = 340.0f;

// The time duration to consider the lid to be recently opened.
// This is used to prevent entering maximize mode if an erroneous accelerometer
// reading makes the lid appear to be fully open when the user is opening the
// lid from a closed position.
const base::TimeDelta kLidRecentlyOpenedDuration =
    base::TimeDelta::FromSeconds(2);

// The mean acceleration due to gravity on Earth in m/s^2.
const float kMeanGravity = 9.80665f;

// When the device approaches vertical orientation (i.e. portrait orientation)
// the accelerometers for the base and lid approach the same values (i.e.
// gravity pointing in the direction of the hinge). When this happens we cannot
// compute the hinge angle reliably and must turn ignore accelerometer readings.
// This is the minimum acceleration perpendicular to the hinge under which to
// detect hinge angle in m/s^2.
const float kHingeAngleDetectionThreshold = 2.5f;

// The maximum deviation from the acceleration expected due to gravity under
// which to detect hinge angle and screen rotation in m/s^2
const float kDeviationFromGravityThreshold = 1.0f;

// The maximum deviation between the magnitude of the two accelerometers under
// which to detect hinge angle and screen rotation in m/s^2. These
// accelerometers are attached to the same physical device and so should be
// under the same acceleration.
const float kNoisyMagnitudeDeviation = 1.0f;

// The angle which the screen has to be rotated past before the display will
// rotate to match it (i.e. 45.0f is no stickiness).
const float kDisplayRotationStickyAngleDegrees = 60.0f;

// The minimum acceleration in m/s^2 in a direction required to trigger screen
// rotation. This prevents rapid toggling of rotation when the device is near
// flat and there is very little screen aligned force on it. The value is
// effectively the sine of the rise angle required times the acceleration due
// to gravity, with the current value requiring at least a 25 degree rise.
const float kMinimumAccelerationScreenRotation = 4.2f;

const float kRadiansToDegrees = 180.0f / 3.14159265f;

// Returns the angle between |base| and |other| in degrees.
float AngleBetweenVectorsInDegrees(const gfx::Vector3dF& base,
                                 const gfx::Vector3dF& other) {
  return acos(gfx::DotProduct(base, other) /
              base.Length() / other.Length()) * kRadiansToDegrees;
}

// Returns the clockwise angle between |base| and |other| where |normal| is the
// normal of the virtual surface to measure clockwise according to.
float ClockwiseAngleBetweenVectorsInDegrees(const gfx::Vector3dF& base,
                                            const gfx::Vector3dF& other,
                                            const gfx::Vector3dF& normal) {
  float angle = AngleBetweenVectorsInDegrees(base, other);
  gfx::Vector3dF cross(base);
  cross.Cross(other);
  // If the dot product of this cross product is normal, it means that the
  // shortest angle between |base| and |other| was counterclockwise with respect
  // to the surface represented by |normal| and this angle must be reversed.
  if (gfx::DotProduct(cross, normal) > 0.0f)
    angle = 360.0f - angle;
  return angle;
}

}  // namespace

MaximizeModeController::MaximizeModeController()
    : rotation_locked_(false),
      have_seen_accelerometer_data_(false),
      ignore_display_configuration_updates_(false),
      lid_open_past_180_(false),
      shutting_down_(false),
      user_rotation_(gfx::Display::ROTATE_0),
      last_touchview_transition_time_(base::Time::Now()),
      tick_clock_(new base::DefaultTickClock()),
      lid_is_closed_(false) {
  Shell::GetInstance()->accelerometer_controller()->AddObserver(this);
  Shell::GetInstance()->AddShellObserver(this);
#if defined(OS_CHROMEOS)
  chromeos::DBusThreadManager::Get()->
      GetPowerManagerClient()->AddObserver(this);
#endif  // OS_CHROMEOS
}

MaximizeModeController::~MaximizeModeController() {
  Shell::GetInstance()->RemoveShellObserver(this);
  Shell::GetInstance()->accelerometer_controller()->RemoveObserver(this);
#if defined(OS_CHROMEOS)
  chromeos::DBusThreadManager::Get()->
      GetPowerManagerClient()->RemoveObserver(this);
#endif  // OS_CHROMEOS
}

void MaximizeModeController::SetRotationLocked(bool rotation_locked) {
  if (rotation_locked_ == rotation_locked)
    return;
  base::AutoReset<bool> auto_ignore_display_configuration_updates(
      &ignore_display_configuration_updates_, true);
  rotation_locked_ = rotation_locked;
  Shell::GetInstance()->display_manager()->
      RegisterDisplayRotationProperties(rotation_locked_, current_rotation_);
  FOR_EACH_OBSERVER(Observer, observers_,
                    OnRotationLockChanged(rotation_locked_));
}

void MaximizeModeController::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void MaximizeModeController::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool MaximizeModeController::CanEnterMaximizeMode() {
  // If we have ever seen accelerometer data, then HandleHingeRotation may
  // trigger maximize mode at some point in the future.
  // The --enable-touch-view-testing switch can also mean that we may enter
  // maximize mode.
  return have_seen_accelerometer_data_ ||
         base::CommandLine::ForCurrentProcess()->HasSwitch(
             switches::kAshEnableTouchViewTesting);
}

void MaximizeModeController::EnableMaximizeModeWindowManager(bool enable) {
  if (enable && !maximize_mode_window_manager_.get()) {
    maximize_mode_window_manager_.reset(new MaximizeModeWindowManager());
    // TODO(jonross): Move the maximize mode notifications from ShellObserver
    // to MaximizeModeController::Observer
    Shell::GetInstance()->OnMaximizeModeStarted();
  } else if (!enable && maximize_mode_window_manager_.get()) {
    maximize_mode_window_manager_.reset();
    Shell::GetInstance()->OnMaximizeModeEnded();
  }
}

bool MaximizeModeController::IsMaximizeModeWindowManagerEnabled() const {
  return maximize_mode_window_manager_.get() != NULL;
}

void MaximizeModeController::AddWindow(aura::Window* window) {
  if (IsMaximizeModeWindowManagerEnabled())
    maximize_mode_window_manager_->AddWindow(window);
}

void MaximizeModeController::Shutdown() {
  shutting_down_ = true;
  LeaveMaximizeMode();
}

void MaximizeModeController::OnAccelerometerUpdated(
    const ui::AccelerometerUpdate& update) {
  bool first_accelerometer_update = !have_seen_accelerometer_data_;
  have_seen_accelerometer_data_ = true;

  // Ignore the reading if it appears unstable. The reading is considered
  // unstable if it deviates too much from gravity and/or the magnitude of the
  // reading from the lid differs too much from the reading from the base.
  float base_magnitude =
      update.has(ui::ACCELEROMETER_SOURCE_ATTACHED_KEYBOARD) ?
      update.get(ui::ACCELEROMETER_SOURCE_ATTACHED_KEYBOARD).Length() :
      0.0f;
  float lid_magnitude = update.has(ui::ACCELEROMETER_SOURCE_SCREEN) ?
      update.get(ui::ACCELEROMETER_SOURCE_SCREEN).Length() : 0.0f;
  bool lid_stable = update.has(ui::ACCELEROMETER_SOURCE_SCREEN) &&
      std::abs(lid_magnitude - kMeanGravity) <= kDeviationFromGravityThreshold;
  bool base_angle_stable = lid_stable &&
      update.has(ui::ACCELEROMETER_SOURCE_ATTACHED_KEYBOARD) &&
      std::abs(base_magnitude - lid_magnitude) <= kNoisyMagnitudeDeviation &&
      std::abs(base_magnitude - kMeanGravity) <= kDeviationFromGravityThreshold;

  if (base_angle_stable) {
    // Responding to the hinge rotation can change the maximize mode state which
    // affects screen rotation, so we handle hinge rotation first.
    HandleHingeRotation(
        update.get(ui::ACCELEROMETER_SOURCE_ATTACHED_KEYBOARD),
        update.get(ui::ACCELEROMETER_SOURCE_SCREEN));
  }
  if (lid_stable)
    HandleScreenRotation(update.get(ui::ACCELEROMETER_SOURCE_SCREEN));

  if (first_accelerometer_update) {
    // On the first accelerometer update we will know if we have entered
    // maximize mode or not. Update the preferences to reflect the current
    // state.
    Shell::GetInstance()->display_manager()->
        RegisterDisplayRotationProperties(rotation_locked_, current_rotation_);
  }
}

void MaximizeModeController::OnDisplayConfigurationChanged() {
  if (ignore_display_configuration_updates_)
    return;
  DisplayManager* display_manager = Shell::GetInstance()->display_manager();
  gfx::Display::Rotation user_rotation = display_manager->
      GetDisplayInfo(gfx::Display::InternalDisplayId()).rotation();
  if (user_rotation != current_rotation_) {
    // A user may change other display configuration settings. When the user
    // does change the rotation setting, then lock rotation to prevent the
    // accelerometer from erasing their change.
    SetRotationLocked(true);
    user_rotation_ = user_rotation;
    current_rotation_ = user_rotation;
  }
}

#if defined(OS_CHROMEOS)
void MaximizeModeController::LidEventReceived(bool open,
                                              const base::TimeTicks& time) {
  if (open)
    last_lid_open_time_ = time;
  lid_is_closed_ = !open;
  LeaveMaximizeMode();
}

void MaximizeModeController::SuspendImminent() {
  RecordTouchViewStateTransition();
}

void MaximizeModeController::SuspendDone(
    const base::TimeDelta& sleep_duration) {
  last_touchview_transition_time_ = base::Time::Now();
}
#endif  // OS_CHROMEOS

void MaximizeModeController::HandleHingeRotation(const gfx::Vector3dF& base,
                                                 const gfx::Vector3dF& lid) {
  static const gfx::Vector3dF hinge_vector(1.0f, 0.0f, 0.0f);
  // Ignore the component of acceleration parallel to the hinge for the purposes
  // of hinge angle calculation.
  gfx::Vector3dF base_flattened(base);
  gfx::Vector3dF lid_flattened(lid);
  base_flattened.set_x(0.0f);
  lid_flattened.set_x(0.0f);

  // As the hinge approaches a vertical angle, the base and lid accelerometers
  // approach the same values making any angle calculations highly inaccurate.
  // Bail out early when it is too close.
  if (base_flattened.Length() < kHingeAngleDetectionThreshold ||
      lid_flattened.Length() < kHingeAngleDetectionThreshold) {
    return;
  }

  // Compute the angle between the base and the lid.
  float lid_angle = 180.0f - ClockwiseAngleBetweenVectorsInDegrees(
      base_flattened, lid_flattened, hinge_vector);
  if (lid_angle < 0.0f)
    lid_angle += 360.0f;

  bool is_angle_stable = lid_angle >= kMinStableAngle &&
                         lid_angle <= kMaxStableAngle;

  // Clear the last_lid_open_time_ for a stable reading so that there is less
  // chance of a delay if the lid is moved from the close state to the fully
  // open state very quickly.
  if (is_angle_stable)
    last_lid_open_time_ = base::TimeTicks();

  // Toggle maximize mode on or off when corresponding thresholds are passed.
  if (lid_open_past_180_ && is_angle_stable &&
      lid_angle <= kExitMaximizeModeAngle) {
    lid_open_past_180_ = false;
    if (!base::CommandLine::ForCurrentProcess()->
            HasSwitch(switches::kAshEnableTouchViewTesting)) {
      LeaveMaximizeMode();
    }
    event_blocker_.reset();
  } else if (!lid_open_past_180_ && !lid_is_closed_ &&
             lid_angle >= kEnterMaximizeModeAngle &&
             (is_angle_stable || !WasLidOpenedRecently())) {
    lid_open_past_180_ = true;
    if (!base::CommandLine::ForCurrentProcess()->
            HasSwitch(switches::kAshEnableTouchViewTesting)) {
      EnterMaximizeMode();
    }
#if defined(USE_X11)
    event_blocker_.reset(new ScopedDisableInternalMouseAndKeyboardX11);
#endif
  }
}

void MaximizeModeController::HandleScreenRotation(const gfx::Vector3dF& lid) {
  bool maximize_mode_engaged = IsMaximizeModeWindowManagerEnabled();

  // TODO(jonross): track the updated rotation angle even when locked. So that
  // when rotation lock is removed the accelerometer rotation can be applied
  // without waiting for the next update.
  if (!maximize_mode_engaged || rotation_locked_)
    return;

  DisplayManager* display_manager =
      Shell::GetInstance()->display_manager();
  gfx::Display::Rotation current_rotation = display_manager->GetDisplayInfo(
      gfx::Display::InternalDisplayId()).rotation();

  // After determining maximize mode state, determine if the screen should
  // be rotated.
  gfx::Vector3dF lid_flattened(lid.x(), lid.y(), 0.0f);
  float lid_flattened_length = lid_flattened.Length();
  // When the lid is close to being flat, don't change rotation as it is too
  // sensitive to slight movements.
  if (lid_flattened_length < kMinimumAccelerationScreenRotation)
    return;

  // The reference vector is the angle of gravity when the device is rotated
  // clockwise by 45 degrees. Computing the angle between this vector and
  // gravity we can easily determine the expected display rotation.
  static const gfx::Vector3dF rotation_reference(-1.0f, -1.0f, 0.0f);

  // Set the down vector to match the expected direction of gravity given the
  // last configured rotation. This is used to enforce a stickiness that the
  // user must overcome to rotate the display and prevents frequent rotations
  // when holding the device near 45 degrees.
  gfx::Vector3dF down(0.0f, 0.0f, 0.0f);
  if (current_rotation == gfx::Display::ROTATE_0)
    down.set_y(-1.0f);
  else if (current_rotation == gfx::Display::ROTATE_90)
    down.set_x(-1.0f);
  else if (current_rotation == gfx::Display::ROTATE_180)
    down.set_y(1.0f);
  else
    down.set_x(1.0f);

  // Don't rotate if the screen has not passed the threshold.
  if (AngleBetweenVectorsInDegrees(down, lid_flattened) <
      kDisplayRotationStickyAngleDegrees) {
    return;
  }

  float angle = ClockwiseAngleBetweenVectorsInDegrees(rotation_reference,
      lid_flattened, gfx::Vector3dF(0.0f, 0.0f, -1.0f));

  gfx::Display::Rotation new_rotation = gfx::Display::ROTATE_90;
  if (angle < 90.0f)
    new_rotation = gfx::Display::ROTATE_0;
  else if (angle < 180.0f)
    new_rotation = gfx::Display::ROTATE_270;
  else if (angle < 270.0f)
    new_rotation = gfx::Display::ROTATE_180;

  if (new_rotation != current_rotation)
    SetDisplayRotation(display_manager, new_rotation);
}

void MaximizeModeController::SetDisplayRotation(
    DisplayManager* display_manager,
    gfx::Display::Rotation rotation) {
  base::AutoReset<bool> auto_ignore_display_configuration_updates(
      &ignore_display_configuration_updates_, true);
  current_rotation_ = rotation;
  display_manager->SetDisplayRotation(gfx::Display::InternalDisplayId(),
                                      rotation);
}

void MaximizeModeController::EnterMaximizeMode() {
  if (IsMaximizeModeWindowManagerEnabled())
    return;
  DisplayManager* display_manager = Shell::GetInstance()->display_manager();
  if (display_manager->HasInternalDisplay()) {
    current_rotation_ = user_rotation_ = display_manager->
        GetDisplayInfo(gfx::Display::InternalDisplayId()).rotation();
    LoadDisplayRotationProperties();
  }
  EnableMaximizeModeWindowManager(true);
  Shell::GetInstance()->display_controller()->AddObserver(this);
}

void MaximizeModeController::LeaveMaximizeMode() {
  if (!IsMaximizeModeWindowManagerEnabled())
    return;
  DisplayManager* display_manager = Shell::GetInstance()->display_manager();
  if (display_manager->HasInternalDisplay()) {
    gfx::Display::Rotation current_rotation = display_manager->
        GetDisplayInfo(gfx::Display::InternalDisplayId()).rotation();
    if (current_rotation != user_rotation_)
      SetDisplayRotation(display_manager, user_rotation_);
  }
  if (!shutting_down_)
    SetRotationLocked(false);
  EnableMaximizeModeWindowManager(false);
  Shell::GetInstance()->display_controller()->RemoveObserver(this);
}

// Called after maximize mode has started, windows might still animate though.
void MaximizeModeController::OnMaximizeModeStarted() {
  RecordTouchViewStateTransition();
}

// Called after maximize mode has ended, windows might still be returning to
// their original position.
void MaximizeModeController::OnMaximizeModeEnded() {
  RecordTouchViewStateTransition();
}

void MaximizeModeController::RecordTouchViewStateTransition() {
  if (CanEnterMaximizeMode()) {
    base::Time current_time = base::Time::Now();
    base::TimeDelta delta = current_time - last_touchview_transition_time_;
    if (IsMaximizeModeWindowManagerEnabled()) {
      UMA_HISTOGRAM_LONG_TIMES("Ash.TouchView.TouchViewInactive", delta);
      total_non_touchview_time_ += delta;
    } else {
      UMA_HISTOGRAM_LONG_TIMES("Ash.TouchView.TouchViewActive", delta);
      total_touchview_time_ += delta;
    }
    last_touchview_transition_time_ = current_time;
  }
}

void MaximizeModeController::LoadDisplayRotationProperties() {
  DisplayManager* display_manager = Shell::GetInstance()->display_manager();
  if (!display_manager->registered_internal_display_rotation_lock())
    return;

  SetDisplayRotation(display_manager,
                     display_manager->registered_internal_display_rotation());
  SetRotationLocked(true);
}

void MaximizeModeController::OnAppTerminating() {
  if (CanEnterMaximizeMode()) {
    RecordTouchViewStateTransition();
    UMA_HISTOGRAM_CUSTOM_COUNTS("Ash.TouchView.TouchViewActiveTotal",
        total_touchview_time_.InMinutes(),
        1, base::TimeDelta::FromDays(7).InMinutes(), 50);
    UMA_HISTOGRAM_CUSTOM_COUNTS("Ash.TouchView.TouchViewInactiveTotal",
        total_non_touchview_time_.InMinutes(),
        1, base::TimeDelta::FromDays(7).InMinutes(), 50);
    base::TimeDelta total_runtime = total_touchview_time_ +
        total_non_touchview_time_;
    if (total_runtime.InSeconds() > 0) {
      UMA_HISTOGRAM_PERCENTAGE("Ash.TouchView.TouchViewActivePercentage",
          100 * total_touchview_time_.InSeconds() / total_runtime.InSeconds());
    }
  }
  Shell::GetInstance()->display_controller()->RemoveObserver(this);
}

bool MaximizeModeController::WasLidOpenedRecently() const {
  if (last_lid_open_time_.is_null())
    return false;

  base::TimeTicks now = tick_clock_->NowTicks();
  DCHECK(now >= last_lid_open_time_);
  base::TimeDelta elapsed_time = now - last_lid_open_time_;
  return elapsed_time <= kLidRecentlyOpenedDuration;
}

void MaximizeModeController::SetTickClockForTest(
    scoped_ptr<base::TickClock> tick_clock) {
  DCHECK(tick_clock_);
  tick_clock_ = tick_clock.Pass();
}

}  // namespace ash
