---
type: history
topic: gpu-engine
status: archived record (moved from docs/agent/gpu-engine.md on 2026-10-07)
sources: [docs/agent/gpu-engine.md, git history]
---

# GPU engine history: generated dabs and brush latency (phases 4.82-4.92)

GPU circle dabs, update period, Wash latency, shared pipelines, immediate canvas uploads. Moved verbatim from `docs/agent/gpu-engine.md`; current state, decisions and
invariants stay there ([gpu-engine.md](../../gpu-engine.md)). Each section records what was true
when it was written; later sections and the current document take precedence.

## Byte-limited GPU brush batches continue at once (phase 4.82)

2026-10-06, analysis of PID 43720/24148 measured strokes. Dabs finish rendering
within about 0.6ms of their request (render job queue delay about 2ms in both
captures), then wait 24-35ms in 4.81 versus 9-15ms in 4.80 to enter a brush batch.
In 4.81 most batches stopped with the next dab already rendered (5 of 7, 5 of 7,
7 of 9, 9 of 10 batches), with 14-58 rendered dabs left behind. In 4.80, batches
more often stopped at an unrendered dab. The time-based `dabsLimit` cannot be
the cap: with 32 worker threads (`idealNumRects`) it would need about 200ms per dab.
The cap is the 32 MiB GPU source byte budget of `KisBrushOp::doAsynchronousUpdate`.
The measured preset (`b)_Basic-4_Flow_Opacity.0011.kpp`, MD5 22a33a4c...) rotates
dabs by drawing angle (`RotationSensor` `drawingangle`). A rotated 256px dab's
bounds reach about 362px, about 2.1 MB in RGBA F32, so about 15-16 dabs fit,
matching the observed 16-dab caps. Near-axis strokes fit about 30 (4.80 maxima
29-30). Drawing direction and speed both differ between the captures (inference
from sizes; dab bytes are not traced).

After a batch that left rendered dabs, upstream sets the update period to
`m_minUpdatePeriod` (10ms). One batch is in flight at a time, so 16 dabs per
10-15ms cannot keep up with 1,000-1,500 dabs/s.

Change (plugin only):

- `KisDabRenderingQueue::takeReadyDabs()` / `KisDabRenderingExecutor::takeReadyDabs()`
  take an optional `stoppedByByteLimit` out parameter, true only when a
  rendered dab was left because of `maxDabBytes`. Existing callers are unchanged.
- `KisBrushOp::doAsynchronousUpdate()`: when the GPU batch was cut by the byte
  budget and rendered dabs remain, its final job sets the update period to 0,
  so the next `FreehandStrokeStrategy::tryDoUpdate()` (next input job, at least
  1ms later) starts the following batch as soon as the current one finished.
  The first follow-up after a non-limited batch still waits the period returned
  then. The CPU path has no byte budget and is unchanged; other caps keep the
  upstream periods. New trace link `batch.byte_limited` (batch id).

Effects to expect: more, full GPU batches during fast large strokes, thus more
dirty dispatches, projection walkers and canvas updates (the latter are batched
since 4.81). One batch in flight and the byte budget are unchanged.

Tests: `KisDabRenderingQueueTest` 12/12 + 1 trace-only skip, 13/13 with
`KRITA_PAINT_TRACE` (byte-budget rows assert the flag equals "dabs left" and
is never set without a budget). `KisGpuStrokeTest testStroke` 16/16 rows
(64/256 Buildup/Wash, mirror, selection, distant, alpha lock) with validation.
`kritadefaultpaintops.dll` installed; build/install SHA256 match. Not measured
in the app yet. Manual check: fast diagonal and horizontal 256px strokes,
mirror, Undo/Redo; then one focused six-stroke capture and compare dab
request -> batch ready, batch count and `batch.byte_limited` count.

Real-app follow-up (2026-10-06): the user ran a separate manual-check process
(PID 42316, archived in `%TEMP%/solstice-stage-482-manual-42316/`; not used for
performance), then a focused capture PID 45728: six 256px strokes, zero dropped
events, 490 uploads covered to swapped commands. Buildup warm-up 3274, measured
4029/5121; Wash warm-up 6386, measured 7228/8144. Archive
`%TEMP%/solstice-stage-482-real-45728/`. Canvas: 506 updates, 193 canvas submissions.

| Stroke | Duration ms | Dabs/s | Dab request -> batch ready median ms | Byte-limited / batches | Input -> swap median ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4.80 Buildup 5252 / 6402 | 181 / 151 | 756 / 1,020 | 9.86 / 13.91 | - | 22.94 / 24.10 |
| 4.81 Buildup 3827 / 4613 | 114 / 105 | 860 / 982 | 25.03 / 24.93 | (not traced) | 38.50 / 32.62 |
| 4.82 Buildup 4029 / 5121 | 151 / 106 | 834 / 944 | 10.15 / 10.33 | 0/8, 1/5 | 18.94 / 22.08 |
| 4.80 Wash 9250 / 10529 | 187 / 161 | 1,086 / 1,199 | 10.24 / 15.12 | - | 21.10 / 28.41 |
| 4.81 Wash 7151 / 8211 | 132 / 118 | 1,270 / 1,566 | 24.89 / 36.78 | (not traced) | 39.46 / 50.41 |
| 4.82 Wash 7228 / 8144 | 157 / 105 | 829 / 1,230 | 11.14 / 11.03 | 2/9, 4/8 | 29.14 / 28.26 |

Over the whole capture, the next batch after a capped batch became ready a median
4.79ms after the capped batch finished (P95 17.52, n=12 byte-limited batches),
versus 8.74ms (P95 22.46, n=39) for 4.81 batches whose next dab was already
rendered. The change works as designed. However, only 7 of 30 measured-stroke batches
were byte-limited in 4.82 (maximum batch sizes 24-28, i.e. smaller dab bounds
than the 4.81 strokes), so stroke direction and speed still differ. The dab
wait is back to the 4.80 level and input-to-swap is in the 4.80 range. This is
consistent with removing the 4.81 backlog, not proof of a speedup over 4.80. The
canvas batching reductions of 4.81 remain (193 canvas submissions for 506 updates).
The user reported the 4.82 manual checks (fast diagonal/horizontal strokes,
mirror, Undo/Redo) without problems and chose to proceed to priority 3
(GPU dab generation) on 2026-10-06.

## GPU-evaluated circle dabs (phase 4.83, priority 3 step 1)

