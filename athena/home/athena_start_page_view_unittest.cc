// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "athena/home/athena_start_page_view.h"

#include "athena/home/home_card_constants.h"
#include "athena/test/base/athena_test_base.h"
#include "base/format_macros.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/app_list/app_list_switches.h"
#include "ui/app_list/search_box_model.h"
#include "ui/app_list/test/app_list_test_model.h"
#include "ui/app_list/test/app_list_test_view_delegate.h"
#include "ui/app_list/views/search_box_view.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/textfield/textfield.h"

namespace athena {

class AthenaTestViewDelegate : public app_list::test::AppListTestViewDelegate {
 public:
  AthenaTestViewDelegate() {}
  ~AthenaTestViewDelegate() override {}

 private:
  // app_list::AppListViewDelegate:
  views::View* CreateStartPageWebView(const gfx::Size& size) override {
    return new views::View();
  }

  DISALLOW_COPY_AND_ASSIGN(AthenaTestViewDelegate);
};

class AthenaStartPageViewTest : public test::AthenaTestBase {
 public:
  AthenaStartPageViewTest() {}
  ~AthenaStartPageViewTest() override {}

  // testing::Test:
  void SetUp() override {
    test::AthenaTestBase::SetUp();
    for (size_t i = 0; i < GetMaxIconNum(); ++i)
      AddTestItem(i);

    view_.reset(new AthenaStartPageView(&view_delegate_));
    SetSize(gfx::Size(1280, 800));
  }
  void TearDown() override {
    view_.reset();
    test::AthenaTestBase::TearDown();
  }

 protected:
  void SetSize(const gfx::Size& new_size) {
    view_->SetSize(new_size);
    view_->Layout();
  }

  void AddTestItem(size_t index) {
    app_list::test::AppListTestModel* model = view_delegate_.GetTestModel();
    model->AddItem(new app_list::test::AppListTestModel::AppListTestItem(
        GetAppIdFor(index), model));
  }

  static size_t GetMaxIconNum() {
    return AthenaStartPageView::GetMaxIconNumForTest();
  }

  static std::string GetAppIdFor(size_t index) {
    return base::StringPrintf("id-%" PRIuS, index);
  }

  app_list::AppListModel* GetModel() { return view_delegate_.GetTestModel(); }

  views::View* GetIconsContainer() { return view_->app_icon_container_; }

  gfx::Rect GetIconsBounds() const {
    return view_->app_icon_container_->layer()->GetTargetBounds();
  }

  gfx::Rect GetControlBounds() const {
    return view_->control_icon_container_->layer()->GetTargetBounds();
  }

  gfx::Rect GetSearchBoxBounds() const {
    return view_->search_box_container_->layer()->GetTargetBounds();
  }

  gfx::Rect GetLogoBounds() const {
    return view_->logo_->layer()->GetTargetBounds();
  }

  bool IsLogoVisible() const {
    return view_->logo_->layer()->GetTargetOpacity() > 0 &&
        view_->logo_->layer()->GetTargetVisibility();
  }

  gfx::Size GetSearchBoxPreferredSize() {
    return view_->search_box_container_->GetPreferredSize();
  }

  void SetSearchQuery(const base::string16& query) {
    view_delegate_.GetModel()->search_box()->SetText(query);
  }

  base::string16 GetVisibleQuery() {
    return view_->search_box_view_->search_box()->text();
  }

  float layout_state() { return view_->layout_state_; }

  scoped_ptr<AthenaStartPageView> view_;

 private:
  AthenaTestViewDelegate view_delegate_;

