// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/app_list/views/tile_item_view.h"

#include "ui/app_list/app_list_constants.h"
#include "ui/app_list/views/app_list_main_view.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace {

const int kTileSize = 90;
const int kTileHorizontalPadding = 10;

}  // namespace

namespace app_list {

TileItemView::TileItemView()
    : views::CustomButton(this),
      icon_(new views::ImageView),
      title_(new views::Label) {
  views::BoxLayout* layout_manager = new views::BoxLayout(
      views::BoxLayout::kVertical, kTileHorizontalPadding, 0, 0);
  layout_manager->set_main_axis_alignment(
      views::BoxLayout::MAIN_AXIS_ALIGNMENT_CENTER);
  SetLayoutManager(layout_manager);

  icon_->SetImageSize(gfx::Size(kTileIconSize, kTileIconSize));

  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  title_->SetAutoColorReadabilityEnabled(false);
  title_->SetEnabledColor(kGridTitleColor);
  title_->set_background(views::Background::CreateSolidBackground(
      kLabelBackgroundColor));
  title_->SetFontList(rb.GetFontList(kItemTextFontStyle));
  title_->SetHorizontalAlignment(gfx::ALIGN_CENTER);

  // When |item_| is NULL, the tile is invisible. Calling SetSearchResult with a
  // non-NULL item makes the tile visible.
  SetVisible(false);

  AddChildView(icon_);
  AddChildView(title_);
}

TileItemView::~TileItemView() {
}

void TileItemView::SetIcon(const gfx::ImageSkia& icon) {
  icon_->SetImage(icon);
}

void TileItemView::SetTitle(const base::string16& title) {
  title_->SetText(title);
}

gfx::Size TileItemView::GetPreferredSize() const {
  return gfx::Size(kTileSize, kTileSize);
}

}  // namespace app_list
