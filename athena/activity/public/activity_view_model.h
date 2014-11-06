// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATHENA_ACTIVITY_PUBLIC_ACTIVITY_VIEW_MODEL_H_
#define ATHENA_ACTIVITY_PUBLIC_ACTIVITY_VIEW_MODEL_H_

#include "athena/athena_export.h"
#include "base/strings/string16.h"

typedef unsigned int SkColor;

namespace gfx {
class ImageSkia;
}

namespace views {
class View;
class Widget;
}

namespace athena {

class ActivityView;

// The view model for the representation of the activity.
class ATHENA_EXPORT ActivityViewModel {
 public:
  virtual ~ActivityViewModel() {}

  // Called after the view model is attached to the widget/window tree and
  // before it gets registered to the ActivityManager and the ResourceManager.
  // At this time the Activity can also be moved to a different place in the
  // Activity history.
  virtual void Init() = 0;

  // Returns a color most representative of this activity.
  virtual SkColor GetRepresentativeColor() const = 0;

  // Returns a title for the activity.
  virtual base::string16 GetTitle() const = 0;

  // Returns an icon for the activity.
  virtual gfx::ImageSkia GetIcon() const = 0;

  // Sets the ActivityView for the model to update. The model does not take
  // ownership of the view.
  virtual void SetActivityView(ActivityView* view) = 0;

  // True if the activity wants to use Widget's frame, or false if the activity
  // draws its own frame.
  virtual bool UsesFrame() const = 0;

  // Returns the contents view which might be nullptr if the activity is not
  // loaded. Note that the caller should not hold on to the view since it can
  // be deleted by the resource manager.
  virtual views::View* GetContentsView() = 0;

  // Returns an image which can be used to represent the activity in e.g. the
  // overview mode. The returned image can have no size if either a view exists
  // or the activity has not yet been loaded or ever been presented. In that
  // case GetRepresentativeColor() should be used to clear the preview area.
  // Note that since the image gets created upon request, and the
  // ActivityViewModel will hold no reference to the returned image data. As
  // such it is advisable to hold on to the image as long as needed instead of
  // calling this function frequently since it will cause time to generate.
  virtual gfx::ImageSkia GetOverviewModeImage() = 0;

  // Prepares the contents view for overview.
  virtual void PrepareContentsForOverview() = 0;

  // Undoes any changes done by PrepareContentsForOverview().
  virtual void ResetContentsView() = 0;
};

}  // namespace athena

#endif  // ATHENA_ACTIVITY_PUBLIC_ACTIVITY_VIEW_MODEL_H_
