---
type: benchmark
updated: 2026-10-07
sources:
  - libs/image/tests/KisGpuPaintDeviceTest.cpp (benchmarkFiltersAndTransforms, opt-in KRITA_GPU_BENCHMARK_FILTERS=1)
  - ../history/gpu-phases-4.93-.md (phases 4.93, 4.94, 4.97, 4.98)
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

## README refresh (2026-10-07)

All README benchmark rows were measured again: three fresh processes per
workload, run one after another, with Vulkan validation off and Solstice
closed.

- Build: `3f1ea61d3f`, RelWithDebInfo.
- Hardware: Ryzen 9 9950X, RTX PRO 6000 Blackwell, driver 596.86.
- Settings: `KRITA_GPU_BENCH_SIZE=4096`, `KRITA_GPU_BENCH_LAYERS=16`,
  `KRITA_GPU_BENCH_REPEATS=5`, `KRITA_GPU_STROKE_REPEATS=5`, and
  `KRITA_GPU_BENCHMARK_FILTERS=1` for the transform and filter run.
- All 15 processes passed.

Commands (absolute paths; `env.bat` changes the working directory):

```bat
KisGpuProjectionTest.exe benchmarkRefresh
KisGpuCanvasUploadTest.exe benchmarkCanvasUpdate
KisGpuBrushTest.exe benchmarkCombinedMirrors
KisGpuStrokeTest.exe testStroke:64-wash testStroke:256-buildup testHalfBlendModes:soft_light_svg-wash1-channels15
KisGpuPaintDeviceTest.exe benchmarkFiltersAndTransforms
```

Medians of the three process results, with the range of process results:

| Workload | CPU (ms) | GPU (ms) |
| --- | ---: | ---: |
| 4096-square, 16-layer resident projection | 254.3 (250.6-259.5) | 58.2 (57.5-58.9) |
| Full CPU readback after it | - | 48.9 (46.0-49.2) |
| 4096-square canvas preparation | 1088.5 (1083.9-1121.4) | 1.695 (1.561-2.021) |
| 256-square canvas preparation | 5.569 (5.213-6.234) | 0.291 (0.151-0.312) |
| Nearby mirrors, 14 dabs at 73px | 0.994 (0.950-1.030) | 0.435 (0.358-0.490) |
| Nearby mirrors, 32 dabs at 256px | 47.67 (46.86-54.08) | 2.061 (1.889-2.099) |
| Distant mirrors, 14 dabs at 73px | 0.875 (0.871-0.880) | 0.311 (0.290-0.419) |
| Distant mirrors, 32 dabs at 256px | 46.52 (45.47-46.96) | 2.075 (1.994-2.129) |
| Affine scale 0.9 + rotate 10°, bicubic | 470.1 (456.1-499.2) | 64.1 (61.6-65.6) |
| Liquify, 20 moves, sigma 200 | 225.2 (220.9-288.7) | 57.8 (57.3-75.6) |

Completed strokes (1024-square, four layers, 24 segments; medians of process
medians):

| Stroke | CPU | GPU projection | GPU projection + brush |
| --- | ---: | ---: | ---: |
| F32 64px Normal Wash | 6.035 (5.822-6.464) | 9.794 (9.750-10.183) | 10.541 (10.296-10.674) |
| F32 256px Normal Buildup | 8.069 (7.807-8.340) | 12.873 (12.777-12.927) | 11.755 (9.970-12.251) |
| F16 128px Soft Light SVG Wash, selection + mirrors | 69.997 (69.120-72.327) | 57.788 (48.370-58.314) | 12.554 (11.870-13.634) |

Filters, one call over 2480x3508 RGBA32F:

| Filter | Time (ms) |
| --- | ---: |
| Gaussian blur r5 | 1040 |
| Gaussian blur r30 | 1088 |
| HSV adjust | 182 |
| Unsharp mask | 6577 |
| Levels | 181 |

Whole-layer readback took 13.4 ms. Curves is still suspect at 0.1 ms.

Notes:

- The benchmark's "puppet warp (rigid MLS)" row measures the old
  `KisWarpTransformWorker`. The Transform Tool's Puppet Warp uses the mesh
  model since phase 4.95, so that row (647 ms) is not used for the README.
- Compared with October 4, the projection, canvas and mirror medians are
  within a few percent; no change is claimed from that difference.

## GPU Gaussian blur (phase 4.98, 2026-10-07)

Build: phase 4.98 working tree on `ac896b0427`, RelWithDebInfo; same machine
and driver. Three processes of `benchmarkFiltersAndTransforms` with
`KRITA_GPU_VALIDATION=0`; medians, with the range of process results.

| Filter (2480x3508 RGBA32F) | CPU single call | CPU 32 bands | GPU single call | GPU 32 bands |
| --- | ---: | ---: | ---: | ---: |
| Gaussian blur r5 | 1015 (1012-1023) | 150 (149-152) | 50.0 (46.1-54.1) | 114 (113-115) |
| Gaussian blur r30 | 1085 (1079-1099) | 213 (210-214) | 62.0 (59.2-64.5) | 124 (124-126) |
| Gaussian blur r100 | 1759 (1745-1769) | 398 (394-400) | 110 (108-110) | 200 (200-215) |
| Unsharp mask | 6345 (6321-6399) | 408 (406-421) | 5011 (4965-5011) | 378 (372-379) |

- The Filter dialog's Gaussian Blur ran as patches (about "CPU 32 bands")
  and now runs as one GPU call (`KisFilter::prefersSingleCall()`).
- Concurrent GPU calls serialize, so "GPU 32 bands" is slower than one call.
- One `applyGaussian()` call on small squares, with the size threshold
  (32,768 source pixels) in place, is in the history page. Without the
  threshold the GPU took 1.7-1.9ms for 64x64 and 128x128 (CPU 0.4-3.0ms).

## GPU Puppet Warp (phase 4.96, 2026-10-07)

Puppet Warp's mesh model (the Transform Tool's current model; the table at
the top measures the legacy MLS worker), a full-layer mesh with three pins,
the middle one turned by 0.5 rad, three stacking groups. Same build setup as
above; medians of three processes.

| Operation | CPU | GPU |
| --- | ---: | ---: |
| Puppet Warp apply (mesh, 3 groups) | 713 ms (690-754) | 190 ms (189-200) |

## Staleness

Re-measure after changes to `KisGpuTileAccess` transfers, the transform
workers, the convolution worker or the filter strokes. Add a row with a new date rather than
overwriting old ones.
