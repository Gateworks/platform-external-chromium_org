// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_RESOURCES_PICTURE_PILE_IMPL_H_
#define CC_RESOURCES_PICTURE_PILE_IMPL_H_

#include <list>
#include <map>
#include <set>
#include <vector>

#include "base/time/time.h"
#include "cc/base/cc_export.h"
#include "cc/debug/rendering_stats_instrumentation.h"
#include "cc/resources/picture_pile_base.h"
#include "skia/ext/analysis_canvas.h"
#include "skia/ext/refptr.h"
#include "third_party/skia/include/core/SkPicture.h"

namespace cc {

class CC_EXPORT PicturePileImpl : public PicturePileBase {
 public:
  static scoped_refptr<PicturePileImpl> Create();
  static scoped_refptr<PicturePileImpl> CreateFromOther(
      const PicturePileBase* other);

  // Raster a subrect of this PicturePileImpl into the given canvas. It is
  // assumed that contents_scale has already been applied to this canvas.
  // Writes the total number of pixels rasterized and the time spent
  // rasterizing to the stats if the respective pointer is not NULL. When
  // slow-down-raster-scale-factor is set to a value greater than 1, the
  // reported rasterize time is the minimum measured value over all runs.
  void RasterDirect(
      SkCanvas* canvas,
      const gfx::Rect& canvas_rect,
      float contents_scale,
      RenderingStatsInstrumentation* rendering_stats_instrumentation);

  // Similar to the above RasterDirect method, but this is a convenience method
  // for when it is known that the raster is going to an intermediate bitmap
  // that itself will then be blended and thus that a canvas clear is required.
  // Note that this function may write outside the canvas_rect.
  void RasterToBitmap(
      SkCanvas* canvas,
      const gfx::Rect& canvas_rect,
      float contents_scale,
      RenderingStatsInstrumentation* stats_instrumentation) const;

  // Called when analyzing a tile. We can use AnalysisCanvas as
  // SkDrawPictureCallback, which allows us to early out from analysis.
  void RasterForAnalysis(
      skia::AnalysisCanvas* canvas,
      const gfx::Rect& canvas_rect,
      float contents_scale,
      RenderingStatsInstrumentation* stats_instrumentation) const;

  skia::RefPtr<SkPicture> GetFlattenedPicture();

  bool likely_to_be_used_for_transform_animation() const {
    return likely_to_be_used_for_transform_animation_;
  }
  void set_likely_to_be_used_for_transform_animation() {
    likely_to_be_used_for_transform_animation_ = true;
  }

  struct CC_EXPORT Analysis {
    Analysis();
    ~Analysis();

    bool is_solid_color;
    SkColor solid_color;
  };

  void AnalyzeInRect(const gfx::Rect& content_rect,
                     float contents_scale,
                     Analysis* analysis) const;

  void AnalyzeInRect(
      const gfx::Rect& content_rect,
      float contents_scale,
      Analysis* analysis,
      RenderingStatsInstrumentation* stats_instrumentation) const;

  class CC_EXPORT PixelRefIterator {
   public:
    PixelRefIterator(const gfx::Rect& content_rect,
                     float contents_scale,
                     const PicturePileImpl* picture_pile);
    ~PixelRefIterator();

    SkPixelRef* operator->() const { return *pixel_ref_iterator_; }
    SkPixelRef* operator*() const { return *pixel_ref_iterator_; }
    PixelRefIterator& operator++();
    operator bool() const { return pixel_ref_iterator_; }

   private:
    void AdvanceToTilePictureWithPixelRefs();

    const PicturePileImpl* picture_pile_;
    gfx::Rect layer_rect_;
    TilingData::Iterator tile_iterator_;
    Picture::PixelRefIterator pixel_ref_iterator_;
    std::set<const void*> processed_pictures_;
  };

  void DidBeginTracing();

 protected:
  friend class PicturePile;
  friend class PixelRefIterator;

  PicturePileImpl();
  explicit PicturePileImpl(const PicturePileBase* other);
  ~PicturePileImpl() override;

 private:
  typedef std::map<const Picture*, Region> PictureRegionMap;

  void CoalesceRasters(const gfx::Rect& canvas_rect,
                       const gfx::Rect& content_rect,
                       float contents_scale,
                       PictureRegionMap* result) const;

  void RasterCommon(
      SkCanvas* canvas,
      SkDrawPictureCallback* callback,
      const gfx::Rect& canvas_rect,
      float contents_scale,
      RenderingStatsInstrumentation* rendering_stats_instrumentation,
      bool is_analysis) const;

  bool likely_to_be_used_for_transform_animation_;

  DISALLOW_COPY_AND_ASSIGN(PicturePileImpl);
};

}  // namespace cc

#endif  // CC_RESOURCES_PICTURE_PILE_IMPL_H_
