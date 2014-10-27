// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "athena/virtual_keyboard/public/virtual_keyboard_manager.h"

#include "athena/screen/public/screen_manager.h"
#include "athena/util/container_priorities.h"
#include "athena/util/fill_layout_manager.h"
#include "base/bind.h"
#include "base/memory/singleton.h"
#include "base/values.h"
#include "content/public/browser/browser_context.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/layout_manager.h"
#include "ui/aura/window.h"
#include "ui/keyboard/keyboard.h"
#include "ui/keyboard/keyboard_constants.h"
#include "ui/keyboard/keyboard_controller.h"
#include "ui/keyboard/keyboard_controller_proxy.h"
#include "ui/keyboard/keyboard_util.h"

namespace athena {

namespace {

VirtualKeyboardManager* instance;

// A very basic and simple implementation of KeyboardControllerProxy.
class BasicKeyboardControllerProxy : public keyboard::KeyboardControllerProxy {
 public:
  BasicKeyboardControllerProxy(content::BrowserContext* context,
                               aura::Window* root_window)
      : browser_context_(context), root_window_(root_window) {}
  ~BasicKeyboardControllerProxy() override {}

  // keyboard::KeyboardControllerProxy:
  virtual ui::InputMethod* GetInputMethod() override {
    ui::InputMethod* input_method =
        root_window_->GetProperty(aura::client::kRootWindowInputMethodKey);
    return input_method;
  }

  virtual void RequestAudioInput(
      content::WebContents* web_contents,
      const content::MediaStreamRequest& request,
      const content::MediaResponseCallback& callback) override {}

  virtual content::BrowserContext* GetBrowserContext() override {
    return browser_context_;
  }

  virtual void SetUpdateInputType(ui::TextInputType type) override {}

 private:
  content::BrowserContext* browser_context_;
  aura::Window* root_window_;

  DISALLOW_COPY_AND_ASSIGN(BasicKeyboardControllerProxy);
};

class VirtualKeyboardManagerImpl : public VirtualKeyboardManager {
 public:
  explicit VirtualKeyboardManagerImpl(content::BrowserContext* browser_context)
      : browser_context_(browser_context), container_(nullptr) {
    CHECK(!instance);
    instance = this;
    Init();
  }

  ~VirtualKeyboardManagerImpl() override {
    CHECK_EQ(this, instance);
    instance = nullptr;

    keyboard::KeyboardController::ResetInstance(nullptr);
  }

 private:
  void Init() {
    athena::ScreenManager::ContainerParams params("VirtualKeyboardContainer",
                                                  CP_VIRTUAL_KEYBOARD);
    container_ = athena::ScreenManager::Get()->CreateContainer(params);
    container_->SetLayoutManager(new FillLayoutManager(container_));

    keyboard::KeyboardController* controller = new keyboard::KeyboardController(
        new BasicKeyboardControllerProxy(browser_context_,
                                         container_->GetRootWindow()));
    // ResetInstance takes ownership.
    keyboard::KeyboardController::ResetInstance(controller);
    aura::Window* kb_container = controller->GetContainerWindow();
    container_->AddChild(kb_container);
    kb_container->Show();
  }

  content::BrowserContext* browser_context_;
  aura::Window* container_;

  DISALLOW_COPY_AND_ASSIGN(VirtualKeyboardManagerImpl);
};

}  // namespace

// static
VirtualKeyboardManager* VirtualKeyboardManager::Create(
    content::BrowserContext* browser_context) {
  CHECK(!instance);
  keyboard::InitializeKeyboard();
  keyboard::SetTouchKeyboardEnabled(true);
  keyboard::InitializeWebUIBindings();

  new VirtualKeyboardManagerImpl(browser_context);
  CHECK(instance);
  return instance;
}

VirtualKeyboardManager* VirtualKeyboardManager::Get() {
  return instance;
}

void VirtualKeyboardManager::Shutdown() {
  CHECK(instance);
  delete instance;
  CHECK(!instance);
}

}  // namespace athena
