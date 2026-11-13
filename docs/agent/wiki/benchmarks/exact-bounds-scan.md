---
type: benchmark
updated: 2026-11-13
sources:
  - libs/image/kis_paint_device.cc (calculateExactBounds, Impl::calculateExactBoundsTileWise, Impl::calculateExactBoundsImpl)
  - libs/image/tests/kis_paint_device_test.cpp (testExactBoundsTileWise, testExactBoundsTileWiseWrapAround)
  - libs/image/tests/KisGpuPaintDeviceTest.cpp (testExactBoundsReadsBorderTilesOnly)
  - docs/agent/gpu-engine.md ("Moved layers and groups (phase 4.112)", the scan itself)
related:
  - move-layer-updates.md
  - ../pitfalls/exact-bounds-after-move.md
  - ../queries/cpu-performance-audit-20261113.md
---

# Exact bounds scan

What `KisPaintDevice::calculateExactBounds(false)` costs per layer in real
documents, with the pixel sweeps and with the tile-wise scan of 2026-11-13.

**Setup:**

- Machine: AMD Ryzen 9 9950X (16 cores), 128 GB. Local development build
  (RelWithDebInfo), 2026-11-13; one run per document.
- Scratch copies of four of the user's documents, loaded by a test program
  that was not committed (it follows `KisMoveDocumentBenchmark`: load, wait,
  then for every visible paint layer one uncached `calculateExactBounds(false)`
  on its paint device). CPU only; the scan reads tiles through a random
  accessor in both versions.
- "Extent" is the layer's tile extent, "content" its exact bounds clipped
  to the canvas.

**Per document** (sum over the visible paint layers; the slowest layer):

| Document | Layers | Pixel sweeps: sum | slowest | Tile-wise: sum | slowest |
| --- | --- | --- | --- | --- | --- |
| 5319x4705 RGBA8 painting | 26 | 3477 ms | 422 ms | 72 ms | 9.3 ms |
| 3899x4204 RGBA8 painting | 112 | 365 ms | 161 ms | 32 ms | 11.5 ms |
| 897x814 RGBA F32 asset | 3 | 2 ms | 1.0 ms | 0.4 ms | 0.2 ms |
| 746x647 RGBA F32 asset | 3 | 4 ms | 2.0 ms | not rerun | |

**Readings:**

- The cost follows the tile extent, not the content. In the first document
  most layers have tiles far outside the canvas (extents up to 9856x6976,
  68.8 Mpx); the median layer cost 86 ms. In the second, one layer with
  81x75 px of content in a 3584x4224 extent cost 161 ms.
- Tile-wise, 68.8 Mpx of mostly missing tiles take 9 ms; 15.1 Mpx of tiles
  that exist but are empty take 11.5 ms. The second is memory bandwidth
  (every tile is read once), the floor for this approach.
- An unoptimized prototype of the same idea (every tile tested, a byte-wise
  alpha test) was 6 to 12 times faster than the sweeps; the committed
  version tests alpha with one typed OR per run and skips cells that cannot
  move an edge.
- Same result: the prototype equalled the sweeps on all 137 layers with a
  transparent default pixel, and the committed scan equals the prototype on
  them. One background layer has an opaque default pixel; that case keeps
  the sweeps (they search outside the image bounds only and took 0 ms).

**Where it is paid:** the first request after a layer's pixels changed: the
start of a Move or Transform stroke, a visibility or property change
(`KisNodePropertyListCommand`), precise layer thumbnails. The cache and its
translation on `moveTo()` are unchanged
([pitfall](../pitfalls/exact-bounds-after-move.md)).
