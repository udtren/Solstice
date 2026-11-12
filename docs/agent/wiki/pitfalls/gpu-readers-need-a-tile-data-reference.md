---
type: pitfall
updated: 2026-11-12
sources:
  - libs/image/tiles3/kis_tile.cc (referencedTileData, lockForWrite, safeReleaseOldTileData)
  - libs/image/gpu/KisGpuTileAccess.cpp (prepareImpl, releaseReplaced)
  - libs/image/gpu/KisGpuTileBackend.cpp (KisTileGpuHooks::prepareCpuRead)
  - libs/image/filter/kis_filter.cc (process, the temporary device)
  - libs/image/tests/KisGpuPaintDeviceTest.cpp (testReadOnlyAccessKeepsTileDataAlive)
  - docs/agent/gpu-engine.md ("GPU access API", tile data lifetime of readers)
related:
  - extent-smaller-than-tiles.md
  - gpu-tiles-read-back-after-strokes.md
  - ../concepts/krita-copy-semantics.md
---

# GPU readers need a tile data reference

**What happened.** During the manual check of phase 4.116 the application
crashed when a Desaturate filter mask was added to a paint layer: an access
violation in `KisSwappedDataStore::swapInTileData()`, called from
`KisTileData::blockSwapping()` in `KisGpuTileAccess::prepareImpl()` of a
merge job. The tile data had no pixel buffer and no swap chunk: it had been
destroyed.

**Why.**

- `KisTile` protects CPU readers with its lock counter: while a reader
  holds `lockForRead()`, a copy-on-write in `lockForWrite()` parks the old
  tile data instead of releasing it. The GPU access cannot take that lock
  (it would download the GPU copy), so it kept `tile->tileData()` as a raw
  pointer, with the tile alive but nothing holding the data.
- The layer sat at x = -10, so its projection device is off the image's
  tile grid: its masks run on the CPU, and two merge jobs of neighbouring
  image tiles share its tiles. The scheduler keeps jobs apart by image
  tiles only.
- One job applied the mask through `KisFilter::process()`: the temporary
  device shares the tile's data, the copy back writes the tile
  (copy-on-write), and the temporary device is destroyed, which frees the
  shared data. The other job had looked that data up a moment before and
  was about to snapshot it for an upload.

**Fix.** `KisTile::referencedTileData()` returns the data with a `ref()`
taken under the lock that guards the release of replaced data. Read-only
accesses and the batched download (`syncToCpu()`) use it and `deref()` when
they are done.

**How to notice.** A crash in the swap code on a machine that does not
swap (`data() == 0` and an empty swap chunk mean a destroyed tile data).
The race is narrow: a stress test with concurrent updates of such a layer
passed three times without the fix. The test that fails without it replays
the sequence in one thread and counts the tile data in the store.

**Rule.** Whoever keeps a `KisTileData *` across anything another thread
can interleave with needs either the tile's lock or a reference. Holding
the `KisTileSP` is not enough.
