// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_BOOKMARKS_BOOKMARKS_BRIDGE_H_
#define CHROME_BROWSER_ANDROID_BOOKMARKS_BOOKMARKS_BRIDGE_H_

#include <jni.h>

#include "base/android/jni_android.h"
#include "base/android/jni_weak_ref.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/android/bookmarks/partner_bookmarks_shim.h"
#include "chrome/browser/bookmarks/chrome_bookmark_client.h"
#include "components/bookmarks/browser/base_bookmark_model_observer.h"
#include "components/bookmarks/common/android/bookmark_id.h"

namespace bookmarks {
class ScopedGroupBookmarkActions;
}

class Profile;

// The delegate to fetch bookmarks information for the Android native
// bookmark page. This fetches the bookmarks, title, urls, folder
// hierarchy.
class BookmarksBridge : public BaseBookmarkModelObserver,
                        public PartnerBookmarksShim::Observer {
 public:
  BookmarksBridge(JNIEnv* env, jobject obj, jobject j_profile);
  void Destroy(JNIEnv*, jobject);

  static bool RegisterBookmarksBridge(JNIEnv* env);

  bool IsDoingExtensiveChanges(JNIEnv* env, jobject obj);

  void LoadEmptyPartnerBookmarkShimForTesting(JNIEnv* env, jobject obj);

  base::android::ScopedJavaLocalRef<jobject> GetBookmarkByID(
      JNIEnv* env,
      jobject obj,
      jlong id,
      jint type);

  void GetPermanentNodeIDs(JNIEnv* env,
                           jobject obj,
                           jobject j_result_obj);

  void GetTopLevelFolderParentIDs(JNIEnv* env,
                                  jobject obj,
                                  jobject j_result_obj);

  void GetTopLevelFolderIDs(JNIEnv* env,
                            jobject obj,
                            jboolean get_special,
                            jboolean get_normal,
                            jobject j_result_obj);

  void GetUncategorizedBookmarkIDs(JNIEnv* env,
                                   jobject obj,
                                   jobject j_result_obj);
  void GetAllFoldersWithDepths(JNIEnv* env,
                               jobject obj,
                               jobject j_folders_obj,
                               jobject j_depths_obj);

  base::android::ScopedJavaLocalRef<jobject> GetMobileFolderId(JNIEnv* env,
                                                               jobject obj);

  base::android::ScopedJavaLocalRef<jobject> GetOtherFolderId(JNIEnv* env,
                                                              jobject obj);

  base::android::ScopedJavaLocalRef<jobject> GetDesktopFolderId(JNIEnv* env,
                                                                jobject obj);

  void GetChildIDs(JNIEnv* env,
                   jobject obj,
                   jlong id,
                   jint type,
                   jboolean get_folders,
                   jboolean get_bookmarks,
                   jobject j_result_obj);

  base::android::ScopedJavaLocalRef<jobject> GetChildAt(JNIEnv* env,
                                                        jobject obj,
                                                        jlong id,
                                                        jint type,
                                                        jint index);

  void GetAllBookmarkIDsOrderedByCreationDate(JNIEnv* env,
                                              jobject obj,
                                              jobject j_result_obj);

  void SetBookmarkTitle(JNIEnv* env,
                        jobject obj,
                        jlong id,
                        jint type,
                        jstring title);

  void SetBookmarkUrl(JNIEnv* env,
                      jobject obj,
                      jlong id,
                      jint type,
                      jstring url);

  bool DoesBookmarkExist(JNIEnv* env, jobject obj, jlong id, jint type);

  void GetBookmarksForFolder(JNIEnv* env,
                             jobject obj,
                             jobject j_folder_id_obj,
                             jobject j_callback_obj,
                             jobject j_result_obj);

  void GetCurrentFolderHierarchy(JNIEnv* env,
                                 jobject obj,
                                 jobject j_folder_id_obj,
                                 jobject j_callback_obj,
                                 jobject j_result_obj);

  void SearchBookmarks(JNIEnv* env,
                       jobject obj,
                       jobject j_list,
                       jstring j_query,
                       jint max_results);

  base::android::ScopedJavaLocalRef<jobject> AddFolder(JNIEnv* env,
                                                       jobject obj,
                                                       jobject j_parent_id_obj,
                                                       jint index,
                                                       jstring j_title);

  void DeleteBookmark(JNIEnv* env, jobject obj, jobject j_bookmark_id_obj);

  void MoveBookmark(JNIEnv* env,
                    jobject obj,
                    jobject j_bookmark_id_obj,
                    jobject j_parent_id_obj,
                    jint index);

  base::android::ScopedJavaLocalRef<jobject> AddBookmark(
      JNIEnv* env,
      jobject obj,
      jobject j_parent_id_obj,
      jint index,
      jstring j_title,
      jstring j_url);

  void Undo(JNIEnv* env, jobject obj);

  void StartGroupingUndos(JNIEnv* env, jobject obj);

  void EndGroupingUndos(JNIEnv* env, jobject obj);

 private:
  virtual ~BookmarksBridge();

  base::android::ScopedJavaLocalRef<jobject> CreateJavaBookmark(
      const BookmarkNode* node);
  void ExtractBookmarkNodeInformation(
      const BookmarkNode* node,
      jobject j_result_obj);
  const BookmarkNode* GetNodeByID(long node_id, int type);
  const BookmarkNode* GetFolderWithFallback(long folder_id, int type);
  // Returns whether |node| can be modified by the user.
  bool IsEditable(const BookmarkNode* node) const;
  // Returns whether |node| is a managed bookmark.
  bool IsManaged(const BookmarkNode* node) const;
  const BookmarkNode* GetParentNode(const BookmarkNode* node);
  int GetBookmarkType(const BookmarkNode* node);
  base::string16 GetTitle(const BookmarkNode* node) const;
  bool IsReachable(const BookmarkNode* node) const;
  bool IsLoaded() const;
  bool IsFolderAvailable(const BookmarkNode* folder) const;
  void NotifyIfDoneLoading();

  // Override BaseBookmarkModelObserver.
  // Called when there are changes to the bookmark model that don't trigger
  // any of the other callback methods. For example, this is called when
  // partner bookmarks change.
  virtual void BookmarkModelChanged() override;
  virtual void BookmarkModelLoaded(BookmarkModel* model,
                                   bool ids_reassigned) override;
  virtual void BookmarkModelBeingDeleted(BookmarkModel* model) override;
  virtual void BookmarkNodeMoved(BookmarkModel* model,
                                 const BookmarkNode* old_parent,
                                 int old_index,
                                 const BookmarkNode* new_parent,
                                 int new_index) override;
  virtual void BookmarkNodeAdded(BookmarkModel* model,
                                 const BookmarkNode* parent,
                                 int index) override;
  virtual void BookmarkNodeRemoved(BookmarkModel* model,
                                   const BookmarkNode* parent,
                                   int old_index,
                                   const BookmarkNode* node,
                                   const std::set<GURL>& removed_urls) override;
  virtual void BookmarkNodeChanged(BookmarkModel* model,
                                   const BookmarkNode* node) override;
  virtual void BookmarkNodeChildrenReordered(BookmarkModel* model,
                                             const BookmarkNode* node) override;
  virtual void ExtensiveBookmarkChangesBeginning(BookmarkModel* model) override;
  virtual void ExtensiveBookmarkChangesEnded(BookmarkModel* model) override;

  // Override PartnerBookmarksShim::Observer
  virtual void PartnerShimChanged(PartnerBookmarksShim* shim) override;
  virtual void PartnerShimLoaded(PartnerBookmarksShim* shim) override;
  virtual void ShimBeingDeleted(PartnerBookmarksShim* shim) override;

  Profile* profile_;
  JavaObjectWeakGlobalRef weak_java_ref_;
  BookmarkModel* bookmark_model_;  // weak
  ChromeBookmarkClient* client_;   // weak
  scoped_ptr<bookmarks::ScopedGroupBookmarkActions> grouped_bookmark_actions_;

  // Information about the Partner bookmarks (must check for IsLoaded()).
  // This is owned by profile.
  PartnerBookmarksShim* partner_bookmarks_shim_;

  DISALLOW_COPY_AND_ASSIGN(BookmarksBridge);
};

#endif  // CHROME_BROWSER_ANDROID_BOOKMARKS_BOOKMARKS_BRIDGE_H_
