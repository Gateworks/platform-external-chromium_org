// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/accelerators/accelerator_controller.h"

#include "ash/accelerators/accelerator_table.h"
#include "ash/accessibility_delegate.h"
#include "ash/ash_switches.h"
#include "ash/display/display_manager.h"
#include "ash/ime_control_delegate.h"
#include "ash/screen_util.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/system/brightness_control_delegate.h"
#include "ash/system/keyboard_brightness/keyboard_brightness_control_delegate.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/display_manager_test_api.h"
#include "ash/test/test_screenshot_delegate.h"
#include "ash/test/test_session_state_animator.h"
#include "ash/test/test_shelf_delegate.h"
#include "ash/test/test_shell_delegate.h"
#include "ash/test/test_volume_control_delegate.h"
#include "ash/volume_control_delegate.h"
#include "ash/wm/lock_state_controller.h"
#include "ash/wm/panels/panel_layout_manager.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm/wm_event.h"
#include "base/command_line.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/test/test_window_delegate.h"
#include "ui/aura/test/test_windows.h"
#include "ui/aura/window.h"
#include "ui/events/event.h"
#include "ui/events/event_processor.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/screen.h"
#include "ui/views/widget/widget.h"

#if defined(USE_X11)
#include <X11/Xlib.h>
#include "ui/events/test/events_test_utils_x11.h"
#endif

namespace ash {

namespace {

class TestTarget : public ui::AcceleratorTarget {
 public:
  TestTarget() : accelerator_pressed_count_(0), accelerator_repeat_count_(0) {}
  ~TestTarget() override {}

  int accelerator_pressed_count() const {
    return accelerator_pressed_count_;
  }

  int accelerator_repeat_count() const { return accelerator_repeat_count_; }

  void reset() {
    accelerator_pressed_count_ = 0;
    accelerator_repeat_count_ = 0;
  }

  // Overridden from ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

 private:
  int accelerator_pressed_count_;
  int accelerator_repeat_count_;

  DISALLOW_COPY_AND_ASSIGN(TestTarget);
};

class ReleaseAccelerator : public ui::Accelerator {
 public:
  ReleaseAccelerator(ui::KeyboardCode keycode, int modifiers)
      : ui::Accelerator(keycode, modifiers) {
    set_type(ui::ET_KEY_RELEASED);
  }
};

class DummyBrightnessControlDelegate : public BrightnessControlDelegate {
 public:
  explicit DummyBrightnessControlDelegate(bool consume)
      : consume_(consume),
        handle_brightness_down_count_(0),
        handle_brightness_up_count_(0) {
  }
  ~DummyBrightnessControlDelegate() override {}

  bool HandleBrightnessDown(const ui::Accelerator& accelerator) override {
    ++handle_brightness_down_count_;
    last_accelerator_ = accelerator;
    return consume_;
  }
  bool HandleBrightnessUp(const ui::Accelerator& accelerator) override {
    ++handle_brightness_up_count_;
    last_accelerator_ = accelerator;
    return consume_;
  }
  void SetBrightnessPercent(double percent, bool gradual) override {}
  void GetBrightnessPercent(
      const base::Callback<void(double)>& callback) override {
    callback.Run(100.0);
  }

  int handle_brightness_down_count() const {
    return handle_brightness_down_count_;
  }
  int handle_brightness_up_count() const {
    return handle_brightness_up_count_;
  }
  const ui::Accelerator& last_accelerator() const {
    return last_accelerator_;
  }

 private:
  const bool consume_;
  int handle_brightness_down_count_;
  int handle_brightness_up_count_;
  ui::Accelerator last_accelerator_;

  DISALLOW_COPY_AND_ASSIGN(DummyBrightnessControlDelegate);
};

class DummyImeControlDelegate : public ImeControlDelegate {
 public:
  explicit DummyImeControlDelegate(bool consume)
      : consume_(consume),
        handle_next_ime_count_(0),
        handle_previous_ime_count_(0),
        handle_switch_ime_count_(0) {
  }
  ~DummyImeControlDelegate() override {}

  void HandleNextIme() override { ++handle_next_ime_count_; }
  bool HandlePreviousIme(const ui::Accelerator& accelerator) override {
    ++handle_previous_ime_count_;
    last_accelerator_ = accelerator;
    return consume_;
  }
  bool HandleSwitchIme(const ui::Accelerator& accelerator) override {
    ++handle_switch_ime_count_;
    last_accelerator_ = accelerator;
    return consume_;
  }

  int handle_next_ime_count() const {
    return handle_next_ime_count_;
  }
  int handle_previous_ime_count() const {
    return handle_previous_ime_count_;
  }
  int handle_switch_ime_count() const {
    return handle_switch_ime_count_;
  }
  const ui::Accelerator& last_accelerator() const {
    return last_accelerator_;
  }
  ui::Accelerator RemapAccelerator(
      const ui::Accelerator& accelerator) override {
    return ui::Accelerator(accelerator);
  }

 private:
  const bool consume_;
  int handle_next_ime_count_;
  int handle_previous_ime_count_;
  int handle_switch_ime_count_;
  ui::Accelerator last_accelerator_;

  DISALLOW_COPY_AND_ASSIGN(DummyImeControlDelegate);
};

class DummyKeyboardBrightnessControlDelegate
    : public KeyboardBrightnessControlDelegate {
 public:
  explicit DummyKeyboardBrightnessControlDelegate(bool consume)
      : consume_(consume),
        handle_keyboard_brightness_down_count_(0),
        handle_keyboard_brightness_up_count_(0) {
  }
  ~DummyKeyboardBrightnessControlDelegate() override {}

  bool HandleKeyboardBrightnessDown(
      const ui::Accelerator& accelerator) override {
    ++handle_keyboard_brightness_down_count_;
    last_accelerator_ = accelerator;
    return consume_;
  }

  bool HandleKeyboardBrightnessUp(const ui::Accelerator& accelerator) override {
    ++handle_keyboard_brightness_up_count_;
    last_accelerator_ = accelerator;
    return consume_;
  }

  int handle_keyboard_brightness_down_count() const {
    return handle_keyboard_brightness_down_count_;
  }

  int handle_keyboard_brightness_up_count() const {
    return handle_keyboard_brightness_up_count_;
  }

  const ui::Accelerator& last_accelerator() const {
    return last_accelerator_;
  }

 private:
  const bool consume_;
  int handle_keyboard_brightness_down_count_;
  int handle_keyboard_brightness_up_count_;
  ui::Accelerator last_accelerator_;

  DISALLOW_COPY_AND_ASSIGN(DummyKeyboardBrightnessControlDelegate);
};

bool TestTarget::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (accelerator.IsRepeat())
    ++accelerator_repeat_count_;
  else
    ++accelerator_pressed_count_;
  return true;
}

bool TestTarget::CanHandleAccelerators() const {
  return true;
}

}  // namespace

class AcceleratorControllerTest : public test::AshTestBase {
 public:
  AcceleratorControllerTest() {}
  ~AcceleratorControllerTest() override {}

 protected:
  void EnableInternalDisplay() {
    test::DisplayManagerTestApi(Shell::GetInstance()->display_manager()).
        SetFirstDisplayAsInternalDisplay();
  }

  static AcceleratorController* GetController();

