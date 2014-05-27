// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_LAYERS_PICTURE_LAYER_IMPL_H_
#define CC_LAYERS_PICTURE_LAYER_IMPL_H_

#include <string>
#include <vector>

#include "cc/base/cc_export.h"
#include "cc/base/scoped_ptr_vector.h"
#include "cc/layers/layer_impl.h"
#include "cc/resources/picture_layer_tiling.h"
#include "cc/resources/picture_layer_tiling_set.h"
#include "cc/resources/picture_pile_impl.h"
#include "cc/trees/layer_tree_impl.h"
#include "skia/ext/refptr.h"
#include "third_party/skia/include/core/SkPicture.h"

namespace cc {

struct AppendQuadsData;
class QuadSink;
class MicroBenchmarkImpl;
class Tile;

class CC_EXPORT PictureLayerImpl
    : public LayerImpl,
      NON_EXPORTED_BASE(public PictureLayerTilingClient) {
 public:
  class CC_EXPORT LayerRasterTileIterator {
   public:
    LayerRasterTileIterator();
    LayerRasterTileIterator(PictureLayerImpl* layer, bool prioritize_low_res);
    ~LayerRasterTileIterator();

    Tile* operator*();
    LayerRasterTileIterator& operator++();
    operator bool() const;

   private:
    enum IteratorType { LOW_RES, HIGH_RES, NUM_ITERATORS };

    PictureLayerImpl* layer_;

    struct IterationStage {
      IteratorType iterator_type;
      TilePriority::PriorityBin tile_type;
    };

    int current_stage_;

    // One low res stage, and three high res stages.
    IterationStage stages_[4];
    PictureLayerTiling::TilingRasterTileIterator iterators_[NUM_ITERATORS];
  };

  class CC_EXPORT LayerEvictionTileIterator {
   public:
    LayerEvictionTileIterator();
    LayerEvictionTileIterator(PictureLayerImpl* layer,
                              TreePriority tree_priority);
    ~LayerEvictionTileIterator();

    Tile* operator*();
    LayerEvictionTileIterator& operator++();
    operator bool() const;

   private:
    void AdvanceToNextIterator();
    bool IsCorrectType(
        PictureLayerTiling::TilingEvictionTileIterator* it) const;

    std::vector<PictureLayerTiling::TilingEvictionTileIterator> iterators_;
    size_t iterator_index_;
    TilePriority::PriorityBin iteration_stage_;
    bool required_for_activation_;

    PictureLayerImpl* layer_;
  };

  static scoped_ptr<PictureLayerImpl> Create(LayerTreeImpl* tree_impl, int id) {
    return make_scoped_ptr(new PictureLayerImpl(tree_impl, id));
  }
  virtual ~PictureLayerImpl();

  // LayerImpl overrides.
  virtual const char* LayerTypeAsString() const OVERRIDE;
  virtual scoped_ptr<LayerImpl> CreateLayerImpl(LayerTreeImpl* tree_impl)
      OVERRIDE;
  virtual void PushPropertiesTo(LayerImpl* layer) OVERRIDE;
  virtual void AppendQuads(QuadSink* quad_sink,
                           AppendQuadsData* append_quads_data) OVERRIDE;
  virtual void UpdateTilePriorities() OVERRIDE;
  virtual void NotifyTileInitialized(const Tile* tile) OVERRIDE;
  virtual void DidBecomeActive() OVERRIDE;
  virtual void DidBeginTracing() OVERRIDE;
  virtual void ReleaseResources() OVERRIDE;
  virtual void CalculateContentsScale(float ideal_contents_scale,
                                      float device_scale_factor,
                                      float page_scale_factor,
                                      float maximum_animation_contents_scale,
                                      bool animating_transform_to_screen,
                                      float* contents_scale_x,
                                      float* contents_scale_y,
                                      gfx::Size* content_bounds) OVERRIDE;
  virtual skia::RefPtr<SkPicture> GetPicture() OVERRIDE;

  // PictureLayerTilingClient overrides.
  virtual scoped_refptr<Tile> CreateTile(
    PictureLayerTiling* tiling,
    const gfx::Rect& content_rect) OVERRIDE;
  virtual void UpdatePile(Tile* tile) OVERRIDE;
  virtual gfx::Size CalculateTileSize(
      const gfx::Size& content_bounds) const OVERRIDE;
  virtual const Region* GetInvalidation() OVERRIDE;
  virtual const PictureLayerTiling* GetTwinTiling(
      const PictureLayerTiling* tiling) const OVERRIDE;
  virtual size_t GetMaxTilesForInterestArea() const OVERRIDE;
  virtual float GetSkewportTargetTimeInSeconds() const OVERRIDE;
  virtual int GetSkewportExtrapolationLimitInContentPixels() const OVERRIDE;

  // PushPropertiesTo active tree => pending tree.
  void SyncTiling(const PictureLayerTiling* tiling);

  // Mask-related functions
  void SetIsMask(bool is_mask);
  virtual ResourceProvider::ResourceId ContentsResourceId() const OVERRIDE;

  virtual size_t GPUMemoryUsageInBytes() const OVERRIDE;

  virtual void RunMicroBenchmark(MicroBenchmarkImpl* benchmark) OVERRIDE;

  bool use_gpu_rasterization() const {
    return layer_tree_impl()->use_gpu_rasterization();
  }

  // Functions used by tile manager.
  void DidUnregisterLayer();
  PictureLayerImpl* GetTwinLayer() { return twin_layer_; }
  WhichTree GetTree() const;
  bool IsOnActiveOrPendingTree() const;

 protected:
  friend class LayerRasterTileIterator;

  PictureLayerImpl(LayerTreeImpl* tree_impl, int id);
  PictureLayerTiling* AddTiling(float contents_scale);
  void RemoveTiling(float contents_scale);
  void RemoveAllTilings();
  void SyncFromActiveLayer(const PictureLayerImpl* other);
  void ManageTilings(bool animating_transform_to_screen,
                     float maximum_animation_contents_scale);
  bool ShouldHaveLowResTiling() const {
    return should_use_low_res_tiling_ && !use_gpu_rasterization();
  }
  virtual bool ShouldAdjustRasterScale(
      bool animating_transform_to_screen) const;
  virtual void RecalculateRasterScales(bool animating_transform_to_screen,
                                       float maximum_animation_contents_scale);
  void CleanUpTilingsOnActiveLayer(
      std::vector<PictureLayerTiling*> used_tilings);
  float MinimumContentsScale() const;
  float SnappedContentsScale(float new_contents_scale);
  void UpdateLCDTextStatus(bool new_status);
  void ResetRasterScale();
  void MarkVisibleResourcesAsRequired() const;
  bool MarkVisibleTilesAsRequired(
      PictureLayerTiling* tiling,
      const PictureLayerTiling* optional_twin_tiling,
      float contents_scale,
      const gfx::Rect& rect,
      const Region& missing_region) const;

  void DoPostCommitInitializationIfNeeded() {
    if (needs_post_commit_initialization_)
      DoPostCommitInitialization();
  }
  void DoPostCommitInitialization();

  bool CanHaveTilings() const;
  bool CanHaveTilingWithScale(float contents_scale) const;
  void SanityCheckTilingState() const;

  virtual void GetDebugBorderProperties(
      SkColor* color, float* width) const OVERRIDE;
  virtual void AsValueInto(base::DictionaryValue* dict) const OVERRIDE;

  PictureLayerImpl* twin_layer_;

  scoped_ptr<PictureLayerTilingSet> tilings_;
  scoped_refptr<PicturePileImpl> pile_;
  Region invalidation_;

  bool is_mask_;

  float ideal_page_scale_;
  float ideal_device_scale_;
  float ideal_source_scale_;
  float ideal_contents_scale_;

  float raster_page_scale_;
  float raster_device_scale_;
  float raster_source_scale_;
  float raster_contents_scale_;
  float low_res_raster_contents_scale_;

  bool raster_source_scale_is_fixed_;
  bool was_animating_transform_to_screen_;
  bool is_using_lcd_text_;
  bool needs_post_commit_initialization_;
  // A sanity state check to make sure UpdateTilePriorities only gets called
  // after a CalculateContentsScale/ManageTilings.
  bool should_update_tile_priorities_;
  bool should_use_low_res_tiling_;

  bool layer_needs_to_register_itself_;

  // Save a copy of the visible rect and viewport size of the last frame that
  // has a valid viewport for prioritizing tiles.
  gfx::Rect visible_rect_for_tile_priority_;
  gfx::Size viewport_size_for_tile_priority_;
  gfx::Transform screen_space_transform_for_tile_priority_;

  friend class PictureLayer;
  DISALLOW_COPY_AND_ASSIGN(PictureLayerImpl);
};

}  // namespace cc

#endif  // CC_LAYERS_PICTURE_LAYER_IMPL_H_
