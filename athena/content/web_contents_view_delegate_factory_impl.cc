// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "athena/content/public/web_contents_view_delegate_creator.h"

#include "athena/content/render_view_context_menu_impl.h"
#include "components/renderer_context_menu/context_menu_delegate.h"
#include "components/web_modal/popup_manager.h"
#include "components/web_modal/single_web_contents_dialog_manager.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_view_delegate.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/window.h"
#include "ui/views/widget/widget.h"

namespace athena {
namespace {

class WebContentsViewDelegateImpl : public content::WebContentsViewDelegate,
                                    public ContextMenuDelegate {
 public:
  explicit WebContentsViewDelegateImpl(content::WebContents* web_contents)
      : ContextMenuDelegate(web_contents), web_contents_(web_contents) {}
  ~WebContentsViewDelegateImpl() override {}

  content::WebDragDestDelegate* GetDragDestDelegate() override {
    // TODO(oshima): crbug.com/401610
    NOTIMPLEMENTED();
    return nullptr;
  }

  bool Focus() override {
    web_modal::PopupManager* popup_manager =
        web_modal::PopupManager::FromWebContents(web_contents_);
    if (popup_manager)
      popup_manager->WasFocused(web_contents_);
    return false;
  }

  void ShowContextMenu(content::RenderFrameHost* render_frame_host,
                       const content::ContextMenuParams& params) override {
    ShowMenu(BuildMenu(
        content::WebContents::FromRenderFrameHost(render_frame_host), params));
  }

  void SizeChanged(const gfx::Size& size) override {
    // TODO(oshima|sadrul): Implement this when sad_tab is componentized.
    // See c/b/ui/views/tab_contents/chrome_web_contents_view_delegate_views.cc
  }

  void ShowDisambiguationPopup(
      const gfx::Rect& target_rect,
      const SkBitmap& zoomed_bitmap,
      const gfx::NativeView content,
      const base::Callback<void(ui::GestureEvent*)>& gesture_cb,
      const base::Callback<void(ui::MouseEvent*)>& mouse_cb) override {
    NOTIMPLEMENTED();
  }

  void HideDisambiguationPopup() override { NOTIMPLEMENTED(); }

  // ContextMenuDelegate:
  scoped_ptr<RenderViewContextMenuBase> BuildMenu(
      content::WebContents* web_contents,
      const content::ContextMenuParams& params) override {
    scoped_ptr<RenderViewContextMenuBase> menu;
    content::RenderFrameHost* focused_frame = web_contents->GetFocusedFrame();
    // If the frame tree does not have a focused frame at this point, do not
    // bother creating RenderViewContextMenuViews.
    // This happens if the frame has navigated to a different page before
    // ContextMenu message was received by the current RenderFrameHost.
    if (focused_frame) {
      menu.reset(new RenderViewContextMenuImpl(focused_frame, params));
      menu->Init();
    }
    return menu.Pass();
  }
  void ShowMenu(scoped_ptr<RenderViewContextMenuBase> menu) override {
    context_menu_.reset(menu.release());

    if (!context_menu_)
      return;

    context_menu_->Show();
  }

  aura::Window* GetActiveNativeView() {
    return web_contents_->GetFullscreenRenderWidgetHostView()
               ? web_contents_->GetFullscreenRenderWidgetHostView()
                     ->GetNativeView()
               : web_contents_->GetNativeView();
  }

  views::Widget* GetTopLevelWidget() {
    return views::Widget::GetTopLevelWidgetForNativeView(GetActiveNativeView());
  }

  views::FocusManager* GetFocusManager() {
    views::Widget* toplevel_widget = GetTopLevelWidget();
    return toplevel_widget ? toplevel_widget->GetFocusManager() : nullptr;
  }

  void SetInitialFocus() {
    if (web_contents_->FocusLocationBarByDefault()) {
      if (web_contents_->GetDelegate())
        web_contents_->GetDelegate()->SetFocusToLocationBar(false);
    } else {
      web_contents_->Focus();
    }
  }
  scoped_ptr<RenderViewContextMenuBase> context_menu_;
  content::WebContents* web_contents_;
  DISALLOW_COPY_AND_ASSIGN(WebContentsViewDelegateImpl);
};

}  // namespace

content::WebContentsViewDelegate* CreateWebContentsViewDelegate(
    content::WebContents* web_contents) {
  return new WebContentsViewDelegateImpl(web_contents);
}

}  // namespace athena