2026-10-06. First step of `gpu-work-priorities.md` priority 3: RGBA32F dabs of
the default circle auto brush are evaluated on the GPU instead of uploading
their pixels. The CPU still generates every dab, so every existing CPU path
(fallback blit, CPU mirroring, refusal) keeps its pixels unchanged. A later
step can skip CPU generation for described dabs.

Data flow:

- `libs/image/KisProceduralCircleDab.h` (new): `centerX/Y, cosa, sina, xcoef,
  ycoef, fadeX, fadeY, color[4], antialias`, plus a CPU reference `fadeAt()` /
  `alphaAt()` that repeats `FastRowProcessor<KisCircleMaskGenerator>` (float,
  same operation order). Dab alpha is `1.0f * (1 - fade)` and RGB is the paint
  color, as `fillInverseAlphaNormedFloatMaskWithColor` writes RGBA F32.
- `KisCircleMaskGenerator::vectorCoefficients()` exposes the vector-path
  coefficients. `KisAutoBrush::proceduralCircleDab()` repeats the state set-up
  of `generateMaskAndApplyMaskOrCreateDab()` (softness, then scale, center
  `hotSpot - 0.5 + subPixel`, angle `shape.rotation + brush angle`, cos/sin
  in double then float). It refuses non-circle generators, randomness or
  density, and the non-vectorized path (supersampling).
- `KisDabRenderingJob` gains `procedural` / `proceduralFlips`.
  `executeOneJob()` describes fresh `Dab` jobs only when the GPU brush is enabled
  (`KRITA_GPU_BRUSH`), the fill is solid, no postprocessing (texture, sharpness)
  is needed, the brush is not an image stamp, and the dab and paint color are
  RGBA F32. The description is accepted only if it reproduces the generated
  pixels on the middle row and column within 1e-6 (RGB exact), so a scalar
  applicator or other path is never described. The `KisMirrorOption` pixel
  flip is recorded in `proceduralFlips`. `Copy` jobs share the source job's
  description. `Postprocess` jobs have none.
- `KisRenderedDab` carries `procedural` and `proceduralFlips`.
  `takeReadyDabs()` copies them. Described dabs count one byte per pixel against
  the brush batch budget (instead of 16) to keep a bound on the batch area.
- `KisBrushOp::addMirroringJobs()`: after CPU pixel mirroring, toggles
  `proceduralFlips` for each dab, including dabs that share an already
  mirrored device.
- `KisGpuBrushPainter::paintImpl()` sends described dabs on RGBA32F devices as
  `KisGpuDabCompositor::Dab::generated` with a `Circle` block. The record
  mirror flags are the pass flips XOR `proceduralFlips`. Axis flips commute,
  so this equals reading the reflected pixels. RGBA16F keeps the pixel upload.
  `generatedDabCount()` counts them, and the trace link
  `path.brush.generated_dabs` marks submissions with generated dabs.
- `KisGpuDabCompositor`: a generated record uploads a 64-byte `Circle` block
  instead of pixels (`DabRecord` stays 64 bytes; `mirrorFlags` bit 4 marks it).
  This is RGBA32F only; the planner refuses it for RGBA16F.
  `paint_dabs.comp::generatedCircle()` evaluates it with `precise` operations
  in the CPU order at the integer dab pixel after the existing reflection.
  Every blend mode, selection, channel flag, opacity, flow and average opacity
  path is unchanged.

Tests (Vulkan validation, zero validation errors):

- `KisGpuBrushTest` 4394/4394, with new tests:
  - `testGeneratedCircleDabs` (normal, Alpha Darken, Erase, Multiply,
    pixel-mirrored passes, combined mirrors). Dabs come from the real
    `KisCircleMaskGenerator` vector applicator, with sizes 12-150 px, ratio,
    angle, fades, softness, scale, subpixel and antialias on/off. The test
    checks the CPU reference against every generated pixel within 1e-6. The GPU
    receives the same dabs with **zeroed pixels** plus descriptions and must
    match the CPU blit within 2e-5. It also checks the generated-dab count,
    Undo/Redo.
  - `testGeneratedCircleDabsUseCpuPixelsForHalf` (RGBA16F ignores
    descriptions).
- `KisGpuStrokeTest` 306/306 (all suites). Every RGBA32F untextured GPU-brush
  stroke (Buildup, Wash, mirror, selection, alpha lock, erase, blend modes,
  masking brush) now asserts generated dabs > 0 with unchanged 2e-5
  layer/projection parity. CPU, projection-only, textured and RGBA16F paths
  assert none.
- `KisDabRenderingQueueTest` 12/12 (+1 trace-only skip), `KisGpuEngineTest`
  10/10, `KisGpuPaintDeviceTest` 151/151, `KisGpuCanvasUploadTest` 38/38.

Installed `libkritagpu`, `libkritaimage`, `libkritalibbrush`, `libkritaui` and
`kritadefaultpaintops`; build/install SHA256 match. Not yet measured in the
app. Expected effect: no dab pixel upload for these strokes, so the 32 MiB
source budget is no longer what caps batch size for them. Manual checks:
256px Buildup/Wash with
the measured preset (rotation by drawing angle), mirror, Undo/Redo, and a
textured preset (pixel path). Then one focused six-stroke capture comparing
`path.brush.generated_dabs`, batch sizes, dab wait and input-to-swap.

Real-app follow-up (2026-10-06): focused capture PID 38504 (six 256px strokes,
zero dropped events, archive `%TEMP%/solstice-stage-483-real-38504/`; manual
process 26612 overflowed and is archived separately) recorded **zero**
`path.brush.generated_dabs`. The measured preset
`b)_Basic-4_Flow_Opacity.0011.kpp` uses `MaskGenerator id="gauss"`, so 4.83
correctly left it on the pixel path. 4.83 therefore had no effect on that
preset; phase 4.84 adds the Gaussian circle.

## Exact Gaussian and fused default circle dabs (phase 4.84)

2026-10-06. Making the Gaussian fade match exactly showed that the CPU vector
kernels of this build are not the source-order float math. The AVX2+FMA variant
of `kis_brush_mask_processor_factories` is compiled with `-ffp-contract=fast`,
and its disassembly (llvm-objdump of the `_AVX2+FMA.cpp.obj`) fuses these
operations:

