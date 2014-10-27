// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_CHILD_CHILD_GPU_MEMORY_BUFFER_MANAGER_H_
#define CONTENT_CHILD_CHILD_GPU_MEMORY_BUFFER_MANAGER_H_

#include "cc/resources/gpu_memory_buffer_manager.h"
#include "content/child/thread_safe_sender.h"

namespace content {

class ChildGpuMemoryBufferManager : public cc::GpuMemoryBufferManager {
 public:
  ChildGpuMemoryBufferManager(ThreadSafeSender* sender);
  ~ChildGpuMemoryBufferManager() override;

  // Overridden from cc::GpuMemoryBufferManager:
  scoped_ptr<gfx::GpuMemoryBuffer> AllocateGpuMemoryBuffer(
      const gfx::Size& size,
      gfx::GpuMemoryBuffer::Format format,
      gfx::GpuMemoryBuffer::Usage usage) override;
  gfx::GpuMemoryBuffer* GpuMemoryBufferFromClientBuffer(
      ClientBuffer buffer) override;

 private:
  scoped_refptr<ThreadSafeSender> sender_;

  DISALLOW_COPY_AND_ASSIGN(ChildGpuMemoryBufferManager);
};

}  // namespace content

#endif  // CONTENT_CHILD_CHILD_GPU_MEMORY_BUFFER_MANAGER_H_
