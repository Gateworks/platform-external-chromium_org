// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_RESOURCES_RASTER_BUFFER_H_
#define CC_RESOURCES_RASTER_BUFFER_H_

#include "cc/base/cc_export.h"
#include "ui/gfx/geometry/rect.h"

namespace cc {
class PicturePileImpl;
class RenderingStatsInstrumentation;

class CC_EXPORT RasterBuffer {
 public:
  RasterBuffer();
  virtual ~RasterBuffer();

  virtual void Playback(const PicturePileImpl* picture_pile,
                        const gfx::Rect& rect,
                        float scale,
                        RenderingStatsInstrumentation* stats) = 0;
};

}  // namespace cc

#endif  // CC_RESOURCES_RASTER_BUFFER_H_
