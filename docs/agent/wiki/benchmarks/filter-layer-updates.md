---
type: benchmark
updated: 2026-11-12
sources:
  - libs/image/tests/KisFilterLayerBenchmark.cpp (opt-in KRITA_FILTER_BENCHMARK=1)
  - plugins/impex/libkra/tests/KisMoveDocumentBenchmark.cpp (a real .kra, KRITA_MOVE_BENCHMARK_SHOW)
  - libs/image/kis_async_merger.cpp (KisUpdateOriginalVisitor::filterOnGpu)
  - libs/image/gpu/KisGpuColorTransform.cpp (channelTables)
  - libs/image/gpu/KisGpuProjectionCompositor.cpp (transformColors)
  - libs/image/gpu/KisGpuMergeBatch.cpp (tryAdd, the Copy mode)
  - libs/image/kis_filter_mask.cpp (applyOnGpu)
  - libs/image/gpu/KisGpuColorReferenceTables.cpp (conversion tables)
  - docs/agent/gpu-engine.md ("Filter layers (phase 4.115)", "Filter masks and Color Reference (phase 4.116)")
related:
  - layer-style-updates.md
  - masked-layer-updates.md
  - transform-and-filter-costs.md
---

# Filter layer updates

What a filter layer costs while a layer below it is painted, before and
after phase 4.115 (`docs/agent/gpu-engine.md`, "Filter layers (phase
4.115)").

**Setup:**

- Machine: AMD Ryzen 9 9950X, NVIDIA RTX PRO 6000 Blackwell (driver
  596.86). Local development build, 2026-11-12; one run per column.
- Image 1754x2480 (the size of the user's documents with filter layers).
  Layers, bottom to top: white background, the painted layer (random
  content in 1200x1200 at (300, 500)), the filter layers, a Multiply layer
  (opacity 180) on top.
- Filter layers, in Copy mode at full opacity as Solstice creates them:
  - Desaturate, type Lightness;
  - Posterize, 6 steps;
  - Color Adjustment with a curve on the RGBA channel;
  - "stack": Desaturate and Posterize, each with the same painted
    selection (a selected block with a soft edge), as in the user's
    documents.
- A stroke is 24 dabs of 256x256 px moving by (20, 16). Each dab is written
  into the painted layer, then the time from `setDirty()` to an idle image
  (and an idle GPU) is measured. Values are the median per dab in ms.
  "Downloads" is the number of GPU tiles read back to the CPU per dab.
- The GPU rows write the dabs on the GPU, like the GPU brush.
- "Refresh" is a refresh of the whole image (median of 5, ms).
- After every scenario the incremental result was compared with a full
  refresh: at most 6e-8 (F32 rounding).
- "Before" is the build without the phase (the same rows, measured before
  the change). Synthetic and synchronous, like the other benchmark pages.
  Not measured in the application.

## GPU engine, RGBA F16: before → after phase 4.115

| Filter layers | dab update (downloads per dab) | full refresh (downloads) |
| --- | --- | --- |
| none | 0.19 → 0.21 (0 → 0) | 2.97 → 2.80 (0 → 0) |
| Desaturate | 1.50 → 0.28 (24.8 → 0) | 17.48 → 8.39 (1092 → 0) |
| Posterize | 11.54 → 0.30 (24.8 → 0) | 78.60 → 5.14 (1092 → 0) |
| curves | 4.37 → 0.27 (24.8 → 0) | 28.46 → 6.09 (1092 → 0) |
| stack | 12.44 → 0.36 (24.8 → 0) | 73.08 → 8.19 (503 → 0) |

## GPU engine, RGBA F32: before → after phase 4.115

| Filter layers | dab update (downloads per dab) | full refresh (downloads) |
| --- | --- | --- |
| none | 0.19 → 0.19 (0 → 0) | 3.64 → 4.31 (0 → 0) |
| Desaturate | 1.45 → 0.36 (24.8 → 0) | 27.79 → 7.64 (1092 → 0) |
| Posterize | 10.91 → 11.01 (24.8 → 24.8) | 79.96 → 78.73 (1092 → 1092) |
| curves | 2.30 → 2.06 (24.8 → 24.8) | 31.88 → 28.15 (1092 → 1092) |
| stack | 11.57 → 12.38 (24.8 → 24.8) | 70.01 → 66.53 (503 → 503) |

Posterize and curves have no GPU form in RGBA F32 (no channel tables), so
those rows only show run-to-run noise; the Posterize layer of the stack
keeps the stack on the CPU.

## CPU paths (unchanged code; the run before / the run after)

| Filter layers | RGBA8 dab | RGBA8 refresh | F16 dab, engine off | F16 refresh, engine off | F32 dab, engine off | F32 refresh, engine off |
| --- | --- | --- | --- | --- | --- | --- |
| none | 0.40 / 0.39 | 3.07 / 2.62 | 1.15 / 1.14 | 6.02 / 7.64 | 0.51 / 0.56 | 7.16 / 7.10 |
| Desaturate | 0.75 / 0.73 | 5.96 / 5.78 | 1.84 / 1.83 | 12.67 / 13.73 | 1.01 / 1.00 | 17.92 / 19.79 |
| Posterize | 4.00 / 4.01 | 29.86 / 27.25 | 11.56 / 11.61 | 78.16 / 79.12 | 10.70 / 12.16 | 75.27 / 76.66 |
| curves | 1.06 / 1.01 | 7.51 / 6.56 | 4.72 / 4.66 | 28.68 / 28.53 | 2.05 / 1.74 | 20.83 / 19.95 |
| stack | 4.44 / 4.43 | 20.14 / 19.19 | 12.56 / 13.92 | 76.43 / 71.28 | 12.49 / 11.84 | 75.99 / 71.81 |

## Real documents: Move Tool step below the filter layers

`KisMoveDocumentBenchmark` on two of the user's documents (scratch copies),
same build, with `KRITA_GPU_FILTER_LAYER=0` (the filters on the CPU, as
before the phase; the Copy mode is composited on the GPU in both columns)
and without it. Both documents keep a Desaturate and a Posterize filter
layer with a painted selection in a hidden group at the top; the benchmark
made the group visible (`KRITA_MOVE_BENCHMARK_SHOW`) and moved a paint
layer below it. Median ms per Move Tool step; downloads per step in
parentheses.

| Document and moved node | GPU engine, filters on the CPU → on the GPU | filter group hidden | GPU engine off |
| --- | --- | --- | --- |
| 938x1172 RGBA F16, 26 layers; a paint layer, dirty rect 865x1041 | 66.33 → 3.21 (524.2 → 0.0) | 2.64 | 79.62 |
| 1754x2480 RGBA F16, 31 layers; the background layer, dirty rect 1754x2480 | 86.31 → 10.64 (2184.0 → 0.0) | 2.97 | 79.27 |
| the same document; a paint layer inside a group with a Color Reference filter mask, dirty rect 1691x2326 | 407.11 → 339.36 (3150.0 → 1050.0) | 334.36 | 410.77 |

In the last row the group's Color Reference filter mask
(`solstice_color_reference`) runs on the CPU at every step and reads the
group's projection back (about 330 ms and 1050 tiles of the step, with the
GPU engine on or off). The filter layers add 5 ms to it on the GPU and
73 ms on the CPU.

