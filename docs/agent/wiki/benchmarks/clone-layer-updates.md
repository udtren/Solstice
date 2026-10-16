---
type: benchmark
updated: 2026-10-16
sources:
  - libs/image/tests/KisCloneLayerBenchmark.cpp (opt-in KRITA_CLONE_BENCHMARK=1)
  - libs/image/gpu/KisGpuTileBackend.cpp (downloadedTileCount)
  - libs/image/gpu/KisGpuMergeBatch.cpp (deferClear, phase 4.111)
  - docs/agent/clone-layer.md
related:
  - ../concepts/krita-copy-semantics.md
---

# Clone Layer updates

**Setup:**

- Machine: AMD Ryzen 9 9950X, NVIDIA RTX PRO 6000 Blackwell (driver
  596.86). Local development build, 2026-10-16.
- Image 2480x3508. Layers: white background, the source (random content in
  1200x1200 at (400, 600)), the clone (Normal, opacity 255) and a Multiply
  layer on top. "group": the clone's source is a group holding the painted
  layer and one more layer. "depth 3": the source and the clone each sit in
  three nested groups.
- Masks on the clone: a transparency mask with a 400x400 hole, or a blur
  filter mask with the default configuration.
- A stroke is 24 dabs of 256x256 px moving by (20, 16). Each dab is written
  into the source, then the time from `setDirty()` to an idle image (and an
  idle GPU) is measured. Values are the median per dab in ms. "Downloads" is
  the number of GPU tiles read back to the CPU per dab.
- F32-GPU writes the dabs on the GPU, like the GPU brush, so the source's
  tiles are GPU-resident.
- The aligned table uses dabs grown to whole 64 px tiles (320x320 px).
- After every scenario the incremental result was compared with a full
  refresh: the largest difference was 1.2e-7 (F32 rounding), 0 for RGBA8.
- Synthetic and synchronous: the application merges strokes asynchronously
  and the canvas reads GPU tiles directly. Not measured in the application.

## CPU paths (before and after phase 4.110; unchanged)

RGBA8:

| Clone | layer | layer, depth 3 | group | group, depth 3 |
| --- | ---: | ---: | ---: | ---: |
| no clone | 0.39 | 0.41 | 0.50 | 0.48 |
| offset 0,0, no mask | 0.79 | 0.82 | 0.89 | 0.90 |
| offset 0,0, transparency mask | 0.89 | 0.94 | 0.96 | 1.00 |
| offset 0,0, blur mask | 5.93 | 6.01 | 7.14 | 6.36 |
| offset 37,23, no mask | 0.92 | 1.09 | 1.05 | 1.14 |
| offset 37,23, transparency mask | 1.02 | 1.05 | 1.12 | 1.19 |
| offset 37,23, blur mask | 8.18 | 8.63 | 8.35 | 8.78 |
| offset 64,128, no mask | 1.14 | 1.33 | 1.24 | 1.26 |
| offset 64,128, transparency mask | 1.25 | 1.33 | 1.37 | 1.99 |
| offset 64,128, blur mask | 12.41 | 11.86 | 12.24 | 12.77 |

RGBA F32 with the GPU engine off:

| Clone | layer | layer, depth 3 | group | group, depth 3 |
| --- | ---: | ---: | ---: | ---: |
| no clone | 0.56 | 0.54 | 0.98 | 0.85 |
| offset 0,0, no mask | 1.31 | 1.22 | 1.53 | 1.63 |
| offset 0,0, transparency mask | 1.65 | 1.28 | 1.66 | 1.83 |
| offset 0,0, blur mask | 6.15 | 6.00 | 6.50 | 6.67 |
| offset 37,23, no mask | 1.45 | 1.60 | 1.78 | 1.83 |
| offset 37,23, transparency mask | 1.51 | 1.57 | 1.89 | 1.87 |
| offset 37,23, blur mask | 8.69 | 10.19 | 9.75 | 9.61 |
| offset 64,128, no mask | 1.97 | 1.99 | 2.45 | 2.44 |
| offset 64,128, transparency mask | 2.07 | 2.05 | 2.61 | 2.41 |
| offset 64,128, blur mask | 13.65 | 13.85 | 14.31 | 14.26 |

## GPU engine, RGBA F32: before → after phase 4.110

Each cell: median ms before → after (downloads per dab before → after).

256 px dabs (not tile-aligned):

