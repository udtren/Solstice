---
type: history
topic: gpu-engine
status: archived record (moved from docs/agent/gpu-engine.md on 2026-10-07)
sources: [docs/agent/gpu-engine.md, git history]
---

# GPU engine history: blend modes, RGBA16F and transfers (phases 4.17-4.57)

Blend-mode coverage, asynchronous submissions, RGBA16F brushes, context reuse, batched readbacks. Moved verbatim from `docs/agent/gpu-engine.md`; current state, decisions and
invariants stay there ([gpu-engine.md](../../gpu-engine.md)). Each section records what was true
when it was written; later sections and the current document take precedence.

## Channel-locked Wash and Erase bundle (phases 4.17-4.19)

At the user's request, these three changes ship and receive manual testing
together. `KisGpuProjectionCompositor::Layer` and `KisGpuLayerCompositor::Layer`
carry a four-bit RGBA channel mask, defaulting to all channels enabled. The
shader reuses the existing fourth 32-bit layer parameter word, retaining the
16-byte GPU layer record and 96-byte push constants. Existing layer projection
callers keep their prior all-RGB/optional-alpha-lock behavior; the layer-stack
eligibility gates are unchanged. Partial channel masks are admitted only for
RGBA32F Normal/Erase brush composition.

Normal Wash preview/final composition uses the same channel semantics as the
direct dab shader and CPU scalar Over: Alpha Lock keeps alpha unchanged; RGB
locks preserve their channels except for the CPU's hidden-RGB clearing when a
zero-alpha pixel receives unlocked alpha with partial RGB channels. Zero source
coverage leaves the pixel unchanged. Selection masks can be combined with all
16 channel masks, including all locked. No new synchronization or scheduling
rules are introduced; final-merge tile exclusivity is retained.

`KoCompositeOpErase` ignores channel flags. The GPU Erase shaders already only
change alpha, so the direct-dab and Wash eligibility gates now allow valid
four-channel flags for Erase. This preserves CPU behavior, including its alpha
changes with Alpha Lock set; it does not introduce a new eraser-lock policy.
Malformed channel arrays still refuse. Restricted direct Alpha Darken still
uses CPU fallback; its unrestricted Wash temporary buffer remains GPU-capable.

Tests cover Normal and Erase across all 16 channel masks, with/without soft
selection, for direct dabs and actual Wash preview/final merge. They compare
hidden RGB, COW snapshots, Undo/Redo, CPU-after-GPU work and failed submission.
Complete selected/mirrored Buildup and Wash strokes add Alpha-only, RGB-only
and combined channel restrictions for both operations. Existing Alpha Lock
Wash stroke cases now require GPU preview/final merge; the deferred large-mirror
cases remain correctness regressions, not a resumed optimization project.

Five samples after warm-up on RTX PRO 6000, validation disabled, 128px brush,
soft selection and both mirror axes in a 1024-square, four-layer image with
four workers, median completed stroke time (ms):

| Operation | CPU | GPU projection only | GPU projection + brush |
| --- | ---: | ---: | ---: |
| Normal Wash, alpha locked | 13.00 | 44.96 | 36.77 |
| Normal Wash, green locked | 14.13 | 37.59 | 33.58 |
| Normal Wash, alpha + green locked | 15.23 | 41.81 | 35.28 |
| Erase Wash, alpha flag locked | 14.48 | 42.56 | 35.02 |
| Erase Buildup, alpha flag locked | 12.24 | 29.02 | 23.89 |

These exclude canvas/tablet input and verification readback. GPU projection
plus brush was faster than projection alone here, but all-CPU strokes were
still faster. This is not a controlled before/after comparison with 4.16 or
evidence that the deferred large-brush lag is fixed.

Validation on 2026-10-03: the targeted channel/indirect-merge/failure set passed
172 cases; the full stroke suite passed 40 cases, including init/cleanup.
All nine GPU suites plus the rendering queue passed with Vulkan validation
(10/10, 65.61 s), including existing F16 and layer-blend regressions. Installed
with Krita closed; all eight related DLL SHA-256 hashes match the build.
No configuration changed. The user confirmed the combined interactive check
passed on 2026-10-03. This confirms reported behavior, not a measured latency
improvement.

Combined manual regression checklist: RGBA32F, normal-sized pixel brush, Wash Normal
with Alpha Lock, with/without a feathered selection. Then check Eraser in Wash
and Buildup against its previous behavior. Individual RGB-lock combinations
are covered by the automated matrix and full-stroke tests; this change adds
no new UI controls for them.
Check appearance after release, ordinary mirroring, Undo/Redo, clearing the
selection/locks and save/reopen. This bundle does not claim to resolve the
deferred large-brush mirror latency.

## Separable brush blend bundle (phases 4.20-4.22)

The RGBA32F brush prototype now also accepts Multiply, Screen, Addition/Linear
Dodge (two IDs, one kernel), Subtract, Darken, Lighten, Difference, Overlay,
Hard Light and Exclusion. Direct Buildup dabs and Wash preview/final merging
support these modes with selections and all 16 RGBA channel masks. Alpha
Darken remains the unrestricted Wash temporary-target operation; its own
restricted-channel eligibility has not changed. Layer-stack eligibility is
unchanged, as are RGBA16F brush, LOD, wrap-around and profile fallback rules.

`shaders/composite_blend.glsl` shares the existing separable blend functions
between the layer and dab shaders. CMake explicitly tracks the include for
both F32/F16 layer variants and the dab shader. Dab mode values 4-13 map to
layer operations 1-10; invalid mode values refuse before recording. The shared
wrapper applies selection before opacity and restores locked RGB channels.
It reproduces `KoCompositeOpBase` clearing hidden RGB at exactly zero
destination alpha for any restricted channel set, even when source alpha or
selection coverage is zero. This differs from Normal's early-return behavior.

The new tests exposed a pre-existing dense-allocation discrepancy in the
single-pass GPU dab path: its bounding rectangle allocated gap tiles absent
from CPU painting. Wash final jobs enumerate the temporary target's tile
region, so restricted generic blends cleared extra hidden RGB in those gaps,
even if final merging fell back to CPU. All GPU dab batches now union the
tile-aligned clipped dab rectangles, as combined mirrors already did. Each
tile has one access/workgroup, and the existing tile/staging limits remain.
Tests assert the CPU/GPU temporary tile regions match, in addition to pixels.

Coverage includes HDR/negative destination colors, exact transparent RGB,
all channel masks with/without soft selection, fractional mirror axes,
separate/combined mirror passes, failed submissions and budget fallback,
COW, Undo/Redo and CPU painting after GPU work. Wash additionally checks
inverted/moved/empty/outside selections and changed/removed selection masks.
Complete strokes cover every added blend ID in Buildup and Wash, with soft
selection, both mirror axes, unrestricted channels, Alpha Lock and combined
RGB/alpha restrictions. Brush-job tests exercise selected locked painting,
fractional axes and failed-submit recovery for Multiply, Screen and Overlay.

Validation on 2026-10-03: all nine GPU suites plus the rendering queue passed
with Vulkan validation enabled (10/10, 121.41 s). The isolated new Wash matrix
passed 453 cases including init/cleanup after the tile-region fix. The full
run includes existing Normal/Erase/Alpha Darken, RGBA16F layer compositing,
canvas interop, save/reload and memory-failure regressions.
Installed with Krita closed; all eight related DLL SHA-256 hashes match the
build. No user configuration changed. The user subsequently confirmed the
combined manual test passed. This confirms reported interactive correctness,
not a measured latency improvement.

Five samples after warm-up on RTX PRO 6000, validation disabled, 128px brush,
soft selection, both mirror axes and Alpha Lock in a 1024-square, four-layer
image with four workers, median completed stroke time (ms):

| Operation | CPU | GPU projection only | GPU projection + brush |
| --- | ---: | ---: | ---: |
| Multiply Buildup | 11.46 | 27.58 | 25.05 |
| Multiply Wash | 12.65 | 37.62 | 42.18 |
| Screen Buildup | 11.98 | 30.95 | 28.43 |
| Screen Wash | 16.40 | 37.44 | 37.49 |
| Overlay Buildup | 16.82 | 31.73 | 21.93 |
| Overlay Wash | 17.28 | 42.17 | 39.52 |

These include GPU completion but exclude canvas/tablet input and verification
readback. GPU brush improves some projection-only cases, but not Multiply
Wash here, and all-CPU strokes remain faster. The bundle expands GPU coverage;
it does not establish a general latency improvement or fix the deferred lag.

Combined manual regression checklist: use the usual GPU brush launcher and an
RGBA32F document with existing painted content. With a normal-sized pixel
brush, try Multiply, Screen and Overlay in Buildup and Wash. Check selected
painting, Alpha Lock, ordinary mirroring, appearance after pen release,
Undo/Redo and save/reopen. Return to Normal and Eraser to check switching.
Individual RGB channel masks are covered by automation; no new UI is added.
The deferred large-brush mirrored Alpha Lock latency is not part of this work.

## Bounded asynchronous brush submissions (phases 4.23-4.25)

`KisGpuBrushPainter` now keeps a process-lifetime ring of three `Work` contexts
under its existing mutex. A successful paint call publishes tile state and
returns after submission, without calling `commands.wait()` at the end.
Source pixels, coverage and address tables are copied into the selected
context's upload buffer before return. Caller-owned dabs and masks can be
changed or destroyed while work is pending. The next use of that context
waits before resetting commands or overwriting its buffer. Queue barriers
preserve dab order; `KisGpuTileAccess` retains tile slots and COW sources by
timeline, and existing CPU reads wait/download the latest pixels.

`KisGpuDabCompositor::requiredUploadBytes` and recording share a layout planner,
including source deduplication and padded mask words. Capacity rounds up to
256 KiB to avoid reallocating for every small brush-size change. A source or
whole layout exceeding 64 MiB refuses before preparing destination tiles.
The sum of all three context upload capacities is capped at the same 64 MiB;
it is not a 64 MiB limit per context. Completed buffers are reclaimed first.
If still necessary, older submissions are drained before their buffers are
released. A growing slot frees its completed old allocation before creating
the new one, so replacement does not temporarily double its capacity.
This budget covers dab/selection/table buffers only, not tile-access upload
snapshots, tile pools, projection/canvas contexts or driver allocations.

No scheduler, transaction or CPU fallback policy changes. A failed submission
still returns false with no submitted write, letting the caller replay it on
CPU. A successfully submitted write is never replayed, even on later device
loss. Work-context waits occur before preparing tile accesses, outside the
backend residency lock. Testing-only reset drains contexts before destroying
their Vulkan resources; normal process teardown retains the existing
process-lifetime policy. Staging statistics expose bytes/context count for
regression checks and do not change user configuration.