## Filter masks and Color Reference: before → after phase 4.116

The same benchmark and image, measured before and after phase 4.116
(2026-11-12, one run per column). "Color Reference" is a filter layer with
the filter's defaults (the Amber preset, Amount 100, Keep brightness);
"Desaturate mask" a filter mask on the painted layer; "Color Reference
mask" a filter mask on a group that holds the painted layer, as in the
user's document.

GPU engine:

| Scenario | F16 dab (downloads) | F16 refresh (downloads) | F32 dab (downloads) | F32 refresh |
| --- | --- | --- | --- | --- |
| Color Reference | 24.49 → 0.44 (24.8 → 0) | 340.71 → 7.17 (1092 → 0) | 24.06 → 24.22 (24.8 → 24.8) | 336.97 → 346.37 |
| Desaturate mask | 2.33 → 0.26 (24.8 → 0) | 11.63 → 5.33 (0 → 0) | 2.12 → 0.26 (24.8 → 0) | 19.57 → 5.18 |
| Color Reference mask | 25.73 → 0.49 (24.8 → 0) | 297.35 → 5.56 (0 → 0) | 24.61 → 25.15 (24.8 → 24.8) | 290.99 → 284.53 |

Color Reference has no GPU form in RGBA F32: those columns show noise.

CPU paths (unchanged code; the run before / the run after):

| Scenario | RGBA8 dab | RGBA8 refresh | F16 dab, engine off | F16 refresh, engine off | F32 dab, engine off | F32 refresh, engine off |
| --- | --- | --- | --- | --- | --- | --- |
| Color Reference | 33.94 / 33.94 | 587.35 / 580.22 | 24.92 / 24.87 | 366.13 / 346.33 | 24.24 / 23.87 | 341.06 / 336.29 |
| Desaturate mask | 0.70 / 0.70 | 5.60 / 5.91 | 1.83 / 1.89 | 14.53 / 13.96 | 0.88 / 0.88 | 16.87 / 16.48 |
| Color Reference mask | 34.27 / 33.91 | 509.92 / 507.63 | 25.37 / 24.82 | 298.36 / 315.77 | 23.69 / 23.34 | 308.15 / 299.04 |

Real document (the third row of the table above: 1754x2480 RGBA F16, a
paint layer inside the group with the Color Reference filter mask, the
filter group hidden, dirty rect 1691x2326), `KisMoveDocumentBenchmark`
with `KRITA_GPU_FILTER_LAYER=0` and without: **333.21 → 10.40 ms** per
Move Tool step, 1050 → 0 tiles read back; GPU engine off 330-338 ms.

One-time costs (`KisGpuProjectionTest::testColorReferenceTables`): the
first Color Reference update of a session builds the conversion tables of
the document's color space, 220 ms; a changed setting builds a new block
in 2 ms.

The Color Reference filter is slow on the CPU in every color space
(24-34 ms per dab, 300-590 ms per refresh): it converts every pixel to
and from its working space with single-pixel LittleCMS calls.

## Reading

- On the CPU, Posterize is the expensive filter: two LittleCMS conversions
  per pixel, one pixel at a time (10-12 ms per dab, 65-80 ms per refresh in
  float). Desaturate and curves cost 1-5 ms per dab.
- On the GPU engine every filter layer also read the projection back
  (about 25 tiles per dab) and was composited on the CPU, because the Copy
  blend mode had no GPU form.
- After, a filter layer with a GPU form adds about 0.1 ms to a dab update
  and reads nothing back, whatever the filter: the channel tables make
  Posterize as cheap as a curve.
- Building the tables costs one transformation of 65536 pixels per
  configuration and color space (the first update after a setting changes;
  not measured separately). Posterize adds a one-time check of its color
  conversions per color space.
- The engine-off and RGBA8 paths are unchanged.
- Found on the way: in one of the user's documents a Color Reference
  filter mask on a group costs about 330 ms per full-image update, far more
  than the filter layers. Filter masks are not part of this phase.