| Clone | layer | layer, depth 3 | group | group, depth 3 |
| --- | --- | --- | --- | --- |
| no clone | 0.94 → 1.01 (15.3 → 15.3) | 0.87 → 0.87 (15.3 → 15.3) | 1.59 → 1.78 (30.7 → 30.7) | 2.01 → 1.65 (30.7 → 30.7) |
| offset 0,0, no mask | 1.64 → 1.95 (30.7 → 30.7) | 1.67 → 1.67 (30.7 → 30.7) | 2.39 → 2.58 (46.0 → 46.0) | 2.64 → 2.26 (46.0 → 46.0) |
| offset 0,0, transparency mask | 3.91 → 3.88 (55.3 → 55.3) | 3.55 → 3.77 (55.3 → 55.3) | 4.27 → 4.11 (55.3 → 55.3) | 4.33 → 4.17 (55.3 → 55.3) |
| offset 0,0, blur mask | 8.31 → 7.50 (56.8 → 56.8) | 8.07 → 8.21 (56.8 → 56.8) | 8.93 → 8.15 (56.8 → 56.8) | 8.72 → 8.14 (56.8 → 56.8) |
| offset 37,23, no mask | 3.73 → 1.82 (57.7 → 33.1) | 4.15 → 1.64 (57.7 → 33.1) | 4.14 → 2.49 (57.7 → 48.4) | 4.57 → 2.48 (57.7 → 48.4) |
| offset 37,23, transparency mask | 4.35 → 4.28 (57.7 → 57.7) | 3.93 → 4.06 (57.7 → 57.7) | 4.52 → 4.27 (57.7 → 57.7) | 5.20 → 4.38 (57.7 → 57.7) |
| offset 37,23, blur mask | 11.51 → 10.63 (57.8 → 57.8) | 11.13 → 11.33 (57.8 → 57.8) | 11.95 → 11.65 (57.8 → 57.8) | 13.38 → 11.72 (57.8 → 57.8) |
| offset 64,128, no mask | 4.06 → 2.25 (60.9 → 36.3) | 3.58 → 2.77 (60.9 → 36.3) | 4.72 → 2.46 (60.9 → 51.7) | 5.06 → 2.49 (60.9 → 51.7) |
| offset 64,128, transparency mask | 3.95 → 4.42 (60.9 → 60.9) | 5.04 → 4.49 (60.9 → 60.9) | 5.27 → 4.67 (60.9 → 60.9) | 5.15 → 4.80 (60.9 → 60.9) |
| offset 64,128, blur mask | 15.75 → 15.07 (62.1 → 62.1) | 15.97 → 14.91 (62.1 → 62.1) | 15.80 → 15.16 (62.1 → 62.1) | 16.87 → 15.86 (62.1 → 62.1) |

Tile-aligned dabs (320 px):

| Clone | layer | layer, depth 3 | group | group, depth 3 |
| --- | --- | --- | --- | --- |
| no clone | 0.41 → 0.49 (0.0 → 0.0) | 0.44 → 0.44 (0.0 → 0.0) | 0.57 → 0.53 (0.0 → 0.0) | 0.60 → 0.56 (0.0 → 0.0) |
| offset 0,0, no mask | 0.54 → 0.57 (0.0 → 0.0) | 0.64 → 0.58 (0.0 → 0.0) | 0.67 → 0.67 (0.0 → 0.0) | 0.79 → 0.65 (0.0 → 0.0) |
| offset 0,0, transparency mask | 2.68 → 2.40 (24.6 → 24.6) | 2.68 → 2.43 (24.6 → 24.6) | 3.08 → 2.82 (24.6 → 24.6) | 2.89 → 2.76 (24.6 → 24.6) |
| offset 0,0, blur mask | 8.44 → 8.44 (48.4 → 48.4) | 9.17 → 8.71 (48.4 → 48.4) | 9.36 → 8.40 (48.4 → 48.4) | 9.73 → 8.59 (48.4 → 48.4) |
| offset 37,23, no mask | 3.02 → 1.01 (35.5 → 10.9) | 2.93 → 1.10 (35.5 → 10.9) | 3.25 → 1.12 (35.5 → 10.9) | 3.12 → 1.14 (35.5 → 10.9) |
| offset 37,23, transparency mask | 3.09 → 3.27 (35.5 → 35.5) | 3.81 → 3.27 (35.5 → 35.5) | 3.59 → 3.23 (35.5 → 35.5) | 3.89 → 3.59 (35.5 → 35.5) |
| offset 37,23, blur mask | 21.82 → 20.64 (35.5 → 35.5) | 21.96 → 20.98 (35.5 → 35.5) | 22.49 → 20.86 (35.5 → 35.5) | 22.49 → 20.90 (35.5 → 35.5) |
| offset 64,128, no mask | 2.74 → 0.67 (24.6 → 0.0) | 2.83 → 0.68 (24.6 → 0.0) | 4.15 → 0.80 (24.6 → 0.0) | 3.47 → 0.76 (24.6 → 0.0) |
| offset 64,128, transparency mask | 3.03 → 2.81 (24.6 → 24.6) | 3.43 → 2.98 (24.6 → 24.6) | 3.40 → 3.10 (24.6 → 24.6) | 3.76 → 3.23 (24.6 → 24.6) |
| offset 64,128, blur mask | 37.66 → 36.32 (38.5 → 38.5) | 37.67 → 36.89 (38.5 → 38.5) | 39.02 → 36.84 (38.5 → 38.5) | 38.23 → 36.31 (38.5 → 38.5) |