`testPendingBatches` holds the actual Vulkan queue behind a host-signalled
timeline semaphore. Three batches must return while the queue remains held,
even though their original dabs and masks are overwritten or released. An
asynchronous CPU reader and, separately, a fourth brush batch must wait until
the gate opens. Normal, Alpha Darken, Erase and Multiply compare CPU pixels,
COW snapshots, Undo/Redo and failed-submit CPU replay. A ten-second watchdog
releases the gate on regression so a synchronous implementation fails rather
than hanging. `testStagingBudgetAndReuse` crosses bucket sizes and forces ring
reclamation with large source buffers clipped to small destinations, checks
the aggregate cap, oversized refusal without mutation, and explicit cleanup.
Brush microbenchmarks now warm all ring slots and include GPU completion in
timing; submission time alone must not be reported as completed brush work.

Validation on 2026-10-03: all nine GPU suites plus the rendering queue passed
with Vulkan validation enabled (10/10, 123.73 s). The subsequently strengthened
blocked-queue/reuse tests and completed-work benchmarks passed 21 cases,
including init/cleanup. Installed with Krita closed; all eight related DLL
SHA-256 hashes match the build. No user configuration changed. Combined
interactive verification passed, as confirmed by the user on 2026-10-03.
This confirms observed behavior, not a measured input-latency improvement.

Performance comparison uses the same `KisGpuStrokeTest` executable with the
previously installed 4.22 GPU/image DLLs or the new DLLs in separate temporary
directories. RTX PRO 6000, validation disabled, 1024-square/four-layer image,
four workers, GPU projection plus brush, completion included and canvas/input
and verification readback excluded. After an initial old-then-new five-sample
run showed mixed results, the order was reversed with ten warmed samples:

| Stroke | Before (ms median) | After (ms median) |
| --- | ---: | ---: |
| Normal 64px Buildup | 12.11 | 12.16 |
| Normal 64px Wash | 20.19 | 19.99 |
| Multiply 128px Buildup, selected/mirrored/alpha locked | 23.94 | 23.39 |
| Multiply 128px Wash, selected/mirrored/alpha locked | 37.53 | 36.35 |
| Overlay 128px Buildup, selected/mirrored/alpha locked | 22.07 | 23.07 |
| Overlay 128px Wash, selected/mirrored/alpha locked | 34.84 | 32.06 |

Most differences are small relative to run-to-run variation. Overlay Wash
improved in both runs (initial 37.43 to 33.20 ms); some other cases regressed
in one run. The blocked-queue tests prove that unconditional per-batch waiting
is removed, but these timings do not establish a general stroke speedup or
an input-to-display latency improvement. No claim is made about the deferred
large-brush mirror issue.

Combined manual regression checklist: use the usual GPU brush launcher with
an RGBA32F document and a normal-sized pixel brush. Draw several quick strokes
in Buildup and Wash, then try Eraser, Multiply or Overlay, selection, Alpha Lock
and ordinary mirroring. Check immediate Undo/Redo, switching layers/documents,
appearance after release and save/reopen. The deferred large-brush mirrored
Alpha Lock latency remains outside this bundle's scope.

## Extended blend coverage (phases 4.26-4.28)

Linear Burn, Linear Light and Pin Light now share the GPU blend functions
across layer stacks (F32/F16), direct dabs and Wash preview/final merging
(F32). Existing selection, channel, mirror, tile alignment, memory budget,
submission and fallback rules apply. The brush path remains opt-in; this
does not implement F16 brushes, GPU dab generation or color smudge.

The CPU implementations in `KoCompositeOpFunctions.h` are the reference:
Linear Burn clamps source RGB to [0, 1], permits HDR destination RGB, and
clamps only the lower end of the blend result. Linear Light leaves source,
destination and result unbounded for float storage. Pin Light clamps both
inputs to [0, 1]. Preserve these policies even for alpha-locked blending;
clamping all three modes alike changes colors. The shared wrapper still
handles zero alpha, hidden RGB, opacity and channel restoration.

The expanded real-brush job tests initially failed Linear Light by up to
0.000156403 (CPU/GPU channel values near -55.567), above the unchanged 2e-5
absolute tolerance. Two rounding differences accumulated across dabs:
contracted interpolation arithmetic, and converting mask bytes by multiplying
a rounded 1/255 reciprocal instead of the CPU lookup's division. The shared
generic shader now uses `precise` Linear Light arithmetic and interpolation.
`maskCoverage` corrects the reciprocal product using its FMA residual, matching
all 256 CPU lookup values without a new buffer or Vulkan feature. Both dab
and layer shaders use the correction for the three new modes; existing modes
retain their established reciprocal rounding. Applying the correction to
all modes exposed a 2.28882e-5 Multiply difference after selected painting,
CPU fallback and deselection, so it is deliberately scoped to this bundle.
`testSelectionCoverageRounding` requires exact alpha
equality for every byte through both shaders; normal tolerance checks alone
would not detect a one-ULP coverage difference. The previously failing real
brush job rows now pass without relaxing tolerances.
Temporarily restoring the uncorrected reciprocal conversion makes all three
exact-coverage rows fail at byte value 3; the correction was restored before
final validation and installation.

Existing internal enum IDs are retained. New layer operations are 12-14,
following brush-only Erase at 11; new dab modes are 14-16. The host mapping
and `paint_dabs.comp` both account for that reserved Erase slot. `Count`
bounds layer-operation validation, so invalid enum values still refuse.
These IDs are internal and do not change persisted composite-op IDs.

`KisGpuProjectionTest` extends actual image-stack checks and directly calls
the GPU compositor for all three modes in F32/F16, with/without alpha lock.
The direct calls require success, preventing CPU fallback from passing a
test just because another layer used the GPU. Their input matrix crosses
0, 0.5 and 1, negative/HDR values, zero/near-zero/partial/full alpha, and
full/partial/zero opacity. Existing tolerances remain 2e-5 for F32 and four
half ULPs at max(|value|, 1) for F16.

The brush test matrices include the three modes across selections, all 16
channel masks, hidden RGB, clipping, separate/combined mirrors, source
immutability, COW, Undo/Redo and failed submits. Real brush jobs add selected
alpha lock, fractional mirror axes and failed-submit recovery. Complete
strokes compare Buildup/Wash layer and projection pixels with CPU, requiring
GPU dab/preview/final counters rather than accepting silent CPU fallback.

Validation on 2026-10-04: all nine GPU suites plus the dab rendering queue
passed with Vulkan validation enabled (10/10, 125.23 s). The isolated layer
blend/boundary run passed 44 cases; real brush jobs passed 41, and the exact
coverage plus existing Multiply regression passed six, including init/cleanup.
The final suite includes all of these and the complete stroke, canvas interop,
save/load and failure regressions. `git diff --check` passed. Installed the
rebuilt `kritagpu` and `kritaimage` libraries with Krita closed. The installed
`kritalibbrush` was older than the build used by the tests, so it was also
installed. SHA-256 checks now match all nine related build/install DLLs;
the other six already matched.
No user settings were changed or application process started/stopped.

Five measured samples after warm-up, RTX PRO 6000 Blackwell, validation off,
128px brush, soft selection, both mirror axes and Alpha Lock, 1024-square
four-layer image, four workers, completed-stroke median in milliseconds:

| Operation | CPU | GPU projection only | GPU projection + brush |
| --- | ---: | ---: | ---: |
| Linear Burn Buildup | 9.89 | 26.68 | 19.25 |
| Linear Burn Wash | 11.93 | 33.64 | 29.71 |
| Linear Light Buildup | 11.68 | 26.19 | 21.11 |
| Linear Light Wash | 15.68 | 42.46 | 37.63 |
| Pin Light Buildup | 13.80 | 33.13 | 23.37 |
| Pin Light Wash | 15.36 | 36.19 | 36.13 |

GPU completion is included; canvas/tablet input and verification readback are
excluded. These are new-mode coverage measurements, not a before/after speedup
claim. All-CPU strokes remain faster in these short workloads, and Pin Light
Wash is essentially unchanged from projection-only here. The timing run also
passed CPU pixel parity (eight cases including init/cleanup).

The user confirmed this bundle's manual verification passed. The checklist was:
use the local GPU brush launcher
(`%LOCALAPPDATA%\Temp\krita-gpu-brush-debug.bat`), not an older downloaded
CI artifact: RGBA32F, a normal-sized pixel brush, the three new modes in
Buildup and Wash, soft selection, Alpha Lock and ordinary mirroring.
Check appearance on pen release, Undo/Redo, save/reopen, then switch back
to Normal/Erase. Also check the three layer modes in an RGBA16F document
(brushes there still use CPU). Only the user launches/closes the app.
The deferred large-brush mirrored Alpha Lock latency remains out of scope.

## Blend-family bundle (phases 4.29-4.31)

Nineteen operations were added together: Soft Light SVG/Photoshop, Color Dodge,
Color Burn, Divide, Vivid Light, Hard Mix (default, Photoshop, Softer Photoshop),
Grain Merge/Extract, Negation, Allanon, HSY Hue/Saturation/Color/Luminosity and
Darker/Lighter Color. They share `composite_blend.glsl` across F32/F16 layers,
F32 Buildup dabs and F32 Wash previews/final merges. Existing enum values are
preserved; the new IDs are appended. The host mapping remains the common
eligibility gate. No settings or document serialization changed.

Preserve the CPU functor's individual clamp rules and exact endpoint behavior.
In particular, Color Dodge, Vivid Light and default Hard Mix clamp the source
but allow HDR destination input before SDR result clamping. Color Burn and
Soft Light clamp both inputs. Divide clamps only negative inputs. Soft Light
uses float shader arithmetic against the CPU's double intermediate reference.
The separately named HDR Dodge/Vivid Light/Hard Mix variants, other Soft Light
variants and HSI/HSL/HSV families remain CPU-only.

HSY uses a separate wrapper: strict alpha zero checks, input clamping only
inside the blend function, and interpolation of the original unclamped RGB.
Its luminance weights, saturation normalization and two-stage gamut correction
follow `KoColorSpaceMaths.h`. F16 rounds the blended HSY color to half before
interpolation, then stores each completed layer as half. Reusing the separable
wrapper would incorrectly discard very small alpha and clamp HDR base colors.

The expanded boundary matrix exposed F16 Divide drift after repeated blending
near zero. New modes now reproduce half-rounded opacity, source alpha, alpha
union, and the intermediate terms of `Arithmetic::blend<half>`, as well as
half-specific fuzzy alpha comparisons in the separable wrapper. This is
restricted to the new operations to preserve established modes' arithmetic.
Color Burn's mirror/selection tests exposed division rounding amplified by
repeated nonlinear blending (up to 0.000243). Preserve the compensated divisions
in its blend function and final alpha normalization; removing the latter fails
the existing major-mode tests. Color Burn also needs precise alpha/interpolation/
weighted-color expressions. Compiling those expressions alongside existing
Linear Light changes shared-expression optimization and fails its fractional
mirror test. Keep `BASIC_BLEND_ONLY` shader variants for existing modes and
separate extended variants for operations at/after Soft Light SVG. Both variants
are generated from the same sources; all include dependencies remain explicit.
Compositors lazily create and retain the extra pipeline when first needed.
Mixed layer batches dispatch consecutive runs of each family with a compute
barrier between runs, retaining the same tables and submission. Existing modes
must never use the extended pipeline merely because another layer needs it.
Image-stack parity interleaves Linear Light and the newly supported modes to
cover both family transitions.

