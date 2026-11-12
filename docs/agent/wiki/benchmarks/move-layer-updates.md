---
type: benchmark
updated: 2026-11-12
sources:
  - libs/image/tests/KisMoveLayerBenchmark.cpp (opt-in KRITA_MOVE_BENCHMARK=1)
  - libs/ui/tool/strokes/move_stroke_strategy.cpp (MoveStrokeStrategy::doCanvasUpdate, moveNodeCommon)
  - libs/image/gpu/KisGpuMergeBatch.cpp (tryAdd)
  - libs/image/kis_group_layer.cc (setX, phase 4.112)
  - plugins/impex/libkra/tests/KisMoveDocumentBenchmark.cpp (opt-in, a real .kra)
  - libs/image/kis_paint_device.cc (moveTo keeps the cached bounds)
  - libs/image/kis_async_merger.cpp (compositeWithProjection, batched readback)
  - docs/agent/gpu-engine.md ("Moved layers and groups (phase 4.112)")
related:
  - clone-layer-updates.md
  - ../history/gpu-phases-4.93-.md
---

# Move Tool updates

What one Move Tool update costs, before and after phase 4.112
(`docs/agent/gpu-engine.md`, "Moved layers and groups (phase 4.112)").

**Setup:**

- Machine: AMD Ryzen 9 9950X, NVIDIA RTX PRO 6000 Blackwell (driver
  596.86). Local development build, 2026-11-12; one run per column.
- Image 2480x3508. Layers, bottom to top: white background, a full-canvas
  layer, the moved node, a full-canvas Multiply layer (opacity 180), an
  empty top layer. Random content with random alpha.
- Moved node: "full layer" (content over the whole canvas), "object layer"
  (1200x1200 at (400, 600)), or "group" (two layers, 1200x1200 and 800x800,
  all three nodes moved, as `MoveStrokeStrategy::recursiveApplyNodes()`
  does).
- A step is what `MoveStrokeStrategy::doCanvasUpdate()` does for one pointer
  position: the dirty rect is the nodes' visible bounds before and after,
  the nodes get the new offset, `refreshGraphAsync(node, dirtyRect)`. The
  time runs until the image and the GPU are idle. 12 steps; "aligned"
  offsets are multiples of 64, "unaligned" ones start at (37, 23) and grow
  by (61, 35). Median per step in ms. "Downloads" is the number of GPU
  tiles read back to the CPU per step.
- "Bounds" is the part of the step spent on the bounds and the offsets.
- "Dab" is the update after painting a 256x256 dab on the top layer over
  the node, which stays where the last step left it (12 dabs, median ms,
  downloads per dab). F32-GPU writes the dabs on the GPU, like the GPU
  brush.
- After every scenario the incremental result was compared with a full
  refresh: no difference in any row.
- Synthetic and synchronous: the Move Tool sends at most one update per
  30 ms and skips a step while updates still run, and the canvas reads GPU
  tiles directly. Not measured in the application. The moved layers were
  written on the CPU, so their tiles are valid on both sides; a layer
  painted with the GPU brush is read back once when the stroke starts
  (its tight bounds), not on every step.

## GPU engine, RGBA F32: before → after

"Before" is `main` before phase 4.112; "after" has all its changes (layer
shift, group projection, loose group bounds, kept bounds). Each cell:
median ms per step before → after (downloads per step before → after).

| Moved node | aligned offsets | unaligned offsets |
| --- | --- | --- |
| full layer | 9.94 → 8.33 (0 → 0) | 166.07 → 8.54 (1751.3 → 0) |
| object layer | 2.64 → 1.32 (0 → 0) | 35.18 → 1.45 (401.7 → 0) |
| group | 70.21 → 16.46 (259.3 → 0) | 110.03 → 15.96 (771.9 → 0) |

Bounds part of a step: 0.6-1.0 → 0.01 ms for the layers; for the group
65.73 → 0.01 ms (aligned) and 56.43 → 0.01 ms (unaligned).

Dab over the parked node (median ms, downloads per dab):