  // Several functions to access ExitWarningHandler (as friend).
  static void StubForTest(ExitWarningHandler* ewh) {
    ewh->stub_timer_for_test_ = true;
  }
  static void Reset(ExitWarningHandler* ewh) {
    ewh->state_ = ExitWarningHandler::IDLE;
  }
  static void SimulateTimerExpired(ExitWarningHandler* ewh) {
    ewh->TimerAction();
  }
  static bool is_ui_shown(ExitWarningHandler* ewh) {
    return !!ewh->widget_;
  }
  static bool is_idle(ExitWarningHandler* ewh) {
    return ewh->state_ == ExitWarningHandler::IDLE;
  }
  static bool is_exiting(ExitWarningHandler* ewh) {
    return ewh->state_ == ExitWarningHandler::EXITING;
  }
  aura::Window* CreatePanel() {
    aura::Window* window =
      CreateTestWindowInShellWithDelegateAndType(NULL,
        ui::wm::WINDOW_TYPE_PANEL, 0, gfx::Rect(5, 5, 20, 20));
    test::TestShelfDelegate* shelf_delegate =
      test::TestShelfDelegate::instance();
    shelf_delegate->AddShelfItem(window);
    PanelLayoutManager* manager = static_cast<PanelLayoutManager*>(
        Shell::GetContainer(window->GetRootWindow(),
                            kShellWindowId_PanelContainer)->layout_manager());
    manager->Relayout();
    return window;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(AcceleratorControllerTest);
};

AcceleratorController* AcceleratorControllerTest::GetController() {
  return Shell::GetInstance()->accelerator_controller();
}

#if !defined(OS_WIN)
// Double press of exit shortcut => exiting
TEST_F(AcceleratorControllerTest, ExitWarningHandlerTestDoublePress) {
  ui::Accelerator press(ui::VKEY_Q, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN);
  ui::Accelerator release(press);
  release.set_type(ui::ET_KEY_RELEASED);
  ExitWarningHandler* ewh = GetController()->GetExitWarningHandlerForTest();
  ASSERT_TRUE(!!ewh);
  StubForTest(ewh);
  EXPECT_TRUE(is_idle(ewh));
  EXPECT_FALSE(is_ui_shown(ewh));
  EXPECT_TRUE(GetController()->Process(press));
  EXPECT_FALSE(GetController()->Process(release));
  EXPECT_FALSE(is_idle(ewh));
  EXPECT_TRUE(is_ui_shown(ewh));
  EXPECT_TRUE(GetController()->Process(press));  // second press before timer.
  EXPECT_FALSE(GetController()->Process(release));
  SimulateTimerExpired(ewh);
  EXPECT_TRUE(is_exiting(ewh));
  EXPECT_FALSE(is_ui_shown(ewh));
  Reset(ewh);
}

// Single press of exit shortcut before timer => idle
TEST_F(AcceleratorControllerTest, ExitWarningHandlerTestSinglePress) {
  ui::Accelerator press(ui::VKEY_Q, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN);
  ui::Accelerator release(press);
  release.set_type(ui::ET_KEY_RELEASED);
  ExitWarningHandler* ewh = GetController()->GetExitWarningHandlerForTest();
  ASSERT_TRUE(!!ewh);
  StubForTest(ewh);
  EXPECT_TRUE(is_idle(ewh));
  EXPECT_FALSE(is_ui_shown(ewh));
  EXPECT_TRUE(GetController()->Process(press));
  EXPECT_FALSE(GetController()->Process(release));
  EXPECT_FALSE(is_idle(ewh));
  EXPECT_TRUE(is_ui_shown(ewh));
  SimulateTimerExpired(ewh);
  EXPECT_TRUE(is_idle(ewh));
  EXPECT_FALSE(is_ui_shown(ewh));
  Reset(ewh);
}

// Shutdown ash with exit warning bubble open should not crash.
TEST_F(AcceleratorControllerTest, LingeringExitWarningBubble) {
  ExitWarningHandler* ewh = GetController()->GetExitWarningHandlerForTest();
  ASSERT_TRUE(!!ewh);
  StubForTest(ewh);

  // Trigger once to show the bubble.
  ewh->HandleAccelerator();
  EXPECT_FALSE(is_idle(ewh));
  EXPECT_TRUE(is_ui_shown(ewh));

  // Exit ash and there should be no crash
}
#endif  // !defined(OS_WIN)

TEST_F(AcceleratorControllerTest, Register) {
  const ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  TestTarget target;
  GetController()->Register(accelerator_a, &target);

  // The registered accelerator is processed.
  EXPECT_TRUE(GetController()->Process(accelerator_a));
  EXPECT_EQ(1, target.accelerator_pressed_count());
}

TEST_F(AcceleratorControllerTest, RegisterMultipleTarget) {
  const ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  TestTarget target1;
  GetController()->Register(accelerator_a, &target1);
  TestTarget target2;
  GetController()->Register(accelerator_a, &target2);

  // If multiple targets are registered with the same accelerator, the target
  // registered later processes the accelerator.
  EXPECT_TRUE(GetController()->Process(accelerator_a));
  EXPECT_EQ(0, target1.accelerator_pressed_count());
  EXPECT_EQ(1, target2.accelerator_pressed_count());
}

TEST_F(AcceleratorControllerTest, Unregister) {
  const ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  TestTarget target;
  GetController()->Register(accelerator_a, &target);
  const ui::Accelerator accelerator_b(ui::VKEY_B, ui::EF_NONE);
  GetController()->Register(accelerator_b, &target);

  // Unregistering a different accelerator does not affect the other
  // accelerator.
  GetController()->Unregister(accelerator_b, &target);
  EXPECT_TRUE(GetController()->Process(accelerator_a));
  EXPECT_EQ(1, target.accelerator_pressed_count());

  // The unregistered accelerator is no longer processed.
  target.reset();
  GetController()->Unregister(accelerator_a, &target);
  EXPECT_FALSE(GetController()->Process(accelerator_a));
  EXPECT_EQ(0, target.accelerator_pressed_count());
}

TEST_F(AcceleratorControllerTest, UnregisterAll) {
  const ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  TestTarget target1;
  GetController()->Register(accelerator_a, &target1);
  const ui::Accelerator accelerator_b(ui::VKEY_B, ui::EF_NONE);
  GetController()->Register(accelerator_b, &target1);
  const ui::Accelerator accelerator_c(ui::VKEY_C, ui::EF_NONE);
  TestTarget target2;
  GetController()->Register(accelerator_c, &target2);
  GetController()->UnregisterAll(&target1);

  // All the accelerators registered for |target1| are no longer processed.
  EXPECT_FALSE(GetController()->Process(accelerator_a));
  EXPECT_FALSE(GetController()->Process(accelerator_b));
  EXPECT_EQ(0, target1.accelerator_pressed_count());

  // UnregisterAll with a different target does not affect the other target.
  EXPECT_TRUE(GetController()->Process(accelerator_c));
  EXPECT_EQ(1, target2.accelerator_pressed_count());
}

TEST_F(AcceleratorControllerTest, Process) {
  const ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  TestTarget target1;
  GetController()->Register(accelerator_a, &target1);

  // The registered accelerator is processed.
  EXPECT_TRUE(GetController()->Process(accelerator_a));
  EXPECT_EQ(1, target1.accelerator_pressed_count());

  // The non-registered accelerator is not processed.
  const ui::Accelerator accelerator_b(ui::VKEY_B, ui::EF_NONE);
  EXPECT_FALSE(GetController()->Process(accelerator_b));
}

TEST_F(AcceleratorControllerTest, IsRegistered) {
  const ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  const ui::Accelerator accelerator_shift_a(ui::VKEY_A, ui::EF_SHIFT_DOWN);
  TestTarget target;
  GetController()->Register(accelerator_a, &target);
  EXPECT_TRUE(GetController()->IsRegistered(accelerator_a));
  EXPECT_FALSE(GetController()->IsRegistered(accelerator_shift_a));
  GetController()->UnregisterAll(&target);
  EXPECT_FALSE(GetController()->IsRegistered(accelerator_a));
}

TEST_F(AcceleratorControllerTest, WindowSnap) {
  scoped_ptr<aura::Window> window(
      CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  const ui::Accelerator dummy;

  wm::WindowState* window_state = wm::GetWindowState(window.get());

  window_state->Activate();

  {
    GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
    gfx::Rect expected_bounds = wm::GetDefaultLeftSnappedWindowBoundsInParent(
        window.get());
    EXPECT_EQ(expected_bounds.ToString(), window->bounds().ToString());
  }
  {
    GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_RIGHT, dummy);
    gfx::Rect expected_bounds = wm::GetDefaultRightSnappedWindowBoundsInParent(
        window.get());
    EXPECT_EQ(expected_bounds.ToString(), window->bounds().ToString());
  }
  {
    gfx::Rect normal_bounds = window_state->GetRestoreBoundsInParent();

    GetController()->PerformAction(TOGGLE_MAXIMIZED, dummy);
    EXPECT_TRUE(window_state->IsMaximized());
    EXPECT_NE(normal_bounds.ToString(), window->bounds().ToString());

    GetController()->PerformAction(TOGGLE_MAXIMIZED, dummy);
    EXPECT_FALSE(window_state->IsMaximized());
    // Window gets restored to its restore bounds since side-maximized state
    // is treated as a "maximized" state.
    EXPECT_EQ(normal_bounds.ToString(), window->bounds().ToString());

    GetController()->PerformAction(TOGGLE_MAXIMIZED, dummy);
    GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
    EXPECT_FALSE(window_state->IsMaximized());

    GetController()->PerformAction(TOGGLE_MAXIMIZED, dummy);
    GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_RIGHT, dummy);
    EXPECT_FALSE(window_state->IsMaximized());

    GetController()->PerformAction(TOGGLE_MAXIMIZED, dummy);
    EXPECT_TRUE(window_state->IsMaximized());
    GetController()->PerformAction(WINDOW_MINIMIZE, dummy);
    EXPECT_FALSE(window_state->IsMaximized());
    EXPECT_TRUE(window_state->IsMinimized());
    window_state->Restore();
    window_state->Activate();
  }
  {
    GetController()->PerformAction(WINDOW_MINIMIZE, dummy);
    EXPECT_TRUE(window_state->IsMinimized());
  }
}

