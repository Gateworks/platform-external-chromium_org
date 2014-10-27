// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_FAKE_TILE_MANAGER_CLIENT_H_
#define CC_TEST_FAKE_TILE_MANAGER_CLIENT_H_

#include <vector>

#include "cc/resources/tile_manager.h"

namespace cc {

class FakeTileManagerClient : public TileManagerClient {
 public:
  FakeTileManagerClient();
  ~FakeTileManagerClient() override;

  // TileManagerClient implementation.
  const std::vector<PictureLayerImpl*>& GetPictureLayers() const override;
  void NotifyReadyToActivate() override {}
  void NotifyTileStateChanged(const Tile* tile) override {}
  void BuildRasterQueue(RasterTilePriorityQueue* queue,
                        TreePriority tree_priority) override {}
  void BuildEvictionQueue(EvictionTilePriorityQueue* queue,
                          TreePriority tree_priority) override {}

 private:
  std::vector<PictureLayerImpl*> picture_layers_;
};

}  // namespace cc

#endif  // CC_TEST_FAKE_TILE_MANAGER_CLIENT_H_