| Moved node | aligned | unaligned |
| --- | --- | --- |
| full layer | 0.21 → 0.20 (0 → 0) | 2.39 → 0.22 (24.6 → 0) |
| object layer | 0.22 → 0.21 (0 → 0) | 2.47 → 0.19 (24.6 → 0) |
| group | 0.20 → 0.21 (0 → 0) | 2.03 → 0.23 (24.6 → 0) |

## CPU paths: before → after

Median ms per step, aligned / unaligned offsets. Only the bounds changed
here (kept across the move; loose for groups).

| Moved node | RGBA8 | RGBA F32, GPU engine off |
| --- | --- | --- |
| full layer | 6.19 / 6.32 → 5.40 / 5.84 | 23.21 / 26.10 → 22.92 / 23.96 |
| object layer | 2.95 / 3.13 → 2.31 / 2.31 | 7.82 / 7.35 → 5.96 / 6.41 |
| group | 4.76 / 4.90 → 3.67 / 3.72 | 13.30 / 15.04 → 11.23 / 13.00 |

## A real document

`KisMoveDocumentBenchmark`, same machine and day. A user's document:
1754x2480, RGBA F16, 68 layers. The moved group has 20 nodes (two nested
groups, 16 paint layers, one of them full-canvas with a transparency mask)
and an Outer Glow layer style; its dirty rect is the whole image. 12 steps
of (7, 5) pixels. "Before" is the state after the layer and group changes
above (what the user tried first); "after" adds the kept bounds and the
batched readback.

| Variant | step median, before → after | of which bounds | downloads per step |
| --- | --- | --- | --- |
| GPU engine off | 364.28 → 29.28 ms | 334.60 → 0.06 ms | 0 |
| GPU engine off, no layer style | 356.91 → 28.77 ms | 328.17 → 0.04 ms | 0 |
| GPU engine off, no layer style, no masks | 353.32 → 20.04 ms | 332.75 → 0.04 ms | 0 |
| GPU engine on | 521.82 → 31.18 ms | 345.82 → 0.06 ms | 2184 → 2184 (now in batches) |
| GPU engine on, no layer style | 362.89 → 24.48 ms | 336.48 → 0.04 ms | 0 |
| GPU engine on, no masks | 494.75 → 23.42 ms | 338.97 → 0.06 ms | 2184 → 2184 (now in batches) |
| GPU engine on, no layer style, no masks | 340.62 → 12.89 ms | 326.32 → 0.03 ms | 0 |

Bounds per node before (one step): 16 of the 17 paint layers took 8-38 ms
each, also a layer whose content is 133x171 pixels (35 ms) and hidden
layers; the groups (loose bounds) took 0.01-0.04 ms.

The largest step of every run stayed at 340-420 ms: the first step
calculates the bounds once. In the application the Move Tool does that
when the stroke starts (the outline rect), once after the layers' pixels
changed.

## Reading

- Before, an unaligned step on the GPU engine was 6 times slower than with
  the engine off (166 ms against 26 ms for a full-canvas layer): the CPU
  composite of the moved layer read the projection back tile by tile
  (about 1750 tiles), and the layers above uploaded it again.
- After, unaligned and aligned offsets cost the same and read nothing
  back. A full-canvas layer takes 8-9 ms, less than the Move Tool's 30 ms
  update interval.
- A layer left at an unaligned offset no longer slows painting over it
  (2.4 → 0.2 ms per dab).
- A group cost 70 ms per step on the GPU engine even at aligned offsets:
  its tight bounds read the projection's pixels, which every update had
  just written on the GPU.
- The synthetic rows did not show what made a real document slow: the
  exact bounds of layers whose tiles cover more than their content (0.6 ms
  per step in the synthetic layers, about 330 ms in the document), with
  the GPU engine on or off.
- Open: the first Move Tool stroke after the layers changed still scans
  them once (about a third of a second in that document).
- Open: a group step (16 ms) is still slower than a full-canvas layer step
  (8-9 ms), and slower than the same step with the engine off (11-13 ms),
  although it covers less area. Not analyzed (70 GPU composites per step
  against 33 for a layer).