## GPU engine, RGBA F32: after phase 4.110 → after phase 4.111

Phase 4.111 clears unaligned dirty rects of GPU-resident projections on the
GPU instead of the CPU (`docs/agent/gpu-engine.md`, "Partial-tile projection
clears (phase 4.111)"). Same benchmark, one run on 2026-10-16 (GPU rows
only, `KRITA_CLONE_BENCHMARK_GPU_ONLY=1`). Each cell: median ms after phase
4.110 → after phase 4.111 (downloads per dab before → after).

256 px dabs (not tile-aligned):

| Clone | layer | layer, depth 3 | group | group, depth 3 |
| --- | --- | --- | --- | --- |
| no clone | 1.01 → 0.20 (15.3 → 0.0) | 0.87 → 0.20 (15.3 → 0.0) | 1.78 → 0.23 (30.7 → 0.0) | 1.65 → 0.24 (30.7 → 0.0) |
| offset 0,0, no mask | 1.95 → 0.25 (30.7 → 0.0) | 1.67 → 0.29 (30.7 → 0.0) | 2.58 → 0.26 (46.0 → 0.0) | 2.26 → 0.31 (46.0 → 0.0) |
| offset 0,0, transparency mask | 3.88 → 2.61 (55.3 → 24.6) | 3.77 → 2.37 (55.3 → 24.6) | 4.11 → 2.54 (55.3 → 24.6) | 4.17 → 3.28 (55.3 → 24.6) |
| offset 0,0, blur mask | 7.50 → 6.35 (56.8 → 24.6) | 8.21 → 6.79 (56.8 → 24.6) | 8.15 → 6.61 (56.8 → 24.6) | 8.14 → 6.59 (56.8 → 24.6) |
| offset 37,23, no mask | 1.82 → 0.33 (33.1 → 0.0) | 1.64 → 0.32 (33.1 → 0.0) | 2.49 → 0.35 (48.4 → 0.0) | 2.48 → 0.35 (48.4 → 0.0) |
| offset 37,23, transparency mask | 4.28 → 2.40 (57.7 → 24.6) | 4.06 → 2.90 (57.7 → 24.6) | 4.27 → 3.01 (57.7 → 24.6) | 4.38 → 2.64 (57.7 → 24.6) |
| offset 37,23, blur mask | 10.63 → 9.39 (57.8 → 24.6) | 11.33 → 10.76 (57.8 → 24.6) | 11.65 → 9.70 (57.8 → 24.6) | 11.72 → 9.98 (57.8 → 24.6) |
| offset 64,128, no mask | 2.25 → 0.30 (36.3 → 0.0) | 2.77 → 0.34 (36.3 → 0.0) | 2.46 → 0.36 (51.7 → 0.0) | 2.49 → 0.40 (51.7 → 0.0) |
| offset 64,128, transparency mask | 4.42 → 3.07 (60.9 → 24.6) | 4.49 → 2.99 (60.9 → 24.6) | 4.67 → 3.19 (60.9 → 24.6) | 4.80 → 2.86 (60.9 → 24.6) |
| offset 64,128, blur mask | 15.07 → 14.61 (62.1 → 24.6) | 14.91 → 14.21 (62.1 → 24.6) | 15.16 → 14.49 (62.1 → 24.6) | 15.86 → 14.68 (62.1 → 24.6) |

