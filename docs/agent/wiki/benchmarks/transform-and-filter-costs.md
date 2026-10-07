---
type: benchmark
updated: 2026-10-07
sources:
  - libs/image/tests/KisGpuPaintDeviceTest.cpp (benchmarkFiltersAndTransforms, opt-in KRITA_GPU_BENCHMARK_FILTERS=1)
  - ../history/gpu-phases-4.93-.md (phases 4.93, 4.94, 4.97)
related:
  - ../concepts/cpu-gpu-bit-parity.md
---

# Transform and filter costs

**Setup:**

- Machine: AMD Ryzen 9 9950X (32 hardware threads), NVIDIA RTX PRO 6000
  Blackwell.
- Builds: local development builds.
- Layer: 2480x3508 RGBA32F (the user's document size), random content.
- Source layers are CPU-resident unless noted.
- Each value is the median of 3 runs in one process.
- Run with `KRITA_GPU_BENCHMARK_FILTERS=1` and the
  `benchmarkFiltersAndTransforms` test function.

## Transforms (one call, the user's wait on apply)

| Operation | CPU | GPU | Date / phase |
| --- | ---: | ---: | --- |
| Affine scale 0.9 + rotate 10°, bicubic | 432-440 ms | 63-65 ms | 2026-10-06, 4.94 |
| Liquify, 20 moves, sigma 200 (about 46,000 operations) | 219 ms | 56 ms | 2026-10-07, 4.97 |
| Puppet Warp (rigid MLS, 9 points) | 602 ms | - | 2026-10-06, 4.93 |

GPU breakdowns are in the history page:

- affine: about 3 ms of compute per pass; the rest is upload, tile
  allocation and the batched download;
- Liquify: about 31 ms of CPU recording and convexity checks, about 20 ms on
  the GPU path.

## Filters (CPU, one call and 32 bands)

| Filter | Single call | 32 bands |
| --- | ---: | ---: |
| Gaussian blur r5 | 1021.7 ms | 143.5 ms |
| Gaussian blur r30 | 1065.9 ms | 199.3 ms |
| HSV adjust s+20 | 175.8 ms | 47.6 ms |
| Unsharp mask | 6155.6 ms | 389.8 ms |
| Readback of a GPU-written layer | 14.9 ms | - |

The Levels and Curves rows are suspect (see phase 4.93) and are not
repeated here.

## Staleness

Re-measure after changes to `KisGpuTileAccess` transfers, the transform
workers or the filter strokes. Add a row with a new date rather than
overwriting old ones.
