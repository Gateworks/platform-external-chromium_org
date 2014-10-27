// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_ENHANCED_BOOKMARKS_ENHANCED_BOOKMARKS_BRIDGE_H_
#define CHROME_BROWSER_ANDROID_ENHANCED_BOOKMARKS_ENHANCED_BOOKMARKS_BRIDGE_H_

#include "base/android/jni_android.h"
#include "base/android/jni_weak_ref.h"
#include "chrome/browser/profiles/profile.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/enhanced_bookmarks/bookmark_server_service.h"

namespace enhanced_bookmarks {

class BookmarkServerClusterService;

namespace android {

class EnhancedBookmarksBridge : public BookmarkServerServiceObserver {
 public:
  EnhancedBookmarksBridge(JNIEnv* env, jobject obj, Profile* profile);
  virtual ~EnhancedBookmarksBridge();
  void Destroy(JNIEnv*, jobject);

  base::android::ScopedJavaLocalRef<jstring> GetBookmarkDescription(
      JNIEnv* env,
      jobject obj,
      jlong id,
      jint type);

  void SetBookmarkDescription(JNIEnv* env,
                              jobject obj,
                              jlong id,
                              jint type,
                              jstring description);
  void GetBookmarksForFilter(JNIEnv* env,
                             jobject obj,
                             jstring filter,
                             jobject j_result_obj);
  base::android::ScopedJavaLocalRef<jobjectArray> GetFilters(JNIEnv* env,
                                                             jobject obj);

  // BookmarkServerServiceObserver
  // Called on changes to cluster data
  virtual void OnChange(BookmarkServerService* service) override;

 private:
  JavaObjectWeakGlobalRef weak_java_ref_;
  BookmarkModel* bookmark_model_;  // weak
  BookmarkServerClusterService* cluster_service_;  // weak
  Profile* profile_;                       // weak
  DISALLOW_COPY_AND_ASSIGN(EnhancedBookmarksBridge);
};

bool RegisterEnhancedBookmarksBridge(JNIEnv* env);

}  // namespace android
}  // namespace enhanced_bookmarks

#endif  // CHROME_BROWSER_ANDROID_ENHANCED_BOOKMARKS_ENHANCED_BOOKMARKS_BRIDGE_H_
