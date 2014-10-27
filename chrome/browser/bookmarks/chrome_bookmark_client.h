// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BOOKMARKS_CHROME_BOOKMARK_CLIENT_H_
#define CHROME_BROWSER_BOOKMARKS_CHROME_BOOKMARK_CLIENT_H_

#include <set>
#include <vector>

#include "base/callback_list.h"
#include "base/deferred_sequenced_task_runner.h"
#include "base/macros.h"
#include "components/bookmarks/browser/base_bookmark_model_observer.h"
#include "components/bookmarks/browser/bookmark_client.h"
#include "components/policy/core/browser/managed_bookmarks_tracker.h"

class BookmarkModel;
class GURL;
class HistoryService;
class HistoryServiceFactory;
class Profile;

class ChromeBookmarkClient : public bookmarks::BookmarkClient,
                             public BaseBookmarkModelObserver {
 public:
  explicit ChromeBookmarkClient(Profile* profile);
  ~ChromeBookmarkClient() override;

  void Init(BookmarkModel* model);

  // KeyedService:
  void Shutdown() override;

  // Returns the managed_node.
  const BookmarkNode* managed_node() { return managed_node_; }

  // Returns true if the given node belongs to the managed bookmarks tree.
  bool IsDescendantOfManagedNode(const BookmarkNode* node);

  // Returns true if there is at least one managed node in the |list|.
  bool HasDescendantsOfManagedNode(
      const std::vector<const BookmarkNode*>& list);

  // bookmarks::BookmarkClient:
  bool PreferTouchIcon() override;
  base::CancelableTaskTracker::TaskId GetFaviconImageForPageURL(
      const GURL& page_url,
      favicon_base::IconType type,
      const favicon_base::FaviconImageCallback& callback,
      base::CancelableTaskTracker* tracker) override;
  bool SupportsTypedCountForNodes() override;
  void GetTypedCountForNodes(
      const NodeSet& nodes,
      NodeTypedCountPairs* node_typed_count_pairs) override;
  bool IsPermanentNodeVisible(const BookmarkPermanentNode* node) override;
  void RecordAction(const base::UserMetricsAction& action) override;
  bookmarks::LoadExtraCallback GetLoadExtraNodesCallback() override;
  bool CanSetPermanentNodeTitle(const BookmarkNode* permanent_node) override;
  bool CanSyncNode(const BookmarkNode* node) override;
  bool CanBeEditedByUser(const BookmarkNode* node) override;

 private:
  friend class HistoryServiceFactory;
  void SetHistoryService(HistoryService* history_service);

  // BaseBookmarkModelObserver:
  void BookmarkModelChanged() override;
  void BookmarkNodeRemoved(BookmarkModel* model,
                           const BookmarkNode* parent,
                           int old_index,
                           const BookmarkNode* node,
                           const std::set<GURL>& removed_urls) override;
  void BookmarkAllUserNodesRemoved(BookmarkModel* model,
                                   const std::set<GURL>& removed_urls) override;
  void BookmarkModelLoaded(BookmarkModel* model, bool ids_reassigned) override;

  // Helper for GetLoadExtraNodesCallback().
  static bookmarks::BookmarkPermanentNodeList LoadExtraNodes(
      scoped_ptr<BookmarkPermanentNode> managed_node,
      scoped_ptr<base::ListValue> initial_managed_bookmarks,
      int64* next_node_id);

  // Returns the management domain that configured the managed bookmarks,
  // or an empty string.
  std::string GetManagedBookmarksDomain();

  Profile* profile_;

  // HistoryService associated to the Profile. Due to circular dependency, this
  // cannot be passed to the constructor, nor lazily fetched. Instead the value
  // is initialized from HistoryServiceFactory.
  HistoryService* history_service_;

  scoped_ptr<base::CallbackList<void(const std::set<GURL>&)>::Subscription>
      favicon_changed_subscription_;

  // Pointer to the BookmarkModel. Will be non-NULL from the call to Init to
  // the call to Shutdown. Must be valid for the whole interval.
  BookmarkModel* model_;

  scoped_ptr<policy::ManagedBookmarksTracker> managed_bookmarks_tracker_;
  BookmarkPermanentNode* managed_node_;

  DISALLOW_COPY_AND_ASSIGN(ChromeBookmarkClient);
};

#endif  // CHROME_BROWSER_BOOKMARKS_CHROME_BOOKMARK_CLIENT_H_
