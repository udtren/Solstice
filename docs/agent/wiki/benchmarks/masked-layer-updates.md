---
type: benchmark
updated: 2026-11-12
sources:
  - libs/image/tests/KisMaskedLayerBenchmark.cpp (opt-in KRITA_MASK_BENCHMARK=1)
  - libs/image/kis_layer.cc (applyMasks, gpuAppliesEffectMasks, copyOriginalToProjection)
  - libs/image/kis_mask.cc (mergeInMaskInternal)
  - libs/image/gpu/KisGpuProjectionCompositor.cpp (applyCoverage, copyShifted)
  - docs/agent/gpu-engine.md ("Transparency masks (phase 4.113)")
related:
  - clone-layer-updates.md
  - move-layer-updates.md
  - ../concepts/krita-copy-semantics.md
---

# Masked layer updates

What a transparency mask costs while its layer is painted, before and
after phase 4.113 (`docs/agent/gpu-engine.md`, "Transparency masks (phase
4.113)").

**Setup:**

- Machine: AMD Ryzen 9 9950X, NVIDIA RTX PRO 6000 Blackwell (driver
  596.86). Local development build, 2026-11-12; one run per column.
- Image 2480x3508. Layers, bottom to top: white background, the painted
  layer (random content in 1200x1200 at (400, 600)) with 0, 1 or 2
  transparency masks, a Multiply layer (opacity 180) on top.
- A mask holds a gradient of every coverage value over the content, a hole
  (coverage 0) and an opaque band (coverage 255).
- A stroke is 24 dabs of 256x256 px moving by (20, 16). Each dab is written
  into the painted layer, then the time from `setDirty()` to an idle image
  (and an idle GPU) is measured. Values are the median per dab in ms.
  "Downloads" is the number of GPU tiles read back to the CPU per dab.
- F32-GPU writes the dabs on the GPU, like the GPU brush, so the layer's
  tiles are GPU-resident.
- "Refresh" is a refresh of the whole image (median of 5, ms).
- After every scenario the incremental result was compared with a full
  refresh: at most 1.2e-7 (F32 rounding).
- Synthetic and synchronous, like the other benchmark pages. Not measured
  in the application.

## GPU engine, RGBA F32: before → after phase 4.113

| Transparency masks | dab update (downloads per dab) | full refresh |
| --- | --- | --- |
| 0 | 0.20 → 0.20 (0 → 0) | 9.71 → 9.68 |
| 1 | 2.19 → 0.31 (24.6 → 0) | 16.07 → 12.90 |
| 2 | 2.40 → 0.29 (24.6 → 0) | 16.33 → 11.78 |

## CPU paths (unchanged code; before / after runs)

| Transparency masks | RGBA8 dab | RGBA8 refresh | F32 dab, engine off | F32 refresh, engine off |
| --- | --- | --- | --- | --- |
| 0 | 0.40 / 0.40 | 3.75 / 4.50 | 0.62 / 0.51 | 11.85 / 11.02 |
| 1 | 0.51 / 0.50 | 5.56 / 5.13 | 0.72 / 0.66 | 17.16 / 14.18 |
| 2 | 0.60 / 0.55 | 5.96 / 6.07 | 0.85 / 0.83 | 17.72 / 17.85 |

The two runs of the same code differ by up to 3 ms in the refresh rows:
single runs, read them as noise.

## A real document: Move Tool step

`KisMoveDocumentBenchmark` on the user's document of
[Move Tool updates](move-layer-updates.md#a-real-document) (a group of 20
nodes, one full-canvas layer with a transparency mask, an Outer Glow layer
style; RGBA F16, 1754x2480), GPU engine on, median ms per step:

| Variant | after 4.112 | after 4.113 |
| --- | --- | --- |
| as it is | 31.18 | 26.75 |
| no layer style | 24.48 | 15.46 |
| no masks | 23.42 | 23.74 |
| no layer style, no masks | 12.89 | 14.39 |

The mask's share of a step went from about 9 ms to about 1 ms. The layer
style (CPU, with its batched readback) is now the largest part.

## Reading

- Before, a transparency mask made a GPU dab update 11 times slower (0.20
  to 2.19 ms): the CPU copy of the original read the dab's tiles back, the
  mask ran on the CPU, and the composite uploaded the projection again.
  With the engine off the same mask cost 0.1 ms.
- After, the masked layer's update reads nothing back and costs 0.1 ms
  more than an unmasked layer, with one or two masks.
