// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_IME_MODE_INDICATOR_VIEW_H_
#define ASH_IME_MODE_INDICATOR_VIEW_H_

#include "ash/ash_export.h"
#include "base/strings/string16.h"
#include "base/timer/timer.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/rect.h"
#include "ui/views/bubble/bubble_delegate.h"

namespace views {
class Label;
class Widget;
}  // namespace views

namespace ash {
namespace ime {

class ASH_EXPORT ModeIndicatorView : public views::BubbleDelegateView {
 public:
  ModeIndicatorView(gfx::NativeView parent,
                    const gfx::Rect& cursor_bounds,
                    const base::string16& label);
  ~ModeIndicatorView() override;

  // Show the mode indicator then hide with fading animation.
  void ShowAndFadeOut();

  // views::BubbleDelegateView override:
  gfx::Size GetPreferredSize() const override;

 protected:
  // views::BubbleDelegateView override:
  void Init() override;

  // views::WidgetDelegateView overrides:
  views::NonClientFrameView* CreateNonClientFrameView(
      views::Widget* widget) override;

 private:
  gfx::Rect cursor_bounds_;
  views::Label* label_view_;
  base::OneShotTimer<views::Widget> timer_;

  DISALLOW_COPY_AND_ASSIGN(ModeIndicatorView);
};

}  // namespace ime
}  // namespace ash

#endif  // ASH_IME_MODE_INDICATOR_VIEW_H_
