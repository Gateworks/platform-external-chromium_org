// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_OPAQUE_BROWSER_FRAME_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_OPAQUE_BROWSER_FRAME_VIEW_H_

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_frame.h"
#include "chrome/browser/ui/views/frame/browser_non_client_frame_view.h"
#include "chrome/browser/ui/views/frame/opaque_browser_frame_view_layout_delegate.h"
#include "chrome/browser/ui/views/tab_icon_view_model.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/menu_button_listener.h"
#include "ui/views/window/non_client_view.h"

class BrowserView;
class OpaqueBrowserFrameViewLayout;
class OpaqueBrowserFrameViewPlatformSpecific;
class TabIconView;
class NewAvatarButton;

namespace views {
class ImageButton;
class FrameBackground;
class Label;
}

class OpaqueBrowserFrameView : public BrowserNonClientFrameView,
                               public content::NotificationObserver,
                               public views::ButtonListener,
                               public views::MenuButtonListener,
                               public chrome::TabIconViewModel,
                               public OpaqueBrowserFrameViewLayoutDelegate {
 public:
  // Constructs a non-client view for an BrowserFrame.
  OpaqueBrowserFrameView(BrowserFrame* frame, BrowserView* browser_view);
  virtual ~OpaqueBrowserFrameView();

  // BrowserNonClientFrameView:
  virtual gfx::Rect GetBoundsForTabStrip(views::View* tabstrip) const override;
  virtual int GetTopInset() const override;
  virtual int GetThemeBackgroundXInset() const override;
  virtual void UpdateThrobber(bool running) override;
  virtual gfx::Size GetMinimumSize() const override;

  // views::NonClientFrameView:
  virtual gfx::Rect GetBoundsForClientView() const override;
  virtual gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const override;
  virtual int NonClientHitTest(const gfx::Point& point) override;
  virtual void GetWindowMask(const gfx::Size& size, gfx::Path* window_mask)
      override;
  virtual void ResetWindowControls() override;
  virtual void UpdateWindowIcon() override;
  virtual void UpdateWindowTitle() override;
  virtual void SizeConstraintsChanged() override;

  // views::View:
  virtual void GetAccessibleState(ui::AXViewState* state) override;

  // views::ButtonListener:
  virtual void ButtonPressed(views::Button* sender, const ui::Event& event)
      override;

  // views::MenuButtonListener:
  virtual void OnMenuButtonClicked(views::View* source, const gfx::Point& point)
      override;

  // chrome::TabIconViewModel:
  virtual bool ShouldTabIconViewAnimate() const override;
  virtual gfx::ImageSkia GetFaviconForTabIconView() override;

  // content::NotificationObserver implementation:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) override;

  // OpaqueBrowserFrameViewLayoutDelegate implementation:
  virtual bool ShouldShowWindowIcon() const override;
  virtual bool ShouldShowWindowTitle() const override;
  virtual base::string16 GetWindowTitle() const override;
  virtual int GetIconSize() const override;
  virtual bool ShouldLeaveOffsetNearTopBorder() const override;
  virtual gfx::Size GetBrowserViewMinimumSize() const override;
  virtual bool ShouldShowCaptionButtons() const override;
  virtual bool ShouldShowAvatar() const override;
  virtual bool IsRegularOrGuestSession() const override;
  virtual gfx::ImageSkia GetOTRAvatarIcon() const override;
  virtual bool IsMaximized() const override;
  virtual bool IsMinimized() const override;
  virtual bool IsFullscreen() const override;
  virtual bool IsTabStripVisible() const override;
  virtual int GetTabStripHeight() const override;
  virtual gfx::Size GetTabstripPreferredSize() const override;

 protected:
  views::ImageButton* minimize_button() const { return minimize_button_; }
  views::ImageButton* maximize_button() const { return maximize_button_; }
  views::ImageButton* restore_button() const { return restore_button_; }
  views::ImageButton* close_button() const { return close_button_; }

  // views::View:
  virtual void OnPaint(gfx::Canvas* canvas) override;

 private:
  // views::NonClientFrameView:
  virtual bool DoesIntersectRect(const views::View* target,
                                 const gfx::Rect& rect) const override;

  // Creates, adds and returns a new image button with |this| as its listener.
  // Memory is owned by the caller.
  views::ImageButton* InitWindowCaptionButton(int normal_image_id,
                                              int hot_image_id,
                                              int pushed_image_id,
                                              int mask_image_id,
                                              int accessibility_string_id,
                                              ViewID view_id);

  // Returns the thickness of the border that makes up the window frame edges.
  // This does not include any client edge.  If |restored| is true, acts as if
  // the window is restored regardless of the real mode.
  int FrameBorderThickness(bool restored) const;

  // Returns the height of the top resize area.  This is smaller than the frame
  // border height in order to increase the window draggable area.
  int TopResizeHeight() const;

  // Returns true if the specified point is within the avatar menu buttons.
  bool IsWithinAvatarMenuButtons(const gfx::Point& point) const;

  // Returns the thickness of the entire nonclient left, right, and bottom
  // borders, including both the window frame and any client edge.
  int NonClientBorderThickness() const;

  // Returns the bounds of the titlebar icon (or where the icon would be if
  // there was one).
  gfx::Rect IconBounds() const;

  // Returns true if the view should draw its own custom title bar.
  bool ShouldShowWindowTitleBar() const;

  // Paint various sub-components of this view.  The *FrameBorder() functions
  // also paint the background of the titlebar area, since the top frame border
  // and titlebar background are a contiguous component.
  void PaintRestoredFrameBorder(gfx::Canvas* canvas);
  void PaintMaximizedFrameBorder(gfx::Canvas* canvas);
  void PaintToolbarBackground(gfx::Canvas* canvas);
  void PaintRestoredClientEdge(gfx::Canvas* canvas);

  // Compute aspects of the frame needed to paint the frame background.
  SkColor GetFrameColor() const;
  gfx::ImageSkia* GetFrameImage() const;
  gfx::ImageSkia* GetFrameOverlayImage() const;
  int GetTopAreaHeight() const;

  // Returns the bounds of the client area for the specified view size.
  gfx::Rect CalculateClientAreaBounds(int width, int height) const;

  // Our layout manager also calculates various bounds.
  OpaqueBrowserFrameViewLayout* layout_;

  // Window controls.
  views::ImageButton* minimize_button_;
  views::ImageButton* maximize_button_;
  views::ImageButton* restore_button_;
  views::ImageButton* close_button_;

  // The window icon and title.
  TabIconView* window_icon_;
  views::Label* window_title_;

  content::NotificationRegistrar registrar_;

  // Background painter for the window frame.
  scoped_ptr<views::FrameBackground> frame_background_;

  // Observer that handles platform dependent configuration.
  scoped_ptr<OpaqueBrowserFrameViewPlatformSpecific> platform_observer_;

  DISALLOW_COPY_AND_ASSIGN(OpaqueBrowserFrameView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_OPAQUE_BROWSER_FRAME_VIEW_H_