Testing is concentrated on major modes. The lightweight layer parity and
boundary cases cover all newly enabled IDs to check mapping and formulas;
the larger brush/stroke matrices add only Soft Light SVG, Dodge, Burn and
HSY Hue/Saturation/Color/Luminosity. CPU-fallback tests now use Dodge HDR
(brushes) and HSL Color (mixed layer stacks), since ordinary Dodge is supported.
Tolerances remain F32 absolute 2e-5 and
F16 four half ULPs at max(abs(value), 1).

A same-session diagnostic run of the existing 4096-square, 16-layer benchmark
measured 745.6 ms GPU with the expanded shader and 758.2 ms with the original
HEAD shader (validation off; CPU 257.7/265.4 ms). These single samples do not
establish a speedup or a regression. They also do not reproduce the historical
42 ms result above; investigate the current whole-stack benchmark separately
before making new performance claims. The temporary baseline was restored;
the final implementation shares layer/dab source math, with separately compiled
variants for the rounding isolation described above, not a claimed speedup.

Validation on 2026-10-04: all ten GPU/brush-queue suites passed with Vulkan
validation enabled, in 167.19 seconds. This includes the interleaved layer
families, F32/F16 boundaries, full brush selection/channel/failure matrices,
complete Buildup/Wash strokes, Undo/Redo, canvas interop, memory budgets and
save/readback protection. The direct diagnostic runs also recorded 2,531
passing brush cases and 41 passing real-brush job cases. No tolerances were
relaxed. Logs are `%LOCALAPPDATA%\Temp\solstice-gpu-429-verified-tests.log`,
`solstice-gpu-429-brush-isolated.txt` and `solstice-gpu-429-jobs-isolated.txt`.
The GPU, image and version DLLs were installed into the local test installation;
SHA-256 matches the build for all nine related application/plugin DLLs.

The user confirmed this bundle's manual verification passed. The checks were
intentionally limited to major modes:
Normal/Multiply/Screen/Overlay regression, Soft Light, Dodge/Burn and Color.
Use the local GPU brush launcher, a normal-sized pixel brush in RGBA32F,
Buildup/Wash, a soft selection and Alpha Lock; check Undo/Redo and save/reopen.
Check Soft Light and Color layer blending in RGBA16F as well (F16 brushes
remain CPU). No need to test the less common added modes individually.
Only the user launches/closes the application. Large-brush mirrored Alpha Lock
latency remains deferred.

## Bulk readback and projection measurements (phases 4.32-4.33)

`KisTiledDataManager::readBytes` and `readPlanarBytes` now prefetch stale GPU
tiles before their existing copy loops. The hook is a no-op without an
existing backend, and automatic prefetch is limited to 8/16-byte pixels.
It never initializes Vulkan just to read a CPU device. Individual iterators
retain their existing single-tile synchronization.

`KisTileGpuHooks::prepareCpuRead` shares the same implementation with explicit
`KisGpuTileAccess::syncToCpu`. It keeps at most 256 stale tile references and
swap read locks at a time (16 MiB of F32 staging), deduplicates shared tile
data, and leaves missing tiles unallocated. `blockSwappingForReadback()` defers
single-tile synchronization until the batch is downloaded; the caller must
finish that download before releasing swap locks or reading the CPU bytes.
The lock order is swap read locks, sorted GPU state locks, transfer mutex.
Callers must continue to exclude writes to the requested region, as for
existing bulk reads and explicit synchronization. Device offsets and planar
API coordinates are unchanged. Failed batches retain the existing per-tile
retry and persistent-content-loss reporting behavior.

The eight new `testBulkReadback` rows cover F32/F16, interleaved reads with
37-byte row padding, planar channels, sparse tiles, negative and unaligned
coordinates, a moved device, null/empty reads and one injected batch failure.
They compare exact bytes with CPU data and assert that 290 stale tiles use
two submissions, or 257 when the first 256-tile batch must be retried tile by
tile. A second read adds no submission and does not enlarge the device.

`benchmarkRefresh` now waits for GPU completion within every timed refresh,
reports the initial refresh and first GPU refresh after CPU work separately,
and uses three resident samples per path by default. Set
`KRITA_GPU_BENCH_REPEATS` (1-20) to change the count. It verifies that the GPU
path ran and compares the final full projection to the CPU within 2e-5.
The old single GPU sample immediately followed a CPU refresh, so it measured
transition cost, not repeated resident updates. A baseline diagnostic split
that into 512.7 ms after CPU versus a 55.3 ms resident median. Retaining empty
tile-pool chunks did not improve either number and was not kept.

Final measurements on 2026-10-04, RTX PRO 6000 Blackwell, validation disabled,
4096x4096 RGBA32F and 16 Normal/Multiply/Screen/Overlay layers:

| Operation | CPU | GPU |
| --- | --- | --- |
| Resident full refresh, median of three samples, completion included | 243.8 ms | 53.9 ms |
| First GPU refresh after CPU projection work | — | 540.4 ms |
| Initial GPU refresh, including first uploads/preparation | — | 2267.7 ms |
| 256px update plus CPU pixel read, average of 20 | 11.19 ms | 1.26 ms |

Full projection `readBytes` took another 47.0 ms. In the separate 256 MiB
paint-device transfer test, automatic `readBytes` took 44.9 ms, explicit
prefetch took 28.0 ms and copying already-current CPU pixels took 20.0 ms.
These transfer figures are single samples, not latency percentiles or direct
before/after comparisons with the historical phase 1 table. No claim is made
about pen-to-screen or application startup latency.

Final validation: all 13 GPU/brush-queue/tile-manager suites passed in
148.57 seconds with Vulkan validation enabled. This covers concurrent
eviction, disk swap, COW/Undo/Redo, persistent readback loss, save protection,
canvas interop and complete strokes as well as the eight new bulk-read rows.
GPU-disabled stub and tile-manager syntax checks also passed (not a complete
GPU-disabled application build). Logs are
`%LOCALAPPDATA%\Temp\solstice-gpu-432-final-tests.log`,
`solstice-gpu-432-projection-bench.txt` and
`solstice-gpu-432-transfer-bench.txt`.
The rebuilt image, brush, UI, KRA and paint-op libraries and default paint-op
plugin are installed in the local test prefix. All nine related DLLs match
the build by SHA-256. This bundle's manual check is pending.

Manual handoff: use an RGBA32F document with several layers, paint with a
normal-sized brush, check Undo/Redo, save/reopen KRA and export PNG. Briefly
check an RGBA16F document too. No additional blend-mode matrix is needed;
the previous bundle is already user-confirmed. Large mirrored-brush latency
remains deferred. Only the user launches/closes the application.

## Textured and masked brush catch-up (phases 4.34-4.35)

The user reported strokes continuing after pen release with textured brushes
at roughly 150px and above. The provided preset also enables Masked Brush.
This is separate from the explicitly deferred large mirrored Alpha Lock case.
Two avoidable costs were reproduced with synthetic textured/masked strokes:

- `doMaskingBrushUpdates` splits CPU masking into small patches; copying a
  GPU-written stroke tile into the mask destination triggered synchronous
  single-tile downloads and CPU COW. A sequential prefetch job now downloads
  the bounding region of the pending patches before any concurrent mask job.
  Only existing stale stroke tiles are transferred; the bounded bulk reader
  retains its 256-tile limit and failure handling. Mask formulas and patch
  parallelism are unchanged. With the opt-in GPU brush switch off, no
  prefetch job is added.
- `WorkPool::acquire` rotated through three staging contexts even when only
  two large buffers fit in the 64 MiB budget. It repeatedly drained, freed
  and reallocated buffers. If growing the next slot would exceed the budget,
  it now reuses the oldest existing context whose capacity is sufficient,
  waiting for its previous work before overwriting any bytes. Normal small
  batches still use three slots. The 32 MiB source batch limit and 64 MiB total
  staging cap are unchanged; larger or incompatible allocations retain the
  previous bounded reclamation/fallback path.

`KisGpuStrokeTest::testTexturedMaskedStroke` adds six rows: 150/300px masked
Wash with and without a generated grain texture, plus 300px texture-only
Buildup/Wash. It checks CPU, GPU projection only and GPU projection+brush,
including complete layer/projection pixels, path counters and Undo/Redo.
`KisGpuBrushTest::testLargeStagingReusesFittingContexts` submits six roughly
30 MiB sources, requires exactly two contexts after warmup and stable reserved
bytes, verifies the 64 MiB ceiling and CPU pixel parity. The old allocator
creates a third context and fails that regression assertion.

Same-session measurements on RTX PRO 6000 Blackwell, validation off, three
measured samples per path after warmup (1024-square F32, four layers/workers,
24 queued stroke segments; 300px rows use dense 0.02 spacing):

| GPU projection+brush workload | Before | After |
| --- | --- | --- |
| 150px texture + masked Wash | 44.8 ms | 30.3 ms |
| 300px texture + masked Wash | 311.3 ms | 112.0 ms |
| 300px masked Wash without texture | 280.4 ms | 87.2 ms |
| 300px texture-only Buildup | 224.0 ms | 64.0 ms |
| 300px texture-only Wash | 235.9 ms | 78.2 ms |

These are complete queued-stroke engine times including GPU completion,
excluding verification readback and actual tablet/canvas latency. They use a
synthetic grain preset, not the user's exact preset. CPU remains faster in
some cases. The user confirmed almost no lag at 150px and acceptable gradual
lag from about 250px upward; this optimization is accepted. Profiling
identified 10-23 ms staging-acquisition stalls in the old large-batch path;
the temporary profiling code and the trial 16 MiB batch limit were removed.
Logs: `%LOCALAPPDATA%\Temp\solstice-gpu-434-baseline.txt`,
`solstice-gpu-434-reuse.txt`, `solstice-gpu-434-staging.txt`.

Final validation: seven suites passed with Vulkan validation enabled in
120.09 seconds (`KisGpuPaintDeviceTest`, `KisGpuBrushTest`,
`KisGpuCanvasUploadTest`, `KisGpuSaveTest`, `KisDabRenderingQueueTest`,
`KisGpuBrushJobsTest`, `KisGpuStrokeTest`), logged in
`%LOCALAPPDATA%\Temp\solstice-gpu-434-final-tests.log`. The rebuilt libraries
and brush plugin are installed; all nine related DLLs match the build by
SHA-256. The user confirmed the real-app latency improvement as described above.

Manual handoff: use the same textured preset at 150px and 300px, compare the
time for the line to catch up after release, and check Undo/Redo. If practical,
compare once with Masked Brush disabled to distinguish the two paths. The
user controls application startup/exit and preset settings. The unrelated
large mirrored Alpha Lock optimization remains deferred.

## Layer channel flags and F16 blend arithmetic (phases 4.36-4.37)

`KisGpuMergeBatch` now forwards all four leaf channel flags to each projection
layer instead of rejecting partial RGB flags. Empty flags still mean all
channels; malformed nonempty flag arrays still fall back. Alpha lock retains
its existing behavior. `KisGpuProjectionCompositor` accepts channel masks for
F16 as well as F32; coverage snapshots remain restricted to single-layer F32.
Layer styles, unsupported modes, unaligned offsets and mismatched color
spaces retain their CPU fallback.