- Both circles: `xr = fma(x_, cosa, -sinay_)` and `yr = fma(x_, sina, cosay_)`.
- Default circle: `n = fma(a, a, b*b)` and `normFade = fma(fb, fb, fa*fa)`.
- Gaussian: `dist = sqrt(fma(xr, xr, b*b))` and the anti-aliasing ramp
  `fma(dist - start, coeff, startValue) / 255`.
- Gaussian erf (VcExtraMath): the denominator `fma(xa, p, 1)`, the polynomial
  as an fma chain, and `fma(-(poly*t), exp, 1)`.
- xsimd exp: the Cephes reduction and polynomial (always fused on this arch).
- Gaussian result: `fullFade = (erfA - erfB) * alphafactor`, clamped at 0, and 0
  when above 254.974.

`KisProceduralCircleDab` (now with `kind`: default/Gauss and the Gaussian
constants) repeats this sequence with `std::fma`. `paint_dabs.comp` repeats it
with GLSL `fma()` and `precise`. Vulkan guarantees correctly rounded add, subtract
and multiply but not division or sqrt, so the shader's `exactDivide()` and
`exactSqrt()` pick the neighbor with the smallest exact fma residual. That gives
the SSE/AVX correctly rounded results the thresholds need. The GPU parameter
block grew to 96 bytes.
`KisGaussCircleMaskGenerator::vectorCoefficients()` and fade-maker getters
expose the constants. `KisAutoBrush::proceduralCircleDab()` accepts both
generators.

The dab-job self-check now requires **exact** equality for both kinds. A CPU
using another kernel (without FMA, or scalar) is rejected and keeps the pixel
upload. A non-FMA variant could be added later if such CPUs matter.

Tests (Vulkan validation, zero validation errors):

- `KisGpuBrushTest` 4395/4395.
  - `testGeneratedCircleDabs` now has 9 shapes. Four are Gaussian: the preset's
    hard 256px edge, soft, asymmetric without AA, and scaled. The CPU reference
    equals every SIMD-generated pixel exactly, and the GPU blends of zeroed-pixel
    dabs stay within 2e-5.
  - New `testGeneratedCircleDabsExact` paints each of 7 shapes alone at full
    opacity on a transparent device. Every GPU alpha equals the CPU alpha bit
    for bit, including the Gaussian thresholds.
- `KisGpuStrokeTest` 322/322, including the 16 new `testGaussStroke` rows
  (`id="gauss"` brush through every `testStroke` variant). They assert
  generated dabs > 0 and 2e-5 layer/projection parity.
- `KisDabRenderingQueueTest`, `KisGpuEngineTest`, `KisGpuPaintDeviceTest` and
  `KisGpuCanvasUploadTest` all pass.

Installed gpu/image/brush/ui/defaultpaintops; hashes match. Not yet measured in
the app.

Real-app follow-up (2026-10-06): focused capture PID 14156, six 256px Basic-4
strokes, zero dropped events, 526 uploads covered to swapped commands; archive
`%TEMP%/solstice-stage-484-real-14156/`. All 40 GPU brush submissions carried
generated dabs (`path.brush.generated_dabs` 40, CPU fallback 0, byte-limited
batches 0). No separate manual-check trace was recorded in this round.

| Capture (same preset) | GPU brush submission CPU median / P95 ms | Byte-limited batches |
| --- | ---: | ---: |
| 4.82 PID 45728 (pixels) | 1.726 / 3.962 | 7 |
| 4.83 PID 38504 (pixels, Gauss not yet covered) | 1.825 / 7.067 | 6 |
| 4.84 PID 14156 (generated) | 0.429 / 1.185 | 0 |

Measured strokes, medians in ms (hand-drawn; speeds 695-998 dabs/s versus
829-1,230 in 4.82):

| Stroke | Dab request -> batch ready | Input -> swap |
| --- | ---: | ---: |
| 4.80 Buildup 5252 / 6402 | 9.86 / 13.91 | 22.94 / 24.10 |
| 4.83 Buildup 4374 / 5387 | 9.97 / 14.06 | 18.96 / 28.77 |
| 4.84 Buildup 3877 / 4835 | 10.01 / 10.03 | 16.91 / 17.79 |
| 4.80 Wash 9250 / 10529 | 10.24 / 15.12 | 21.10 / 28.41 |
| 4.83 Wash 7777 / 8886 | 14.96 / 9.33 | 25.60 / 23.53 |
| 4.84 Wash 7249 / 8337 | 10.08 / 10.12 | 21.51 / 25.18 |

The submission cost drop is a direct effect: no dab pixel copy into the
staging buffer and no 2 MB-per-dab source reads. Buildup input-to-swap is the
lowest of these captures. Wash is in the earlier range. With one process per
version and different stroke speeds, this is not a proven end-to-end speedup.
The remaining dab wait (about 10ms) matches the brush minimum update period, not
the GPU path. Batch sizes are no longer byte-capped. The user reported the 4.84
manual checks (Basic-4 256px Buildup/Wash, mirror, Undo/Redo) as OK.

## Skipped CPU generation of described dabs (phase 4.85)

