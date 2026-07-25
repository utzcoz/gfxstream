// Copyright 2025 Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

// This library is header-only. This translation unit exists solely so that the
// resulting static archive contains at least one member: some archivers (e.g.
// macOS `ar`) refuse to create an empty archive.
namespace gfxstream {
namespace {
[[maybe_unused]] const int kHostSnapshotStub = 0;
}  // namespace
}  // namespace gfxstream