The new channel tests exposed a pre-existing F16 mismatch in basic generic
blends: GPU Multiply/Screen/Overlay used float alpha tolerances and unrounded
intermediates while the CPU uses half arithmetic. For example, with locked
alpha near 0.001, the CPU keeps the destination color but the old GPU shader
blended it. All F16 generic modes now use the CPU's 0.002 zero/unit-alpha
tolerance, rounded source alpha/opacity, blend color, alpha union and weighted
products. Screen and Hard Light/Overlay also round their internal products;
Hard Light uses the half-specific 0.001 midpoint tolerance. Normal and Erase
keep their separate CPU-compatible formulas. F32 formulas and the separate
basic/extended shader families remain intact.

`KisGpuProjectionTest::testLayerChannelFlags` adds 16 rows (Normal, Multiply,
Screen, Overlay, Soft Light SVG, Dodge, Burn, HSY Color, each in F32/F16).
Every row checks all 16 channel masks, actual merge-batch acceptance, full
refresh and an unaligned partial update against CPU rendering.
`testChannelFlagBoundaries` adds 16 more rows with direct successful GPU
submissions, all masks and three opacities (0, 179, 255). It checks every
component, including hidden RGB at zero alpha, HDR/negative colors, half-alpha
thresholds and Overlay midpoint boundaries. Tolerances stay at 2e-5 for F32
and four half ULPs at max(abs(value), 1) for F16; they were not loosened.

Validation: all ten GPU/brush-queue suites passed with Vulkan validation
enabled in 163.85 seconds, including complete strokes, Undo/Redo, save and
canvas interop. Logs: `%LOCALAPPDATA%\Temp\solstice-gpu-436-final-tests.log`
and `solstice-gpu-436-channels.txt`. The rebuilt GPU, image and UI libraries
are installed in the local test prefix; all nine related DLLs match the
build by SHA-256. The user confirmed the combined real-app check passed.

Manual handoff: in layer properties (Active Channels), disable one or two RGB channels on a
paint layer above another colored layer. Check Normal, Multiply and Overlay,
opacity changes, painting, Undo/Redo and save/reopen in RGBA32F; briefly repeat
in RGBA16F. Layer flags are distinct from display-only channel selection in
the Channels docker. No exhaustive blend-mode matrix or large mirrored-brush
latency check is requested. The user controls application startup/exit.

## CPU-to-GPU transition preparation (phases 4.38-4.40)

Profiling the 4096-square, 16-layer benchmark showed that the first new tile
after CPU projection work collected 512 completed upload resource entries.
Their destruction took about 640 ms inside the retirement mutex, making
other image workers wait. These snapshots used `Location::Upload`, which
prefers device-local host-visible ReBAR memory. The cost was largely memory
unmapping/freeing, rather than the layer compute dispatch.

- `KisGpuTileAccess::UploadArena` appends snapshots into shared allocations
  for one projection submission. Each access retains its buffer and offset;
  snapshot timing, generation checks, skipped uploads, COW rollback and GPU
  completion lifetime are unchanged. It is not copyable or thread-safe.
  The first allocation is exact-sized; later allocations grow geometrically
  up to the projection's 16 MiB chunk target. Uploads below 256 KiB do not
  trigger growth, and a single larger upload retains its exact size. Failed
  speculative growth retries an exact allocation. No storage is allocated
  for resident tiles. This is temporary per-submission storage, not a new
  persistent cache or a global upload-memory budget.
- `pinState` reclaims at most 16 completed retired-resource entries per
  automatic collection. Resources are moved out under `m_garbageMutex`, then
  destroyed after unlocking. Deferred slots still release only after their
  timeline completes; explicit `collectGarbage`/`flush` drains all completed
  entries. Chunk trimming retains the pool's own locking. There is no new
  background thread.
- `KisGpuBuffer::Location::Staging` requires coherent host-visible memory and
  prefers host-cached memory for transfer-only CPU snapshots. The existing
  memory-type fallback works if no cached type exists. Shader-readable brush
  buffers and tables retain `Location::Upload` and its ReBAR preference.
  Allocation/coalescing and lock changes alone left transition times near
  600 ms; choosing cached snapshot storage produced the substantial gain on
  the tested NVIDIA/Windows environment.

Regression tests: `testSharedUploadArena` has eight rows (F32/F16, normal or
oversized chunks, successful or injected failed submission), requiring five
source uploads to use three allocations in the shared case and five in the
oversized case. It destroys the arena before submit and accesses before the
GPU wait, checks all source regions byte-for-byte on CPU, and checks unchanged
output after failed submit. `testRetiredResourcesReleasedOutsideLock` holds a
destructor open while another thread retires a resource; the latter must
finish before the destructor is released. `testTileAllocationBoundsResourceReclamation`
checks the 16-entry limit and complete explicit flushing. Existing projection
tests cover partial updates, selections, channel flags and CPU fallback.

Same-session benchmark, RTX PRO 6000 Blackwell, validation disabled,
4096x4096 F32, 16 layers, GPU completion included:

| Measurement | Before (one fresh process) | After (three fresh processes) |
| --- | --- | --- |
| Initial refresh, including uploads/preparation | 2520.5 ms | 620.2-655.0 ms |
| First GPU refresh after CPU projection work | 629.1 ms | 58.2-59.1 ms |
| Resident GPU refresh, three-sample median per process | 56.2 ms | 55.7-56.3 ms |
| Resident CPU refresh, three-sample median per process | 251.6 ms | 251.3-258.5 ms |
| Full CPU readback | 46.3 ms | 46.2-47.4 ms |
| 256px dirty update plus canvas read, average of 20 | 1.24 ms | 0.85-0.98 ms |

Every run verifies full CPU/GPU pixel parity. Initial/transition values are
single samples per fresh process, not latency percentiles. These are engine
measurements, not application startup, document loading or pen-to-screen
latency. Logs: `%LOCALAPPDATA%\Temp\solstice-gpu-438-before.txt`,
`solstice-gpu-438-host.txt`, `solstice-gpu-438-host-repeat2.txt`,
`solstice-gpu-438-host-repeat3.txt`. Temporary profiling instrumentation was
removed. Large mirrored Alpha Lock latency remains explicitly deferred.

Validation: all ten GPU/brush-queue suites passed with Vulkan validation
enabled in 138.63 seconds (`solstice-gpu-438-final-tests.log` in the same temp
directory). GPU, image and UI libraries are installed in the test prefix;
all nine related DLLs match the build by SHA-256. The user confirmed the
combined real-app check passed.

Manual handoff: open a multi-layer RGBA32F document, paint on several layers,
change visibility/opacity and check Undo/Redo and KRA save/reopen. Apply and
undo a CPU filter to exercise returning to GPU projection. Briefly check
RGBA16F too; no additional blend-mode matrix is needed. The user launches
and closes the application.

## Batched voluntary tile eviction (phase 4.41)

`KisGpuTileBackend::evictTiles` retains the existing historical/CPU-current
candidate priority but visits up to 256 candidates per batch. The tile store
holds its iteration lock for object lifetimes and takes non-blocking swap
write locks through `tryEvictGpuTileDataBatch`. Missing, already-evicted,
duplicate and busy tiles do not contribute released bytes. The backend then
takes the residency lock, rejects pinned/in-flight slots, sorts state locks
and separates pixel sizes before calling `downloadBatch`.

At most 256 tiles (16 MiB for F32, 8 MiB for F16) are downloaded per transfer.
CPU-current tiles need no transfer. Only CPU-valid contents are released;
a failed batch retains its GPU-only slots without retries, content-loss
reporting or stopping the engine. Other pixel sizes/CPU-current neighbors
can still be reclaimed. The existing single-tile hook remains for disk swap.
Lock order stays store lifetime, swap locks, residency, sorted tile states,
then transfer commands. The no-GPU hook is a no-op returning zero.

`testBatchedEviction` adds eight rows (F32/F16, successful transfer, failed
submission, busy swap lock and denied readback allocation). A 17x17 tile
image now requires two readback submissions rather than 289; all pixels are
compared exactly after successful reclamation/retry. `testMixedEvictionBatch`
mixes CPU-current F32, GPU-only F32/F16 and duplicate entries. It fails the
F16 transfer, checks successful F32 eviction and exact returned byte counts,
then checks that retry preserves F16 pixels and already-evicted entries
return zero. Existing pressure, prepared-access, COW source, Undo/Redo,
disk-swap and concurrent-eviction tests remain required.

The full regression run also exposed an intermittent pre-existing assertion
in `testChannelFlagBoundaries(normal-f160)`: channels 15, opacity 179,
component 256 had CPU 0 versus GPU -0.25, with both output alphas zero.
`KoOptimizedCompositeOpOver128` may copy source RGB when every destination
alpha in a SIMD batch is zero, while its scalar path preserves destination
RGB for a zero source alpha. The test now independently asserts the GPU's
scalar preservation rule against the original pixels only for unrestricted
Normal RGB where both output alphas are exactly zero. Alpha, visible pixels,
restricted channels, other modes and numeric tolerances are unchanged. No
production blend behavior was changed to match CPU SIMD grouping.

Validation on 2026-10-04: all 13 selected suites passed with Vulkan validation
enabled (GPU engine/interop/paint-device/projection/brush/brush-jobs/stroke/
canvas/save, dab queue and three tile-store suites; 152.42 seconds). The
projection suite then passed three consecutive CTest runs (72.54 seconds).
The no-GPU tile hook and tile store also passed syntax-only compilation;
this was not a complete no-GPU build. Updated image, brush, UI, KRA,
paint-op and default paint-op libraries were installed; all nine relevant
built/installed DLL hashes match. The user confirmed the application's
phase 4.41 manual check passed. Logs are under `%LOCALAPPDATA%/Temp/` as
`solstice-gpu-441-final-tests-fixed.log`,
`solstice-gpu-441-projection-repeat.log` and `solstice-gpu-441-install.log`.

This reduces transfer submission count; no application latency benchmark
is claimed for eviction. Manual check: paint and change layer visibility in
a multi-layer float document, Undo/Redo, save/reopen, and briefly check F16.
There is no need to change the user's memory settings to force pressure;
low-budget and failure conditions are covered by automated tests. The user
controls application startup/exit.

## Current-build benchmark baseline (phase 4.42)

Measured on 2026-10-04 from application sources at `24c26c3416`, with only
the canvas benchmark changed afterwards. Environment: Windows 11, Ryzen 9
9950X (32 logical processors), RTX PRO 6000 Blackwell, driver 596.86,
Qt 6.8.0, Clang 21.1.6, RelWithDebInfo. Each workload ran in three fresh
processes, sequentially, with validation disabled. The user application was
not running. No settings or installed binaries were changed.