  DISALLOW_COPY_AND_ASSIGN(AthenaStartPageViewTest);
};

TEST_F(AthenaStartPageViewTest, BasicLayout) {
  // BOTTOM state. logo is invisible. icons, search box, and controls are
  // arranged horizontally.
  EXPECT_FALSE(IsLogoVisible());

  // Three components are aligned at the middle point.
  EXPECT_NEAR(GetIconsBounds().CenterPoint().y(),
              GetControlBounds().CenterPoint().y(),
              1);
  EXPECT_NEAR(GetIconsBounds().CenterPoint().y(),
              GetSearchBoxBounds().CenterPoint().y(),
              1);
  EXPECT_NEAR(GetControlBounds().CenterPoint().y(),
              GetSearchBoxBounds().CenterPoint().y(),
              1);

  // Horizonttaly aligned in the order of icons, search_box, and controls.
  EXPECT_LE(GetIconsBounds().right(), GetSearchBoxBounds().x());
  EXPECT_LE(GetSearchBoxBounds().right(), GetControlBounds().x());
  EXPECT_LE(0, GetIconsBounds().y());

  // Search box should appear in the middle.
  EXPECT_NEAR(GetSearchBoxBounds().CenterPoint().x(),
              view_->bounds().CenterPoint().x(),
              1);

  // Should fit inside of the home card height.
  EXPECT_GE(kHomeCardHeight, GetIconsBounds().height());
  EXPECT_GE(kHomeCardHeight, GetSearchBoxBounds().height());
  EXPECT_GE(kHomeCardHeight, GetControlBounds().height());
  EXPECT_EQ(GetSearchBoxPreferredSize().ToString(),
            GetSearchBoxBounds().size().ToString());

  // CENTERED state. logo is visible. search box appears below the logo,
  // icons and controls are arranged horizontally and below the search box.
  view_->SetLayoutState(1.0f);
  EXPECT_TRUE(IsLogoVisible());
  EXPECT_NEAR(GetLogoBounds().x() + GetLogoBounds().width() / 2,
              GetSearchBoxBounds().x() + GetSearchBoxBounds().width() / 2,
              1);
  EXPECT_LE(GetLogoBounds().bottom(), GetSearchBoxBounds().y());
  EXPECT_EQ(GetIconsBounds().y(), GetControlBounds().y());
  EXPECT_LE(GetIconsBounds().right(), GetControlBounds().x());
  EXPECT_LE(GetSearchBoxBounds().bottom(), GetIconsBounds().y());
}

TEST_F(AthenaStartPageViewTest, NarrowLayout) {
  SetSize(gfx::Size(800, 1280));

  // BOTTOM state. Similar to BasicLayout.
  EXPECT_FALSE(IsLogoVisible());
  // Three components are aligned at the middle point.
  EXPECT_NEAR(GetIconsBounds().CenterPoint().y(),
              GetControlBounds().CenterPoint().y(),
              1);
  EXPECT_NEAR(GetIconsBounds().CenterPoint().y(),
              GetSearchBoxBounds().CenterPoint().y(),
              1);
  EXPECT_NEAR(GetControlBounds().CenterPoint().y(),
              GetSearchBoxBounds().CenterPoint().y(),
              1);

  // Horizonttaly aligned in the order of icons, search_box, and controls.
  EXPECT_LE(GetIconsBounds().right(), GetSearchBoxBounds().x());
  EXPECT_LE(GetSearchBoxBounds().right(), GetControlBounds().x());
  EXPECT_LE(0, GetIconsBounds().y());

  // Search box should appear in the middle.
  EXPECT_NEAR(GetSearchBoxBounds().CenterPoint().x(),
              view_->bounds().CenterPoint().x(),
              1);

  // Should fit inside of the home card height.
  EXPECT_GE(kHomeCardHeight, GetIconsBounds().height());
  EXPECT_GE(kHomeCardHeight, GetSearchBoxBounds().height());
  EXPECT_GE(kHomeCardHeight, GetControlBounds().height());

  // Search box is narrower because of the size is too narrow.
  EXPECT_GT(GetSearchBoxPreferredSize().width(), GetSearchBoxBounds().width());
  EXPECT_EQ(GetSearchBoxPreferredSize().height(),
            GetSearchBoxBounds().height());

  // CENTERED state. Search box should be back to the preferred size.
  view_->SetLayoutState(1.0f);
  EXPECT_EQ(GetSearchBoxPreferredSize().ToString(),
            GetSearchBoxBounds().size().ToString());

  // Back to BOTTOM state, the search box shrinks again.
  view_->SetLayoutState(0.0f);
  EXPECT_GT(GetSearchBoxPreferredSize().width(), GetSearchBoxBounds().width());

  // Then set back to the original size, now the size is wide enough so the
  // search box bounds becomes as preferred.
  SetSize(gfx::Size(1280, 800));
  EXPECT_EQ(GetSearchBoxPreferredSize().ToString(),
            GetSearchBoxBounds().size().ToString());
}

TEST_F(AthenaStartPageViewTest, SearchBox) {
  view_->SetLayoutState(1.0f);
  EXPECT_TRUE(IsLogoVisible());

  const gfx::Rect base_search_box_bounds = GetSearchBoxBounds();

  const base::string16 query = base::UTF8ToUTF16("test");
  SetSearchQuery(query);

  EXPECT_FALSE(IsLogoVisible());
  EXPECT_GT(base_search_box_bounds.y(), GetSearchBoxBounds().y());
  EXPECT_EQ(query, GetVisibleQuery());

  SetSearchQuery(base::string16());
  EXPECT_TRUE(IsLogoVisible());
  EXPECT_EQ(base_search_box_bounds.ToString(), GetSearchBoxBounds().ToString());
  EXPECT_TRUE(GetVisibleQuery().empty());
}

TEST_F(AthenaStartPageViewTest, SearchFromBottom) {
  view_->SetLayoutState(0.0f);

  const base::string16 query = base::UTF8ToUTF16("test");
  SetSearchQuery(query);

  EXPECT_FALSE(IsLogoVisible());
  EXPECT_EQ(query, GetVisibleQuery());
  EXPECT_EQ(1.0f, layout_state());

  SetSearchQuery(base::string16());
  EXPECT_TRUE(IsLogoVisible());
  EXPECT_TRUE(GetVisibleQuery().empty());
  EXPECT_EQ(1.0f, layout_state());
}

TEST_F(AthenaStartPageViewTest, AppAddRemove) {
  gfx::Rect icons_bounds = GetIconsBounds();
  EXPECT_EQ(GetMaxIconNum(),
            static_cast<size_t>(GetIconsContainer()->child_count()));

  GetModel()->DeleteItem(GetAppIdFor(1));

  // The removed icon disappear, however its bound should not change.
  EXPECT_EQ(GetMaxIconNum() - 1,
            static_cast<size_t>(GetIconsContainer()->child_count()));
  EXPECT_EQ(icons_bounds.size().ToString(), GetIconsBounds().size().ToString());

  AddTestItem(GetMaxIconNum() + 1);
  EXPECT_EQ(GetMaxIconNum(),
            static_cast<size_t>(GetIconsContainer()->child_count()));
  EXPECT_EQ(icons_bounds.size().ToString(), GetIconsBounds().size().ToString());

  // Adding more doesn't cause any effects.
  AddTestItem(GetMaxIconNum() + 2);
  EXPECT_EQ(GetMaxIconNum(),
            static_cast<size_t>(GetIconsContainer()->child_count()));
  EXPECT_EQ(icons_bounds.size().ToString(), GetIconsBounds().size().ToString());
}

}  // namespace athena
