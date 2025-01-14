// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_IPC_CLIENT_GPU_MEMORY_BUFFER_IMPL_WEBOS_SURFACE_H_
#define GPU_IPC_CLIENT_GPU_MEMORY_BUFFER_IMPL_WEBOS_SURFACE_H_

#include <stddef.h>

#include <memory>
#include <webossurface/webossurface.h>

#include "base/macros.h"
#include "gpu/gpu_export.h"
#include "gpu/ipc/client/gpu_memory_buffer_impl.h"

namespace gpu {

// Implementation of GPU memory buffer based on Ozone native pixmap.
class GPU_EXPORT GpuMemoryBufferImplWebOSSurface : public GpuMemoryBufferImpl {
 public:
  ~GpuMemoryBufferImplWebOSSurface() override;

  static std::unique_ptr<GpuMemoryBufferImplWebOSSurface> CreateFromHandle(
      const gfx::GpuMemoryBufferHandle& handle,
      const gfx::Size& size,
      gfx::BufferFormat format,
      gfx::BufferUsage usage,
      const DestructionCallback& callback);

  static bool IsConfigurationSupported(gfx::BufferFormat format,
                                       gfx::BufferUsage usage);

  static base::Closure AllocateForTesting(const gfx::Size& size,
                                          gfx::BufferFormat format,
                                          gfx::BufferUsage usage,
                                          gfx::GpuMemoryBufferHandle* handle);

  // Overridden from gfx::GpuMemoryBuffer:
  bool Map() override;
  void* memory(size_t plane) override;
  void Unmap() override;
  int stride(size_t plane) const override;
  gfx::GpuMemoryBufferHandle GetHandle() const override;

 private:
  GpuMemoryBufferImplWebOSSurface(gfx::GpuMemoryBufferId id,
                                  const gfx::Size& size,
                                  gfx::BufferFormat format,
                                  const DestructionCallback& callback,
                                  surface_handle_t surface,
                                  gfx::WebOSSurfaceHandle surface_handle);

  surface_handle_t surface_;
  gfx::WebOSSurfaceHandle surface_handle_;
  void* data_;

  DISALLOW_COPY_AND_ASSIGN(GpuMemoryBufferImplWebOSSurface);
};

}  // namespace gpu

#endif  // GPU_IPC_CLIENT_GPU_MEMORY_BUFFER_IMPL_WEBOS_SURFACE_H_
