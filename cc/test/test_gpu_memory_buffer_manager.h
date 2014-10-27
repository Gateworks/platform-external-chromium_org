// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_TEST_GPU_MEMORY_BUFFER_MANAGER_H_
#define CC_TEST_TEST_GPU_MEMORY_BUFFER_MANAGER_H_

#include "cc/resources/gpu_memory_buffer_manager.h"

namespace cc {

class TestGpuMemoryBufferManager : public GpuMemoryBufferManager {
 public:
  TestGpuMemoryBufferManager();
  ~TestGpuMemoryBufferManager() override;

  // Overridden from GpuMemoryBufferManager:
  scoped_ptr<gfx::GpuMemoryBuffer> AllocateGpuMemoryBuffer(
      const gfx::Size& size,
      gfx::GpuMemoryBuffer::Format format,
      gfx::GpuMemoryBuffer::Usage usage) override;
  gfx::GpuMemoryBuffer* GpuMemoryBufferFromClientBuffer(
      ClientBuffer buffer) override;
};

}  // namespace cc

#endif  // CC_TEST_TEST_GPU_MEMORY_BUFFER_MANAGER_H_
