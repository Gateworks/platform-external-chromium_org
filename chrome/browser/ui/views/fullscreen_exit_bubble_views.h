// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FULLSCREEN_EXIT_BUBBLE_VIEWS_H_
#define CHROME_BROWSER_UI_VIEWS_FULLSCREEN_EXIT_BUBBLE_VIEWS_H_

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/fullscreen/fullscreen_exit_bubble.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;
class GURL;
namespace gfx {
class SlideAnimation;
}
namespace views {
class View;
class Widget;
}

// FullscreenExitBubbleViews is responsible for showing a bubble atop the
// screen in fullscreen mode, telling users how to exit and providing a click
// target. The bubble auto-hides, and re-shows when the user moves to the
// screen top.
class FullscreenExitBubbleViews : public FullscreenExitBubble,
                                  public content::NotificationObserver,
                                  public views::WidgetObserver {
 public:
  FullscreenExitBubbleViews(BrowserView* browser,
                            const GURL& url,
                            FullscreenExitBubbleType bubble_type);
  ~FullscreenExitBubbleViews() override;

  void UpdateContent(const GURL& url, FullscreenExitBubbleType bubble_type);

  // Repositions |popup_| if it is visible.
  void RepositionIfVisible();

 private:
  class FullscreenExitView;

  enum AnimatedAttribute {
    ANIMATED_ATTRIBUTE_BOUNDS,
    ANIMATED_ATTRIBUTE_OPACITY
  };

  // Starts or stops polling the mouse location based on |popup_| and
  // |bubble_type_|.
  void UpdateMouseWatcher();

  // Updates any state which depends on whether the user is in immersive
  // fullscreen.
  void UpdateForImmersiveState();

  // Updates |popup|'s bounds given |animation_| and |animated_attribute_|.
  void UpdateBounds();

  // Returns the root view containing |browser_view_|.
  views::View* GetBrowserRootView() const;

  // FullScreenExitBubble overrides:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;
  gfx::Rect GetPopupRect(bool ignore_animation_state) const override;
  gfx::Point GetCursorScreenPoint() override;
  bool WindowContainsPoint(gfx::Point pos) override;
  bool IsWindowActive() override;
  void Hide() override;
  void Show() override;
  bool IsAnimating() override;
  bool CanMouseTriggerSlideIn() const override;

  // content::NotificationObserver override:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  // views::WidgetObserver override:
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;

  BrowserView* browser_view_;

  views::Widget* popup_;

  // Animation controlling showing/hiding of the exit bubble.
  scoped_ptr<gfx::SlideAnimation> animation_;

  // Attribute animated by |animation_|.
  AnimatedAttribute animated_attribute_;

  // The contents of the popup.
  FullscreenExitView* view_;

  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(FullscreenExitBubbleViews);
};

#endif  // CHROME_BROWSER_UI_VIEWS_FULLSCREEN_EXIT_BUBBLE_VIEWS_H_
