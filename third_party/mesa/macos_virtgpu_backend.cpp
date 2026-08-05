// Copyright 2025 Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

// macOS has no native (DRM/libdrm) virtio-gpu backend. Instead of the DRM
// backend used on Linux, the native platform entry points on macOS delegate to
// the kumquat userspace virtio-gpu backend, which does not depend on libdrm or
// a kernel DRM device. This lets the guest platform library provide a working
// virtio-gpu backend on macOS without libdrm.

#include "Sync.h"
#include "VirtGpu.h"

VirtGpuDevice* osCreateVirtGpuDevice(enum VirtGpuCapset capset, int fd) {
    return kumquatCreateVirtGpuDevice(capset, fd);
}

namespace gfxstream {

SyncHelper* osCreateSyncHelper() { return kumquatCreateSyncHelper(); }

}  // namespace gfxstream