`KisGpuCanvasUploadTest::benchmarkCanvasUpdate` previously took a single sample
per path, and did not reject successful CPU fallback. It now uses
`KRITA_GPU_BENCH_REPEATS` (default 3, bounded 1-20), discards one warmup per
path, alternates CPU/GPU order, and rebuilds the GPU projection outside each
timed interval so both paths start with GPU-authoritative pixels. It requires
an actual projection dispatch, exactly one GPU upload for a GPU sample, no
GPU upload for a CPU sample, and the correct upload type on every tile.
GPU completion is included. The last sample of each size also uploads to real
GL textures and compares every texel to the CPU path outside the timer,
retaining the existing linear-sRGB conversion tolerance of 5e-4. The maximum
measured error was 0.000100363. Interop state is restored by a scope guard.
No timing threshold determines pass/fail.

Five resident samples per process were used for projection and canvas;
mirror measurements retain five-update averages after three warmup updates.
The following ranges are the three process results, not sample percentiles:

| Workload | CPU range (ms) | GPU range (ms) |
| --- | ---: | ---: |
| 4096-square, 16-layer resident projection | 241.5-247.6 | 55.4-57.1 |
| 4096-square, 8-layer canvas preparation | 1037.68-1115.46 | 1.4284-1.7955 |
| 256-square canvas preparation | 5.2294-5.8447 | 0.1334-0.1561 |
| Nearby mirrors, 14 dabs at 73px | 0.95572-1.00690 | 0.35190-0.38422 |
| Nearby mirrors, 32 dabs at 256px | 43.6301-48.0712 | 1.96470-2.03704 |
| Distant mirrors, 14 dabs at 73px | 0.85332-0.87598 | 0.30836-0.32426 |
| Distant mirrors, 32 dabs at 256px | 43.6443-50.7916 | 1.88942-1.97296 |

Initial projection was 609.920-639.517 ms, first GPU refresh after CPU
projection 57.8085-71.8103 ms, full CPU readback 46.1-48.8 ms. README values
are the median of the three per-process results, rounded for display.
This is a current baseline, not a controlled old/new performance comparison.
Canvas measurement boundaries changed, so do not present its difference from
the historical 1025/1.6 ms single samples as an optimization speedup.

Four ordinary Normal queued strokes were also measured using the existing
`KisGpuStrokeTest`, five samples after warmup per process, alternating path
order. CPU layer/projection parity, GPU-path counters and warmup Undo/Redo
checks remain enabled. Median of the three per-process medians (ms):

| Stroke | CPU | GPU projection only | GPU projection and brush |
| --- | ---: | ---: | ---: |
| 64px Buildup | 4.5080 | 9.2534 | 9.0022 |
| 64px Wash | 5.5280 | 10.8005 | 12.8794 |
| 256px Buildup | 8.0430 | 13.3256 | 13.7579 |
| 256px Wash | 10.6316 | 16.3119 | 19.6269 |

The image is 1024-square F32, four layers/workers, 24 queued segments,
without mirrors or selections. Timings include generation, jobs, final Wash
merge and GPU completion, but exclude canvas/input and verification reads.
Per-process GPU projection+brush medians ranged 8.8033-10.2944, 12.3777-12.8978,
13.7365-14.1701 and 18.7192-21.4228 ms respectively. CPU remains faster;
compositor microbenchmarks must not be presented as complete stroke speedups.

Reproduce after rebuilding `KisGpuProjectionTest`, `KisGpuCanvasUploadTest`,
`KisGpuBrushTest` and `KisGpuStrokeTest`. From the configured build environment
(`call <krita-dev-root>\env.bat >nul`), set `KRITA_GPU_VALIDATION=0`,
`KRITA_GPU_BENCH_SIZE=4096`, `KRITA_GPU_BENCH_LAYERS=16`,
`KRITA_GPU_BENCH_REPEATS=5`, `KRITA_GPU_STROKE_REPEATS=5`, and leave budget
overrides unset. Run each command three times in separate processes, writing
each run to a distinct temporary output file with `-o <file>,txt`:

```bat
KisGpuProjectionTest.exe benchmarkRefresh
KisGpuCanvasUploadTest.exe benchmarkCanvasUpdate
KisGpuBrushTest.exe benchmarkCombinedMirrors
KisGpuStrokeTest.exe testStroke:64-buildup testStroke:64-wash testStroke:256-buildup testStroke:256-wash
```

Do not overlap GPU workloads. Logs are `%TEMP%\solstice-gpu-442-<name>-<run>.txt`
with names `projection`, `canvas`, `mirrors`, `stroke`, and runs 1-3.
All 12 processes passed (54 total cases including init/cleanup; no skips).
The complete canvas suite then passed 28 cases with Vulkan validation enabled
and zero validation errors (`solstice-gpu-442-canvas-validation.txt`).
A separate negative control with `KRITA_GPU_CANVAS_BUDGET_MIB=1` correctly
failed the GPU-upload assertion (actual 0, expected 1), proving CPU fallback
cannot be published as a GPU timing (`solstice-gpu-442-fallback-rejection.txt`).
This was a test-process override; user preferences were not modified.
No application installation or new interactive handoff is needed for this
test/documentation-only stage.

The subsequent ordinary-stroke tile-cost work is recorded below. Remaining
implementation priorities include GPU dab/mask generation with CPU parity and
actual stroke measurements.
RGBA16F brush arithmetic and color smudge remain separate coverage work.
Keep GPU brush opt-in until complete drawing latency is measured; do not
reopen the explicitly deferred large mirrored Alpha Lock investigation.

## Ordinary-stroke tile costs (phases 4.43-4.45)

Implemented together after the phase 4.42 baseline. Temporary stage profiling
identified tile preparation and COW as substantial costs. Those diagnostic
totals include fixture setup/verification as well as strokes and must not be
published as isolated stroke timings. All temporary instrumentation was removed.

- **4.43:** `KisGpuTileAccess` now uses
  `KisTileDataStore::duplicateCpuSnapshot`. The existing tile-data copy
  constructor allocates and copies once, avoiding a per-pixel zero fill before
  the full snapshot copy. It retains memory accounting and the CPU recovery
  snapshot, does not consume a pooler preclone or download current GPU pixels.
  The caller must protect the source from swapping and concurrent writes, using
  the existing GPU residency pin or swap read lock. Successful GPU copies and
  failed-submission rollback remain unchanged.
- **4.44:** whole-tile branches of exact/rough `bitBlt`, including old transaction
  data, use `KisTile::cloneShared`. Its COW mutex protects reference acquisition;
  no CPU byte access means no readback or swap-in is needed. The existing caller
  contract still excludes concurrent pixel writes. Partial copies retain normal
  read/write locking and CPU synchronization. Do not weaken `lockForRead` itself.
- **4.45:** `KisTiledDataManager::clear` prefetches only partially covered edge
  tiles through `KisTileGpuHooks::prepareCpuRead` before taking write locks.
  Whole interior tiles are replaced without downloading their old contents.
  The hook retains its bounded 256-tile batches, duplicate corners become no-ops,
  and individual reads retain retry/content-loss handling after a failed batch.
  Empty clipped clears return immediately. CPU-only/no-backend paths do not
  initialize Vulkan.

`KisGpuPaintDeviceTest` adds 24 data rows across three tests:
`testGpuCopyKeepsCpuSnapshot`, `testWholeTileCopyStaysOnGpu` and
`testPartialClearBatchesReadback`. Coverage includes F32/F16, GPU-authoritative
pixels with a deliberately older CPU snapshot, ReadWrite/WriteOnly COW,
submission failure, current/old exact/rough copies, subsequent partial CPU
copies, independent writes, shared originals and Undo/Redo. The clear fixture
uses a 10x10 tile region at negative coordinates: 36 distinct boundary tiles
download in four submissions, aligned clears use zero, and a single partial row
uses one. Injected first-batch failure retries safely (13 submissions).

Controlled before/after measurements on 2026-10-04 use the phase 4.42 hardware
and ordinary stroke workload, with `KRITA_GPU_STROKE_REPEATS=10` and validation
disabled. The baseline is the previously installed image DLL; the new version
contains all three changes. Both use the same stroke test and remaining build
dependencies. Three fresh process pairs run sequentially in before/after,
after/before, before/after order, without a running application. Values are
medians of the three process medians, in milliseconds:

| Stroke | CPU before / after | GPU projection before / after | GPU projection + brush before / after |
| --- | ---: | ---: | ---: |
| 64px Buildup | 4.3209 / 4.2697 | 9.0472 / 7.8149 | 8.2757 / 7.1773 |
| 64px Wash | 5.2293 / 5.1414 | 10.2257 / 8.8534 | 11.7439 / 10.4859 |
| 256px Buildup | 9.0366 / 8.9238 | 14.7018 / 12.4097 | 13.8735 / 11.6092 |
| 256px Wash | 11.6993 / 11.8162 | 17.5883 / 15.7613 | 19.9072 / 18.1752 |

Projection-plus-brush process medians before/after span 8.23-8.45 / 7.04-7.39,
11.51-11.99 / 10.35-10.60, 13.61-14.23 / 11.30-11.65, and
19.50-20.78 / 16.93-18.36 ms, respectively. Each process passes CPU image parity,
GPU path counters and warmup Undo/Redo checks (six passed cases per process).
Logs: `%TEMP%/solstice-gpu-445-{before,after}-{1,2,3}.txt`.
Early measurements with only phases 4.43/4.44 did not establish a reliable
whole-stroke improvement; report the combined result, not individual speedups.
CPU remains faster for these strokes. No canvas/input latency claim is made,
and the GPU brush remains opt-in. Projection/canvas/mirror values in the README
retain the earlier phase 4.42 baseline; only the ordinary-stroke example changed.

Validation-enabled regression: **23/23 CTest suites passed**, including GPU
engine/interop/paint-device/projection/brush/canvas/save/brush-jobs/full-stroke,
CPU painter/transaction/paint-device/iterators, and all nine tile-store suites
(including the concurrent low-memory and disk-swap checks). No GPU validation
assertion failed. Log: `%TEMP%/solstice-gpu-445-regressions.log`.
The modified C++ lines were formatted and `git diff --check` passed. Image,
brush, UI, KRA, paintop and default-paintop DLLs were rebuilt and installed;
all six built/installed hashes matched. Install log:
`%TEMP%/solstice-gpu-445-install.log`. The application was closed during
installation, and no preferences were changed. The user confirmed the combined
real-app test passed.

Manual handoff: ordinary Normal Buildup/Wash in RGBA32F, Undo/Redo, layer copy,
save/reopen and a brief RGBA16F check. The user controls application startup
and exit. Do not reopen the deferred large mirrored Alpha Lock investigation.

## RGBA16F Normal/Erase dabs (phase 4.46)

The opt-in brush path now accepts F16 Normal and Erase in addition to the
existing F32 modes. `KisBrushOp` retains the owning-RGBA-float-image gate:
an F16 layer in an integer image must not bypass the GPU-aware scheduling
requirement. Matching dab/device profiles, selection bounds, disjoint paint
rectangles, tile budgets, LOD/wrap restrictions and sequential job ownership
are unchanged. At this phase F16 Alpha Darken and Wash remained CPU paths;
phases 4.47-4.48 below extend them. Other blend modes remain CPU paths.
No new preference or automatic enablement is added.

