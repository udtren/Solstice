---
type: history
topic: gpu-engine
status: archived record (moved from docs/agent/gpu-engine.md on 2026-10-07)
sources: [docs/agent/gpu-engine.md, git history]
---

# GPU engine history: filters and transforms (phases 4.93-)

CPU filter/transform baseline, GPU affine passes, GPU Liquify grid warp. Moved verbatim from `docs/agent/gpu-engine.md`; current state, decisions and
invariants stay there ([gpu-engine.md](../../gpu-engine.md)). Each section records what was true
when it was written; later sections and the current document take precedence.

## Priority 4 baseline: CPU filter and transform costs (phase 4.93)

The user chose priority 4 (filters and transforms) after phase 4.92.
`KisGpuPaintDeviceTest::benchmarkFiltersAndTransforms`, opt-in with
`KRITA_GPU_BENCHMARK_FILTERS=1`, measures a 2480x3508 RGBA32F layer (the
user's document size) with random content, as the median of 3 runs on 32
hardware threads.

- **Filter rows** time one `KisFilter::process` call over the layer, and the
  layer split into 32 bands on a `QThreadPool` (source to destination, like a
  filter stroke's parallel patches).
- **Transform rows** time one worker call, as the Transform Tool does:
  - `InplaceTransformStrokeStrategy::reapplyTransform` renders the live
    preview with `KisTransformUtils::transformDevice`, at a preview level of
    detail (`forceLodMode`).
  - The final apply renders at full resolution.

| Operation | Single call | 32 bands |
| --- | --- | --- |
| Readback of a GPU-written layer | 14.9ms | - |
| Gaussian blur r5 | 1021.7ms | 143.5ms |
| Gaussian blur r30 | 1065.9ms | 199.3ms |
| Levels (lightness curve) | 173.5ms | 229.2ms |
| Curves (all channels) | 0.1ms | 24.3ms |
| HSV adjust s+20 | 175.8ms | 47.6ms |
| Unsharp mask | 6155.6ms | 389.8ms |
| Affine scale 0.9 + rotate 10°, bicubic | 440.5ms | - |
| Puppet Warp (rigid MLS, 9 points) | 601.6ms | - |
| Liquify (20 moves, sigma 200) | 217.0ms | - |

Notes:

- **Readback is not the bottleneck.** About 15ms for the whole layer, after
  phases 4.54-4.57 batched it.
- **The transform workers are single-threaded.** Their full call is the
  user's wait on apply. Their preview cost scales with the preview level of
  detail.
- **Filters are already parallel.** About 140-390ms per full-layer
  application on this machine.
- **Suspect rows:**
  - Curves: the single in-place call returned in 0.1ms, so the properties set
    by name may not have produced a non-identity transfer. This row needs a
    configuration built through the filter's own API before it is used.
  - Levels: slower in bands, probably because each `process` call builds the
    color transformation.
- **Proposed order by these numbers:**
  1. Transform Tool rendering: affine bicubic first, as the most common
     operation, then the warp/Puppet Warp and Liquify workers.
  2. The blur family: Gaussian, then Unsharp, which is built on it.
  3. Per-pixel color adjustments, where parallel CPU code is already fast.

  This needs the user's choice.

## GPU affine transform passes (phase 4.94)

The user chose the Transform Tool, starting with the affine transform.

**How the CPU worker works.** `KisTransformWorker::runPartial` resamples with
two in-place passes, `transformPass<H>` then `transformPass<V>`. The general
branch uses `a, b, c` / `e, d, f`; the shear branch uses
`xscale, yscale*xshear, 0` / `yscale, yshear, 0`.

- Each line runs `KisFilterWeightsApplicator::processLine`. For every
  destination pixel, `calculateBlendSpan` uses `KisFixedPoint` (24.8)
  arithmetic plus one double `dstToSrc()` division to pick one of the 256
  `KisFilterWeightsBuffer` entries (`qint16` weights summing to 255).
- `KoMixColorsOpImpl::mixColors` blends with double accumulators
  (`mixtype` is double for float and half), divides, clamps to
  +-FLT_MAX / +-HALF_MAX, and stores; half stores go double -> float -> half.
- Source pixels outside the line come from the line's first/last pixel when
  `shear == 0` (clamp to edge); otherwise from the device default pixel.
- The source line is cleared to the default pixel, then the destination range
  is written.

**GPU implementation.**

- **`libs/gpu/shaders/transform_pass.comp`** (RGBA32F/F16):
  - One invocation per pixel of the destination tile grid.
  - Resampled pixels inside each line's `[dstStart, dstEnd)`; the default
    pixel elsewhere, which is what the in-place CPU passes leave there.
  - The span lookup is the same integer fixed-point code with `precise`
    doubles and a correctly rounded division (fma residual over neighbouring
    doubles), plus `int()` truncation.
  - Accumulation follows the CPU's order without contraction. Divisions are
    correctly rounded, and double -> float uses nearest-even with neighbour
    checks; F16 then rounds like Imath (`roundToHalf`).
  - A non-positive total alpha gives zeros, as the CPU's `memset`.
- **`KisGpuTransformPass`** (`libs/gpu`) records a pass from line ranges,
  weights and tile tables. It requires `shaderFloat64`, which `KisGpuContext`
  now enables when supported (`KisGpuDeviceInfo::supportsFloat64`).
- **`KisFilterWeightsApplicator::setupLine()`** now holds the line setup from
  `processLine()` (destination range and source borders); `processLine()`
  calls it. Both the CPU and the GPU plan use the same code.
- **`kis_transform_worker.cc::planGpuPass()/runGpuPasses()`** plan both
  passes with the CPU classes. The weights headers define non-inline
  functions and can only be included in this translation unit.
- **`KisGpuTransformWorker::runPlannedPasses()`** (`libs/image/gpu`):
  - The x pass goes from the device (`ReadOnly`) into a temporary device
    (`WriteOnly`).
  - The y pass goes from the temporary device into the device, `WriteOnly`
    over the tile grid of source | x rect | y rect.
  - Each pass is its own submission and waits; on failure the device is
    unchanged and the CPU passes run.
  - The written rect is then downloaded in one batch, because the following
    CPU `purgeDefaultPixels()` downloaded the tiles one by one (about 250ms
    instead of about 15ms).
- **Scope.**
  - Only `KisTransformWorker::run()` (`m_wholeDevice`): `runPartial()` callers
    may have pixels outside the rect, which the in-place CPU passes leave
    untouched.
  - RGBA32F/F16, with LOD 0, no wrap-around, and a line setup that is never
    empty (an empty CPU line keeps its source pixels).
  - `KRITA_GPU_TRANSFORM=0` disables it; it is also off when the GPU engine
    is disabled.
  - The 90-degree quadrant pre-rotation, simple translations and flips, and
    the single-pass shear stay on the CPU, as do the perspective worker, warp,
    Puppet Warp, Liquify, cage and mesh.
  - The Transform Tool's live preview renders at a preview level of detail,
    so it stays on the CPU. The final apply (`transformDevice` ->
    `KisTransformWorker::run()`) uses the GPU. Free Transform's perspective
    pass returns at once for an identity perspective.
- **Cached bounds.** GPU writes did not invalidate `KisPaintDevice`'s cached
  bounds, whereas writable CPU iterators invalidate them when created. The
  first parity run returned the pre-transform `exactBounds()`. New
  `KisPaintDevice::invalidateCachedBounds()` is now called by
  `KisGpuTileAccess` for write accesses at construction and on publish.
- **Debug.** `KRITA_GPU_TRANSFORM_DEBUG=1` prints stage times; trace scope
  `transform.gpu_passes`.

**Tests** (Vulkan validation):

- New `KisGpuPaintDeviceTest::testGpuTransformMatchesCpu`, 58 rows, all
  bit-identical to the CPU, with exact bounds and Undo/Redo:
  - F32 and F16;
  - scale+rotate, upscale, downscale, rotate only, sub-pixel scale,
    non-uniform, flip+scale, 100 degrees (quadrant), shear+scale, two-axis
    shear and tiny scale;
  - plain, offset device and opaque default pixel variants;
  - every filter strategy.
- `KisGpuPaintDeviceTest` 209 (1 skipped: the opt-in benchmark),
  `KisGpuProjectionTest` 199, `KisGpuBrushTest` 4451, `KisGpuStrokeTest` 360,
  `KisGpuBrushJobsTest` 41, `KisGpuEngineTest` 11, `KisGpuCanvasUploadTest` 38,
  `KisGpuGLInteropTest` 3.
- `kis_transform_worker_test` 40, also with `KRITA_GPU_PROJECTION=1`; its
  devices are mostly 8-bit, which the GPU path does not handle.
- `kis_paint_device_test` 51.

**Benchmark** (2480x3508 RGBA32F, scale 0.9, rotate 10 degrees, bicubic,
CPU-resident source): CPU 432ms, GPU 63ms (6.8x).

- The GPU passes take 33-56ms. The compute is about 3ms per pass; the rest is
  uploading a CPU-resident source, allocating the destination tiles and the
  batched download.
- Planning takes 0.1ms.

Installed gpu/image/ui/defaultpaintops; hashes match. The user reported the
manual checks OK on 2026-10-06:
- Free Transform scale and rotate on normal and Background layers: speed,
  result, Undo/Redo;
- flips, large down- and upscales;
- RGBA16F documents;
- unchanged perspective, warp, Puppet Warp and Liquify;
- image resize and rotate.

## GPU Liquify grid warp (phase 4.97)

The user chose Liquify next (task order 1, 3, 4, 6 of 2026-10-07). Phase
4.96 stays reserved for Puppet Warp's mesh rendering.

**How the CPU worker paints.** `KisLiquifyTransformWorker::run()` clears the
destination and calls `GridIterationTools::iterateThroughGrid()` over the
sub-grid touched by strokes (`calculateCorrectSubGrid()`), then copies the
rest of the source rect (`cutOutSubgridFromBounds()`). For every cell,
`PaintDevicePolygonOp`:

- skips an empty bound rect (`clipDstPolygon.boundingRect().toAlignedRect()`);
- copies a pixel-aligned identity cell with `KisPainter::copyAreaOptimized()`,
  postponed and merged (`KisRegion::mergeSparseRects()`) when
  `canProcessRectsInRandomOrder()` found every cell convex;
- otherwise first flushes the postponed copies, then visits every pixel of the
  bound rect inside the polygon (`QPolygonF::containsPoint`, odd-even) and
  writes `KisRandomSubAccessor::sampledOldRawData()` at
  `KisFourPointInterpolatorBackward::getValue()` (or at
  `fallbackSourcePoint()` when the interpolator is not valid).

A later cell overwrites the pixels of an earlier one where they overlap
(folds, and the one-pixel overlap from `adjustAlignedPolygon()`).

**GPU implementation.**

- **`KisGpuGridWarpWorker::Recorder`** (`libs/image/gpu`) is passed to
  `iterateThroughGrid()` in place of `PaintDevicePolygonOp` and mirrors its
  decisions, recording operations in order:
  - copies (split into 128x128 chunks);
  - warps, with the CPU's bound rect, the polygon edges' values that do not
    depend on the pixel (`qFuzzyCompare` skip, direction,
    `(x2 - x1) / (y2 - y1)`, the implicit closing edge), the interpolator's
    coefficients (new `KisFourPointInterpolatorBackward::coefficients()`),
    `isValid(0.1)` and the fallback point.
- **`libs/gpu/shaders/grid_warp.comp`**, through **`KisGpuGridWarpPass`**:
  - *claim* (`-DCLAIM`): one workgroup per operation; each pixel it writes
    stores `atomicMax(owner, index + 1)` in a device-local owner buffer over
    the union of the bound rects;
  - *resolve* (RGBA32F/F16): one invocation per destination tile-grid pixel;
    the owner computes it, the default pixel elsewhere.
  - `getValue()` in doubles with `precise`, correctly rounded division and
    square root (fma residuals over neighbouring doubles), and CPU branch order,
    including `yMu2` computed with `xBasedMu()`.
  - Sampling: `qRound()` is `int(d + 0.5)` for these non-negative values (Qt
    6.8 on x86-64 with SSE2); the `qint16` weights are mixed like
    `KoMixColorsOpImpl::mixColors()` with their sum as the alpha divisor,
    with the double -> float (-> half) rounding of phase 4.94.
  - A sample outside the source tile grid sets a flag. The source access
    covers the source polygons plus 8 pixels and the copy rects. The worker
    then returns false, and `run()` clears the destination and paints on the
    CPU.
- **`KisLiquifyTransformWorker::run()`** records and runs the GPU path when
  `KisGpuGridWarpWorker::canRun()`; the trailing copies outside the sub-grid
  stay on the CPU (`copyAreaOptimized()` shares whole tiles). The written rect
  is downloaded in one batch first.
- **Scope** (`canRun()`):
  - RGBA32F/F16, LOD 0, no wrap-around, `shaderFloat64`;
  - distinct devices with the same color space and default pixel;
  - **the same offset.** With different offsets `bitBlt()` cannot use
    `fastBitBlt()`. The Copy composite op (`KoOptimizedCompositeOpCopy128`)
    then clears fully transparent pixels in whole SIMD batches, and the
    color of transparent pixels depends on the batch grouping. The first test
    run failed on exactly those pixels. The Transform Tool's source is a copy
    of the node's device, so the offsets match.
  - `KRITA_GPU_LIQUIFY=0` or `KRITA_GPU_TRANSFORM=0` disables it.
  - The Liquify preview while painting (`runOnQImage()` on the 8-bit
    thumbnail) and level-of-detail previews stay on the CPU.
- **Debug.** `KRITA_GPU_TRANSFORM_DEBUG=1` prints the operation count and
  stage times; trace scope `transform.gpu_grid_warp`.

**Tests** (Vulkan validation):

- New `KisGpuPaintDeviceTest::testGpuLiquifyMatchesCpu`, 44 rows. All rows
  are bit-identical to the CPU, with exact bounds, Undo/Redo, and a
  destination that held pixels before `run()`:
  - F32 and F16;
  - push, wash, grow, shrink, rotate;
  - fold: large moves with small radii, giving crossing, non-convex cells and
    unmerged copies;
  - far: content pushed outside the source rect;
  - the undo brush, and no stroke;
  - pixel precision 1, 4, 8 and 16;
  - plain, offset and opaque-default variants.
- `KisGpuPaintDeviceTest` 251 (1 skipped: the opt-in benchmark),
  `kis_liquify_transform_worker_test` 15 (also with `KRITA_GPU_PROJECTION=1`),
  `KisGpuProjectionTest` 199, `KisGpuEngineTest` 11.

**Benchmark** (2480x3508 RGBA32F, 20 moves with sigma 200, about 46,000
operations; `benchmarkFiltersAndTransforms`): CPU 219ms, GPU 56ms (3.9x).
Temporary instrumentation, since removed, gave this breakdown:

| Stage | Time |
| --- | --- |
| Clear and `canProcessRectsInRandomOrder()` | about 13ms |
| Recording through `iterateThroughGrid()` | about 18ms |
| GPU path | about 20ms |
| Remaining CPU copies | about 2ms |

Inside the GPU path:

| Stage | Time |
| --- | --- |
| Prepare (source upload, 1216 tiles) | about 5ms |
| Table write (about 15MB of operations) | about 3.5ms |
| GPU wait | about 1.8ms |
| Batched download | about 7.6ms |

Further gains would need a recorder that walks the grid without the per-cell
`QVector`/`QPolygonF` allocations of `iterateThroughGrid()`, or a GPU
convexity check.

Installed gpu/image; hashes match. The user reported the manual checks OK on 2026-10-07:

- Liquify move, scale, rotate and undo strokes on RGBA32F and RGBA16F layers;
  apply, Undo and Redo;
- a large Liquify over most of a layer: apply time;
- 8-bit layers unchanged (CPU);
- the preview while painting unchanged.