2026-10-06, priority 3 step 2 (user's choice). Described dabs no longer need
their CPU pixels, so their CPU generation is skipped. Pixels are produced only
when a CPU path needs them.

- **Skipping.** `KisDabRenderingJobRunner::executeOneJob()` first builds the
  description of a `Dab` job (`buildCircleDab()`). If its mask kind is verified,
  `describeWithoutPixels()` gives the device the generator's bounds
  (`maskWidth/maskHeight`, origin 0) and keeps an allocated, unwritten buffer,
  so CPU reflections stay memory-safe. It marks the job `pixelsPending` and does
  not call `generateDab()`. Trace link `dab.generation_skipped`; counter
  `KisDabRenderingJobRunner::skippedGenerationCount()`.
- **Verification gate.** Each kind (default, Gauss) needs 16 dabs fully
  generated and exactly matching their description in the process
  (`describeCircleDab()`). Any mismatch disables skipping for that kind for the
  process. A CPU without the AVX2+FMA kernel therefore never skips.
- **Propagation.** `pixelsPending` travels with Copy and Postprocess jobs and
  `KisRenderedDab`. A Postprocess job renders the description into its own
  postprocessed device and never writes the shared original. Its result is not
  described.
- **Materialization.** `KisRenderedDab::materialize()` uses
  `KisProceduralCircleDab::render()`, the bit-exact CPU reference, including
  `proceduralFlips` (the mirror option and CPU mirror jobs). `KisBrushOp`
  materializes before every CPU use: the refused GPU batch fallback
  (`bltFixed`), and a sequential job before CPU rectangle jobs when the GPU path
  is unsupported for the painter (e.g. LOD).
  `KisBrushOp::materializedDabCount()` counts it. CPU mirror jobs reflect the
  (unwritten) buffer and toggle `proceduralFlips`; materialization later
  overwrites the whole buffer in that orientation.
- **Test hook.** `KisGpuBrushPainter::refusePendingBatchesForTesting(count)`
  refuses batches that contain pending dabs, as a GPU failure would.

The rendering reference uses `std::fma` (a library call in this baseline-x86
build), so materialization is slower than the SIMD generator. It runs only on
fallbacks.

Tests (Vulkan validation, zero validation errors):

- `KisGpuBrushTest` 4396/4396. `testGeneratedCircleDabsExact` checks
  `materialize()` against the CPU generator plus `KisFixedPaintDevice::mirror()`
  byte for byte, for all four flip combinations.
  `testPendingDabsFallBackToMaterializedPixels` paints pending dabs after an
  injected submit failure and matches the CPU blit (transparent RGB excepted).
- `KisGpuStrokeTest` 338/338.
  - Every RGBA32F untextured GPU-brush stroke asserts skipped generations
    (23,712 in the suite).
  - New `testRefusedPendingBatches` (all `testStroke` rows) refuses two batches
    that contain pending dabs. It asserts materialization (1,776 dabs in total)
    and unchanged 2e-5 layer/projection parity and Undo/Redo.
- `KisDabRenderingQueueTest`, `KisGpuEngineTest`, `KisGpuPaintDeviceTest` and
  `KisGpuCanvasUploadTest` all pass.

Installed gpu/image/brush/ui/defaultpaintops; hashes match. Not yet measured in
the app. Expected effect: less worker-thread CPU per dab (no mask generation and
no 2 MB fill per 256px dab). The GPU-side cost is unchanged.

Real-app follow-up (2026-10-06): the user reported the manual checks OK
(including the materialization paths they could reach). Focused capture PID
39228: six 256px Basic-4 strokes, zero dropped events, 516 uploads covered to
swapped commands; archive `%TEMP%/solstice-stage-485-real-39228/`. The manual
process 45248 overflowed and is not analyzed.

- **Dabs.** 607 of the 2,103 `dab.request` events skipped CPU generation. Every
  stroke dab after the first 13 verification dabs was skipped. The other
  1,496 are bursts of 142-551 dabs outside the stroke inputs (around 2-4s, 9.5s
  and 15s), which look like a separate preview renderer on an ineligible device.
  All 34 GPU brush submissions used generated dabs, with no CPU fallback and no
  byte-limited batch.
- **Dab job cost.** Dab jobs within the stroke window (which includes those
  bursts) took median 18.9 us (P95 127.6), 29.0 ms in total. In 4.84 they took
  248.1 us (P95 585.4), 220.6 ms in total. GPU brush submission is unchanged
  (median 0.39 ms).
- **Latency.** Measured strokes (Buildup 4347/5247, Wash 7664/8442) had dab
  wait medians of about 10.0-10.6 ms. Input-to-swap medians were Buildup
  20.23/18.07 ms and Wash 22.59/23.69 ms, within the range of 4.84 (16.9/17.8,
  21.5/25.2). This is a worker-CPU reduction, not a demonstrated latency change.
  The dominant remaining dab wait is the brush update period (10 ms minimum).

## RGBA16F generated dabs (phase 4.86)

2026-10-06, user's choice ("Soft circle and 16-bit float").

- **CPU side.** The F16 CPU dab is the same vector fade written by
  `fillInverseAlphaNormedFloatMaskWithColor` as `half(1.0f * (1 - fade))`.
  Imath/OpenEXR 2 `half(float)` rounds to nearest even, including the
  denormal `convert()` path. The F16 paint color is used as stored.
- **Descriptor.** `KisProceduralCircleDab::halfPixels` holds the half color as
  exact floats. `render()` writes halves, and the new `matchesPixel()` compares
  half bits exactly, as the dab-job self-check does.
- **Brush and painter.** `KisAutoBrush::proceduralCircleDab(..., halfColor)`
  and `buildCircleDab()` accept RGBA F16 dabs. `KisGpuBrushPainter` uses the
  description when the device pixel size matches (16 for F32, 8 for F16).
- **Shader.** The compositor accepts generated records for pixelSize 8 when
  `Circle::halfPixels` matches. `paint_dabs.comp::roundToHalf()` repeats the
  Imath rounding with integer operations. The TILE_F16 variants then blend the
  value as an uploaded half dab.

## Soft (curve) circle generated dabs (phase 4.87)

- **CPU kernel.** The AVX2+FMA disassembly of
  `FastRowProcessor<KisCurveCircleMaskGenerator>` shows this sequence:
  - `xr` and `yr` are fused (as in the other kernels), then
    `dist = fma(a, a, b*b)`.
  - The fade maker uses the square-norm coefficients (radius 1); its ramp is
    `fma(dist - start, coeff, startValue) / 255`.
  - `index = cvttps2dq(dist * resolution)`, with the fraction taken from the
    unclamped index and negative indices clamped to 0.
  - Two table gathers of the double curve table, converted to float.
  - `full = fma(c0, 1 - f, c1*f)`, then `max(0, full)`; the fade is
    `1 - full`, or 0 when `full >= 1`.
- **Accessors.** `KisCurveCircleMaskGenerator::vectorCoefficients()` and
  `curveTable()` expose this state.
- **Descriptor.** `KisProceduralCircleDab::SoftCircle` with `curveResolution`
  and a shared float `curveTable`. `KisAutoBrush::Private` caches the float table
  while the generator's (implicitly shared) table is unchanged, so a stroke's
  dabs share one table.
- **GPU.** `KisGpuDabCompositor::Circle` is now 112 bytes, with
  `curveTable` (device address) and `curveResolution`. `Dab::curveTable/Size`
  are uploaded once per distinct pointer in each batch, and every record keeps
  that address. A first version removed the table from the offset map after
  copying, which gave later mirrored records address 0. The combined-mirror
  test caught this, and it is fixed with a separate copied set.
  `paint_dabs.comp::softFade()` reads the table through a buffer reference. The
  verification gate now tracks three kinds.

Tests (Vulkan validation, zero validation errors):

- `KisGpuBrushTest` 4402/4402.
  - The generated-dab parity has 11 shapes, including the Soft default curve and
    a custom curve with softness. It runs on RGBA32F and five new RGBA16F rows
    (normal, Alpha Darken, Erase, pixel-mirrored and combined mirrors; F16
    tolerance 1/1024 as for uploaded half dabs).
  - `testGeneratedCircleDabsExact` runs for RGBA32F and RGBA16F with 10 shapes,
    including three Soft ones. GPU alpha equals CPU alpha bit for bit, and
    `materialize()` equals the CPU pixels in every reflection.
- `KisGpuStrokeTest` 354/354.
  - New `testSoftStroke` (16 rows, the test brush with `id="soft"` and a custom
    curve). The F16 strokes (`testHalfStroke`, `testHalfBlendModes`) now assert
    generated and skipped dabs.
  - 41,817 skipped CPU generations in the suite; 1,776 materialized in the
    refused-batch rows.
- `KisDabRenderingQueueTest`, `KisGpuEngineTest`, `KisGpuPaintDeviceTest` and
  `KisGpuCanvasUploadTest` all pass.

Installed gpu/image/brush/ui/defaultpaintops; hashes match. Note: the stroke test
loads the installed plugins, so install before running it after an API change.
Not yet measured in the app. Manual checks: Soft brushes (default and edited
curves, softness by pressure), RGBA 16-bit float documents with the Basic-4 and
Soft brushes, mirror, Undo/Redo. The user reported them OK on 2026-10-06.

## GPU brush update period (phase 4.88)

- **Problem.** In the 4.84/4.85 captures, dabs were ready about 0.06ms after
  their request but waited about 10ms for the next batch. `KisBrushOp`
  returns an update period of 10-100ms (upstream, tuned for CPU rasterization
  by worker threads), and `FreehandStrokeStrategy::tryDoUpdate()` starts a
  batch only when `elapsed() > period`. With inputs every 5ms, the strict
  `> 10` let only every third input start a batch.
- **Change.**
  - On the GPU path (`m_isRgbaFloatImage && KisGpuBrushPainter::supports()`),
    `KisBrushOp::doAsynchronousUpdate()` returns `gpuMinimumUpdatePeriod()`
    both before taking a batch and from the batch's final job, overriding the
    adaptive and byte-limit values.
  - The default is -1, so every trigger (input, or a finished dab job, which
    is a `FreehandStrokeRunnableJobDataWithUpdate`) starts a batch once the
    previous one is done. Only one batch is in flight (`m_updateSharedState`).
    The 32 MiB byte budget per batch is unchanged.
  - `KRITA_GPU_BRUSH_MIN_UPDATE_MS` (-1 to 100, read once) overrides the
    default for comparisons. `setGpuMinimumUpdatePeriodForTesting()` overrides
    it in tests; a value below -1 restores the default.
  - `FreehandStrokeStrategy` starts at -1 instead of 40ms when the GPU brush is
    enabled, so the first batch is not delayed either.
  - The CPU path is unchanged.
- **Batch partition and mirrors.** Each batch paints its dabs and then their
  reflections. With mirroring and a non-commutative blend mode (overlay, dodge
  and so on), where reflections overlap the original dabs, the result depends on
  how the stroke is split into batches. This holds for the CPU path as well,
  whose split also depends on timing. With -1, 74 rows of
  `testBlendModes`/`testHalfBlendModes` (mirrored, Buildup) failed against the
  CPU reference by up to 0.05. With `KRITA_GPU_BRUSH_MIN_UPDATE_MS` 0, 4 or 10
  they passed. So the cause is the different split, not the GPU math. Those
  mirrored blend-mode rows now give the GPU path the 10ms period. The new
  unmirrored rows (overlay, dodge, saturation; Buildup and Wash) keep the -1
  default and match the CPU.

Tests (Vulkan validation): `KisGpuStrokeTest` 360/360 (6 new unmirrored rows).
Installed ui/defaultpaintops. Manual checks (fast diagonal and horizontal
strokes with Basic-4 and Soft, mirror, Wash, Undo/Redo, long strokes) were
reported OK by the user on 2026-10-06.

Real-app capture PID 45620 (6 strokes, 0 dropped events; the separate
manual-check process 18676 overflowed and is not analyzed), against 4.85
PID 39228. Both use Basic-4 at 256px with similar dab rates (760-980 vs
620-1150 dabs/s):

| Metric (per stroke) | 4.85 | 4.88 |
| --- | --- | --- |
| Dab request to batch intake, median | 10.0-11.0ms | 0.33-0.81ms (one stroke 4.5ms) |
| Dab request to batch intake, p95 | 20.0-25.0ms | 5.1-5.5ms |
| Batches per stroke | 4-7 (3-41 dabs) | 20-31 (1-11 dabs) |
| Input to swap, median | 18.1-23.7ms | 7.0-12.7ms |
| Input to swap, p95 | 40.4-46.4ms | 11.6-39.3ms |

Byte-limited batches: none in either capture. Covered-to-swapped commands
rose from 516 to 1125, consistent with more, smaller canvas updates. Strokes
are hand-drawn, so the numbers vary with direction and speed. Still, the
removed wait matches the old 10ms period, and every stroke improved its median
input-to-swap.

## Wash latency breakdown and chained brush updates (phase 4.89)

Analysis of the 4.88 capture (PID 45620). Each timed input is walked back from
its last required upload through update, walker, projection request and dirty
dispatch to its brush batch, always following the latest predecessor:

| Stage (median / p95, ms) | Buildup (63 inputs) | Wash (61 inputs) |
| --- | --- | --- |
| Input to batch intake | 0.45 / 5.77 | 3.80 / 6.00 |
| Batch to dirty dispatch | 0.12 / 0.39 | 0.28 / 1.24 |
| Projection request to merge start | 0.08 / 0.59 | 0.68 / 4.22 |
| Projection merge | 0.01 / 0.04 | 3.24 / 5.67 |
| Update ready to upload issue | 2.16 / 3.75 | 0.85 / 3.12 |
| Input to last upload | 3.88 / 10.33 | 9.80 / 13.83 |

Two Wash costs stand out.

- **Missed brush triggers.**
  - Wash dabs were ready 0.23ms after their request (median). The time from a
    finished dab to its batch had a p75 of 4.7ms, which is the 5ms input
    interval (Buildup: 2.2ms).
  - `KisBrushOp::doAsynchronousUpdate()` returns `needsMoreUpdates` when a batch
    is still in flight and prepared dabs remain. `FreehandStrokeStrategy::tryDoUpdate()`
    used it only at stroke end. So dabs finished during a batch waited for the
    next input or the next finished dab job.
  - Wash batches stay in flight longer (GPU job median 0.25ms vs 0.09ms, plus
    2ms merges on the same workers), so they miss triggers more often.
- **Wash merges.** Buildup merges reuse the layer projection (0.01ms). Wash
  merges recompose the layer projection from the original plus the temporary
  target, with a median of 2.0ms. The instrumented compositor, tile-access and
  submit spans inside them account for only about 0.5ms.

Change (implemented, not yet measured in the app):

- **Chained trigger.** When GPU brushing is enabled and an update started a
  batch, `tryDoUpdate()` appends a sequential job after the batch (and after
  its dirty signals) that calls `tryDoUpdate()` again. With nothing ready, or
  while the paint op's period has not passed (the CPU brush path), the call does
  nothing. The chain continues only while new batches start, and one batch is
  still in flight at a time. The forced end-of-stroke chain is unchanged.
- **Trace scopes for the uninstrumented merge time:**
  - `layer.copy_original` and `layer.wash_preview` in
    `KisPaintLayer::copyOriginalToProjection()`;
  - `merge.recalculate_root`, `merge.recalculate_filthy`, `merge.composite`,
    `merge.write_projection` and `merge.gpu_flush` in
    `KisAsyncMerger::startMerge()`.
  - Scopes only record while paint tracing is enabled.

Tests (Vulkan validation where applicable): `KisGpuStrokeTest` 360/360,
`KisGpuEngineTest` 10/10, `KisGpuPaintDeviceTest` 151/151,
`kis_async_merger_test` 12/12, `kis_paint_layer_test` 5/5, `kis_walkers_test`
18/18. Installed image/ui.

The breakdown script is `chain.py`, kept in the session scratchpad; it reuses
the edge rules of `summarize.py::pipeline_summary`. Manual checks (fast Wash and
Buildup strokes, mirror, Undo/Redo, stroke ends) were reported OK by the user on
2026-10-06.

Real-app capture PID 24596: 6 strokes (3 Buildup, 3 Wash), 0 dropped events.
The manual checks ran in a separate process, 48700.

| Stage (median / p95, ms) | Buildup 4.88 → 4.89 | Wash 4.88 → 4.89 |
| --- | --- | --- |
| Input to batch intake | 0.45 / 5.77 → 0.39 / 5.66 | 3.80 / 6.00 → 0.39 / 5.51 |
| Projection merge | 0.01 → 0.01 | 3.24 / 5.67 → 3.35 / 4.75 |
| Input to last upload | 3.88 / 10.33 → 3.52 / 7.83 | 9.80 / 13.83 → 7.75 / 14.65 |

- **Batch intake.** In Wash, the finished-dab-to-batch p75 fell from 4.7ms to
  0.55ms. The remaining p95 of about 5ms is the first dabs of an input whose
  batch was cut by the previous one.
- **Input to swap, per-stroke medians.** Buildup 6.5-8.1ms (4.88: 7.0-8.9ms).
  Wash 11.0-11.5ms (4.88: 11.3-12.7ms).
- **Wash merge breakdown.** Median 1.64ms over all merges.
  - `layer.copy_original` takes 1.08ms. This is the CPU `COMPOSITE_COPY` blit
    of the layer original into the layer projection for every dirty rect, with
    the projection tiles last written by the GPU preview.
  - `layer.wash_preview` takes 0.43ms. Compositor preparation and submission
    are inside it.
  - The base copy is now the largest single Wash cost. Next candidate: do it
    on the GPU inside `paintWashPreview` (copy, then composite the temporary
    target in one submission).

## GPU base copy in the Wash preview (phase 4.90)

`KisPaintLayer::copyOriginalToProjection()` used to copy the layer original into
the layer projection with `KisPainter::copyAreaOptimized()` (a CPU
`COMPOSITE_COPY` blit). The Wash preview then composited the temporary target
on the GPU. The projection tiles were last written by the previous GPU
preview, so the CPU copy wrote into GPU-valid, CPU-stale tiles before every
preview.

- **Replace layers.** `KisGpuLayerCompositor::Layer::replace` (flag 8 in
  `composite_layers.comp`) sets the pixel to the layer's pixel, or to 0 where
  the layer has no tile. Opacity, channels and coverage are ignored.
  - With a coverage mask, pixels outside it now apply only replace layers
    instead of being skipped. Pixels with no replace layer are written back
    unchanged.
  - The coverage op is taken from the top (blended) layer.
  - `record()` accepts a mask for one blended layer above any number of replace
    layers.
- **`KisGpuProjectionCompositor::Layer::replace`.**
  - Replace layers must lead the stack.
  - Every replace device needs the projection's default pixel (byte-equal).
    The shader writes the device's default pixel, `Layer::fill` in tile memory
    order, where the device has no tile. With equal defaults this matches
    `copyAreaOptimized`: inside the rect the projection becomes the source, or
    the shared default where the source has no data. Unequal defaults keep the
    CPU copy, which may clear to the destination's default.
  - A replace layer without tiles in the rect still records a clearing pass.
  - Coverage is dropped when the blended layer has no tiles.
  - F16 coverage rules apply to the top layer.
- **`KisGpuBrushPainter::paintWashPreview(painter, source, rect, base)`.**
  - With `base`, it records the base copy over the whole rect and the Wash
    layer (selection coverage only on it) in one submission.
  - Selection without coverage in the rect gives a copy-only submission.
  - `base` must differ from the destination and the source.
  - `washBaseCopyCount()` counts successful combined previews.
- **`KisPaintLayer::copyOriginalToProjection()`.** For a GPU color space with a
  temporary target, it first tries the combined path (trace
  `layer.wash_preview_with_copy`). If that path refuses or fails, the projection
  is unchanged and the previous sequence runs: CPU copy (`layer.copy_original`),
  the plain GPU preview, then the CPU blend.
  - A failed combined submission is retried as the plain preview after the CPU
    copy.
  - Unaligned originals, unequal defaults, LOD and unsupported modes keep the
    CPU copy.

Tests (Vulkan validation):

- `KisGpuBrushTest` 4438/4438.
  - New `testWashPreviewBaseCopy`: F32/F16 × Normal/Erase/Overlay × plain,
    selection, selection outside the rect, empty original, opaque original
    default (CPU copy expected) and GPU-stale projection. Each row compares
    the area around the rect as well and expects one submission.
  - `testIndirectMerge*` expect the base-copy count. Copy-only submissions
    occur without coverage, and two injected failures reach the CPU fallback.
- `KisGpuStrokeTest` 360/360, now asserting base copies in GPU Wash strokes.
- `KisGpuProjectionTest` 199, `KisGpuBrushJobsTest` 41,
  `KisGpuPaintDeviceTest` 151, `KisGpuEngineTest` 10, `KisGpuCanvasUploadTest`
  38, `KisDabRenderingQueueTest` 12 (1 skipped as before), `kis_paint_layer_test`
  5 and `kis_async_merger_test` 12.
- The full build stops in the PyKrita SIP module on an unrelated, existing error
  (`KisPresetChooser::eventFilter` is private).

`benchmarkWashPreview` (12 × 256px dabs, four mirror passes, median of 5, GPU
preview path) compared with the 4.87 run:

| Mode | Before | After |
| --- | --- | --- |
| Normal | 1.15ms | 0.27ms |
| Erase | 1.11ms | 0.25ms |
| Selected Normal | 1.89ms | 0.51ms |
| Selected Erase | 1.35ms | 0.69ms |

First real-app capture, PID 55584: 6 strokes, 0 dropped events. The user
reported the manual checks OK in a separate process, 49396.

- **Combined path refused.** In every Wash merge, `layer.wash_preview_with_copy`
  returned after a median of 0.027ms and the CPU copy still ran
  (`layer.copy_original` 0.64ms).
- **Cause.** The first version required all-zero default pixels.
  `KisDocument::newImage()` gives the Background raster layer an opaque
  default pixel, and the layer projection clones it.
- **Fix.** The default-pixel rule above (replace fill color, `LayerParams` grows
  to 32 bytes). New test rows: `background` and `empty-background`, where the
  original and projection share an opaque default.
- **Other results.**
  - Buildup input-to-swap medians: 6.8-7.1ms.
  - The first Wash stroke of the capture, 11423, had a 35.9ms median to its
    last upload. Its previews spent 252ms resolving tiles and 1246ms in
    `layer.wash_preview`, summed over parallel merges; the other two Wash
    strokes spent 67-125ms and 117-163ms. The code path matched 4.89 there,
    since the combined path was refused. Cause not determined: check whether
    it repeats as a first-Wash warm-up.
  - The other Wash strokes: 7.2/8.7ms to the last upload, swap medians
    9.7/12.2ms.

Tests after the fix (Vulkan validation): `KisGpuBrushTest` 4450,
`KisGpuStrokeTest` 360, `KisGpuProjectionTest` 199, `KisGpuBrushJobsTest` 41,
`KisGpuPaintDeviceTest` 151, `KisGpuEngineTest` 10 and
`KisGpuCanvasUploadTest` 38. Installed gpu/image/ui/defaultpaintops; hashes
match.

Capture after the fix, PID 52420: 6 strokes on the Background layer, 0 dropped
events. The manual checks ran in a separate process, 12736; the user reported
them OK, including Background and normal layers, selection, eraser, mirror,
Undo/Redo and F16.

- **Combined path used.** `layer.wash_preview_with_copy` ran in every Wash
  merge, and `layer.copy_original` no longer appears.
- **Wash merge median:** 1.93ms → 0.33ms (4.90 first capture; 4.89: 1.64ms).

| Stroke (median, ms) | Input to last upload | Input to swap |
| --- | --- | --- |
| Buildup 3597 / 4885 / 6126 | 8.1 / 3.2 / 2.8 | 14.1 / 7.5 / 5.4 |
| Wash 8515 (first Wash) | 31.6 | 36.2 |
| Wash 9609 / 11043 | 3.6 / 3.4 | 5.8 / 6.5 |

- **Later Wash strokes** are now as fast as Buildup. In 4.89 the Wash
  input-to-swap medians were 11.0-11.5ms.
- **First Wash stroke of the session.** It is slow again, as in the previous
  capture (35.9ms), so the slowdown repeats.
  - Its 88 merges spent 1092ms in `layer.wash_preview_with_copy`, about 12ms
    each; later strokes take about 0.3ms per merge.
  - Of that time, `tile_access.resolve_tiles` (122ms) and
    `compositor.prepare_target` (74ms) are instrumented; the rest is not
    covered yet.
  - The first Buildup stroke (3597) is also slower than the later ones.
  - This is the next candidate: a first-use cost of Wash, likely the new layer
    projection and its first GPU residency.

## Shared and precompiled compute pipelines (phase 4.91)

- **Cause of the slow first Wash stroke.** In the slowest first-Wash merge of
  PID 52420, 58.7ms of the 64.8ms passed between `compositor.wait_context` and
  `compositor.prepare_target`. The tile work itself took about 6ms.
  - That gap is `KisGpuLayerCompositor::create()`, which compiled the
    composite pipeline without a pipeline cache.
  - Every `KisGpuProjectionCompositor` work context owned its own compositor.
    The first parallel Wash merges each compiled the same shader: 12 merges at
    about 60ms, then about 10 at 20-30ms.
  - Buildup never uses this compositor. `KisGpuDabCompositor` (one per brush
    work slot), the layer stack compositor, tile fill and canvas patch writer
    had the same per-instance pattern.
- **Shared pipelines.** `KisGpuContext::sharedComputePipeline(spirv, size,
  pushConstantSize)` compiles a pipeline once per context and embedded SPIR-V
  array, keyed by pointer and push-constant size.
  - A per-entry mutex makes concurrent callers wait for one compilation, while
    different pipelines compile in parallel. Failures are not cached.
  - The context releases its references after `vkDeviceWaitIdle` and before
    destroying the device.
  - All `KisGpuComputePipeline::create` users now go through it and hold a
    `shared_ptr`: layer, dab and layer stack compositors, tile fill, canvas
    patch writer. Dispatch only records commands, so sharing is thread-safe.
  - `sharedComputePipelineCompileCount()` exposes the number of compilations.
- **Precompiling.**
  - `KisGpuLayerCompositor::preparePipelines(context, format, extended)` and
    `KisGpuDabCompositor::preparePipelines(context)` compile ahead of use.
  - `KisGpuTileBackend::preparePipelines()` prepares the dab pipelines and
    the F32/F16 basic and extended layer pipelines (trace
    `gpu.prepare_pipelines`).
  - `KisGpuEngineUi::install()` (main window construction, app only) starts it
    on `QThreadPool::globalInstance()` when the GPU engine is enabled. The
    backend is created there if needed and is never destroyed.
- **Cold compile times** on the RTX PRO 6000 (`testSharedComputePipelines`, a
  fresh context):
  - F32 basic composite: 60ms, with 12 concurrent creates sharing that single
    compilation.
  - F32 extended: 136ms.
  - F16 basic and extended: 211ms.

Tests (Vulkan validation):

- New `KisGpuEngineTest::testSharedComputePipelines`: one compilation under
  concurrency, prepared pipelines reused, dab pipelines shared.
- New `KisGpuBrushTest::testPreparePipelines`: precompiling is idempotent, and
  a Wash preview on fresh work contexts compiles nothing afterwards.
- `KisGpuBrushTest` 4451, `KisGpuStrokeTest` 360, `KisGpuProjectionTest` 199,
  `KisGpuBrushJobsTest` 41, `KisGpuPaintDeviceTest` 151, `KisGpuEngineTest` 11,
  `KisGpuCanvasUploadTest` 38 and `KisGpuGLInteropTest` 3.
- Installed gpu/image/ui/defaultpaintops; hashes match.

Real-app capture PID 51608 (Background layer, 0 dropped events). The manual
checks ran in a separate process, 49252, and the user reported them OK.

- **Precompile.** `gpu.prepare_pipelines` started with the trace and took
  624ms in the background, ending long before the first press at 19.8s.

| Stroke (median, ms) | Input to last upload (4.90 → 4.91) | Input to swap (4.90 → 4.91) |
| --- | --- | --- |
| First Buildup | 8.1 → 3.0 | 14.1 → 5.2 |
| Other Buildup | 3.2 / 2.8 → 3.2 / 3.7 | 7.5 / 5.4 → 5.0 / 6.2 |
| First Wash | 31.6 → 5.7 | 36.2 → 10.8 |
| Other Wash | 3.6 / 3.4 → 4.4 / 4.3 | 5.8 / 6.5 → 7.8 / 7.5 |

- **First Wash merges.** The first 16 took 2.7-5.4ms; the rest 0.1-1.2ms
  (previously about 60ms).
- **Remaining first-use cost.** About 3-5ms per merge over the first few
  batches remains, likely first GPU residency of the new layer projection and
  temporary target tiles.
- **Other Wash strokes.** They vary within the range of hand-drawn strokes.

## Immediate canvas uploads on the shared GPU path (phase 4.92)

Breakdown of the canvas side in PID 51608, for each timed input's last
required upload (`canvas.py` in the session scratchpad; medians):

| Stage | Buildup | Wash |
| --- | --- | --- |
| `update.ready` to GUI `canvas.upload` start | 1.58ms | 1.99ms |
| `canvas.upload` span | 0.21ms | 0.27ms |
| Upload issued to `frame.submitted` | 1.32ms | 2.08ms |
| Frame submitted to swapped | 0.09ms | 0.10ms |
| Input to required swap | 5.40ms | 8.19ms |

Frames were submitted about every 6.4ms.

- **Where the time goes.** `sigCanvasCacheUpdated` (emitted after
  `update.ready`) started `frameRenderStartCompressor`: FIRST_ACTIVE, with a
  delay of 1000 / `fpsLimit` (3ms with the user's `fpsLimit=300`). An update
  arriving inside that window waited for it to end before
  `updateCanvasProjection()` uploaded it.
  - This build has `KRITA_QT_HAS_UPDATE_COMPRESSION_PATCH`, so
    `slotDoCanvasUpdate()` hands the repaint to Qt, which paces paints to the
    screen.
  - The compressor was a second pacing layer in front of an upload that costs
    only 0.2-0.3ms of GUI time on the shared GPU path.
- **Change.**
  - `sigCanvasCacheUpdated` now connects, always queued, to
    `KisCanvas2::slotCanvasCacheUpdated()`.
  - When the canvas widget `sharesProjectionUploads()`, the slot runs
    `updateCanvasProjection()` at once. Pending updates are taken together;
    later queued calls find nothing and do nothing.
  - Otherwise, including CPU uploads, it starts the compressor as before.
  - `KRITA_GPU_CANVAS_IMMEDIATE_UPLOAD=0` restores the compressor for
    comparisons.
  - Paint pacing (Qt) and the frame rate are unchanged; each frame shows newer
    pixels.

Tests: `KisGpuCanvasUploadTest` 38/38, `KisCanvasUpdateBatcherTest` 8/8.
Installed ui; hashes match.

Real-app capture PID 12232: 6 strokes, 0 dropped events. The manual checks ran
in a separate process, 42220, and the user reported them OK: flicker, pan,
zoom and rotate, long strokes, mirror, Undo/Redo, non-paint tools.

| Stage (median, ms) | Buildup 4.91 → 4.92 | Wash 4.91 → 4.92 |
| --- | --- | --- |
| `update.ready` to upload issued | 1.74 → 0.14 | 2.12 → 0.15 |
| Upload issued to frame submitted | 1.32 → 3.09 | 2.08 → 2.44 |
| Input to required swap | 5.40 → 5.60 | 8.19 → 5.72 |

- **Per-stroke input-to-swap medians:**
  - Buildup: 6.4 / 5.4 / 5.5ms (4.91: 5.2 / 5.0 / 6.2).
  - Wash: 7.9 / 5.1 / 6.5ms (4.91: 10.8 / 7.8 / 7.5; its first Wash stroke
    was slower).
- **Findings.**
  - The upload now starts at once, but most of the saved time moved into
    waiting for the next frame. Frames are submitted about every 6.2ms (the
    display refresh with Qt's update compression), and the waits on either
    side of the upload are both bounded by that cadence.
  - Buildup is unchanged within noise. Wash improved, partly because uploads
    no longer queue behind longer merges.
- **What remains.** About 5.5ms from input to swap, of which about 2.5-3ms is
  waiting for the next refresh tick and about 2.5ms is input, brush, merge
  and upload. Further gains on the canvas side would need presentation timed
  to the refresh (rendering just before the tick), not shorter queues.