`KisGpuDabCompositor` takes a storage size (8 or 16 bytes) for both upload
planning and recording. Source pointers describe raw pixels of that format;
the caller must not mix formats in a batch. Half sources use eight bytes per
pixel, with each unique source padded to 16-byte alignment, including odd
pixel counts. Shared sources are uploaded once. The existing three-slot ring
and combined 64 MiB cap cover both formats; no additional staging cache is
introduced. F16 uses a lazily created shader pipeline in the same work slot.

The F16 shader follows `KoCompositeOpAlphaBase<half>` / `KoCompositeOpOver`
and `KoCompositeOpErase`: opacity and alpha intermediates round separately,
then the destination rounds after every dab, not just at batch completion.
Normal's selection formula uses the original byte before dividing by 255;
Erase rounds selection coverage to half before multiplying. Erase preserves
RGB and ignores channel flags, matching CPU behavior. `channelMask` bit 4
records nonempty explicit flags for F16 Normal's hidden-RGB handling; bits
0-3 retain their established channel meaning. The debug counter index masks
off this additional bit. F32 arithmetic/shader variants are retained.

New tests: 20 F16 Normal/Erase data rows cover soft masks containing all
256 coverage values, empty/all/partial/locked channel flags, transparent
hidden RGB, HDR/negative color, odd-sized dabs, shared snapshots, Undo/Redo,
failed submission with CPU replay, and unsupported-mode refusal. The observed
maximum difference in this fixture is 0.00048828125; the assertion allows
1/1024 (one half ULP at 1.0), including hidden RGB. Erase matched exactly in
the fixture. This is bounded parity testing, not a bit-exact guarantee for
arbitrary repeated strokes.

Four additional queue-gated F16 cases verify asynchronous snapshots, source
and mask destruction, deferred CPU reads, fourth-batch ring reuse and failed
submission rollback. Eight full-stroke rows compare CPU, GPU projection and
GPU projection-plus-brush: Normal/Erase, Buildup/Wash, ordinary strokes and
selected/alpha-locked mirrors. Buildup must increment GPU batch counters;
F16 Wash initially did not increment GPU dab, preview or final-merge counters
(updated to require GPU submissions in phase 4.48). Layer
and projection tolerances are 0.002 and 0.004 respectively, with exact layer
Undo/Redo restoration. The projection tolerance includes the pre-existing
F16 layer-compositing rounding difference.

Manual check: the user confirmed phase 4.46 passed. The checklist was to
launch the usual GPU-brush build, create an RGBA16F
document, paint Normal Buildup and erase, try a soft selection and Alpha Lock,
then Undo/Redo and save/reopen. Briefly switch to Wash and an existing RGBA32F
document. Large mirrored Alpha Lock tuning remains explicitly deferred.

Validation: all **10/10** GPU/interop/paint-device/projection/brush/canvas/save,
brush-jobs, complete-stroke and dab-queue CTest suites passed, including the
new cases and the existing F32 matrix. Vulkan validation was enabled and no
validation assertion failed. Log: `%TEMP%/solstice-gpu-446-regressions.log`.
Version, GPU, image and default-paintop DLLs were installed with matching
build/install hashes (`solstice-gpu-446-install.log`). No preferences changed.

Benchmark: 2026-10-04 on the phase 4.42 hardware, `KRITA_GPU_VALIDATION=0`,
`KRITA_GPU_STROKE_REPEATS=5`. Three fresh sequential processes, alternating
CPU/projection/brush path order within each process, no running application.
Run `KisGpuStrokeTest.exe` with `testHalfStroke:` rows
`erase{0,1}-wash0-selected-locked-mirrors{0,1}` (four explicit row arguments).
The workload is a 128px Normal/Erase Buildup brush, 1024-square RGBA16F image,
four layers/workers and 24 segments. Timing includes generation, scheduling
and GPU completion; input, canvas and verification reads are excluded.
Median of three process medians, milliseconds:

| Stroke | CPU | GPU projection | GPU projection + brush |
| --- | ---: | ---: | ---: |
| Normal | 12.1493 | 10.2716 | 7.4203 |
| Erase | 12.5181 | 9.9734 | 7.2347 |
| Normal, selection + Alpha Lock + both mirrors | 32.7680 | 18.9381 | 10.6735 |
| Erase, selection + Alpha Lock + both mirrors | 42.1137 | 29.4617 | 9.8655 |

The corresponding CPU ranges are 12.10-12.29, 10.96-12.59, 27.04-33.05 and
32.17-43.18 ms; GPU projection+brush ranges are 7.17-8.41, 6.90-7.34,
10.37-11.07 and 9.07-11.12 ms. Erase ignores Alpha Lock as usual. The combined
rows show substantial run variation; do not generalize to arbitrary strokes
or claim a solution to the deferred large mirrored Alpha Lock case. All three
processes passed parity/path/Undo checks (six cases each). Logs:
`%TEMP%/solstice-gpu-446-bench-{1,2,3}.txt`. This adds F16 coverage and separate
F16 measurements; it does not replace the existing F32 README timings.

## RGBA16F Alpha Darken and Wash (phases 4.47-4.48)

The opt-in F16 brush path now supports both hard and creamy Alpha Darken,
including Wash's temporary target. The shader follows the scalar CPU half
implementation: flow, opacity, average opacity, mask/alpha products and lerps
round at their CPU storage boundaries. Hard flow's legacy union rounds its
product separately. The full-flow branch tests the original float flow,
not its rounded half value (0.9999 must not become the exact-1 branch).
Unrestricted Alpha Darken accepts either empty or explicit all-enabled flags;
restricted Alpha Darken still falls back. The existing three-slot/64 MiB
staging budget and asynchronous tile lifetime rules also apply to F16.

Normal/Erase Wash preview and final merging use `halfBrush` on the projection
and low-level layer descriptors. It is valid only for F16 Normal/Erase.
`composite_half_brush.glsl` shares the scalar half formulas with direct dabs;
ordinary F16 layer projection keeps its prior arithmetic. Layer shader flag
bits are 0: alpha lock, 1: half-brush arithmetic, 2: explicit channel flags.
The explicit-flags distinction preserves hidden RGB handling for scalar
Normal. Soft selection coverage is accepted for a single F16 brush layer.
Matching profiles, aligned tiles, owning-image scheduling, readback/fallback,
transaction and Undo/Redo rules remain unchanged. Other F16 blend modes at
this phase remained CPU paths; phases 4.49-4.50 below extend the basic modes.
LOD, wrap-around and unsupported profiles still use the CPU. No preferences
are changed and no extra cache or shader work pool is introduced.

Regression coverage:

- `testAlphaDarkenVariants`: 128 rows across F32/F16, hard/creamy, flow
  0/0.43/0.9999/1, opacity/average combinations and soft masks. Uses the real
  scalar F16 CPU ops and optimized F32 CPU ops, independently of the current
  hard/creamy preference. F16 tolerance is 1/1024, including hidden RGB;
  F32 remains 2e-5.
- `testPendingBatches`: two extra F16 Alpha Darken queue-gated rows prove
  asynchronous source/mask lifetime, readback waits and ring reuse.
- `testHalfIndirectMerge` and `testHalfWashChannelLocks`: 102 F16 rows cover
  all 16 channel masks, selection, inverted/moved/empty coverage, preview and
  merge submit failures, low budget, limited readback, unaligned source and
  integer owning-image fallback. Includes hidden RGB, shared snapshots and
  exact Undo/Redo. Combined dab/merge tolerance is 0.002.
- `testHalfStroke`: eight actual Normal/Erase Buildup/Wash rows, ordinary
  and selected/alpha-locked mirrored strokes. Wash now requires GPU dab,
  preview and final-merge counters. CPU/projection/projection+brush results
  retain layer tolerance 0.002 and projection tolerance 0.004, with exact
  Undo/Redo; ordinary F32 coverage remains unchanged.

Validation: the focused arithmetic/lifecycle run passed **266/266** cases;
the full-stroke F16 run passed all eight data rows plus init/cleanup.
All **10/10** GPU/interop/paint-device/projection/brush/canvas/save,
brush-jobs, complete-stroke and dab-queue CTest suites then passed with
Vulkan validation enabled (163.41 seconds). Logs:
`%TEMP%/solstice-gpu-447-targeted.txt`, `solstice-gpu-447-strokes.txt` and
`solstice-gpu-447-regressions.log`. The scalar F16 reference is guarded by
`HAVE_OPENEXR` so it does not introduce a compile dependency when half color
support is absent; that configuration was not separately built here.

GPU, image and UI DLLs were installed after confirming no running application;
their build/install SHA256 hashes match. Log:
`%TEMP%/solstice-gpu-447-install.log`. No application was started or stopped,
and preferences were not changed.

Benchmark: 2026-10-04, same hardware and fixture as phase 4.46 (128px,
1024-square F16, four layers/workers, 24 segments), now using Wash.
`KRITA_GPU_VALIDATION=0`, `KRITA_GPU_STROKE_REPEATS=5`, three fresh sequential
processes, no running application. Use the four `testHalfStroke:` rows
`erase{0,1}-wash1-selected-locked-mirrors{0,1}`. Median of the three process
medians, milliseconds, including generation, scheduling, final merging and
GPU completion; excluding tablet input, canvas and verification reads:

| Wash stroke | CPU | GPU projection | GPU projection + brush |
| --- | ---: | ---: | ---: |
| Normal | 15.2945 | 12.3001 | 10.7169 |
| Erase | 16.1339 | 11.8975 | 10.1295 |
| Normal, selection + Alpha Lock + both mirrors | 45.8234 | 34.0871 | 15.7167 |
| Erase, selection + Alpha Lock + both mirrors | 47.4446 | 35.9930 | 13.7794 |

CPU ranges across processes: 14.67-16.31, 15.58-16.47, 45.55-46.34 and
46.18-48.39 ms. GPU projection+brush ranges: 10.15-11.69, 9.39-10.13,
15.60-16.06 and 12.22-14.32 ms. Erase ignores Alpha Lock, matching the CPU.
All three processes passed parity/path/Undo checks (six cases each).
Logs: `%TEMP%/solstice-gpu-447-bench-{1,2,3}.txt`. These results supplement
the F16 Buildup table; they do not replace F32 README timings or establish
input latency for arbitrary brush sizes.

Manual check: the user confirmed phases 4.47-4.48 passed. The checklist was:
in an RGBA16F document, paint Normal Wash with partial
opacity/flow, erase, use a soft selection and Alpha Lock, then Undo/Redo and
save/reopen. Check that pen release does not alter the preview unexpectedly.
Briefly verify Buildup and RGBA32F again. Large mirrored Alpha Lock tuning
remains explicitly deferred.

## RGBA16F basic blend brushes (phases 4.49-4.50)

