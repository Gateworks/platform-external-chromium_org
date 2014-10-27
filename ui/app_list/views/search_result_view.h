// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_APP_LIST_VIEWS_SEARCH_RESULT_VIEW_H_
#define UI_APP_LIST_VIEWS_SEARCH_RESULT_VIEW_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "ui/app_list/search_result_observer.h"
#include "ui/app_list/views/search_result_actions_view_delegate.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/custom_button.h"

namespace gfx {
class RenderText;
}

namespace views {
class ImageButton;
class ImageView;
class MenuRunner;
}

namespace app_list {
namespace test {
class SearchResultListViewTest;
}  // namespace test

class ProgressBarView;
class SearchResult;
class SearchResultListView;
class SearchResultViewDelegate;
class SearchResultActionsView;

// SearchResultView displays a SearchResult.
class SearchResultView : public views::CustomButton,
                         public views::ButtonListener,
                         public views::ContextMenuController,
                         public SearchResultObserver,
                         public SearchResultActionsViewDelegate {
 public:
  // Internal class name.
  static const char kViewClassName[];

  explicit SearchResultView(SearchResultListView* list_view);
  virtual ~SearchResultView();

  // Sets/gets SearchResult displayed by this view.
  void SetResult(SearchResult* result);
  SearchResult* result() { return result_; }

  // Clears reference to SearchResult but don't schedule repaint.
  void ClearResultNoRepaint();

  // Clears the selected action.
  void ClearSelectedAction();

 private:
  friend class app_list::test::SearchResultListViewTest;

  void UpdateTitleText();
  void UpdateDetailsText();

  // views::View overrides:
  virtual const char* GetClassName() const override;
  virtual gfx::Size GetPreferredSize() const override;
  virtual void Layout() override;
  virtual bool OnKeyPressed(const ui::KeyEvent& event) override;
  virtual void ChildPreferredSizeChanged(views::View* child) override;
  virtual void OnPaint(gfx::Canvas* canvas) override;

  // views::ButtonListener overrides:
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) override;

  // views::ContextMenuController overrides:
  virtual void ShowContextMenuForView(views::View* source,
                                      const gfx::Point& point,
                                      ui::MenuSourceType source_type) override;

  // SearchResultObserver overrides:
  virtual void OnIconChanged() override;
  virtual void OnActionsChanged() override;
  virtual void OnIsInstallingChanged() override;
  virtual void OnPercentDownloadedChanged() override;
  virtual void OnItemInstalled() override;
  virtual void OnItemUninstalled() override;

  // SearchResultActionsViewDelegate overrides:
  virtual void OnSearchResultActionActivated(size_t index,
                                             int event_flags) override;

  SearchResult* result_;  // Owned by AppListModel::SearchResults.

  // Parent list view. Owned by views hierarchy.
  SearchResultListView* list_view_;

  views::ImageView* icon_;  // Owned by views hierarchy.
  scoped_ptr<gfx::RenderText> title_text_;
  scoped_ptr<gfx::RenderText> details_text_;
  SearchResultActionsView* actions_view_;  // Owned by the views hierarchy.
  ProgressBarView* progress_bar_;  // Owned by views hierarchy.

  scoped_ptr<views::MenuRunner> context_menu_runner_;

  DISALLOW_COPY_AND_ASSIGN(SearchResultView);
};

}  // namespace app_list

#endif  // UI_APP_LIST_VIEWS_SEARCH_RESULT_VIEW_H_
