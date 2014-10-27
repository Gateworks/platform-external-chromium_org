// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/jni_android.h"
#include "base/android/jni_registrar.h"
#include "base/android/library_loader/library_loader_hooks.h"
#include "base/basictypes.h"
#include "base/debug/debugger.h"
#include "base/logging.h"
#include "chromecast/android/cast_jni_registrar.h"
#include "chromecast/android/platform_jni_loader.h"
#include "chromecast/app/cast_main_delegate.h"
#include "content/public/app/android_library_loader_hooks.h"
#include "content/public/app/content_main.h"
#include "content/public/browser/android/compositor.h"

// This is called by the VM when the shared library is first loaded.
JNI_EXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  base::android::SetLibraryLoadedHook(&content::LibraryLoaded);
  base::android::InitVM(vm);
  JNIEnv* env = base::android::AttachCurrentThread();

  if (!base::android::RegisterLibraryLoaderEntryHook(env)) return -1;

  // To be called only from the UI thread.  If loading the library is done on
  // a separate thread, this should be moved elsewhere.
  if (!chromecast::android::RegisterJni(env)) return -1;
  // Allow platform-specific implementations to perform more JNI registration.
  if (!chromecast::android::PlatformRegisterJni(env)) return -1;

  content::Compositor::Initialize();
  content::SetContentMainDelegate(new chromecast::shell::CastMainDelegate);

  return JNI_VERSION_1_4;
}