TEST_F(AcceleratorControllerTest, WindowSnapLeftDockLeftRestore) {
  scoped_ptr<aura::Window> window0(
      CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  scoped_ptr<aura::Window> window1(
    CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  const ui::Accelerator dummy;

  wm::WindowState* window1_state = wm::GetWindowState(window1.get());
  window1_state->Activate();

  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  gfx::Rect normal_bounds = window1_state->GetRestoreBoundsInParent();
  gfx::Rect expected_bounds = wm::GetDefaultLeftSnappedWindowBoundsInParent(
                                window1.get());
  EXPECT_EQ(expected_bounds.ToString(), window1->bounds().ToString());
  EXPECT_TRUE(window1_state->IsSnapped());
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  EXPECT_FALSE(window1_state->IsNormalOrSnapped());
  EXPECT_TRUE(window1_state->IsDocked());
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  EXPECT_FALSE(window1_state->IsDocked());
  EXPECT_EQ(normal_bounds.ToString(), window1->bounds().ToString());
}

TEST_F(AcceleratorControllerTest, WindowSnapRightDockRightRestore) {
  scoped_ptr<aura::Window> window0(
      CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  scoped_ptr<aura::Window> window1(
    CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  const ui::Accelerator dummy;

  wm::WindowState* window1_state = wm::GetWindowState(window1.get());
  window1_state->Activate();

  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_RIGHT, dummy);
  gfx::Rect normal_bounds = window1_state->GetRestoreBoundsInParent();
  gfx::Rect expected_bounds =
    wm::GetDefaultRightSnappedWindowBoundsInParent(window1.get());
  EXPECT_EQ(expected_bounds.ToString(), window1->bounds().ToString());
  EXPECT_TRUE(window1_state->IsSnapped());
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_RIGHT, dummy);
  EXPECT_FALSE(window1_state->IsNormalOrSnapped());
  EXPECT_TRUE(window1_state->IsDocked());
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_RIGHT, dummy);
  EXPECT_FALSE(window1_state->IsDocked());
  EXPECT_EQ(normal_bounds.ToString(), window1->bounds().ToString());
}

TEST_F(AcceleratorControllerTest, WindowSnapLeftDockLeftSnapRight) {
  scoped_ptr<aura::Window> window0(
      CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  scoped_ptr<aura::Window> window1(
    CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  const ui::Accelerator dummy;

  wm::WindowState* window1_state = wm::GetWindowState(window1.get());
  window1_state->Activate();

  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  gfx::Rect expected_bounds =
    wm::GetDefaultLeftSnappedWindowBoundsInParent(window1.get());
  gfx::Rect expected_bounds2 =
    wm::GetDefaultRightSnappedWindowBoundsInParent(window1.get());
  EXPECT_EQ(expected_bounds.ToString(), window1->bounds().ToString());
  EXPECT_TRUE(window1_state->IsSnapped());
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  EXPECT_FALSE(window1_state->IsNormalOrSnapped());
  EXPECT_TRUE(window1_state->IsDocked());
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_RIGHT, dummy);
  EXPECT_FALSE(window1_state->IsDocked());
  EXPECT_TRUE(window1_state->IsSnapped());
  EXPECT_EQ(expected_bounds2.ToString(), window1->bounds().ToString());
}

TEST_F(AcceleratorControllerTest, WindowDockLeftMinimizeWindowWithRestore) {
  scoped_ptr<aura::Window> window0(
      CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  scoped_ptr<aura::Window> window1(
    CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  const ui::Accelerator dummy;

  wm::WindowState* window1_state = wm::GetWindowState(window1.get());
  window1_state->Activate();

  scoped_ptr<aura::Window> window2(
    CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));

  wm::WindowState* window2_state = wm::GetWindowState(window2.get());

  scoped_ptr<aura::Window> window3(
    CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));

  wm::WindowState* window3_state = wm::GetWindowState(window3.get());
  window3_state->Activate();

  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  gfx::Rect window3_docked_bounds = window3->bounds();

  window2_state->Activate();
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  window1_state->Activate();
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);

  EXPECT_TRUE(window3_state->IsDocked());
  EXPECT_TRUE(window2_state->IsDocked());
  EXPECT_TRUE(window1_state->IsDocked());
  EXPECT_TRUE(window3_state->IsMinimized());

  window1_state->Activate();
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  window2_state->Activate();
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  window3_state->Unminimize();
  EXPECT_FALSE(window1_state->IsDocked());
  EXPECT_FALSE(window2_state->IsDocked());
  EXPECT_TRUE(window3_state->IsDocked());
  EXPECT_EQ(window3_docked_bounds.ToString(), window3->bounds().ToString());
}

TEST_F(AcceleratorControllerTest, WindowPanelDockLeftDockRightRestore) {
  scoped_ptr<aura::Window> window0(
      CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));

  scoped_ptr<aura::Window> window(CreatePanel());
  const ui::Accelerator dummy;
  wm::WindowState* window_state = wm::GetWindowState(window.get());
  window_state->Activate();

  gfx::Rect window_restore_bounds2 = window->bounds();
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_LEFT, dummy);
  gfx::Rect expected_bounds =
      wm::GetDefaultLeftSnappedWindowBoundsInParent(window.get());
  gfx::Rect window_restore_bounds =
      window_state->GetRestoreBoundsInScreen();
  EXPECT_NE(expected_bounds.ToString(), window->bounds().ToString());
  EXPECT_FALSE(window_state->IsSnapped());
  EXPECT_FALSE(window_state->IsNormalOrSnapped());
  EXPECT_TRUE(window_state->IsDocked());
  window_state->Restore();
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_RIGHT, dummy);
  EXPECT_TRUE(window_state->IsDocked());
  GetController()->PerformAction(WINDOW_CYCLE_SNAP_DOCK_RIGHT, dummy);
  EXPECT_FALSE(window_state->IsDocked());
  EXPECT_EQ(window_restore_bounds.ToString(),
            window_restore_bounds2.ToString());
  EXPECT_EQ(window_restore_bounds.ToString(), window->bounds().ToString());
}

