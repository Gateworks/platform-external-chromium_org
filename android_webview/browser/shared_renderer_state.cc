// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/shared_renderer_state.h"

#include "android_webview/browser/browser_view_renderer.h"
#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/location.h"

namespace android_webview {

namespace internal {

class RequestDrawGLTracker {
 public:
  RequestDrawGLTracker();
  bool ShouldRequestOnNonUiThread(SharedRendererState* state);
  bool ShouldRequestOnUiThread(SharedRendererState* state);
  void DidRequestOnUiThread();
  void ResetPending();

 private:
  base::Lock lock_;
  SharedRendererState* pending_ui_;
  SharedRendererState* pending_non_ui_;
};

RequestDrawGLTracker::RequestDrawGLTracker()
    : pending_ui_(NULL), pending_non_ui_(NULL) {
}

bool RequestDrawGLTracker::ShouldRequestOnNonUiThread(
    SharedRendererState* state) {
  base::AutoLock lock(lock_);
  if (pending_ui_ || pending_non_ui_)
    return false;
  pending_non_ui_ = state;
  return true;
}

bool RequestDrawGLTracker::ShouldRequestOnUiThread(SharedRendererState* state) {
  base::AutoLock lock(lock_);
  if (pending_non_ui_) {
    pending_non_ui_->ResetRequestDrawGLCallback();
    pending_non_ui_ = NULL;
  }
  if (pending_ui_)
    return false;
  pending_ui_ = state;
  return true;
}

void RequestDrawGLTracker::ResetPending() {
  base::AutoLock lock(lock_);
  pending_non_ui_ = NULL;
  pending_ui_ = NULL;
}

}  // namespace internal

namespace {

base::LazyInstance<internal::RequestDrawGLTracker> g_request_draw_gl_tracker =
    LAZY_INSTANCE_INITIALIZER;

}

SharedRendererState::SharedRendererState(
    const scoped_refptr<base::SingleThreadTaskRunner>& ui_loop,
    BrowserViewRenderer* browser_view_renderer)
    : ui_loop_(ui_loop),
      browser_view_renderer_(browser_view_renderer),
      force_commit_(false),
      inside_hardware_release_(false),
      needs_force_invalidate_on_next_draw_gl_(false),
      weak_factory_on_ui_thread_(this) {
  DCHECK(ui_loop_->BelongsToCurrentThread());
  DCHECK(browser_view_renderer_);
  ui_thread_weak_ptr_ = weak_factory_on_ui_thread_.GetWeakPtr();
  ResetRequestDrawGLCallback();
}

SharedRendererState::~SharedRendererState() {
  DCHECK(ui_loop_->BelongsToCurrentThread());
}

void SharedRendererState::ClientRequestDrawGL() {
  if (ui_loop_->BelongsToCurrentThread()) {
    if (!g_request_draw_gl_tracker.Get().ShouldRequestOnUiThread(this))
      return;
    ClientRequestDrawGLOnUIThread();
  } else {
    if (!g_request_draw_gl_tracker.Get().ShouldRequestOnNonUiThread(this))
      return;
    base::Closure callback;
    {
      base::AutoLock lock(lock_);
      callback = request_draw_gl_closure_;
    }
    ui_loop_->PostTask(FROM_HERE, callback);
  }
}

void SharedRendererState::DidDrawGLProcess() {
  g_request_draw_gl_tracker.Get().ResetPending();
}

void SharedRendererState::ResetRequestDrawGLCallback() {
  DCHECK(ui_loop_->BelongsToCurrentThread());
  base::AutoLock lock(lock_);
  request_draw_gl_cancelable_closure_.Reset(
      base::Bind(&SharedRendererState::ClientRequestDrawGLOnUIThread,
                 base::Unretained(this)));
  request_draw_gl_closure_ = request_draw_gl_cancelable_closure_.callback();
}

void SharedRendererState::ClientRequestDrawGLOnUIThread() {
  DCHECK(ui_loop_->BelongsToCurrentThread());
  ResetRequestDrawGLCallback();
  if (!browser_view_renderer_->RequestDrawGL(false)) {
    g_request_draw_gl_tracker.Get().ResetPending();
    LOG(ERROR) << "Failed to request GL process. Deadlock likely";
  }
}

void SharedRendererState::UpdateParentDrawConstraintsOnUIThread() {
  DCHECK(ui_loop_->BelongsToCurrentThread());
  browser_view_renderer_->UpdateParentDrawConstraints();
}

void SharedRendererState::SetScrollOffsetOnUI(gfx::Vector2d scroll_offset) {
  base::AutoLock lock(lock_);
  scroll_offset_ = scroll_offset;
}

gfx::Vector2d SharedRendererState::GetScrollOffsetOnRT() {
  base::AutoLock lock(lock_);
  return scroll_offset_;
}

bool SharedRendererState::HasCompositorFrameOnUI() const {
  base::AutoLock lock(lock_);
  return compositor_frame_.get();
}

void SharedRendererState::SetCompositorFrameOnUI(
    scoped_ptr<cc::CompositorFrame> frame,
    bool force_commit) {
  base::AutoLock lock(lock_);
  DCHECK(!compositor_frame_.get());
  compositor_frame_ = frame.Pass();
  force_commit_ = force_commit;
}

scoped_ptr<cc::CompositorFrame> SharedRendererState::PassCompositorFrame() {
  base::AutoLock lock(lock_);
  return compositor_frame_.Pass();
}

bool SharedRendererState::ForceCommitOnRT() const {
  base::AutoLock lock(lock_);
  return force_commit_;
}

bool SharedRendererState::UpdateDrawConstraintsOnRT(
    const ParentCompositorDrawConstraints& parent_draw_constraints) {
  base::AutoLock lock(lock_);
  if (needs_force_invalidate_on_next_draw_gl_ ||
      !parent_draw_constraints_.Equals(parent_draw_constraints)) {
    parent_draw_constraints_ = parent_draw_constraints;
    return true;
  }

  return false;
}

void SharedRendererState::PostExternalDrawConstraintsToChildCompositorOnRT(
    const ParentCompositorDrawConstraints& parent_draw_constraints) {
  if (UpdateDrawConstraintsOnRT(parent_draw_constraints)) {
    // No need to hold the lock_ during the post task.
    ui_loop_->PostTask(
        FROM_HERE,
        base::Bind(&SharedRendererState::UpdateParentDrawConstraintsOnUIThread,
                   ui_thread_weak_ptr_));
  }
}

void SharedRendererState::DidSkipCommitFrameOnRT() {
  ui_loop_->PostTask(FROM_HERE,
                     base::Bind(&SharedRendererState::DidSkipCommitFrameOnUI,
                                ui_thread_weak_ptr_));
}

void SharedRendererState::DidSkipCommitFrameOnUI() {
  DCHECK(ui_loop_->BelongsToCurrentThread());
  browser_view_renderer_->DidSkipCommitFrame();
}

ParentCompositorDrawConstraints
SharedRendererState::GetParentDrawConstraintsOnUI() const {
  base::AutoLock lock(lock_);
  return parent_draw_constraints_;
}

void SharedRendererState::SetForceInvalidateOnNextDrawGLOnUI(
    bool needs_force_invalidate_on_next_draw_gl) {
  base::AutoLock lock(lock_);
  needs_force_invalidate_on_next_draw_gl_ =
      needs_force_invalidate_on_next_draw_gl;
}

bool SharedRendererState::NeedsForceInvalidateOnNextDrawGLOnUI() const {
  base::AutoLock lock(lock_);
  return needs_force_invalidate_on_next_draw_gl_;
}

void SharedRendererState::SetInsideHardwareRelease(bool inside) {
  base::AutoLock lock(lock_);
  inside_hardware_release_ = inside;
}

bool SharedRendererState::IsInsideHardwareRelease() const {
  base::AutoLock lock(lock_);
  return inside_hardware_release_;
}

void SharedRendererState::InsertReturnedResourcesOnRT(
    const cc::ReturnedResourceArray& resources) {
  base::AutoLock lock(lock_);
  returned_resources_.insert(
      returned_resources_.end(), resources.begin(), resources.end());
}

void SharedRendererState::SwapReturnedResourcesOnUI(
    cc::ReturnedResourceArray* resources) {
  DCHECK(resources->empty());
  base::AutoLock lock(lock_);
  resources->swap(returned_resources_);
}

bool SharedRendererState::ReturnedResourcesEmpty() const {
  base::AutoLock lock(lock_);
  return returned_resources_.empty();
}

InsideHardwareReleaseReset::InsideHardwareReleaseReset(
    SharedRendererState* shared_renderer_state)
    : shared_renderer_state_(shared_renderer_state) {
  DCHECK(!shared_renderer_state_->IsInsideHardwareRelease());
  shared_renderer_state_->SetInsideHardwareRelease(true);
}

InsideHardwareReleaseReset::~InsideHardwareReleaseReset() {
  shared_renderer_state_->SetInsideHardwareRelease(false);
}

}  // namespace android_webview
