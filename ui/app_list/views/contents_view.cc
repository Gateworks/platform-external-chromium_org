// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/app_list/views/contents_view.h"

#include <algorithm>
#include <vector>

#include "base/logging.h"
#include "ui/app_list/app_list_constants.h"
#include "ui/app_list/app_list_switches.h"
#include "ui/app_list/app_list_view_delegate.h"
#include "ui/app_list/views/app_list_folder_view.h"
#include "ui/app_list/views/app_list_main_view.h"
#include "ui/app_list/views/apps_container_view.h"
#include "ui/app_list/views/apps_grid_view.h"
#include "ui/app_list/views/contents_switcher_view.h"
#include "ui/app_list/views/search_box_view.h"
#include "ui/app_list/views/search_result_list_view.h"
#include "ui/app_list/views/start_page_view.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/tween.h"
#include "ui/resources/grit/ui_resources.h"
#include "ui/views/view_model.h"
#include "ui/views/view_model_utils.h"

namespace app_list {

ContentsView::ContentsView(AppListMainView* app_list_main_view)
    : search_results_view_(NULL),
      start_page_view_(NULL),
      app_list_main_view_(app_list_main_view),
      contents_switcher_view_(NULL),
      view_model_(new views::ViewModel),
      page_before_search_(0) {
  pagination_model_.SetTransitionDurations(kPageTransitionDurationInMs,
                                           kOverscrollPageTransitionDurationMs);
  pagination_model_.AddObserver(this);
}

ContentsView::~ContentsView() {
  pagination_model_.RemoveObserver(this);
  if (contents_switcher_view_)
    pagination_model_.RemoveObserver(contents_switcher_view_);
}

void ContentsView::Init(AppListModel* model,
                        AppListViewDelegate* view_delegate) {
  DCHECK(model);

  if (app_list::switches::IsExperimentalAppListEnabled()) {
    std::vector<views::View*> custom_page_views =
        view_delegate->CreateCustomPageWebViews(GetLocalBounds().size());
    for (std::vector<views::View*>::const_iterator it =
             custom_page_views.begin();
         it != custom_page_views.end();
         ++it) {
      AddLauncherPage(*it, IDR_APP_LIST_NOTIFICATIONS_ICON);
    }

    start_page_view_ = new StartPageView(app_list_main_view_, view_delegate);
    AddLauncherPage(
        start_page_view_, IDR_APP_LIST_SEARCH_ICON, AppListModel::STATE_START);
  } else {
    search_results_view_ =
        new SearchResultListView(app_list_main_view_, view_delegate);
    AddLauncherPage(
        search_results_view_, 0, AppListModel::STATE_SEARCH_RESULTS);
    search_results_view_->SetResults(model->results());
  }

  apps_container_view_ = new AppsContainerView(app_list_main_view_, model);

  AddLauncherPage(
      apps_container_view_, IDR_APP_LIST_APPS_ICON, AppListModel::STATE_APPS);

  int initial_page_index = app_list::switches::IsExperimentalAppListEnabled()
                               ? GetPageIndexForState(AppListModel::STATE_START)
                               : GetPageIndexForState(AppListModel::STATE_APPS);
  DCHECK_GE(initial_page_index, 0);

  page_before_search_ = initial_page_index;
  pagination_model_.SelectPage(initial_page_index, false);
  ActivePageChanged(false);
}

void ContentsView::CancelDrag() {
  if (apps_container_view_->apps_grid_view()->has_dragged_view())
    apps_container_view_->apps_grid_view()->EndDrag(true);
  if (apps_container_view_->app_list_folder_view()
          ->items_grid_view()
          ->has_dragged_view()) {
    apps_container_view_->app_list_folder_view()->items_grid_view()->EndDrag(
        true);
  }
}

void ContentsView::SetDragAndDropHostOfCurrentAppList(
    ApplicationDragAndDropHost* drag_and_drop_host) {
  apps_container_view_->SetDragAndDropHostOfCurrentAppList(drag_and_drop_host);
}

void ContentsView::SetContentsSwitcherView(
    ContentsSwitcherView* contents_switcher_view) {
  DCHECK(!contents_switcher_view_);
  contents_switcher_view_ = contents_switcher_view;
  if (contents_switcher_view_)
    pagination_model_.AddObserver(contents_switcher_view_);
}

void ContentsView::SetActivePage(int page_index) {
  if (GetActivePageIndex() == page_index)
    return;

  SetActivePageInternal(page_index, false);
}

int ContentsView::GetActivePageIndex() const {
  // The active page is changed at the beginning of an animation, not the end.
  return pagination_model_.SelectedTargetPage();
}

bool ContentsView::IsStateActive(AppListModel::State state) const {
  int active_page_index = GetActivePageIndex();
  return active_page_index >= 0 &&
         GetPageIndexForState(state) == active_page_index;
}

int ContentsView::GetPageIndexForState(AppListModel::State state) const {
  // Find the index of the view corresponding to the given state.
  std::map<AppListModel::State, int>::const_iterator it =
      state_to_view_.find(state);
  if (it == state_to_view_.end())
    return -1;

  return it->second;
}

int ContentsView::NumLauncherPages() const {
  return pagination_model_.total_pages();
}

void ContentsView::SetActivePageInternal(int page_index,
                                         bool show_search_results) {
  if (!show_search_results)
    page_before_search_ = page_index;
  // Start animating to the new page.
  pagination_model_.SelectPage(page_index, true);
  ActivePageChanged(show_search_results);
}

void ContentsView::ActivePageChanged(bool show_search_results) {
  AppListModel::State state = AppListModel::INVALID_STATE;

  // TODO(calamity): This does not report search results being shown in the
  // experimental app list as a boolean is currently used to indicate whether
  // search results are showing. See http://crbug.com/427787/.
  std::map<int, AppListModel::State>::const_iterator it =
      view_to_state_.find(pagination_model_.SelectedTargetPage());
  if (it != view_to_state_.end())
    state = it->second;

  app_list_main_view_->model()->SetState(state);

  // TODO(xiyuan): Highlight default match instead of the first.
  if (IsStateActive(AppListModel::STATE_SEARCH_RESULTS) &&
      search_results_view_->visible()) {
    search_results_view_->SetSelectedIndex(0);
  }
  if (search_results_view_)
    search_results_view_->UpdateAutoLaunchState();

  if (IsStateActive(AppListModel::STATE_START)) {
    if (show_search_results)
      start_page_view_->ShowSearchResults();
    else
      start_page_view_->Reset();
  }

  // Notify parent AppListMainView of the page change.
  app_list_main_view_->UpdateSearchBoxVisibility();
}

void ContentsView::ShowSearchResults(bool show) {
  int search_page =
      GetPageIndexForState(app_list::switches::IsExperimentalAppListEnabled()
                               ? AppListModel::STATE_START
                               : AppListModel::STATE_SEARCH_RESULTS);
  DCHECK_GE(search_page, 0);

  SetActivePageInternal(show ? search_page : page_before_search_, show);
}

bool ContentsView::IsShowingSearchResults() const {
  return app_list::switches::IsExperimentalAppListEnabled()
             ? IsStateActive(AppListModel::STATE_START) &&
                   start_page_view_->IsShowingSearchResults()
             : IsStateActive(AppListModel::STATE_SEARCH_RESULTS);
}

gfx::Rect ContentsView::GetOffscreenPageBounds(int page_index) const {
  gfx::Rect bounds(GetContentsBounds());
  // The start page and search page origins are above; all other pages' origins
  // are below.
  int page_height = bounds.height();
  bool origin_above =
      GetPageIndexForState(AppListModel::STATE_START) == page_index ||
      GetPageIndexForState(AppListModel::STATE_SEARCH_RESULTS) == page_index;
  bounds.set_y(origin_above ? -page_height : page_height);
  return bounds;
}

void ContentsView::UpdatePageBounds() {
  // The bounds calculations will potentially be mid-transition (depending on
  // the state of the PaginationModel).
  int current_page = std::max(0, pagination_model_.selected_page());
  int target_page = current_page;
  double progress = 1;
  if (pagination_model_.has_transition()) {
    const PaginationModel::Transition& transition =
        pagination_model_.transition();
    if (pagination_model_.is_valid_page(transition.target_page)) {
      target_page = transition.target_page;
      progress = transition.progress;
    }
  }

  // Move |current_page| from 0 to its origin. Move |target_page| from its
  // origin to 0.
  gfx::Rect on_screen(GetDefaultContentsBounds());
  gfx::Rect current_page_origin(GetOffscreenPageBounds(current_page));
  gfx::Rect target_page_origin(GetOffscreenPageBounds(target_page));
  gfx::Rect current_page_rect(
      gfx::Tween::RectValueBetween(progress, on_screen, current_page_origin));
  gfx::Rect target_page_rect(
      gfx::Tween::RectValueBetween(progress, target_page_origin, on_screen));

  view_model_->view_at(current_page)->SetBoundsRect(current_page_rect);
  view_model_->view_at(target_page)->SetBoundsRect(target_page_rect);
}

PaginationModel* ContentsView::GetAppsPaginationModel() {
  return apps_container_view_->apps_grid_view()->pagination_model();
}

void ContentsView::ShowFolderContent(AppListFolderItem* item) {
  apps_container_view_->ShowActiveFolder(item);
}

void ContentsView::Prerender() {
  apps_container_view_->apps_grid_view()->Prerender();
}

views::View* ContentsView::GetPageView(int index) {
  return view_model_->view_at(index);
}

void ContentsView::AddBlankPageForTesting() {
  AddLauncherPage(new views::View, 0);
}

int ContentsView::AddLauncherPage(views::View* view, int resource_id) {
  int page_index = view_model_->view_size();
  AddChildView(view);
  view_model_->Add(view, page_index);
  if (contents_switcher_view_ && resource_id)
    contents_switcher_view_->AddSwitcherButton(resource_id, page_index);
  pagination_model_.SetTotalPages(view_model_->view_size());
  return page_index;
}

int ContentsView::AddLauncherPage(views::View* view,
                                  int resource_id,
                                  AppListModel::State state) {
  int page_index = AddLauncherPage(view, resource_id);
  bool success =
      state_to_view_.insert(std::make_pair(state, page_index)).second;
  success = success &&
            view_to_state_.insert(std::make_pair(page_index, state)).second;

  // There shouldn't be duplicates in either map.
  DCHECK(success);
  return page_index;
}

gfx::Rect ContentsView::GetDefaultSearchBoxBounds() const {
  gfx::Rect search_box_bounds(
      0,
      0,
      GetDefaultContentsSize().width(),
      app_list_main_view_->search_box_view()->GetPreferredSize().height());
  if (switches::IsExperimentalAppListEnabled()) {
    search_box_bounds.set_y(kExperimentalWindowPadding);
    search_box_bounds.Inset(kExperimentalWindowPadding, 0);
  }
  return search_box_bounds;
}

gfx::Rect ContentsView::GetDefaultContentsBounds() const {
  gfx::Rect bounds(gfx::Point(0, GetDefaultSearchBoxBounds().bottom()),
                   GetDefaultContentsSize());
  return bounds;
}

gfx::Size ContentsView::GetDefaultContentsSize() const {
  const gfx::Size container_size =
      apps_container_view_->apps_grid_view()->GetPreferredSize();
  const gfx::Size results_size = search_results_view_
                                     ? search_results_view_->GetPreferredSize()
                                     : gfx::Size();

  int width = std::max(container_size.width(), results_size.width());
  int height = std::max(container_size.height(), results_size.height());
  return gfx::Size(width, height);
}

gfx::Size ContentsView::GetPreferredSize() const {
  gfx::Rect search_box_bounds = GetDefaultSearchBoxBounds();
  gfx::Rect default_contents_bounds = GetDefaultContentsBounds();
  gfx::Vector2d bottom_right =
      search_box_bounds.bottom_right().OffsetFromOrigin();
  bottom_right.SetToMax(
      default_contents_bounds.bottom_right().OffsetFromOrigin());
  return gfx::Size(bottom_right.x(), bottom_right.y());
}

void ContentsView::Layout() {
  // Immediately finish all current animations.
  pagination_model_.FinishAnimation();

  // Move the current view onto the screen, and all other views off screen to
  // the left. (Since we are not animating, we don't need to be careful about
  // which side we place the off-screen views onto.)
  gfx::Rect rect(GetDefaultContentsBounds());
  if (rect.IsEmpty())
    return;
  // TODO(mgiuca): Temporary work-around for http://crbug.com/441962 and
  // http://crbug.com/446978. This will first be called while ContentsView is
  // 0x0, which means that the child views will be positioned incorrectly in RTL
  // mode (the position is based on the parent's size). When the parent is later
  // resized, the children are not repositioned due to http://crbug.com/446407.
  // Therefore, we must not position the children until the parent is the
  // correct size.
  // NOTE: There is a similar hack in AppsGridView::CalculateIdealBounds; both
  // should be removed once http://crbug.com/446407 is resolved.
  if (GetContentsBounds().IsEmpty())
    return;

  gfx::Rect offscreen_target(rect);
  offscreen_target.set_x(-rect.width());

  for (int i = 0; i < view_model_->view_size(); ++i) {
    view_model_->view_at(i)->SetBoundsRect(
        i == pagination_model_.SelectedTargetPage() ? rect : offscreen_target);
  }
}

bool ContentsView::OnKeyPressed(const ui::KeyEvent& event) {
  return view_model_->view_at(GetActivePageIndex())->OnKeyPressed(event);
}

void ContentsView::TotalPagesChanged() {
}

void ContentsView::SelectedPageChanged(int old_selected, int new_selected) {
}

void ContentsView::TransitionStarted() {
}

void ContentsView::TransitionChanged() {
  UpdatePageBounds();
}

}  // namespace app_list