TEST_F(AcceleratorControllerTest, CenterWindowAccelerator) {
  scoped_ptr<aura::Window> window(
      CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  const ui::Accelerator dummy;
  wm::WindowState* window_state = wm::GetWindowState(window.get());
  window_state->Activate();

  // Center the window using accelerator.
  GetController()->PerformAction(WINDOW_POSITION_CENTER, dummy);
  gfx::Rect work_area =
      Shell::GetScreen()->GetDisplayNearestWindow(window.get()).work_area();
  gfx::Rect bounds = window->GetBoundsInScreen();
  EXPECT_NEAR(bounds.x() - work_area.x(),
              work_area.right() - bounds.right(),
              1);
  EXPECT_NEAR(bounds.y() - work_area.y(),
              work_area.bottom() - bounds.bottom(),
              1);

  // Add the window to docked container and try to center it.
  window->SetBounds(gfx::Rect(0, 0, 20, 20));
  aura::Window* docked_container = Shell::GetContainer(
      window->GetRootWindow(), kShellWindowId_DockedContainer);
  docked_container->AddChild(window.get());
  gfx::Rect docked_bounds = window->GetBoundsInScreen();
  GetController()->PerformAction(WINDOW_POSITION_CENTER, dummy);
  // It should not get centered and should remain docked.
  EXPECT_EQ(kShellWindowId_DockedContainer, window->parent()->id());
  EXPECT_EQ(docked_bounds.ToString(), window->GetBoundsInScreen().ToString());
}

TEST_F(AcceleratorControllerTest, AutoRepeat) {
  ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  accelerator_a.set_type(ui::ET_KEY_PRESSED);
  TestTarget target_a;
  GetController()->Register(accelerator_a, &target_a);
  ui::Accelerator accelerator_b(ui::VKEY_B, ui::EF_CONTROL_DOWN);
  accelerator_b.set_type(ui::ET_KEY_PRESSED);
  TestTarget target_b;
  GetController()->Register(accelerator_b, &target_b);

  ui::test::EventGenerator& generator = GetEventGenerator();
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  generator.ReleaseKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);

  EXPECT_EQ(1, target_a.accelerator_pressed_count());
  EXPECT_EQ(0, target_a.accelerator_repeat_count());

  // Long press should generate one
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN | ui::EF_IS_REPEAT);
  EXPECT_EQ(2, target_a.accelerator_pressed_count());
  EXPECT_EQ(1, target_a.accelerator_repeat_count());
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN | ui::EF_IS_REPEAT);
  EXPECT_EQ(2, target_a.accelerator_pressed_count());
  EXPECT_EQ(2, target_a.accelerator_repeat_count());
  generator.ReleaseKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  EXPECT_EQ(2, target_a.accelerator_pressed_count());
  EXPECT_EQ(2, target_a.accelerator_repeat_count());

  // Long press was intercepted by another key press.
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN | ui::EF_IS_REPEAT);
  generator.PressKey(ui::VKEY_B, ui::EF_CONTROL_DOWN);
  generator.ReleaseKey(ui::VKEY_B, ui::EF_CONTROL_DOWN);
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  generator.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN | ui::EF_IS_REPEAT);
  generator.ReleaseKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);

  EXPECT_EQ(1, target_b.accelerator_pressed_count());
  EXPECT_EQ(0, target_b.accelerator_repeat_count());
  EXPECT_EQ(4, target_a.accelerator_pressed_count());
  EXPECT_EQ(4, target_a.accelerator_repeat_count());
}

TEST_F(AcceleratorControllerTest, Previous) {
  ui::test::EventGenerator& generator = GetEventGenerator();
  generator.PressKey(ui::VKEY_VOLUME_MUTE, ui::EF_NONE);
  generator.ReleaseKey(ui::VKEY_VOLUME_MUTE, ui::EF_NONE);

  EXPECT_EQ(ui::VKEY_VOLUME_MUTE,
            GetController()->previous_accelerator_for_test().key_code());
  EXPECT_EQ(ui::EF_NONE,
            GetController()->previous_accelerator_for_test().modifiers());

  generator.PressKey(ui::VKEY_TAB, ui::EF_CONTROL_DOWN);
  generator.ReleaseKey(ui::VKEY_TAB, ui::EF_CONTROL_DOWN);

  EXPECT_EQ(ui::VKEY_TAB,
            GetController()->previous_accelerator_for_test().key_code());
  EXPECT_EQ(ui::EF_CONTROL_DOWN,
            GetController()->previous_accelerator_for_test().modifiers());
}

TEST_F(AcceleratorControllerTest, DontRepeatToggleFullscreen) {
  const AcceleratorData accelerators[] = {
      {true, ui::VKEY_J, ui::EF_ALT_DOWN, TOGGLE_FULLSCREEN},
      {true, ui::VKEY_K, ui::EF_ALT_DOWN, TOGGLE_FULLSCREEN},
  };
  GetController()->RegisterAccelerators(accelerators, arraysize(accelerators));

  views::Widget::InitParams params(views::Widget::InitParams::TYPE_WINDOW);
  params.context = CurrentContext();
  params.bounds = gfx::Rect(5, 5, 20, 20);
  views::Widget* widget = new views::Widget;
  widget->Init(params);
  widget->Show();
  widget->Activate();
  widget->GetNativeView()->SetProperty(aura::client::kCanMaximizeKey, true);

  ui::test::EventGenerator& generator = GetEventGenerator();
  wm::WindowState* window_state = wm::GetWindowState(widget->GetNativeView());

  // Toggling not suppressed.
  generator.PressKey(ui::VKEY_J, ui::EF_ALT_DOWN);
  EXPECT_TRUE(window_state->IsFullscreen());

  // The same accelerator - toggling suppressed.
  generator.PressKey(ui::VKEY_J, ui::EF_ALT_DOWN | ui::EF_IS_REPEAT);
  EXPECT_TRUE(window_state->IsFullscreen());

  // Different accelerator.
  generator.PressKey(ui::VKEY_K, ui::EF_ALT_DOWN);
  EXPECT_FALSE(window_state->IsFullscreen());
}

// TODO(oshima): Fix this test to use EventGenerator.
#if defined(OS_WIN)
// crbug.com/317592
#define MAYBE_ProcessOnce DISABLED_ProcessOnce
#else
#define MAYBE_ProcessOnce ProcessOnce
#endif

#if defined(OS_WIN) || defined(USE_X11)
TEST_F(AcceleratorControllerTest, MAYBE_ProcessOnce) {
  ui::Accelerator accelerator_a(ui::VKEY_A, ui::EF_NONE);
  TestTarget target;
  GetController()->Register(accelerator_a, &target);

  // The accelerator is processed only once.
  ui::EventProcessor* dispatcher =
      Shell::GetPrimaryRootWindow()->GetHost()->event_processor();
#if defined(OS_WIN)
  MSG msg1 = { NULL, WM_KEYDOWN, ui::VKEY_A, 0 };
  ui::KeyEvent key_event1(msg1);
  key_event1.SetTranslated(true);
  ui::EventDispatchDetails details = dispatcher->OnEventFromSource(&key_event1);
  EXPECT_TRUE(key_event1.handled() || details.dispatcher_destroyed);

  MSG msg2 = { NULL, WM_CHAR, L'A', 0 };
  ui::KeyEvent key_event2(msg2);
  key_event2.SetTranslated(true);
  details = dispatcher->OnEventFromSource(&key_event2);
  EXPECT_FALSE(key_event2.handled() || details.dispatcher_destroyed);

  MSG msg3 = { NULL, WM_KEYUP, ui::VKEY_A, 0 };
  ui::KeyEvent key_event3(msg3);
  key_event3.SetTranslated(true);
  details = dispatcher->OnEventFromSource(&key_event3);
  EXPECT_FALSE(key_event3.handled() || details.dispatcher_destroyed);
#elif defined(USE_X11)
  ui::ScopedXI2Event key_event;
  key_event.InitKeyEvent(ui::ET_KEY_PRESSED, ui::VKEY_A, 0);
  ui::KeyEvent key_event1(key_event);
  key_event1.SetTranslated(true);
  ui::EventDispatchDetails details = dispatcher->OnEventFromSource(&key_event1);
  EXPECT_TRUE(key_event1.handled() || details.dispatcher_destroyed);

  ui::KeyEvent key_event2('A', ui::VKEY_A, ui::EF_NONE);
  key_event2.SetTranslated(true);
  details = dispatcher->OnEventFromSource(&key_event2);
  EXPECT_FALSE(key_event2.handled() || details.dispatcher_destroyed);

  key_event.InitKeyEvent(ui::ET_KEY_RELEASED, ui::VKEY_A, 0);
  ui::KeyEvent key_event3(key_event);
  key_event3.SetTranslated(true);
  details = dispatcher->OnEventFromSource(&key_event3);
  EXPECT_FALSE(key_event3.handled() || details.dispatcher_destroyed);
#endif
  EXPECT_EQ(1, target.accelerator_pressed_count());
}
#endif

