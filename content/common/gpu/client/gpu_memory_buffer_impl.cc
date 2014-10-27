// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/client/gpu_memory_buffer_impl.h"

#include "ui/gl/gl_bindings.h"

namespace content {

GpuMemoryBufferImpl::GpuMemoryBufferImpl(const gfx::Size& size,
                                         Format format,
                                         const DestructionCallback& callback)
    : size_(size), format_(format), callback_(callback), mapped_(false) {
}

GpuMemoryBufferImpl::~GpuMemoryBufferImpl() {
  callback_.Run();
}

// static
GpuMemoryBufferImpl* GpuMemoryBufferImpl::FromClientBuffer(
    ClientBuffer buffer) {
  return reinterpret_cast<GpuMemoryBufferImpl*>(buffer);
}

// static
size_t GpuMemoryBufferImpl::BytesPerPixel(Format format) {
  switch (format) {
    case RGBA_8888:
    case RGBX_8888:
    case BGRA_8888:
      return 4;
  }

  NOTREACHED();
  return 0;
}

gfx::GpuMemoryBuffer::Format GpuMemoryBufferImpl::GetFormat() const {
  return format_;
}

bool GpuMemoryBufferImpl::IsMapped() const {
  return mapped_;
}

ClientBuffer GpuMemoryBufferImpl::AsClientBuffer() {
  return reinterpret_cast<ClientBuffer>(this);
}

}  // namespace content
