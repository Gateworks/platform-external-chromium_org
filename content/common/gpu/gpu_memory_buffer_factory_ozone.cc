// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/gpu_memory_buffer_factory.h"

#include "base/logging.h"
#include "ui/gl/gl_image.h"
#include "ui/gl/gl_image_shared_memory.h"
#include "ui/ozone/gpu/gpu_memory_buffer_factory_ozone_native_buffer.h"

namespace content {
namespace {

class GpuMemoryBufferFactoryImpl : public GpuMemoryBufferFactory {
 public:
  // Overridden from GpuMemoryBufferFactory:
  virtual gfx::GpuMemoryBufferHandle CreateGpuMemoryBuffer(
      const gfx::GpuMemoryBufferHandle& handle,
      const gfx::Size& size,
      gfx::GpuMemoryBuffer::Format format,
      gfx::GpuMemoryBuffer::Usage usage) override {
    switch (handle.type) {
      case gfx::OZONE_NATIVE_BUFFER:
        return ozone_buffer_factory_.CreateGpuMemoryBuffer(
                   handle.global_id, size, format, usage)
                   ? handle
                   : gfx::GpuMemoryBufferHandle();
      default:
        NOTREACHED();
        return gfx::GpuMemoryBufferHandle();
    }
  }
  virtual void DestroyGpuMemoryBuffer(
      const gfx::GpuMemoryBufferHandle& handle) override {
    switch (handle.type) {
      case gfx::OZONE_NATIVE_BUFFER:
        ozone_buffer_factory_.DestroyGpuMemoryBuffer(handle.global_id);
        break;
      default:
        NOTREACHED();
        break;
    }
  }
  virtual scoped_refptr<gfx::GLImage> CreateImageForGpuMemoryBuffer(
      const gfx::GpuMemoryBufferHandle& handle,
      const gfx::Size& size,
      gfx::GpuMemoryBuffer::Format format,
      unsigned internalformat,
      int client_id) override {
    switch (handle.type) {
      case gfx::SHARED_MEMORY_BUFFER: {
        scoped_refptr<gfx::GLImageSharedMemory> image(
            new gfx::GLImageSharedMemory(size, internalformat));
        if (!image->Initialize(handle, format))
          return NULL;

        return image;
      }
      case gfx::OZONE_NATIVE_BUFFER:
        // Verify that client is the owner of the buffer we're about to use.
        if (handle.global_id.secondary_id != client_id)
          return scoped_refptr<gfx::GLImage>();

        return ozone_buffer_factory_.CreateImageForGpuMemoryBuffer(
            handle.global_id, size, format, internalformat);
      default:
        NOTREACHED();
        return scoped_refptr<gfx::GLImage>();
    }
  }

 private:
  ui::GpuMemoryBufferFactoryOzoneNativeBuffer ozone_buffer_factory_;
};

}  // namespace

// static
scoped_ptr<GpuMemoryBufferFactory> GpuMemoryBufferFactory::Create() {
  return make_scoped_ptr<GpuMemoryBufferFactory>(
      new GpuMemoryBufferFactoryImpl);
}

}  // namespace content
