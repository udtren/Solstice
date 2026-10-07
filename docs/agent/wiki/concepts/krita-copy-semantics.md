---
type: concept
updated: 2026-10-07
sources:
  - libs/image/kis_painter.cc (copyAreaOptimizedImpl, bitBltImpl COMPOSITE_COPY branch)
  - libs/image/kis_paint_device.cc (fastBitBltPossibleImpl)
  - libs/pigment/compositeops/KoOptimizedCompositeOpCopy128.h
  - libs/image/kis_random_sub_accessor.cpp (sampledOldRawData)
  - libs/image/kis_grid_interpolation_tools.h (PaintDevicePolygonOp)
related:
  - cpu-gpu-bit-parity.md
  - ../history/gpu-phases-4.93-.md
---

# Krita copy and sampling semantics

These details decide whether a GPU reimplementation can match the CPU
exactly. They were found in the Liquify GPU work (phase 4.97).

## `KisPainter::copyAreaOptimized(dstPt, src, dst, rect)`

- If both the source sample (`src->extent() & rect`) and the destination
  sample are empty: nothing happens.
- If only the source is empty: `dst->clear(dstRect)`, so the destination's
  default pixel results, not the source's.
- If the source extent does not contain the rect and both default pixels
  are equal: only `srcSample | dstSample` is copied. Pixels outside both
  extents are default on both sides, so the result equals a full copy.
- **With equal default pixels, the result is "every pixel of the rect
  becomes the source pixel"**, as far as `bitBlt()` copies exactly (next
  section).

## `bitBlt()` with `COMPOSITE_COPY`

- **Fast path** (`KisPainter::bitBltImpl`): no selection, unit opacity, same
  source and destination position, `fastBitBltPossible()` (both devices
  have the same `x()`/`y()` offset and color space), no wrap-around. Tile
  data is copied: exact bytes, including the color of transparent pixels.
- **Otherwise** the Copy composite op runs. For 128-bit pixels (RGBA F32)
  this is `KoOptimizedCompositeOpCopy128`:
  - the SIMD path clears a whole batch when *all* its source alphas are 0,
    and otherwise copies the batch verbatim;
  - the scalar path clears a pixel when its source alpha is 0.
  The color left in transparent pixels therefore depends on the SIMD width,
  the row alignment and the neighbouring pixels. It is not reproducible on
  the GPU.
- Consequence: `KisGpuGridWarpWorker::canRun()` requires equal device
  offsets. The Transform Tool always meets this, because its source is a
  copy of the node's device.
- `KisPaintDevice::fastBitBltPossible()` is protected; outside the class,
  compare `x()`, `y()` and the color spaces.

## Transparent-pixel color in general

Krita leaves the color of alpha-0 pixels unspecified (see the phase 0 parity
rule in `docs/agent/gpu-engine.md`). Bit-identical tests do compare it, so a
GPU path must follow the CPU's exact branch, or be excluded as above.

## `KisRandomSubAccessor::sampledOldRawData()`

- `x = qFloor(px)`, `hsub = px - x` (and the same for y).
- Four `qint16` weights:
  - `qRound((1 - hsub) * (1 - vsub) * 255)`;
  - `qRound((1 - vsub) * hsub * 255)`;
  - `qRound(vsub * (1 - hsub) * 255)`;
  - `qRound(hsub * vsub * 255)`.
- The pixels are `(x, y)`, `(x+1, y)`, `(x, y+1)` and `(x+1, y+1)`.
- `mixColors(pixels, weights, 4, dst, sumOfWeights)`: the sum can be 254-256,
  and it is the alpha divisor. `qRound()` semantics are in
  [Qt numeric and geometry semantics](qt-numeric-and-geometry.md).

## `GridIterationTools::PaintDevicePolygonOp`

For each grid cell, in order:

1. Skip the cell when its aligned bound rect is empty.
2. When the cell is an identity cell (same polygon, pixel-aligned rect),
   copy it. The copy is postponed and later merged with
   `KisRegion::mergeSparseRects()` when `setCanMergeRects(true)`.
3. Otherwise:
   - flush the postponed copies first;
   - iterate the bound rect;
   - for pixels inside the polygon (`containsPoint`, odd-even), sample at
     `KisFourPointInterpolatorBackward::getValue()`, or at
     `fallbackSourcePoint()` when the interpolator is invalid.

A later cell overwrites an earlier one where they overlap: folds, and the
1-pixel overlap created by `adjustAlignedPolygon()`'s 1e-5 offsets.
`KisGpuGridWarpWorker::Recorder` mirrors these decisions step by step; keep
the two in sync if either changes.
