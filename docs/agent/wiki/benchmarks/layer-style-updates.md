---
type: benchmark
updated: 2026-11-12
sources:
  - libs/image/tests/KisLayerStyleBenchmark.cpp (opt-in KRITA_STYLE_BENCHMARK=1)
  - plugins/impex/libkra/tests/KisMoveDocumentBenchmark.cpp (a real .kra)
  - libs/image/layerstyles/kis_layer_style_projection_plane.cpp (gpuCompositesStyles, applyOnGpu)
  - libs/image/layerstyles/kis_ls_stroke_filter.cpp (applyStroke)
  - libs/image/gpu/KisGpuProjectionCompositor.cpp (compositeStrokeStyle)
  - docs/agent/gpu-engine.md ("Stroke layer style (phase 4.114)")
related:
  - masked-layer-updates.md
  - move-layer-updates.md
---

# Layer style updates

What a Stroke layer style costs while its layer is painted, before and
after phase 4.114 (`docs/agent/gpu-engine.md`, "Stroke layer style (phase
4.114)").

**Setup:**

- Machine: AMD Ryzen 9 9950X, NVIDIA RTX PRO 6000 Blackwell (driver
  596.86). Local development build, 2026-11-12; one run per column.
- Image 2480x3508. Layers, bottom to top: white background, the painted
  layer (random content in 1200x1200 at (400, 600)), a Multiply layer
  (opacity 180) on top.
- The painted layer's style: Stroke, Outside, a solid color, Multiply,
  75 % opacity, size 1, 4 or 10 pixels (size 0: no style). This is the
  stroke the user's documents use.
- A stroke is 24 dabs of 256x256 px moving by (20, 16). Each dab is written
  into the painted layer, then the time from `setDirty()` to an idle image
  (and an idle GPU) is measured. Values are the median per dab in ms.
  "Downloads" is the number of GPU tiles read back to the CPU per dab.
- F32-GPU writes the dabs on the GPU, like the GPU brush.
- "Refresh" is a refresh of the whole image (median of 5, ms).
- After every scenario the incremental result was compared with a full
  refresh: at most 6e-8 (F32 rounding).
- Synthetic and synchronous, like the other benchmark pages. Not measured
  in the application.

## GPU engine, RGBA F32: before → after phase 4.114

| Stroke size | dab update (downloads per dab) | full refresh (downloads) |
| --- | --- | --- |
| none | 0.21 → 0.19 (0 → 0) | 9.05 → 8.92 (0 → 0) |
| 1 px | 8.95 → 0.24 (50.0 → 0) | 264.89 → 10.98 (2145 → 0) |
| 4 px | 7.65 → 0.27 (52.0 → 0) | 218.32 → 10.87 (2145 → 0) |
| 10 px | 7.21 → 0.35 (52.6 → 0) | 237.01 → 18.42 (2145 → 0) |

## CPU paths (unchanged code; the run before / the run after)

| Stroke size | RGBA8 dab | RGBA8 refresh | F32 dab, engine off | F32 refresh, engine off |
| --- | --- | --- | --- | --- |
| none | 0.40 / 0.41 | 4.19 / 3.66 | 0.52 / 0.56 | 10.68 / 10.26 |
| 1 px | 6.06 / 6.06 | 133.65 / 135.11 | 6.67 / 6.56 | 193.65 / 142.80 |
| 4 px | 5.08 / 5.28 | 98.07 / 94.81 | 5.86 / 5.39 | 137.70 / 107.27 |
| 10 px | 3.63 / 3.87 | 116.06 / 108.74 | 4.14 / 4.04 | 159.19 / 117.75 |

The F32 refresh rows of the two runs differ by up to 50 ms: single runs,
read them as noise.

## Real documents: Move Tool step

`KisMoveDocumentBenchmark` on two of the user's documents (scratch copies),
same build, with `KRITA_GPU_LAYER_STYLE=0` (the stroke on the CPU, as
before the phase) and without it. Median ms per Move Tool step; downloads
per step in parentheses.

| Document and moved node | GPU engine, before → after | without the node's style | GPU engine off |
| --- | --- | --- | --- |
| 746x647 RGBA F32, 20 visible stroke-styled nodes; a styled group of 3 nodes, dirty rect the whole image | 73.60 → 7.16 (392 → 0) | 1.54 → 1.64 | 50.09 |
| 1754x2480 RGBA F16; a stroke-styled paint layer, dirty rect 327x664 | 18.04 → 0.93 (30.9 → 0) | 0.29 → 0.33 | 20.59 |

In the first document the other styled layers inside the dirty rect are
composited on every step too, which is why the styled step (7 ms) is well
above the step without the moved node's style.

## Reading

- A Stroke style is expensive on the CPU: 4-7 ms per dab and 95-195 ms per
  full refresh, 10 to 30 times an unstyled layer. The work is the style's
  own (selection copies, an 8-bit convolution, clones of the projection),
  and a 1 px stroke is the slowest (it takes the spatial convolution
  path).
- On the GPU engine the same style cost 7-9 ms per dab before: the CPU
  work plus about 50 tile downloads.
- After, a styled layer's dab update costs 0.05-0.15 ms more than an
  unstyled one and reads nothing back; a full refresh costs 2-10 ms more.
  The 10 px stroke is the slowest now (441 taps per pixel near the layer's
  edge).
- The engine-off and RGBA8 paths are unchanged and still pay the CPU cost.
