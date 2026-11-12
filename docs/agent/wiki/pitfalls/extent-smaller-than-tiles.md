---
type: pitfall
updated: 2026-11-12
sources:
  - "libs/image/tiles3/KisTiledExtentManager.cpp (updateExtent(), clear())"
  - "libs/image/tiles3/tests/kis_tiled_data_manager_test.cpp (stressTestExtentAfterConcurrentAdds)"
  - "libs/image/kis_layer_projection_plane.cpp (applyImpl(), needRect &= device->extent())"
  - "libs/image/gpu/KisGpuProjectionCompositor.cpp (composite(), rect & extent)"
  - "libs/image/tiles3/kis_tiled_data_manager.h (rollback() and rollforward() call recalculateExtent())"
  - "docs/agent/gpu-engine.md (Risks and open questions)"
related:
  - compressed-updates-outlive-their-model.md
  - shared-settings-file-stale-writes.md
---

# A device extent smaller than its tiles

`KisPaintDevice::extent()` comes from `KisTiledExtentManager`, which counts
the tiles per row and column and caches the resulting rectangle. Layer
compositing trusts it: `KisLayerProjectionPlane::applyImpl()` and the GPU
`KisGpuProjectionCompositor::composite()` both clip the update rect to the
layer's extent and skip the rest. An extent that is too small therefore
removes the layer from the projection there, in whole 64 px tiles, without
any error.

## What went wrong

`updateExtent()` read the rows and columns, then took the lock and stored
the rectangle. Two threads that each added the first tile of a new row or
column could store in the opposite order of their reads, so the older,
smaller rectangle was stored last. It stayed until the next row or column
appeared or disappeared, or until the extent was recomputed (`rollback()` /
`rollforward()`, so Undo and Redo repair it).

Measured with four threads released together, each adding tiles that extend
the extent: 793 of 200,000 rounds ended with a stale extent; one round in
20,000 when the threads were started per round. Upstream code
(`KisTiledExtentManager.cpp`, 2017-2018), not part of the GPU engine.

## Rule

- "Compute, then lock and store" loses updates. Compute and store under the
  same lock, or store only if nothing changed since the read. Fixed
  2026-11-12: `updateExtent()` holds the extent lock for the whole step and
  `clear()` takes it first. Every change of a row or column is followed by
  the changing thread's own `updateExtent()`, so the last one to run sees
  all changes.
- A check that compares two results which read the same cached value cannot
  see that value being wrong. `KisGpuProjectionStressTest` compares the
  projection after a stroke with a full refresh; both skip the same strip.
  Compare against something that recomputes the cache (Undo/Redo, or
  `extent()` against the tiles).
- To hit a race between two short critical sections, keep the worker threads
  alive and release them together; starting threads per round made this one
  40 times rarer.

## Open

Found while looking for the cause of a rare white rectangle during
painting (see the GPU engine risks). It is not shown to be that cause.
