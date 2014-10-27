// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_CLIENT_GPU_MEMORY_BUFFER_IMPL_SURFACE_TEXTURE_H_
#define CONTENT_COMMON_GPU_CLIENT_GPU_MEMORY_BUFFER_IMPL_SURFACE_TEXTURE_H_

#include "content/common/gpu/client/gpu_memory_buffer_impl.h"

struct ANativeWindow;

namespace content {

// Implementation of GPU memory buffer based on SurfaceTextures.
class GpuMemoryBufferImplSurfaceTexture : public GpuMemoryBufferImpl {
 public:
  static void Create(const gfx::Size& size,
                     Format format,
                     int client_id,
                     const CreationCallback& callback);

  static void AllocateForChildProcess(const gfx::Size& size,
                                      Format format,
                                      int child_client_id,
                                      const AllocationCallback& callback);

  static scoped_ptr<GpuMemoryBufferImpl> CreateFromHandle(
      const gfx::GpuMemoryBufferHandle& handle,
      const gfx::Size& size,
      Format format,
      const DestructionCallback& callback);

  static bool IsFormatSupported(Format format);
  static bool IsUsageSupported(Usage usage);
  static bool IsConfigurationSupported(Format format, Usage usage);
  static int WindowFormat(Format format);

  // Overridden from gfx::GpuMemoryBuffer:
  virtual void* Map() override;
  virtual void Unmap() override;
  virtual gfx::GpuMemoryBufferHandle GetHandle() const override;
  virtual uint32 GetStride() const override;

 private:
  GpuMemoryBufferImplSurfaceTexture(const gfx::Size& size,
                                    Format format,
                                    const DestructionCallback& callback,
                                    const gfx::GpuMemoryBufferId& id,
                                    ANativeWindow* native_window);
  virtual ~GpuMemoryBufferImplSurfaceTexture();

  gfx::GpuMemoryBufferId id_;
  ANativeWindow* native_window_;
  size_t stride_;

  DISALLOW_COPY_AND_ASSIGN(GpuMemoryBufferImplSurfaceTexture);
};

}  // namespace content

#endif  // CONTENT_COMMON_GPU_CLIENT_GPU_MEMORY_BUFFER_IMPL_SURFACE_TEXTURE_H_
