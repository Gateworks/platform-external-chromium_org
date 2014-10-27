// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_FILTERS_SKCANVAS_VIDEO_RENDERER_H_
#define MEDIA_FILTERS_SKCANVAS_VIDEO_RENDERER_H_

#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "media/base/media_export.h"
#include "media/base/video_rotation.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkXfermode.h"
#include "ui/gfx/rect.h"

class SkCanvas;

namespace media {

class VideoFrame;
class VideoImageGenerator;

// Handles rendering of VideoFrames to SkCanvases, doing any necessary YUV
// conversion and caching of resulting RGB bitmaps.
class MEDIA_EXPORT SkCanvasVideoRenderer {
 public:
  SkCanvasVideoRenderer();
  ~SkCanvasVideoRenderer();

  // Paints |video_frame| on |canvas|, scaling and rotating the result to fit
  // dimensions specified by |dest_rect|.
  //
  // Black will be painted on |canvas| if |video_frame| is null.
  void Paint(const scoped_refptr<VideoFrame>& video_frame,
             SkCanvas* canvas,
             const gfx::RectF& dest_rect,
             uint8 alpha,
             SkXfermode::Mode mode,
             VideoRotation video_rotation);

  // Copy |video_frame| on |canvas|.
  void Copy(const scoped_refptr<VideoFrame>&, SkCanvas* canvas);

 private:
  VideoImageGenerator* generator_;

  // An RGB bitmap and corresponding timestamp of the previously converted
  // video frame data.
  SkBitmap last_frame_;
  base::TimeDelta last_frame_timestamp_;

  DISALLOW_COPY_AND_ASSIGN(SkCanvasVideoRenderer);
};

}  // namespace media

#endif  // MEDIA_FILTERS_SKCANVAS_VIDEO_RENDERER_H_