F16 pixel brushes now accept Multiply, Screen, Addition/Linear Dodge,
Subtract, Darken, Lighten, Difference, Overlay, Hard Light, Exclusion,
Linear Burn, Linear Light and Pin Light: 14 action IDs, 13 distinct operations.
Both Buildup dabs and Wash preview/final merging use GPU composition.
Normal/Erase/Alpha Darken support from phases 4.46-4.48 is retained.
Extended modes beginning with Soft Light remained CPU brush paths at this
phase; phases 4.51-4.52 below extend the selected major modes.
F32 support and the existing owning-float-image, profile, LOD, wrap and
staging-budget restrictions are unchanged.

The F16 dab shader now dispatches basic generic modes through the existing
half-aware `compositeGeneric`, masking off the explicit-flags bit used only
by scalar Normal, and stores half after each dab. Wash accepts a soft mask
for one basic generic F16 layer. Its `halfBrush` descriptor flag remains
exclusive to Normal/Erase, whose scalar formulas differ from projection.
Generic modes preserve the CPU's half alpha tolerances, channel locks and
hidden RGB handling.

The new repeated-dab test exposed an existing half Exclusion mismatch:
`CFExclusion` rounds `Arithmetic::mul<half>` before widening it for the
remaining calculation. The shader now rounds the product at the same point,
also correcting F16 Exclusion layer composition. Six initial direct-dab
cases failed at 0.00146484; the fix passes the existing 1/1024 tolerance
without loosening it. F32 Exclusion is unchanged.

Coverage is limited to these major modes:

- `testHalfDabs`: 544 rows including Normal/Erase and the 14 new IDs, empty
  flags and all 16 explicit channel masks, masked/unmasked HDR dabs, half-alpha
  fuzzy boundaries, hidden RGB, odd source sizes, shared snapshots, exact
  Undo/Redo and failed-submit CPU replay. The 1/1024 bound includes hidden RGB.
- `testPendingBatches`: six added queue-gated half Multiply/Screen/Overlay
  rows verify in-flight staging ownership, deferred readback and ring reuse.
- `testHalfWashBlendModes`: 574 rows across all channel masks, soft/inverted/
  translated/empty/outside selection, preview/merge submission failure,
  low budget, unaligned source and integer owning-image fallback. Existing
  0.002 combined dab/merge tolerance and exact Undo/Redo checks are retained.
- `testHalfBlendModes`: 84 full-stroke rows (14 modes, Buildup/Wash, channel
  masks 15/7/5), with selection and both mirrors. CPU, projection-only and
  projection+brush paths are compared, with GPU dab/preview/merge counters
  required. Existing layer/projection bounds remain 0.002/0.004.

The focused dab/queue/Wash run passed 1140/1140 cases with Vulkan validation
enabled; its maximum direct-dab error was 0.00048828125. The new complete
stroke fixture passed 86/86 cases (84 data rows plus init/cleanup) in 163.97
seconds. Logs: `%TEMP%/solstice-gpu-449-targeted.txt` and
`solstice-gpu-449-strokes.txt`.

Final regression: all 10/10 GPU/interop/paint-device/projection/brush/canvas/
save/brush-jobs/complete-stroke/dab-queue CTest suites passed with Vulkan
validation enabled (375.70 seconds). The complete stroke suite now takes
246.96 seconds, so this run used `--timeout 420` rather than 240. Log:
`%TEMP%/solstice-gpu-449-regressions.log`. Final build log:
`%TEMP%/solstice-gpu-449-final-build.log`.

Representative timing on 2026-10-04, same hardware as phase 4.46: 128px
brush, 1024-square RGBA16F, four layers/workers, 24 segments, soft selection
and both mirrors, all channels enabled. Run `testHalfBlendModes:` rows
`multiply-wash0-channels15` and `overlay-wash1-channels15` with
`KRITA_GPU_STROKE_REPEATS=5`, `KRITA_GPU_VALIDATION=0`. Three fresh sequential
processes with alternating path order; medians of the three process medians:

| Stroke | CPU | GPU projection | GPU projection + brush |
| --- | ---: | ---: | ---: |
| Multiply Buildup | 69.2854 ms | 41.5280 ms | 12.1696 ms |
| Overlay Wash | 69.0667 ms | 55.6355 ms | 15.2692 ms |

CPU process-median ranges were 67.65-71.79 and 66.60-70.56 ms; projection-only
39.27-56.19 and 53.08-56.28 ms; GPU brush 11.46-12.57 and 14.86-15.38 ms.
All three processes passed path/parity/Undo checks (four cases each).
Timing includes generation, scheduling, final merge and completed GPU work;
tablet input, canvas display and verification reads are excluded. These are
representative cases, not a speed guarantee for all modes or brush sizes.
Logs: `%TEMP%/solstice-gpu-449-bench-{1,2,3}.txt`. Existing F32 README timings
are a separate workload and are not replaced by these F16 measurements.

GPU, image and UI DLLs were installed after confirming the application was
closed; build/install SHA256 hashes match. Log:
`%TEMP%/solstice-gpu-449-install.log`. No application preferences changed.

The user confirmed the real-app check passed: RGBA16F Multiply, Screen and
Overlay in Buildup/Wash, selection, Alpha Lock, Undo/Redo and save/reopen.
The deferred large mirrored Alpha Lock performance case is outside this bundle.

## RGBA16F extended major blend brushes (phases 4.51-4.52)

F16 Buildup and Wash now also accept Soft Light (SVG), Color Dodge, Color
Burn and HSY Color, Hue, Saturation and Luminosity. This adds seven major
mode IDs to the preceding 14. Other extended modes remain CPU brush paths;
in particular Soft Light Photoshop, Divide and the separately named HDR or
HSI/HSL/HSV modes are not enabled by this bundle.

`paint_dabs_extended_rgba16f` compiles the extended shader with `TILE_F16`.
`KisGpuDabCompositor` creates its pipeline lazily, independently of the basic
F16 and both F32 pipelines. The low-level dab gate admits only the selected
major modes. `kisGpuSupportsHalfBrushBlend` shares the painter/Wash coverage
gate; Normal/Erase retain their separate scalar half-brush descriptor flag.
Per-dab storage rounding, upload budgets, asynchronous ownership and CPU
fallback are unchanged.

The first CPU comparison caught 42 failures in Dodge/Burn dabs and queued
Dodge batches (largest direct error 0.00585938). CPU `Arithmetic::inv<half>`
rounds before division; Burn also rounds `clampToSDR<half>` before inverting
the quotient. The F16 shader now reproduces those intermediate values in
`composite_blend.glsl`, including layer composition. F32 arithmetic is
unchanged. No comparison tolerance was relaxed.

Validation-layer checks:

- `testHalfDabs`: 782 rows (Normal/Erase plus 21 major mode IDs, default or
  each of 16 channel masks, with/without selection), 24 overlapping odd-size
  dabs, negative origins, HDR colors, alpha boundaries, hidden RGB, retained
  snapshots, failed-submit CPU replay and exact Undo/Redo. Maximum error was
  0.0009765625, within the existing 1/1024 bound.
- `testHalfWashBlendModes`: 861 rows, covering all channel masks, selected
  and unselected previews/final merges, inverted/translated/empty/outside
  selections, failed submit/merge, merge budget, offset and integer fallback.
  The existing 0.002 bound and exact Undo/Redo checks are retained.
- `testPendingBatches`: 26 F32/F16 rows, including added F16 Soft Light SVG,
  Dodge and Color read/wrap cases. Queue gating proves source/mask ownership
  before GPU completion and correct ring reuse.

The targeted run passed 1,671 cases including init/cleanup. Log:
`%TEMP%/solstice-gpu-451-targeted2.txt`; the preceding failing run is retained
as `solstice-gpu-451-targeted.txt`.

The full validation-layer regression passed all ten suites in 477.19 s:
Engine, GL interop, paint device, projection, brush, canvas upload, save,
dab queue, brush jobs and complete strokes. `KisGpuStrokeTest` includes 126
F16 generic-mode data rows: 21 major IDs x Buildup/Wash x channel masks
15/7/5, using a soft selection and both mirrors. GPU path counters, layer
parity (0.002), projection parity (0.004) and exact layer Undo/Redo remain
required. Log: `%TEMP%/solstice-gpu-451-regressions.log`.

Representative timings on October 4, 2026, use the preceding 128px F16
fixture (1024-square document, four layers/workers, 24 segments, soft
selection, both mirrors and all channels), validation off, five samples
after warmup in each of three fresh sequential processes. Values are the
median of process medians; parentheses show their ranges, not percentiles.

| Stroke | CPU only, ms | GPU projection, CPU brush, ms | GPU projection + brush, ms |
| --- | ---: | ---: | ---: |
| HSY Color Buildup | 107.501 (104.781-111.501) | 101.301 (100.955-105.960) | 12.611 (11.239-13.586) |
| Soft Light SVG Wash | 69.205 (68.546-70.331) | 55.250 (51.952-55.408) | 15.377 (14.742-17.047) |

All three processes passed parity, path and Undo checks (four cases each).
Timing includes completed GPU work, brush generation, scheduling and final
merging; input, canvas display and verification readback are excluded.
Logs: `%TEMP%/solstice-gpu-451-bench-{1,2,3}.txt`. These additional F16 cases
do not replace the different F32 README workloads or establish input latency.

The final GPU, image and UI DLLs were installed with the application closed;
build/install SHA256 hashes match. Logs: `%TEMP%/solstice-gpu-451-final-build.log`
and `%TEMP%/solstice-gpu-451-install.log`. No application settings changed.

The user confirmed the real-app check passed: RGBA16F Soft Light (SVG),
Color Dodge/Burn and Color/Hue in Buildup/Wash, including selection,
Alpha Lock, Undo/Redo and save/reopen. The deferred large mirrored Alpha
Lock performance case is outside this bundle.

## Projection context reuse (phase 4.53)

The previous free-context stack immediately selected the last submitted
context again. `commands.begin()` then waited even when another free context
had already completed; serial projection/Wash calls could not retain multiple
submissions in flight. `KisGpuProjectionCompositor` now tracks each context's
last successful timeline value and selects the oldest available context.
Completed work is reused immediately. If all available contexts are pending,
it may lazily create up to three contexts; after that it waits for the oldest.

As before, concurrent callers lease separate contexts and may grow the pool
when every existing context is leased. Thus the count is bounded by the
larger of three and peak simultaneous leases, not by queued updates or
thread lifetime. Both F16/F32 compositors and their table/mask allocations
remain owned by their context. This is a count bound, not a new global GPU
memory budget; large retained table allocations still have their existing
per-context lifetime.

Vulkan resource creation and completion waits run outside the pool mutex.
An explicit successful wait is required before reusing command/table memory;
a failed wait returns failure without resetting commands or overwriting
tables. Submission failure leaves `lastUse` zero after the preceding work
has completed, allowing CPU replay through the existing transaction path.
Tile residency, deferred upload/source lifetime and queue dependency barriers
remain unchanged. The brush upload ring and its 64 MiB limit are unchanged.