TEST_F(AcceleratorControllerTest, GlobalAccelerators) {
  // CycleBackward
  EXPECT_TRUE(GetController()->Process(
      ui::Accelerator(ui::VKEY_TAB, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN)));
  // CycleForward
  EXPECT_TRUE(
      GetController()->Process(ui::Accelerator(ui::VKEY_TAB, ui::EF_ALT_DOWN)));
  // CycleLinear
  EXPECT_TRUE(GetController()->Process(
      ui::Accelerator(ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_NONE)));

#if defined(OS_CHROMEOS)
  // Take screenshot / partial screenshot
  // True should always be returned regardless of the existence of the delegate.
  {
    test::TestScreenshotDelegate* delegate = GetScreenshotDelegate();
    delegate->set_can_take_screenshot(false);
    EXPECT_TRUE(GetController()->Process(
        ui::Accelerator(ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_CONTROL_DOWN)));
    EXPECT_TRUE(
        GetController()->Process(ui::Accelerator(ui::VKEY_PRINT, ui::EF_NONE)));
    EXPECT_TRUE(GetController()->Process(ui::Accelerator(
        ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));

    delegate->set_can_take_screenshot(true);
    EXPECT_EQ(0, delegate->handle_take_screenshot_count());
    EXPECT_TRUE(GetController()->Process(
        ui::Accelerator(ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_CONTROL_DOWN)));
    EXPECT_EQ(1, delegate->handle_take_screenshot_count());
    EXPECT_TRUE(
        GetController()->Process(ui::Accelerator(ui::VKEY_PRINT, ui::EF_NONE)));
    EXPECT_EQ(2, delegate->handle_take_screenshot_count());
    EXPECT_TRUE(GetController()->Process(ui::Accelerator(
        ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));
    EXPECT_EQ(2, delegate->handle_take_screenshot_count());
  }
#endif
  const ui::Accelerator volume_mute(ui::VKEY_VOLUME_MUTE, ui::EF_NONE);
  const ui::Accelerator volume_down(ui::VKEY_VOLUME_DOWN, ui::EF_NONE);
  const ui::Accelerator volume_up(ui::VKEY_VOLUME_UP, ui::EF_NONE);
  {
    TestVolumeControlDelegate* delegate =
        new TestVolumeControlDelegate(false);
    ash::Shell::GetInstance()->system_tray_delegate()->SetVolumeControlDelegate(
        scoped_ptr<VolumeControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_volume_mute_count());
    EXPECT_FALSE(GetController()->Process(volume_mute));
    EXPECT_EQ(1, delegate->handle_volume_mute_count());
    EXPECT_EQ(volume_mute, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_volume_down_count());
    EXPECT_FALSE(GetController()->Process(volume_down));
    EXPECT_EQ(1, delegate->handle_volume_down_count());
    EXPECT_EQ(volume_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_volume_up_count());
    EXPECT_FALSE(GetController()->Process(volume_up));
    EXPECT_EQ(1, delegate->handle_volume_up_count());
    EXPECT_EQ(volume_up, delegate->last_accelerator());
  }
  {
    TestVolumeControlDelegate* delegate = new TestVolumeControlDelegate(true);
    ash::Shell::GetInstance()->system_tray_delegate()->SetVolumeControlDelegate(
        scoped_ptr<VolumeControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_volume_mute_count());
    EXPECT_TRUE(GetController()->Process(volume_mute));
    EXPECT_EQ(1, delegate->handle_volume_mute_count());
    EXPECT_EQ(volume_mute, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_volume_down_count());
    EXPECT_TRUE(GetController()->Process(volume_down));
    EXPECT_EQ(1, delegate->handle_volume_down_count());
    EXPECT_EQ(volume_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_volume_up_count());
    EXPECT_TRUE(GetController()->Process(volume_up));
    EXPECT_EQ(1, delegate->handle_volume_up_count());
    EXPECT_EQ(volume_up, delegate->last_accelerator());
  }
#if defined(OS_CHROMEOS)
  // Brightness
  // ui::VKEY_BRIGHTNESS_DOWN/UP are not defined on Windows.
  const ui::Accelerator brightness_down(ui::VKEY_BRIGHTNESS_DOWN, ui::EF_NONE);
  const ui::Accelerator brightness_up(ui::VKEY_BRIGHTNESS_UP, ui::EF_NONE);
  {
    DummyBrightnessControlDelegate* delegate =
        new DummyBrightnessControlDelegate(false);
    GetController()->SetBrightnessControlDelegate(
        scoped_ptr<BrightnessControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_brightness_down_count());
    EXPECT_FALSE(GetController()->Process(brightness_down));
    EXPECT_EQ(1, delegate->handle_brightness_down_count());
    EXPECT_EQ(brightness_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_brightness_up_count());
    EXPECT_FALSE(GetController()->Process(brightness_up));
    EXPECT_EQ(1, delegate->handle_brightness_up_count());
    EXPECT_EQ(brightness_up, delegate->last_accelerator());
  }
  {
    DummyBrightnessControlDelegate* delegate =
        new DummyBrightnessControlDelegate(true);
    GetController()->SetBrightnessControlDelegate(
        scoped_ptr<BrightnessControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_brightness_down_count());
    EXPECT_TRUE(GetController()->Process(brightness_down));
    EXPECT_EQ(1, delegate->handle_brightness_down_count());
    EXPECT_EQ(brightness_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_brightness_up_count());
    EXPECT_TRUE(GetController()->Process(brightness_up));
    EXPECT_EQ(1, delegate->handle_brightness_up_count());
    EXPECT_EQ(brightness_up, delegate->last_accelerator());
  }

  // Keyboard brightness
  const ui::Accelerator alt_brightness_down(ui::VKEY_BRIGHTNESS_DOWN,
                                            ui::EF_ALT_DOWN);
  const ui::Accelerator alt_brightness_up(ui::VKEY_BRIGHTNESS_UP,
                                          ui::EF_ALT_DOWN);
  {
    EXPECT_TRUE(GetController()->Process(alt_brightness_down));
    EXPECT_TRUE(GetController()->Process(alt_brightness_up));
    DummyKeyboardBrightnessControlDelegate* delegate =
        new DummyKeyboardBrightnessControlDelegate(false);
    GetController()->SetKeyboardBrightnessControlDelegate(
        scoped_ptr<KeyboardBrightnessControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_keyboard_brightness_down_count());
    EXPECT_FALSE(GetController()->Process(alt_brightness_down));
    EXPECT_EQ(1, delegate->handle_keyboard_brightness_down_count());
    EXPECT_EQ(alt_brightness_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_keyboard_brightness_up_count());
    EXPECT_FALSE(GetController()->Process(alt_brightness_up));
    EXPECT_EQ(1, delegate->handle_keyboard_brightness_up_count());
    EXPECT_EQ(alt_brightness_up, delegate->last_accelerator());
  }
  {
    DummyKeyboardBrightnessControlDelegate* delegate =
        new DummyKeyboardBrightnessControlDelegate(true);
    GetController()->SetKeyboardBrightnessControlDelegate(
        scoped_ptr<KeyboardBrightnessControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_keyboard_brightness_down_count());
    EXPECT_TRUE(GetController()->Process(alt_brightness_down));
    EXPECT_EQ(1, delegate->handle_keyboard_brightness_down_count());
    EXPECT_EQ(alt_brightness_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_keyboard_brightness_up_count());
    EXPECT_TRUE(GetController()->Process(alt_brightness_up));
    EXPECT_EQ(1, delegate->handle_keyboard_brightness_up_count());
    EXPECT_EQ(alt_brightness_up, delegate->last_accelerator());
  }
#endif

#if !defined(OS_WIN)
  // Exit
  ExitWarningHandler* ewh = GetController()->GetExitWarningHandlerForTest();
  ASSERT_TRUE(!!ewh);
  StubForTest(ewh);
  EXPECT_TRUE(is_idle(ewh));
  EXPECT_FALSE(is_ui_shown(ewh));
  EXPECT_TRUE(GetController()->Process(
      ui::Accelerator(ui::VKEY_Q, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));
  EXPECT_FALSE(is_idle(ewh));
  EXPECT_TRUE(is_ui_shown(ewh));
  SimulateTimerExpired(ewh);
  EXPECT_TRUE(is_idle(ewh));
  EXPECT_FALSE(is_ui_shown(ewh));
  Reset(ewh);
#endif

  // New tab
  EXPECT_TRUE(GetController()->Process(
      ui::Accelerator(ui::VKEY_T, ui::EF_CONTROL_DOWN)));

  // New incognito window
  EXPECT_TRUE(GetController()->Process(
      ui::Accelerator(ui::VKEY_N, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));

  // New window
  EXPECT_TRUE(GetController()->Process(
      ui::Accelerator(ui::VKEY_N, ui::EF_CONTROL_DOWN)));

  // Restore tab
  EXPECT_TRUE(GetController()->Process(
      ui::Accelerator(ui::VKEY_T, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));

  // Show task manager
  EXPECT_TRUE(GetController()->Process(
      ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_SHIFT_DOWN)));

#if defined(OS_CHROMEOS)
  // Open file manager
  EXPECT_TRUE(GetController()->Process(
      ui::Accelerator(ui::VKEY_M, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN)));

  // Lock screen
  // NOTE: Accelerators that do not work on the lock screen need to be
  // tested before the sequence below is invoked because it causes a side
  // effect of locking the screen.
  EXPECT_TRUE(GetController()->Process(
      ui::Accelerator(ui::VKEY_L, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));
#endif
}

TEST_F(AcceleratorControllerTest, GlobalAcceleratorsToggleAppList) {
  AccessibilityDelegate* delegate =
          ash::Shell::GetInstance()->accessibility_delegate();
  EXPECT_FALSE(ash::Shell::GetInstance()->GetAppListTargetVisibility());

  // The press event should not open the AppList, the release should instead.
  EXPECT_FALSE(
      GetController()->Process(ui::Accelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  EXPECT_EQ(ui::VKEY_LWIN,
            GetController()->previous_accelerator_for_test().key_code());

  EXPECT_TRUE(
      GetController()->Process(ReleaseAccelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  EXPECT_TRUE(ash::Shell::GetInstance()->GetAppListTargetVisibility());

  // When spoken feedback is on, the AppList should not toggle.
  delegate->ToggleSpokenFeedback(ui::A11Y_NOTIFICATION_NONE);
  EXPECT_FALSE(
      GetController()->Process(ui::Accelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  EXPECT_FALSE(
      GetController()->Process(ReleaseAccelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  delegate->ToggleSpokenFeedback(ui::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(ash::Shell::GetInstance()->GetAppListTargetVisibility());

  EXPECT_FALSE(
      GetController()->Process(ui::Accelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  EXPECT_TRUE(
      GetController()->Process(ReleaseAccelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  EXPECT_FALSE(ash::Shell::GetInstance()->GetAppListTargetVisibility());

  // When spoken feedback is on, the AppList should not toggle.
  delegate->ToggleSpokenFeedback(ui::A11Y_NOTIFICATION_NONE);
  EXPECT_FALSE(
      GetController()->Process(ui::Accelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  EXPECT_FALSE(
      GetController()->Process(ReleaseAccelerator(ui::VKEY_LWIN, ui::EF_NONE)));
  delegate->ToggleSpokenFeedback(ui::A11Y_NOTIFICATION_NONE);
  EXPECT_FALSE(ash::Shell::GetInstance()->GetAppListTargetVisibility());
}

TEST_F(AcceleratorControllerTest, ImeGlobalAccelerators) {
  // Test IME shortcuts.
  {
    const ui::Accelerator control_space(ui::VKEY_SPACE, ui::EF_CONTROL_DOWN);
    const ui::Accelerator convert(ui::VKEY_CONVERT, ui::EF_NONE);
    const ui::Accelerator non_convert(ui::VKEY_NONCONVERT, ui::EF_NONE);
    const ui::Accelerator wide_half_1(ui::VKEY_DBE_SBCSCHAR, ui::EF_NONE);
    const ui::Accelerator wide_half_2(ui::VKEY_DBE_DBCSCHAR, ui::EF_NONE);
    const ui::Accelerator hangul(ui::VKEY_HANGUL, ui::EF_NONE);
    EXPECT_FALSE(GetController()->Process(control_space));
    EXPECT_FALSE(GetController()->Process(convert));
    EXPECT_FALSE(GetController()->Process(non_convert));
    EXPECT_FALSE(GetController()->Process(wide_half_1));
    EXPECT_FALSE(GetController()->Process(wide_half_2));
    EXPECT_FALSE(GetController()->Process(hangul));
    DummyImeControlDelegate* delegate = new DummyImeControlDelegate(true);
    GetController()->SetImeControlDelegate(
        scoped_ptr<ImeControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_previous_ime_count());
    EXPECT_TRUE(GetController()->Process(control_space));
    EXPECT_EQ(1, delegate->handle_previous_ime_count());
    EXPECT_EQ(0, delegate->handle_switch_ime_count());
    EXPECT_TRUE(GetController()->Process(convert));
    EXPECT_EQ(1, delegate->handle_switch_ime_count());
    EXPECT_TRUE(GetController()->Process(non_convert));
    EXPECT_EQ(2, delegate->handle_switch_ime_count());
    EXPECT_TRUE(GetController()->Process(wide_half_1));
    EXPECT_EQ(3, delegate->handle_switch_ime_count());
    EXPECT_TRUE(GetController()->Process(wide_half_2));
    EXPECT_EQ(4, delegate->handle_switch_ime_count());
    EXPECT_TRUE(GetController()->Process(hangul));
    EXPECT_EQ(5, delegate->handle_switch_ime_count());
  }

  // Test IME shortcuts that are triggered on key release.
  {
    const ui::Accelerator shift_alt_press(ui::VKEY_MENU,
                                          ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);
    const ReleaseAccelerator shift_alt(ui::VKEY_MENU, ui::EF_SHIFT_DOWN);
    const ui::Accelerator alt_shift_press(ui::VKEY_SHIFT,
                                          ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);
    const ReleaseAccelerator alt_shift(ui::VKEY_SHIFT, ui::EF_ALT_DOWN);

    DummyImeControlDelegate* delegate = new DummyImeControlDelegate(true);
    GetController()->SetImeControlDelegate(
        scoped_ptr<ImeControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_next_ime_count());
    EXPECT_FALSE(GetController()->Process(shift_alt_press));
    EXPECT_FALSE(GetController()->Process(shift_alt));
    EXPECT_EQ(1, delegate->handle_next_ime_count());
    EXPECT_FALSE(GetController()->Process(alt_shift_press));
    EXPECT_FALSE(GetController()->Process(alt_shift));
    EXPECT_EQ(2, delegate->handle_next_ime_count());

    // We should NOT switch IME when e.g. Shift+Alt+X is pressed and X is
    // released.
    const ui::Accelerator shift_alt_x_press(
        ui::VKEY_X,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);
    const ReleaseAccelerator shift_alt_x(ui::VKEY_X,
                                         ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);

    EXPECT_FALSE(GetController()->Process(shift_alt_press));
    EXPECT_FALSE(GetController()->Process(shift_alt_x_press));
    EXPECT_FALSE(GetController()->Process(shift_alt_x));
    EXPECT_FALSE(GetController()->Process(shift_alt));
    EXPECT_EQ(2, delegate->handle_next_ime_count());

    // But we _should_ if X is either VKEY_RETURN or VKEY_SPACE.
    // TODO(nona|mazda): Remove this when crbug.com/139556 in a better way.
    const ui::Accelerator shift_alt_return_press(
        ui::VKEY_RETURN,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);
    const ReleaseAccelerator shift_alt_return(
        ui::VKEY_RETURN,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);

    EXPECT_FALSE(GetController()->Process(shift_alt_press));
    EXPECT_FALSE(GetController()->Process(shift_alt_return_press));
    EXPECT_FALSE(GetController()->Process(shift_alt_return));
    EXPECT_FALSE(GetController()->Process(shift_alt));
    EXPECT_EQ(3, delegate->handle_next_ime_count());

    const ui::Accelerator shift_alt_space_press(
        ui::VKEY_SPACE,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);
    const ReleaseAccelerator shift_alt_space(
        ui::VKEY_SPACE,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);

    EXPECT_FALSE(GetController()->Process(shift_alt_press));
    EXPECT_FALSE(GetController()->Process(shift_alt_space_press));
    EXPECT_FALSE(GetController()->Process(shift_alt_space));
    EXPECT_FALSE(GetController()->Process(shift_alt));
    EXPECT_EQ(4, delegate->handle_next_ime_count());
  }

#if defined(OS_CHROMEOS)
  // Test IME shortcuts again with unnormalized accelerators (Chrome OS only).
  {
    const ui::Accelerator shift_alt_press(ui::VKEY_MENU, ui::EF_SHIFT_DOWN);
    const ReleaseAccelerator shift_alt(ui::VKEY_MENU, ui::EF_SHIFT_DOWN);
    const ui::Accelerator alt_shift_press(ui::VKEY_SHIFT, ui::EF_ALT_DOWN);
    const ReleaseAccelerator alt_shift(ui::VKEY_SHIFT, ui::EF_ALT_DOWN);

    DummyImeControlDelegate* delegate = new DummyImeControlDelegate(true);
    GetController()->SetImeControlDelegate(
        scoped_ptr<ImeControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_next_ime_count());
    EXPECT_FALSE(GetController()->Process(shift_alt_press));
    EXPECT_FALSE(GetController()->Process(shift_alt));
    EXPECT_EQ(1, delegate->handle_next_ime_count());
    EXPECT_FALSE(GetController()->Process(alt_shift_press));
    EXPECT_FALSE(GetController()->Process(alt_shift));
    EXPECT_EQ(2, delegate->handle_next_ime_count());

    // We should NOT switch IME when e.g. Shift+Alt+X is pressed and X is
    // released.
    const ui::Accelerator shift_alt_x_press(
        ui::VKEY_X,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);
    const ReleaseAccelerator shift_alt_x(ui::VKEY_X,
                                         ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);

    EXPECT_FALSE(GetController()->Process(shift_alt_press));
    EXPECT_FALSE(GetController()->Process(shift_alt_x_press));
    EXPECT_FALSE(GetController()->Process(shift_alt_x));
    EXPECT_FALSE(GetController()->Process(shift_alt));
    EXPECT_EQ(2, delegate->handle_next_ime_count());
  }
#endif
}

// TODO(nona|mazda): Remove this when crbug.com/139556 in a better way.
TEST_F(AcceleratorControllerTest, ImeGlobalAcceleratorsWorkaround139556) {
  // The workaround for crbug.com/139556 depends on the fact that we don't
  // use Shift+Alt+Enter/Space with ET_KEY_PRESSED as an accelerator. Test it.
  const ui::Accelerator shift_alt_return_press(
      ui::VKEY_RETURN,
      ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);
  EXPECT_FALSE(GetController()->Process(shift_alt_return_press));
  const ui::Accelerator shift_alt_space_press(
      ui::VKEY_SPACE,
      ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);
  EXPECT_FALSE(GetController()->Process(shift_alt_space_press));
}

TEST_F(AcceleratorControllerTest, PreferredReservedAccelerators) {
#if defined(OS_CHROMEOS)
  // Power key is reserved on chromeos.
  EXPECT_TRUE(GetController()->IsReserved(
      ui::Accelerator(ui::VKEY_POWER, ui::EF_NONE)));
  EXPECT_FALSE(GetController()->IsPreferred(
      ui::Accelerator(ui::VKEY_POWER, ui::EF_NONE)));
#endif
  // ALT+Tab are not reserved but preferred.
  EXPECT_FALSE(GetController()->IsReserved(
      ui::Accelerator(ui::VKEY_TAB, ui::EF_ALT_DOWN)));
  EXPECT_FALSE(GetController()->IsReserved(
      ui::Accelerator(ui::VKEY_TAB, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN)));
  EXPECT_TRUE(GetController()->IsPreferred(
      ui::Accelerator(ui::VKEY_TAB, ui::EF_ALT_DOWN)));
  EXPECT_TRUE(GetController()->IsPreferred(
      ui::Accelerator(ui::VKEY_TAB, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN)));

  // Others are not reserved nor preferred
  EXPECT_FALSE(GetController()->IsReserved(
      ui::Accelerator(ui::VKEY_PRINT, ui::EF_NONE)));
  EXPECT_FALSE(GetController()->IsPreferred(
      ui::Accelerator(ui::VKEY_PRINT, ui::EF_NONE)));
  EXPECT_FALSE(GetController()->IsReserved(
      ui::Accelerator(ui::VKEY_TAB, ui::EF_NONE)));
  EXPECT_FALSE(GetController()->IsPreferred(
      ui::Accelerator(ui::VKEY_TAB, ui::EF_NONE)));
  EXPECT_FALSE(GetController()->IsReserved(
      ui::Accelerator(ui::VKEY_A, ui::EF_NONE)));
  EXPECT_FALSE(GetController()->IsPreferred(
      ui::Accelerator(ui::VKEY_A, ui::EF_NONE)));
}

namespace {

class PreferredReservedAcceleratorsTest : public test::AshTestBase {
 public:
  PreferredReservedAcceleratorsTest() {}
  ~PreferredReservedAcceleratorsTest() override {}

  // test::AshTestBase:
  void SetUp() override {
    AshTestBase::SetUp();
    Shell::GetInstance()->lock_state_controller()->
        set_animator_for_test(new test::TestSessionStateAnimator);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(PreferredReservedAcceleratorsTest);
};

}  // namespace

TEST_F(PreferredReservedAcceleratorsTest, AcceleratorsWithFullscreen) {
  aura::Window* w1 = CreateTestWindowInShellWithId(0);
  aura::Window* w2 = CreateTestWindowInShellWithId(1);
  wm::ActivateWindow(w1);

  wm::WMEvent fullscreen(wm::WM_EVENT_FULLSCREEN);
  wm::WindowState* w1_state = wm::GetWindowState(w1);
  w1_state->OnWMEvent(&fullscreen);
  ASSERT_TRUE(w1_state->IsFullscreen());

  ui::test::EventGenerator& generator = GetEventGenerator();
#if defined(OS_CHROMEOS)
  // Power key (reserved) should always be handled.
  LockStateController::TestApi test_api(
      Shell::GetInstance()->lock_state_controller());
  EXPECT_FALSE(test_api.is_animating_lock());
  generator.PressKey(ui::VKEY_POWER, ui::EF_NONE);
  EXPECT_TRUE(test_api.is_animating_lock());
#endif

  // A fullscreen window can consume ALT-TAB (preferred).
  ASSERT_EQ(w1, wm::GetActiveWindow());
  generator.PressKey(ui::VKEY_TAB, ui::EF_ALT_DOWN);
  ASSERT_EQ(w1, wm::GetActiveWindow());
  ASSERT_NE(w2, wm::GetActiveWindow());

  // ALT-TAB is non repeatable. Press A to cancel the
  // repeat record.
  generator.PressKey(ui::VKEY_A, ui::EF_NONE);
  generator.ReleaseKey(ui::VKEY_A, ui::EF_NONE);

  // A normal window shouldn't consume preferred accelerator.
  wm::WMEvent normal(wm::WM_EVENT_NORMAL);
  w1_state->OnWMEvent(&normal);
  ASSERT_FALSE(w1_state->IsFullscreen());

  EXPECT_EQ(w1, wm::GetActiveWindow());
  generator.PressKey(ui::VKEY_TAB, ui::EF_ALT_DOWN);
  ASSERT_NE(w1, wm::GetActiveWindow());
  ASSERT_EQ(w2, wm::GetActiveWindow());
}

#if defined(OS_CHROMEOS)
TEST_F(AcceleratorControllerTest, DisallowedAtModalWindow) {
  std::set<AcceleratorAction> all_actions;
  for (size_t i = 0 ; i < kAcceleratorDataLength; ++i)
    all_actions.insert(kAcceleratorData[i].action);
  std::set<AcceleratorAction> all_debug_actions;
  for (size_t i = 0 ; i < kDebugAcceleratorDataLength; ++i)
    all_debug_actions.insert(kDebugAcceleratorData[i].action);

  std::set<AcceleratorAction> actionsAllowedAtModalWindow;
  for (size_t k = 0 ; k < kActionsAllowedAtModalWindowLength; ++k)
    actionsAllowedAtModalWindow.insert(kActionsAllowedAtModalWindow[k]);
  for (std::set<AcceleratorAction>::const_iterator it =
           actionsAllowedAtModalWindow.begin();
       it != actionsAllowedAtModalWindow.end(); ++it) {
    EXPECT_TRUE(all_actions.find(*it) != all_actions.end() ||
                all_debug_actions.find(*it) != all_debug_actions.end())
        << " action from kActionsAllowedAtModalWindow"
        << " not found in kAcceleratorData or kDebugAcceleratorData. "
        << "action: " << *it;
  }
  scoped_ptr<aura::Window> window(
      CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
  const ui::Accelerator dummy;
  wm::ActivateWindow(window.get());
  Shell::GetInstance()->SimulateModalWindowOpenForTesting(true);
  for (std::set<AcceleratorAction>::const_iterator it = all_actions.begin();
       it != all_actions.end(); ++it) {
    if (actionsAllowedAtModalWindow.find(*it) ==
        actionsAllowedAtModalWindow.end()) {
      EXPECT_TRUE(GetController()->PerformAction(*it, dummy))
          << " for action (disallowed at modal window): " << *it;
    }
  }
  //  Testing of top row (F5-F10) accelerators that should still work
  //  when a modal window is open
  //
  // Screenshot
  {
    test::TestScreenshotDelegate* delegate = GetScreenshotDelegate();
    delegate->set_can_take_screenshot(false);
    EXPECT_TRUE(GetController()->Process(
        ui::Accelerator(ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_CONTROL_DOWN)));
    EXPECT_TRUE(
        GetController()->Process(ui::Accelerator(ui::VKEY_PRINT, ui::EF_NONE)));
    EXPECT_TRUE(GetController()->Process(ui::Accelerator(
        ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));
    delegate->set_can_take_screenshot(true);
    EXPECT_EQ(0, delegate->handle_take_screenshot_count());
    EXPECT_TRUE(GetController()->Process(
        ui::Accelerator(ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_CONTROL_DOWN)));
    EXPECT_EQ(1, delegate->handle_take_screenshot_count());
    EXPECT_TRUE(
        GetController()->Process(ui::Accelerator(ui::VKEY_PRINT, ui::EF_NONE)));
    EXPECT_EQ(2, delegate->handle_take_screenshot_count());
    EXPECT_TRUE(GetController()->Process(ui::Accelerator(
        ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN)));
    EXPECT_EQ(2, delegate->handle_take_screenshot_count());
  }
  // Brightness
  const ui::Accelerator brightness_down(ui::VKEY_BRIGHTNESS_DOWN, ui::EF_NONE);
  const ui::Accelerator brightness_up(ui::VKEY_BRIGHTNESS_UP, ui::EF_NONE);
  {
    DummyBrightnessControlDelegate* delegate =
        new DummyBrightnessControlDelegate(false);
    GetController()->SetBrightnessControlDelegate(
        scoped_ptr<BrightnessControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_brightness_down_count());
    EXPECT_FALSE(GetController()->Process(brightness_down));
    EXPECT_EQ(1, delegate->handle_brightness_down_count());
    EXPECT_EQ(brightness_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_brightness_up_count());
    EXPECT_FALSE(GetController()->Process(brightness_up));
    EXPECT_EQ(1, delegate->handle_brightness_up_count());
    EXPECT_EQ(brightness_up, delegate->last_accelerator());
  }
  {
    DummyBrightnessControlDelegate* delegate =
        new DummyBrightnessControlDelegate(true);
    GetController()->SetBrightnessControlDelegate(
        scoped_ptr<BrightnessControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_brightness_down_count());
    EXPECT_TRUE(GetController()->Process(brightness_down));
    EXPECT_EQ(1, delegate->handle_brightness_down_count());
    EXPECT_EQ(brightness_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_brightness_up_count());
    EXPECT_TRUE(GetController()->Process(brightness_up));
    EXPECT_EQ(1, delegate->handle_brightness_up_count());
    EXPECT_EQ(brightness_up, delegate->last_accelerator());
  }
  // Volume
  const ui::Accelerator volume_mute(ui::VKEY_VOLUME_MUTE, ui::EF_NONE);
  const ui::Accelerator volume_down(ui::VKEY_VOLUME_DOWN, ui::EF_NONE);
  const ui::Accelerator volume_up(ui::VKEY_VOLUME_UP, ui::EF_NONE);
  {
    EXPECT_TRUE(GetController()->Process(volume_mute));
    EXPECT_TRUE(GetController()->Process(volume_down));
    EXPECT_TRUE(GetController()->Process(volume_up));
    TestVolumeControlDelegate* delegate =
        new TestVolumeControlDelegate(false);
    ash::Shell::GetInstance()->system_tray_delegate()->SetVolumeControlDelegate(
        scoped_ptr<VolumeControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_volume_mute_count());
    EXPECT_FALSE(GetController()->Process(volume_mute));
    EXPECT_EQ(1, delegate->handle_volume_mute_count());
    EXPECT_EQ(volume_mute, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_volume_down_count());
    EXPECT_FALSE(GetController()->Process(volume_down));
    EXPECT_EQ(1, delegate->handle_volume_down_count());
    EXPECT_EQ(volume_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_volume_up_count());
    EXPECT_FALSE(GetController()->Process(volume_up));
    EXPECT_EQ(1, delegate->handle_volume_up_count());
    EXPECT_EQ(volume_up, delegate->last_accelerator());
  }
  {
    TestVolumeControlDelegate* delegate = new TestVolumeControlDelegate(true);
    ash::Shell::GetInstance()->system_tray_delegate()->SetVolumeControlDelegate(
        scoped_ptr<VolumeControlDelegate>(delegate).Pass());
    EXPECT_EQ(0, delegate->handle_volume_mute_count());
    EXPECT_TRUE(GetController()->Process(volume_mute));
    EXPECT_EQ(1, delegate->handle_volume_mute_count());
    EXPECT_EQ(volume_mute, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_volume_down_count());
    EXPECT_TRUE(GetController()->Process(volume_down));
    EXPECT_EQ(1, delegate->handle_volume_down_count());
    EXPECT_EQ(volume_down, delegate->last_accelerator());
    EXPECT_EQ(0, delegate->handle_volume_up_count());
    EXPECT_TRUE(GetController()->Process(volume_up));
    EXPECT_EQ(1, delegate->handle_volume_up_count());
    EXPECT_EQ(volume_up, delegate->last_accelerator());
  }
}
#endif

TEST_F(AcceleratorControllerTest, DisallowedWithNoWindow) {
  const ui::Accelerator dummy;
  AccessibilityDelegate* delegate =
      ash::Shell::GetInstance()->accessibility_delegate();

  for (size_t i = 0; i < kActionsNeedingWindowLength; ++i) {
    delegate->TriggerAccessibilityAlert(ui::A11Y_ALERT_NONE);
    EXPECT_TRUE(
        GetController()->PerformAction(kActionsNeedingWindow[i], dummy));
    EXPECT_EQ(delegate->GetLastAccessibilityAlert(),
              ui::A11Y_ALERT_WINDOW_NEEDED);
  }

  // Make sure we don't alert if we do have a window.
  scoped_ptr<aura::Window> window;
  for (size_t i = 0; i < kActionsNeedingWindowLength; ++i) {
    window.reset(CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
    wm::ActivateWindow(window.get());
    delegate->TriggerAccessibilityAlert(ui::A11Y_ALERT_NONE);
    GetController()->PerformAction(kActionsNeedingWindow[i], dummy);
    EXPECT_NE(delegate->GetLastAccessibilityAlert(),
              ui::A11Y_ALERT_WINDOW_NEEDED);
  }

  // Don't alert if we have a minimized window either.
  for (size_t i = 0; i < kActionsNeedingWindowLength; ++i) {
    window.reset(CreateTestWindowInShellWithBounds(gfx::Rect(5, 5, 20, 20)));
    wm::ActivateWindow(window.get());
    GetController()->PerformAction(WINDOW_MINIMIZE, dummy);
    delegate->TriggerAccessibilityAlert(ui::A11Y_ALERT_NONE);
    GetController()->PerformAction(kActionsNeedingWindow[i], dummy);
    EXPECT_NE(delegate->GetLastAccessibilityAlert(),
              ui::A11Y_ALERT_WINDOW_NEEDED);
  }
}

}  // namespace ash