Tile-aligned dabs (320 px):

| Clone | layer | layer, depth 3 | group | group, depth 3 |
| --- | --- | --- | --- | --- |
| no clone | 0.49 → 0.42 (0.0 → 0.0) | 0.44 → 0.42 (0.0 → 0.0) | 0.53 → 0.57 (0.0 → 0.0) | 0.56 → 0.59 (0.0 → 0.0) |
| offset 0,0, no mask | 0.57 → 0.57 (0.0 → 0.0) | 0.58 → 0.61 (0.0 → 0.0) | 0.67 → 0.80 (0.0 → 0.0) | 0.65 → 0.77 (0.0 → 0.0) |
| offset 0,0, transparency mask | 2.40 → 2.43 (24.6 → 24.6) | 2.43 → 3.00 (24.6 → 24.6) | 2.82 → 2.62 (24.6 → 24.6) | 2.76 → 3.13 (24.6 → 24.6) |
| offset 0,0, blur mask | 8.44 → 7.85 (48.4 → 24.6) | 8.71 → 8.22 (48.4 → 24.6) | 8.40 → 8.21 (48.4 → 24.6) | 8.59 → 8.44 (48.4 → 24.6) |
| offset 37,23, no mask | 1.01 → 0.62 (10.9 → 0.0) | 1.10 → 0.62 (10.9 → 0.0) | 1.12 → 0.67 (10.9 → 0.0) | 1.14 → 0.67 (10.9 → 0.0) |
| offset 37,23, transparency mask | 3.27 → 2.79 (35.5 → 24.6) | 3.27 → 2.92 (35.5 → 24.6) | 3.23 → 3.32 (35.5 → 24.6) | 3.59 → 3.44 (35.5 → 24.6) |
| offset 37,23, blur mask | 20.64 → 20.91 (35.5 → 24.6) | 20.98 → 22.14 (35.5 → 24.6) | 20.86 → 21.87 (35.5 → 24.6) | 20.90 → 21.69 (35.5 → 24.6) |
| offset 64,128, no mask | 0.67 → 0.76 (0.0 → 0.0) | 0.68 → 0.67 (0.0 → 0.0) | 0.80 → 0.93 (0.0 → 0.0) | 0.76 → 0.84 (0.0 → 0.0) |
| offset 64,128, transparency mask | 2.81 → 3.33 (24.6 → 24.6) | 2.98 → 3.27 (24.6 → 24.6) | 3.10 → 3.98 (24.6 → 24.6) | 3.23 → 3.58 (24.6 → 24.6) |
| offset 64,128, blur mask | 36.32 → 37.01 (38.5 → 24.6) | 36.89 → 38.00 (38.5 → 24.6) | 36.84 → 39.04 (38.5 → 24.6) | 36.31 → 37.75 (38.5 → 24.6) |

## Reading

- A clone roughly doubles the update cost on the CPU: its second walker
  recomposites the root (`docs/agent/clone-layer.md`, "Update path").
- Before phase 4.110, every offset clone downloaded the source tiles under
  the dab on the GPU engine. The GPU copy removes those downloads. The
  remaining downloads with unaligned dabs came from partial-tile
  projection clears, which every GPU merge had (the "no clone" row).
- Phase 4.111 removes the clear downloads: unaligned dabs without masks
  download nothing (15.3-51.7 → 0 per dab) and take 0.20-0.40 ms instead of
  0.87-2.77 ms, now less than the larger tile-aligned dabs. Aligned dabs
  of an offset clone, whose shifted rect is unaligned, lose their 10.9
  downloads. Masked clones keep 24.6 downloads per dab, the source tiles
  their CPU masks read; their unaligned dabs are 0.5-1.9 ms faster. Aligned
  dabs with a blur mask also drop to 24.6 downloads, without a measurable
  time change (not verified: presumably the blur's larger rect around the
  dab is unaligned).
- The aligned rows otherwise differ by run-to-run noise: up to about 1 ms,
  about 2 ms in the slow blur rows (single runs, as between the 4.110
  columns).
- Masks: the copy stays on the CPU (same numbers before and after). An
  intermediate version that also copied on the GPU for masked clones made
  transparency masks slower (offset 64,128: 3.95 → 5.38 ms, downloads
  60.9 → 77.8 per dab), because the CPU mask then read the copied tiles.
- Nesting depth adds little; a blur filter mask dominates every path.