`KisGpuBrushTest::testProjectionWorkContexts` adds 12 queue-gated cases:
F32/F16 x unlocked/Alpha Lock x CPU read/fourth submission/failed fourth
submission. Three composites must return with the queue blocked, despite
source, descriptor and coverage storage being destroyed between calls.
The fourth waits without growing the serial pool or holding its mutex.
The tests compare CPU output, check one-time CPU replay after failure,
retained snapshots, exact Undo/Redo, completed-context reuse and test-pool
reset. A watchdog opens the queue on regression instead of hanging the suite;
lease-count polling avoids relying on CPU scheduling to enter the fourth call.

The initial queue-gated run passed all 14 cases including init/cleanup
(`%TEMP%/solstice-gpu-453-contexts.txt`). With explicit lease polling added,
the full validation-layer regression passed all ten suites in 493.00 s:
Engine, GL interop, paint device, projection, brush, canvas upload, save,
dab queue, brush jobs and complete strokes. This includes the existing
concurrent projection checks and all 126 F16 major blend-stroke conditions.
Log: `%TEMP%/solstice-gpu-453-regressions.log`.

Before/after timings use the same local hardware on October 4, 2026,
validation off, five samples after warmup in each of three fresh sequential
processes per version. The before series preceded the implementation; the
after series followed the regression run, so this was not an alternating
A/B comparison. Values are medians of process medians, in milliseconds:

| Stroke | CPU before / after | GPU projection before / after | GPU projection + brush before / after |
| --- | ---: | ---: | ---: |
| F32 64px Normal Wash | 5.521 / 5.672 | 9.334 / 8.906 | 10.753 / 10.608 |
| F32 256px Normal Buildup | 7.901 / 8.072 | 12.300 / 12.310 | 10.743 / 10.550 |
| F16 128px Soft Light SVG Wash, selection + mirrors | 68.885 / 74.361 | 55.779 / 54.327 | 13.658 / 13.794 |

The GPU projection+brush process-median ranges overlap: 10.624-11.550 versus
10.467-11.644 ms (64px Wash), 10.715-11.050 versus 10.464-11.821 ms (256px
Buildup), and 12.897-15.002 versus 13.118-16.828 ms (F16 Wash). CPU-only
timings also drifted. These measurements do not establish a substantial
complete-stroke speedup; the deterministic gate test establishes the removed
serialization and safe bounded reuse. Input and canvas display are excluded.
All six benchmark processes passed their parity/path/Undo checks (five cases
each). Logs: `%TEMP%/solstice-gpu-453-{before,after}-{1,2,3}.txt`.

Image, GPU and UI DLLs were installed while the application was closed;
build/install SHA256 hashes match. Log: `%TEMP%/solstice-gpu-453-install.log`.
No application preferences changed.

The user confirmed the real-app check passed for this bundle. Large mirrored
Alpha Lock tuning remains explicitly deferred.

## Batched CPU filter and FFT readback (phases 4.54-4.55)

`KisFilter::process` now calls `KisGpuTileAccess::syncToCpu` for the source's
`neededRect` before composition-source conversion or CPU filter iteration.
When a temporary device is copied back, it also batches the destination's
`applyRect` before the selected/different-destination copy. These calls reuse
the existing bounded tile download hook and its failure fallback; they are
no-ops for devices without GPU state. Filter math, color conversion,
selection compositing and scheduling are unchanged.

`KisConvolutionWorkerFFT::fillCacheFromDevice` separately batches its actual
read region. FFT padding can extend beyond a filter's declared `neededRect`.
For repeat-border reads, the underlying horizontal iterator locks through
`dataRect.right()`, even beyond the requested cache width. The hook therefore
clamps the read start and vertical extent to the data bounds and includes
that row tail; ordinary iterators use the cache rectangle directly. This
does not expand the written region or change border arithmetic. Both hooks
are compiled only with `HAVE_KRITA_GPU_ENGINE`.

`KisGpuPaintDeviceTest::testCpuFiltersBatchReadback` adds 24 data rows:
F32/F16 x actual Invert/Gaussian Blur x in-place/unselected,
in-place/fractional-selection, separate-destination/fractional-selection x
normal/injected failed download. Fixtures contain HDR pixels, negative
coordinates, device offsets, partial tile boundaries, retained snapshots,
exact Undo/Redo, and a subsequent upload. Finite image default bounds are
required by Gaussian's repeat-border convolution: an initial fixture omitted
them and triggered `kis_convolution_painter.cc:151`. That test fixture was
fixed; no application assertion was suppressed.

The selected fixture uses coverage 1..254. Existing F32 CPU Copy SIMD and
scalar paths can differ in HDR clamping at exactly opaque coverage (255),
depending on alignment and neighboring coverage. This was observed before
the new hooks as well; it is not fixed by this transfer change. The fixture
avoids that separate arithmetic discrepancy without relaxing tolerances.

The final targeted validation-layer run passed all 26 cases including
init/cleanup, with maximum CPU-reference error zero in all 24 data rows.
Invert readback submissions decreased from 12 to 1 for in-place processing
and from 24 to 2 for the separate selected destination. Final Gaussian
in-place processing uses at most two submissions, including the FFT row
tail. These are transfer counts for this fixture, not elapsed-time or input
latency benchmarks. The initial baseline run was interrupted by the missing
default-bounds fixture and is not a complete Gaussian baseline.
Logs: `%TEMP%/solstice-gpu-454-before.txt` and
`%TEMP%/solstice-gpu-454-targeted3.txt`.

All six validation-layer regression suites passed in 51.84 s: convolution
painter, filter, GPU paint device, projection, canvas upload and save.
Log: `%TEMP%/solstice-gpu-454-regressions.log`. The image DLL was installed
with the application closed and its build/install SHA256 hashes match.
Install log: `%TEMP%/solstice-gpu-454-install.log`. Application preferences
were not changed.

Manual check: on RGBA16F and RGBA32F GPU-backed layers, apply Invert and
Gaussian Blur with and without a feathered selection; check the preview,
final result, Undo/Redo, subsequent painting and save/reopen.

The user confirmed the real-app check passed on October 4, 2026.

## Batched affine transform and layer-flip readback (phases 4.56-4.57)

`kis_transform_worker.cc` now prefetches existing GPU tiles before CPU
processing in `runPartial` and `mirror_impl`. Full `run` and the centered
`mirrorX`/`mirrorY` helpers also prefetch before `exactBounds`, preventing
their edge scan from downloading tiles individually. A repeated hook only
inspects residency; already current CPU tiles are not downloaded again.
All hooks use `KisGpuTileAccess::syncToCpu` under `HAVE_KRITA_GPU_ENGINE`.

The full existing device extent is intentional: affine transforms finish
with `purgeDefaultPixels`, whose existing CPU implementation inspects all
allocated tiles even for partial transforms or a simple translation. The
prefetch batches that existing read set instead of adding a new read region.
Mirror operations process the device's content bounds. No interpolation,
border behavior, position rounding, mirror axis, clearing, transaction or
purge semantics changed. Canvas mirroring and mirror brush dabs are separate
paths. Puppet Warp, perspective, cage and Liquify implementations are untouched.

`testCpuTransformsBatchReadback` adds 44 rows: F32/F16 x 11 operations x
normal/failed initial download. Operations include bicubic scaling, two-axis
shear, 90-degree/arbitrary rotation, translation, both explicit half-pixel
mirror axes, both centered flips, full `run` and partial-region scaling.
The fixture uses negative coordinates, a device offset, HDR/translucent
pixels and an asymmetric patch. Comparisons require exact bytes over a
region larger than both input and output, identical bounds, retained
snapshot bytes and exact Undo/Redo. The existing fallback retries individual
downloads after an injected batch failure and must preserve all pixels.

The initial seven-operation baseline issued 16 readback submissions per
normal operation (four by four resident tiles). The transfer-count assertion
failed before the hooks. The final 11-operation matrix requires exactly one
submission in every normal row, and passed all 46 cases including
init/cleanup, with exact CPU parity in all 44 data rows. This is a submission
count measurement, not a claimed wall-clock speedup or input-latency result.
Logs: `%TEMP%/solstice-gpu-456-before.txt` and
`%TEMP%/solstice-gpu-456-targeted2.txt`.

All five validation-layer regression suites passed in 56.85 s: transform
worker, GPU paint device, projection, canvas upload and save. No Vulkan
validation errors were reported. Log: `%TEMP%/solstice-gpu-456-regressions.log`.
The image DLL was installed with the application closed; build/install
SHA256 hashes match. Log: `%TEMP%/solstice-gpu-456-install.log`. Application
preferences were not changed.

Manual check: in RGBA16F and RGBA32F, paint on a GPU-backed layer, then scale,
rotate, move and flip that layer horizontally/vertically. Check partial
selections, Undo/Redo, subsequent painting and save/reopen. Use actual layer
flips rather than canvas-only mirror view for this check.

## Closed Transform Tool Undo investigation (October 4, 2026)

The user initially reported that horizontal flip followed by 90-degree
rotation and Ctrl+Z skipped the intermediate flipped image. A subsequent
real-app check explicitly without Apply/Enter confirmed that one Ctrl+Z
correctly restores the flipped image. This closes the reported Undo concern;
it does not establish completion of every 4.56-4.57 manual check above.

`KisGpuPaintDeviceTest::testTransformSequenceUndo` checks two committed
transactions (flip, then rotate) in F32/F16, with/without GPU authority
restored inside the second transaction. All four variants preserve both
intermediate images through two Undo and two Redo operations. This does not
exercise the active tool's internal history or Ctrl+Z routing. An early
fixture performed an untracked GPU write between transactions, violating
the memento sequence; it was corrected to write inside the second transaction.
Final log: `%TEMP%/solstice-transform-undo-test4.txt` (no assertion warnings).

Temporary `KRITA_TRANSFORM_UNDO_DEBUG` diagnostics recorded tool-local
commits/restores, lifecycle and Windows module-relative call stacks. The
first trace showed Undo arriving after stroke completion with empty tool
history (`%TEMP%/solstice-transform-undo-debug-first.log`). The second trace
(`%TEMP%/solstice-transform-undo-debug.log`) identified both completion stacks as
`QWidgetWindow::handleMouseEvent -> QAbstractButton::mouseReleaseEvent ->
QDialogButtonBoxPrivate::handleButtonClicked -> QDialogButtonBox::clicked ->
KisToolTransform::slotApplyTransform -> endStroke`. Plugin relative PCs
0x4249e/0x43c06 and Qt Widgets PCs 0x2109f2/0x13ec82 were resolved against
the matching DLLs using `llvm-symbolizer --relative-address`. These are Apply
button mouse events, not GPU waits or Ctrl+Z completing the stroke.

The trace also starts the next rotation with the previous negative X scale:
the established `tryFetchArgsFromCommandAndUndo` continuation path reuses and
overrides the previous transform command, explaining why document Undo then
removes the combined transform. The later explicit no-Apply check confirms
that tool-local Undo retains the intermediate state. No GPU regression or
required history behavior change was established. Temporary diagnostics have
been removed; the sequence regression test remains. Application preferences
and `kritarc` were not changed.

The plugin without diagnostics was rebuilt and installed with the application
closed; build/install SHA256 hashes match. Logs:
`%TEMP%/solstice-transform-undo-clean-{build,install}.log`.
