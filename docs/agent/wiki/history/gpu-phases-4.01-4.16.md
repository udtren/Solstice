---
type: history
topic: gpu-engine
status: archived record (moved from docs/agent/gpu-engine.md on 2026-10-07)
sources: [docs/agent/gpu-engine.md, git history]
---

# GPU engine history: pixel brush and Wash (phases 4.1-4.16)

GPU brush prototype, selections, channel locks, mirrors, Wash preview and final merge. Moved verbatim from `docs/agent/gpu-engine.md`; current state, decisions and
invariants stay there ([gpu-engine.md](../../gpu-engine.md)). Each section records what was true
when it was written; later sections and the current document take precedence.

## Pixel brush prototype (phase 4.1)

`KisGpuBrushPainter` (`libs/image/gpu/`) is the gate and tile integration;
`KisGpuDabCompositor` plus `shaders/paint_dabs.comp` (`libs/gpu/`) copies
rendered fixed-device dabs into a host-visible buffer and composites each
destination pixel in dab order. CPU mask generation and brush option
processing in `KisDabRenderingExecutor` are unchanged. This is the first
part of phase 4, not a replacement for the entire brush engine.

Enable only for development with `KRITA_GPU_BRUSH=1`, alongside the GPU
engine. `KRITA_GPU_BRUSH_DEBUG=1` logs the first 20 candidate checks and
the first three successful batches for each mode/selection/channel-mask/mirror/
combined-submission combination. No preferences are changed. Supported: RGBA32F,
Normal/Over or Alpha Darken, matching dab/device color spaces, LOD 0, and
no wrap-around. Mirror painting can combine reflected passes (phase 4.5).
Normal supports channel flags;
Alpha Darken requires all channels enabled.
Buildup and indirect/Wash can use the prototype. For Wash, the dab painter
writes an unrestricted temporary target; selection, final composite mode,
stroke opacity, and channel locks are applied by the existing CPU final
merge. Direct painting supports selection coverage (phase 4.2 below), and
Normal supports alpha/channel locks (phase 4.3). F16,
other dab composite modes, and color smudge remain future work.

Alpha Darken follows `KoOptimizedCompositeOpAlphaDarken128`, including flow
and average opacity. The process-wide `useCreamyAlphaDarken()` choice is
shared with the CPU color-space registration: Creamy uses the unscaled
opacity/average and destination alpha for zero flow; Hard scales opacity/
average by flow and uses union alpha for zero flow. The shader takes the
same normalized opacity/average pair as `KoCompositeOp::ParameterInfo`.
No configuration is changed. Diagnostics include composite mode and shader
variant (0 Normal, 1 Hard, 2 Creamy), selection presence, and a channel bitmask
(RGBA = bits 0 through 3, 15 = all enabled, 7 = alpha locked), plus mirror
flags (1 horizontal, 2 vertical, 3 both), and `passes` (1 for separate
submissions, 2 or 4 for a combined submission).

`plugins/paintops/defaultpaintops/brush/kis_brushop.cpp` queues the entire
supported dab batch as a sequential job. Never call this GPU path from the
existing concurrent rectangle jobs: distinct rectangles may share a tile.
The brush op also requires its owning image to be RGBA32F; a float paint
layer inside an integer image stays on the CPU because that image does not
use the GPU-aware merge scheduling.
Unsupported brushes retain those original jobs, including mirroring.
If preparation or submission fails, the sequential job paints the original
rectangles on the CPU. If submission succeeded, never replay the batch on
the CPU after a failed wait: that would apply the stroke twice; existing
tile readback/content-loss handling reports lost content. Dirty rectangles,
average opacity, and stroke update metrics retain their existing reporting.

One process-lifetime work context serializes uploads/submissions and waits
before reusing its buffer. Source/table storage is capped at 64 MiB; shared
fixed-device pixels are uploaded once per batch. Separate passes exceeding
4096x4096 bounding area or conservatively 8192 tiles use the CPU. Combined
mirror passes use sparse tiles with a 4096-tile cap (phase 4.10); selection
snapshots still require a bounding area no larger than 4096x4096.
The tile access is ReadWrite, so partial tiles, device offsets, COW snapshots,
and memento registration use the existing GPU tile backend. No changes to
the generic `KisPainter::bltFixed` API or other paint ops are made.

`KisGpuBrushTest` covers negative/device offsets, nonzero fixed-device
origins, repeated source storage, overlapping HDR dabs and varying opacity,
copy isolation, Undo/Redo, later CPU painting, rejected malformed channel
flags/restricted Alpha Darken/modes/integer color space, failed submit,
and insufficient tile budget.
Both supported modes run the Undo/CPU-write and failure tests. Twenty-four
Alpha Darken rows directly compare the two real optimized CPU implementations
with the shader, without editing settings: flow 0/intermediate/1, zero/full/
partial opacity, rising/falling average opacity, transparent/opaque/partial
destination alpha, negative/HDR colors, and overlapping dabs. The indirect
painting tests leave the temporary target CPU-stale until the actual
`KisIndirectPaintingSupport::mergeToLayer` reads it; plain and selection plus
alpha-lock cases check layer pixels, target release, and Undo/Redo.
Parity is within 2e-5 per channel, ignoring RGB only when both alphas are
exactly zero (the established projection criterion). The CPU's SIMD and
scalar Over code itself differs for invisible RGB on transparent pixels.

Initial measurement on RTX PRO 6000 Blackwell: 32 dabs of 256x256, warmed
destination/pipeline, five completed batches averaged 9.51 ms CPU and
1.85 ms GPU, including copying the dab pixels. This compares the serial
CPU batch API, not the existing parallel stroke pipeline. It excludes dab
generation, event scheduling, and display latency; it does not establish
lower input-to-pixel latency or completion of phase 4's exit criteria.

Validation on 2026-10-03: all seven GPU ctest suites passed with validation
enabled, plus `KisDabRenderingQueueTest` and `kis_painter_test`. The queue
test passed again after adding the owning-image gate. New brush test:
Initially 5 cases including init/cleanup. Installed `kritagpu`, `kritaimage`,
`kritaversion`, `kritalibpaintop`, and the `kritadefaultpaintops` plugin;
build/install SHA-256 hashes match. The user subsequently confirmed Normal
painting, Undo/Redo and save/reopen. The real application log contained
20 successful GPU batches; its earlier Alpha Darken candidates still used
the CPU in that first version.

Alpha Darken follow-up validation on 2026-10-03: `KisGpuBrushTest` now has
33 passing cases including init/cleanup at that stage. All seven GPU suites passed with
Vulkan validation enabled, followed by `KisDabRenderingQueueTest` and
`kis_painter_test`. Reinstalled the shared libraries and default paint-op
plugin listed above; all five build/install DLL hashes match. The user then
confirmed Wash operation. The real application log contains 20 completed
`alphadarken` GPU batches with variant 2 (Creamy); there are no matching
validation-error, device-loss, assertion, or GPU-engine-stop messages.

Alpha Darken regression checklist: use the same brush prototype launcher,
RGBA32F and a pixel brush in Wash mode, no mirroring, instant preview
disabled. Check `GPU brush: batch ... mode "alphadarken"` in the log,
then vary flow/opacity and pressure, confirm appearance while drawing and
after releasing the pen, Undo/Redo, and save/reopen. Also check a selection
and alpha lock through Wash's final merge, then Normal/Buildup as a control.
Only the user launches and closes Krita. Next steps: full stroke parity/
latency, F16 and masks, then GPU dab generation.

## Selection coverage for pixel brushes (phase 4.2)

The direct dab painter now accepts a selection. `KisGpuBrushPainter` clips
the batch's tile-access bounds to `selection->selectedRect()` and reads
`selection->projection()` into one contiguous 8-bit mask, matching
`KisPainter::bltFixed`. Empty intersections are successful no-ops without
a submission. The usual area/tile-count limits apply before allocating the
mask snapshot. Vector selections use their existing raster projection;
selection generation and updates remain on the CPU.

`KisGpuDabCompositor` copies that mask into its existing upload buffer,
charged to the same 64 MiB source/table limit. The shader reads four packed
bytes per uint; the last partial word is padded. Pixels outside the mask
rectangle are untouched. Normal applies coverage after dab opacity; Alpha
Darken applies it to source alpha before opacity, matching their respective
CPU implementations. No additional Vulkan storage feature is required.
Stroke sequencing, selection lifetime, and CPU fallback are unchanged.

Regression coverage includes Normal and Alpha Darken against the actual
CPU painter, all 256 coverage values, inverted selections with a nonzero
default pixel, translated selection and paint-device origins, empty and
nonintersecting selections, pixels outside the selection, Undo/Redo,
injected submission failure followed by CPU fallback, and deselection
without retaining the previous upload's mask. The 24 Alpha Darken parameter
rows also run with masks against both optimized CPU implementations. The
low-level test uses an odd mask byte count to cover the padded word read.

Validation on 2026-10-03: the brush suite now has 66 passing cases including
init/cleanup. All seven GPU suites passed with Vulkan validation enabled,
as did `KisDabRenderingQueueTest` and `kis_painter_test`. Installed the five
DLLs listed in phase 4.1 and verified their build/install SHA-256 hashes.
For 32 256x256 Normal dabs, five warmed completed batches averaged 10.07 ms
CPU / 1.77 ms GPU with a varying selection, and 10.12 / 1.80 ms without it.
GPU timing includes mask readback/upload and completion wait. These are
serial batch API measurements, not full stroke or input-to-display latency.

Manual follow-up: in the same launcher, use an RGBA32F document and a Normal
pixel brush in Buildup mode. Draw across a soft selection edge, invert the
selection and paint again, then deselect. Check Undo/Redo and save/reopen.
Check that `GPU brush: batch` messages include `selection true`; Wash applies its selection at final merge and
does not exercise this direct-mask path. Keep mirroring and instant preview
disabled. The user starts and closes Krita.

The user reported correct operation after phase 4.2. Its first-20-batches
log contained only `alphadarken / selection false` (Wash temporary-target
painting), so that log does not establish use of the direct-mask path.
Phase 4.3 changes the diagnostic cap to three messages per distinct path,
allowing later Normal/selected/locked batches to be observed in one run.

## Normal brush channel locks (phase 4.3)

Normal dabs now accept all 16 RGBA channel-flag combinations, with or without
a selection. The flags are passed in the existing push-constant block.
The shader follows `KoOptimizedCompositeOpOver128`'s scalar restricted-channel
kernel, which differs from generic projection blending: alpha lock preserves
destination alpha and blends enabled RGB channels with masked source alpha.
With alpha unlocked, partial RGB flags, and destination alpha exactly zero,
the CPU clears hidden RGB before applying the enabled channels; the GPU does
the same. A zero source alpha leaves every channel untouched. Alpha Darken
with restricted flags still uses the CPU; its optimized CPU kernel ignores
those flags and normal Wash applies the actual locks at final merge.

`testChannelLocks` runs all 16 masks with/without varying selection coverage
against the CPU, including transparent/partial/opaque alpha, hidden nonzero
RGB, HDR/negative colors, device offsets, COW isolation, Undo/Redo, later CPU
painting, and injected submit failure for selected alpha-locked painting.
For restricted channels, parity checks include hidden RGB (the CPU scalar
kernel defines it). Locked alpha and preserved channels are also compared
exactly to their previous values. Benchmarks retain serial-batch scope.

Validation on 2026-10-03: 100 brush cases passed, no skips; all seven GPU
suites passed with Vulkan validation enabled, plus the dab queue and CPU
painter suites. All five installed DLL hashes match their build outputs.
For 32 256x256 Normal dabs over a partially opaque background, warmed
alpha-locked batches averaged 3.65 ms CPU / 1.92 ms GPU; with selection and
alpha lock, 4.16 / 2.04 ms. These include upload/wait but exclude dab
generation and input-to-display latency. The user subsequently confirmed
operation. Its log contains Normal/selection false/channels 7 (alpha lock)
and Normal/selection true/channels 15, establishing both the direct alpha-
lock and selected GPU paths. Combined selected alpha lock and other RGB
flags are covered by automated tests but not separately identified in that
manual log. No validation-error/device-loss/assertion/engine-stop messages
matched in the log.

Manual follow-up: use RGBA32F, Normal blending, and the pixel brush's
**Painting Mode > Build up** setting in the brush editor. First paint content
on a transparent layer, then enable alpha lock and paint across its edge.
Repeat with a soft selection, then invert/deselect and unlock. Check Undo/
Redo and save/reopen. The log should contain `mode "normal"`, `channels 7`
for alpha lock and `selection true` for the direct selected path. Wash alone
does not exercise these direct-painter restrictions. Other RGB locks are
covered by unit tests; if available in the active UI, check them as well.

## Mirrored pixel-brush compositing (phase 4.4)

The separate-pass path in `KisBrushOp::addDabPaintingJobs` schedules the original and each reflected
pass through the same sequential GPU job, retaining the original concurrent
CPU rectangle jobs when unsupported. Reflection itself remains in the
existing CPU `mirrorDab` jobs with their before/after sequential barriers.
Consecutive dabs sharing a fixed device are still reflected only once.
Order is unchanged: original, horizontal, horizontal+vertical, vertical for
both axes; two passes for a single axis. Dirty rectangles, dab ownership,
average opacity, and update timing bookkeeping retain their original paths.
`KisGpuBrushPainter::paint` always composites exactly one supplied pass;
it does not automatically generate more reflections.

`mirrorRect` rounds the axis to an integer while `mirrorDab` uses the QPointF
axis. This can put an edge of a reflected dab outside the CPU paint rectangles.
For mirror painting, the GPU helper receives those rectangles and verifies
their QRegion union covers every dab within the effective selection bounds.
At this stage, incomplete coverage used the original CPU rectangle clipping;
phase 4.7 below replaces that fallback with GPU clipping. Do not
silently paint the full dab in this case or change the upstream rounding.
Per-pass submit failures similarly use CPU fallback; a following GPU pass
then uploads any changed CPU tiles normally. Wrap-around and LOD remain CPU.

`testMirroring` adds 26 cases: horizontal/vertical/both, Normal/Alpha Darken,
selection on/off, integer/fractional axes, shared consecutive source storage,
overlapping mirror passes, shifted destination, alpha lock, failed second-
pass submission, COW and Undo/Redo. CPU parity is checked after every pass.
Tests model the existing reflection and clipping APIs; actual brush job
scheduling and interactive input still need the manual check below.

Validation on 2026-10-03: 126 brush cases passed without skips; all seven GPU
suites plus the dab queue and CPU painter tests passed with Vulkan validation
enabled. One diagnostic measurement of 14 73x73 dabs over four mirror passes
was 0.82 ms CPU / 7.10 ms GPU (compositing only; excludes reflection and
display, includes GPU upload and waits). Small mirrored batches are slower
here; this is a coverage milestone, not a latency improvement. Investigate
submission batching and small-batch routing before default enablement.
The reflected-pass diagnostic was then added to the brush op; its rebuild
and queue test passed. All five installed DLLs match their build SHA-256
hashes. No preferences were modified and Krita was not launched by the agent.

Manual follow-up: use the usual launcher in RGBA32F. Test horizontal, vertical,
then both mirror axes with a Normal/Build up pixel brush; move the axes and
paint across their intersection. Also try Wash, a selection, and alpha lock.
Confirm reflected shapes and overlaps, Undo/Redo and save/reopen. Diagnostics
include `mirrors 1/2/3`; the brush op also logs the first 12 successfully
handled reflected passes as `GPU brush: reflected pass`. User launches and
closes Krita. All prototype changes remain opt-in via `KRITA_GPU_BRUSH=1`.

The user completed this check and closed Krita. The runtime log confirms
Alpha Darken reflected passes with horizontal, vertical and both axes, plus
Normal reflected passes with selection and alpha lock together. No validation
error, device-loss or assertion message was found in that log.

## Combined mirror submissions (phase 4.5)

Before the separate-pass path, the original sequential brush job now tries
`KisGpuBrushPainter::paintMirrored`. It constructs reflected positions with
the existing `mirrorDab(..., true)` API and checks each pass against its
`mirrorRect` coverage before preparing any destination tiles. Records remain
in original/H/HV/V order (two passes for one axis). Each record carries source
reflection bits; the shader reflects source coordinates without modifying
the CPU dab pixels. All passes share deduplicated source uploads and one
destination preparation, submission and wait.

A successful combined operation publishes `gpuMirrorsPainted` in that
sequential job. Later reflection and painting jobs skip their work after the
existing barriers. Dirty rectangles and final stroke bookkeeping are unchanged.
On refusal or an unsubmitted failure, the flag stays false and the existing
separate GPU/CPU passes run. A submitted write is never replayed after a wait
failure. This preserves the existing content-loss behavior.

In phase 4.5 the combined bounding rectangle had to be no larger than twice the sum of
the individual pass bounding areas, clipped to the selection. Widely separated
reflections therefore keep separate submissions instead of allocating blank
tiles between them. Existing area, tile-count and upload-size limits also
applied. Phase 4.10 replaces this density restriction with sparse destinations.
Fractional-axis clipping differences kept the original fallback until phase 4.7 below.

`testCombinedMirroring` reuses all 26 mirror cases, including source sharing,
selection, alpha lock, COW/Undo/Redo and submission failure. It checks source
bytes and positions remain unchanged, and successful combined painting uses
one submission. `testCombinedMirroringRefusal` covers distant axes and
low-budget preparation rollback followed by a successful separate pass.
The benchmark compares final CPU/GPU pixels as well as timing.

Validation on 2026-10-03: 155 brush cases passed without skips; all seven GPU
suites plus the dab queue and CPU painter tests passed with Vulkan validation
enabled. Separate timing with validation disabled, one warm-up and five timed
updates per path, on the RTX PRO 6000:

| Four-pass workload | CPU | Separate GPU | Combined GPU |
| --- | ---: | ---: | ---: |
| 14 dabs, 73 × 73 pixels | 0.932 ms | 1.171 ms | 0.459 ms |
| 32 dabs, 256 × 256 pixels | 45.545 ms | 15.733 ms | 1.874 ms |

These averages include reflection, upload and completed GPU waits, but exclude
dab generation, job scheduling and display. The CPU reference uses serial
painting APIs. This is not an input-to-pixel latency measurement and is not
directly comparable to the cold phase 4.4 diagnostic above.

All five installed DLLs match their build SHA-256 hashes after installation.
Krita was closed throughout installation and was not launched by the agent.

Manual follow-up: use the same launcher and RGBA32F pixel brushes. Paint near
the intersection of both mirror axes in Normal/Build up and Wash, then with
selection and alpha lock. Check overlap appearance, Undo/Redo and save/reopen.
Look for successful `passes 4` (or `passes 2` for one axis) batches in the log;
`passes 1` is expected when combining is refused. User launches and closes
Krita. Preferences remain unchanged.

The user subsequently confirmed correct operation and closed Krita. Its log
contains Alpha Darken `mirrors 1 / passes 2` batches, establishing combined
single-axis operation. Both-axis Normal/Alpha Darken batches in this run used
`passes 1`, including Normal with selection and alpha lock. This does not
establish interactive use of the four-pass combined path. No validation-error,
device-loss, assertion or engine-stop messages matched in this log.

## Brush job integration coverage (phase 4.6)

`KisGpuBrushJobsTest` lives beside the native pixel-brush tests and links the
actual brush op through `kritadefaultpaintops_static`. Each row runs three
updates with a programmatically defined, rotated elliptical auto brush, using
its real dab generation, cache, rectangle splitting, reflection and painting
jobs. A `KisRunnableBasedStrokeStrategy` dispatches these jobs through the
image scheduler with four worker threads. The CPU and GPU runs differ only
in the brush opt-in; no production test hooks or brush resources are changed.

Fifteen rows cover Normal/Alpha Darken with no, horizontal, vertical or both
mirrors; selected alpha lock; selected Alpha Darken; fractional-axis clipping;
distant mirrors; failed combined and single-pass submissions; and a float layer
owned by an integer image. Pixel parity is within 2e-5 including hidden RGB
on the partially opaque background. Dirty regions must match exactly. COW
snapshots and transaction Undo/Redo are checked after completed image updates.
Wait for image workers after transaction commands before reading CPU pixels;
those commands can schedule projection refreshes.

Exact brush submission counts distinguish paths: three for three nearby
updates even with four passes, twelve for distant both-axis painting, six
when the first combined submit fails and retries through separate passes,
two after one failed single-pass update, and zero for the integer owning
image. Together with CPU pixel parity these assertions catch skipped painting,
duplicate reflected painting and accidental GPU use outside the image gate.

This is job-level integration coverage, not a full `FreehandStrokeStrategy`,
tablet event, Wash final-merge or display-latency test. Wash final merging is
covered separately in `KisGpuBrushTest`; full input-to-pixel measurements,
GPU dab generation, RGBA16F brush compositing and color smudge remain open.
Validation on 2026-10-03: 17 integration cases including init/cleanup passed,
with no failures or skips. All eight GPU ctest suites passed with Vulkan
validation enabled; the integration suite reported zero validation errors.
No production DLL changed in this stage, so no additional interactive test
or installation is required.

## GPU clipping to CPU paint rectangles (phase 4.7)

`mirrorRect` and `mirrorDab` retain their original rounding. Instead of
requiring full dab coverage, `KisGpuBrushPainter` now intersects each dab with
the supplied CPU paint rectangles and effective selection bounds. Each
disjoint fragment becomes a record in the original dab/pass order. The
shader tests its document-coordinate clip before reflecting source coordinates;
the full dab origin and dimensions remain unchanged. Shared source pixels
are still uploaded only once, including across mirror passes and fragments.

The GPU record is now 64 bytes, including a 16-byte clip rectangle. A null
clip in the low-level API means the full dab. Host metadata is capped at
65,536 records before destination preparation; the existing upload, tile,
area and combined-pass density limits still apply. Empty intersections are
successful no-ops without submissions. Overlapping input rectangles use CPU
fallback because their CPU loop can paint the same pixel twice; a region
union would change that behavior. Production brush splitting supplies
disjoint rectangles. The debug batch log now includes `fragments`.

Twelve new brush rows cover disjoint strips with gaps, empty/outside painting
regions, Normal/Alpha Darken, soft selections, shifted devices, unchanged
pixels, COW, Undo/Redo and failed-submit rollback. Overlapping input rectangles
must be refused before modifying pixels. Existing separate/combined mirror
rows now require GPU success for fractional axes, and the actual brush-job
integration test requires exactly three submissions for its three fractional-
axis updates, including four mirror passes per update. CPU pixel parity is
checked within 2e-5; integration dirty regions must match exactly.

Validation on 2026-10-03: 167 brush cases passed without failures or skips.
All eight GPU ctest suites passed with Vulkan validation enabled, as did
the dab queue and CPU painter suites. All five installed DLL SHA-256 hashes
match their build outputs. The user subsequently confirmed correct operation
and closed Krita. The runtime log establishes combined both-axis painting
(`mirrors 3 / passes 4`) for Alpha Darken and Normal, including Normal with
selection and alpha lock together. It does not record exact axis coordinates;
fractional-coordinate coverage remains established by automated tests.
Four projection batches fell back to CPU because a CPU user held a tile lock.
No validation-error, device-loss, assertion or engine-stop messages matched
in the log.

Manual follow-up: in an RGBA32F document, enable both mirror axes and paint
near their intersection using Normal/Build up, then Wash. Move the axes,
repeat with a soft selection and alpha lock, and check reflected edges,
overlaps, Undo/Redo and save/reopen. Look for `mirrors 3 / passes 4` batches
in the usual brush debug log. Distant reflections can still use separate
passes. User launches and closes Krita; preferences remain unchanged.

## Queued stroke measurements and Wash readback batching (phase 4.8)

`plugins/paintops/defaultpaintops/brush/tests/KisGpuStrokeTest.cpp` runs actual
`FreehandStrokeStrategy` strokes with programmatic auto-brush presets. Eight
rows cover 64/256 px Buildup/Wash and 128 px both-axis mirrors, with/without
selection and alpha lock. Each uses a 1024x1024 RGBA32F image with four
partially opaque layers, four image workers and 24 queued line segments.
Three paths are compared: CPU only, GPU projection with CPU brush, and GPU
projection with GPU brush. Layer and projection pixels must match within
2e-5, including hidden RGB. Warm-up strokes also check Undo/Redo. Counters
assert the intended brush and projection paths were actually used.

Timing begins at stroke start, includes dab generation, worker scheduling,
stroke completion, Wash final merge and projection, and explicitly waits for
the Vulkan queue to finish. Image/preset setup and verification readbacks are
outside the interval. One warm-up per path precedes the samples; path order
alternates. `KRITA_GPU_STROKE_REPEATS=5` requests five measured samples (default
one, maximum 20). There is no canvas, tablet event delivery, event pacing or
screen presentation, so this is queued-stroke completion time, not interactive
latency. Do not use wall-clock timing as a test pass/fail threshold.

This exposed repeated per-tile downloads when GPU-painted Wash temporary
targets enter CPU preview/final compositing. `KisPaintLayer::copyOriginalToProjection`
and `KisIndirectPaintingSupport::writeMergeData` now call the existing
`KisGpuTileAccess::syncToCpu` for source and destination before CPU iteration,
only with the GPU engine enabled. Existing indirect-target locks, job barriers,
selection/channel handling and CPU compositing remain in place. Downloads
retain the backend's 256-tile batching and single-tile retry on allocation or
submission failure. No new setting or file format is introduced.

`KisGpuBrushTest::testIndirectMerge` checks real preview and final merge,
selection/alpha lock, Undo/Redo and a forced 64 KiB readback limit. Its regular
preview must download the 12 stale tiles in one submission, and final merge
must use at most one download per supplied rectangle. These assertions detect
regression to individual-tile waits without relying on timing. Under the
forced limit, individual-tile fallback must preserve CPU pixel parity.

Diagnostic measurements on 2026-10-03, RTX PRO 6000 Blackwell, validation off,
five samples after warm-up, medians in milliseconds:

| Wash workload | CPU before / after | GPU projection+brush before / after | GPU submissions before / after |
| --- | ---: | ---: | ---: |
| 256 px, no mirrors | 13.43 / 11.88 | 36.18 / 26.56 | 144 / 57 |
| 128 px, both mirror axes | 15.78 / 18.06 | 42.43 / 37.38 | 166 / 74 |

Scheduling and system load affect these small samples, as the unchanged CPU
baseline demonstrates. Submission reduction is the clearer result. GPU strokes
remain slower than CPU in this workload; this does not establish phase 4's
latency exit criterion. Next investigate remaining CPU/GPU crossings and
projection overhead, then paced input/display measurements and GPU dab
generation. Keep GPU brush painting opt-in.

Validation: all nine GPU ctest suites passed with Vulkan validation enabled.
The 17 layer, projection, merger, scheduler, walker, filter-mask and painter
suites also passed with GPU projection disabled and enabled. The new stroke
suite passed all eight rows plus init/cleanup without skips. All five installed
DLL SHA-256 hashes match the build outputs. No Krita process was running during
installation. The user confirmed correct operation, but reported lag with both
mirror axes in Wash; Buildup appeared unaffected. The log includes combined
four-pass and separate-pass Alpha Darken painting, and six projection fallbacks
for CPU-held tile locks. No validation-error, device-loss or assertion message
matched. It also contains Qt precise-timer fallback warnings; their effect on
interactive latency has not been isolated.

Build target: `KisGpuStrokeTest`; ctest name:
`plugins-paintops-defaultpaintops-brush-KisGpuStrokeTest`. For timing, disable
validation, set `KRITA_GPU_STROKE_REPEATS=5`, and run the executable with
`-o <temporary-output-file>,txt`. Retain the CPU/GPU path labels and timing
scope when reporting numbers.

Manual follow-up: with the usual GPU brush launcher, paint in RGBA32F using
Wash, including a large brush, both mirror axes, a soft selection and alpha
lock. Check that the stroke does not change appearance on release, then
Undo/Redo and save/reopen. Check Buildup as well. User launches and closes
Krita; no configuration is changed by the agent.

## GPU Wash preview (phase 4.9)

Wash still composites its temporary target into a separate layer projection
during painting. Phase 4.8 batched the downloads but still performed this blend
on CPU. `KisGpuBrushPainter::paintWashPreview` now uses the existing thread-safe
`KisGpuProjectionCompositor` for Normal RGBA32F without selection or restricted
channels. It remains behind `KRITA_GPU_BRUSH=1` and the GPU engine. Source
default alpha must be zero, since the compositor reads only the source extent;
color spaces and tile offsets must match the compositor's existing requirements.

`KisPaintLayer::copyOriginalToProjection` retains its original base copy and
indirect-target read lock. It attempts GPU blending only in an owning float
image, where merge jobs sharing destination tiles are exclusive. The helper
rejects LOD/wrap-around, unsupported blending, selections, alpha/RGB locks and
source/destination aliasing. Failure before submission leaves the existing CPU
blend available, with phase 4.8's batched readback. Success leaves preview pixels
on GPU for subsequent projection/canvas work; it does not wait. Onion-skin and
effect-mask processing retain their existing paths. Final Wash merging stays
on CPU. No settings are changed.

`washPreviewCount()` and the first three `GPU brush: Wash preview` debug messages
identify actual use. Full-stroke tests require this count to increase only for
unrestricted Wash with GPU brush enabled. Indirect merge tests compare against
an explicitly CPU-only preview, including submission failure, unaligned source,
integer owning image and limited readback memory, plus selection/alpha lock.

With validation disabled, five samples after warm-up on the RTX PRO 6000,
the isolated 1024x1024 preview of 12 256px dabs over four mirror passes measured
3.708 ms CPU blend versus 2.134 ms GPU blend (medians). Both paths receive fresh
GPU-painted temporary pixels; timing includes the base copy and completed GPU
wait, but excludes brush jobs and canvas. The complete queued mirror-Wash
stroke measured 31.31 ms in this run, versus 37.38 ms in phase 4.8; the CPU
control also changed from 18.06 to 14.37 ms, so do not treat the whole-stroke
difference as a precise speedup. These tests do not measure perceived latency.

Validation on 2026-10-03: all nine GPU suites passed with Vulkan validation;
the 17 layer/merger/projection/scheduler/walker/filter-mask/painter suites passed
with GPU projection disabled, and again with both GPU projection and brush
enabled. All five installed DLL SHA-256 hashes match the build. Krita was not
running during installation. The user subsequently confirmed improved response
near the mirror intersection, but lag at distant positions in both Wash and Buildup.

Manual follow-up: repeat the reported large Wash brush with both mirror axes,
first without selection or channel locks. Compare responsiveness while drawing
near and far from the mirror intersection, and check the appearance on release.
Then test selection/alpha lock, Undo/Redo and save/reopen. The log should contain
`GPU brush: Wash preview`. Buildup remains a control case. Further investigation
is needed if lag persists, including separate mirror submissions, CPU projection
crossings and timer/display scheduling.

## Sparse mirror destinations (phase 4.10)

The phase 4.9 manual check improved painting near the axes' intersection, but
both Wash and Buildup still lagged far away. Debug logs showed those distant
updates taking separate one-pass submissions. The combined path's density
guard rejected them to avoid allocating the large empty bounding rectangle.

`paintMirrored` now unions the clipped dab footprints on the destination
device's 64-pixel tile grid, including its offset. Each distinct tile is
prepared once through disjoint `KisGpuTileAccess` regions. The compositor's
16-byte destination records carry both a GPU address and document origin;
one workgroup owns each destination tile and processes dabs in the same
original/H/HV/V order. Empty gaps allocate no tiles. All regions share one
source upload, submission and wait. The ordinary rectangular compositor API
remains available. The debug batch message now includes the destination tile count.

Combined work is capped at 4096 destination tiles; upload/table/mask storage
retains its 64 MiB cap. Selection coverage still uses a dense snapshot capped
at 4096x4096 bounding area. Refusal retains the separate GPU/CPU passes.
Preparation or submission failure rolls back every prepared region together;
no submitted writes are replayed. The change remains behind the brush opt-in.

Regression coverage adds distant Normal/Alpha Darken cases with fractional
axes, shifted devices, selections, submission failure, COW and Undo/Redo.
The unmasked test spans over 20,000 pixels and asserts that allocated device
regions contain no gap tiles. A low-budget test fails after partial region
preparation. Real brush-job tests now require three submissions for three
distant four-pass updates instead of twelve. Complete queued-stroke tests add
distant Wash and Buildup with CPU layer/projection parity and Undo/Redo.

On 2026-10-03, all nine GPU suites and `KisDabRenderingQueueTest` passed with
Vulkan validation (10/10 suites); `KisGpuBrushTest` passed 179 cases. Validation
disabled, one warm-up and five measured updates on the RTX PRO 6000 gave:

| Distant four-pass update | CPU | Separate GPU | Sparse combined GPU |
| --- | ---: | ---: | ---: |
| 14 dabs, 73px | 0.858 ms | 0.972 ms | 0.324 ms |
| 32 dabs, 256px | 45.110 ms | 15.640 ms | 1.874 ms |

These averages include reflection/compositing and GPU completion, not dab
generation or canvas display. They compare both paths in the same binary.
The complete 128px distant stroke (five samples after warm-up, median) took
10.55/30.94/34.10 ms for Wash and 7.61/26.61/18.78 ms for Buildup, in CPU/GPU
projection/GPU projection+brush order. This is a new trajectory, not a direct
before/after whole-stroke comparison. GPU is still slower than CPU for these
short strokes; Wash preview/projection and display/input latency need further
investigation. Do not equate the isolated compositor speedup with perceived
latency or claim the reported lag is fixed before manual verification.

The GPU/image/version/paintop libraries were installed with Krita closed;
all five installed DLL SHA-256 hashes match the build. No configuration was changed.

Manual result: ordinary Wash and Buildup improved substantially; Alpha Lock
still lagged in both modes, specifically after pen release. The earlier check was to repeat the same RGBA32F brush with horizontal and
vertical mirror axes, drawing both near their intersection and far away in
Wash and Buildup. Check release, Undo/Redo and save/reopen. The brush debug log
should now show `passes 4` even for distant painting (within the documented
caps), together with a sparse tile count.

## Post-stroke speculative CPU clones (phase 4.11)

The user clarified that the remaining Alpha Lock delay affects both Wash and
Buildup after pen release, rather than primarily while drawing. Build up logs
already show Normal/channel mask 7 using combined GPU painting. Wash with
Alpha Lock still uses CPU preview/final blending, but that cannot explain
Build up by itself. The log also contains CPU-held tile projection fallbacks
and a 64 ms layer-thumbnail warning; neither is yet proven to account for
the complete interactive delay.

`KisMementoManager::commit()` wakes the background tile pooler. Its speculative
CPU COW preallocation used `blockSwapping()` on GPU-only shared tiles, forcing
a separate synchronous download per tile even though no CPU consumer needed
their pixels yet. The isolated regression reproduced 64 submissions and
4.648 ms for 64 shared GPU tiles; after the fix it took 0.0044 ms with zero
submissions (validation disabled, single diagnostic runs, not an end-to-end
latency benchmark). This is a common post-commit path, not an
Alpha Lock-specific rule, and is a confirmed source of unnecessary work.

`KisTileDataPooler::numClonesNeeded` now returns zero for CPU-stale GPU tiles;
`processLists` rechecks demand and skips zero/negative work. GPU COW remains on
GPU. Actual CPU access still synchronizes through the existing tile hooks,
and the pooler can prepare CPU clones once CPU pixels are current. The test
temporarily suspends the real pooler using the existing test-only facility
(friend access for `KisGpuPaintDeviceTest`), executes a deterministic pooler
pass, requires zero submissions/no CPU-current transition/no clone memory,
then verifies CPU reading, resumed clone preparation and COW isolation.
Existing Undo/Redo, eviction, disk swap and CPU pooler tests remain required.
The CPU-valid check is a performance hint: a concurrent change after the
check still goes through the original synchronization, so correctness does
not rely on that hint being atomic with the later clone operation.

`KisGpuStrokeTest` adds separate Alpha Lock-only 256px distant-mirror Wash and
Buildup cases; previous restricted rows combined selection and Alpha Lock.
Before this change their five-sample median GPU projection+brush times were
28.12 ms Wash and 18.27 ms Buildup. These short queued strokes do not include
background pooler completion, thumbnails, canvas or tablet input, so they
cannot validate the user's post-release delay on their 1.5 GiB document.

Validation on 2026-10-03: all nine GPU suites, the dab queue, CPU tile pooler
and tiled data manager passed with Vulkan validation (12/12). The 17 existing
layer/merger/projection/scheduler/walker/filter-mask/painter suites passed
with GPU disabled and again with projection+brush enabled. Installed with
Krita closed; all five related installed DLL hashes match the build.

Manual result: lag remained for large brushes in both modes. The user clarified
200-300px and above, with the line continuing to catch up after release; this
points to outstanding painting work rather than only post-stroke housekeeping.
The earlier follow-up was to use the same document, preset and mirror axes, enable Alpha
Lock, and compare the time after pen release in both Wash and Buildup. Check
Undo/Redo and save/reopen. Do not report the interactive issue fixed until
this check confirms it; if it persists, profile final Wash blending,
thumbnail/exact-bounds CPU reads and queued projection/canvas completion.

## Byte-bounded brush updates (phase 4.12)

The adaptive ready-dab count estimated execution time but did not bound source
storage. A queued 300px stroke with spacing 0.02 returned 95 dabs in one update;
the 1024px/0.1-spacing case returned six. Both exceeded the 64 MiB compositor
staging budget and refused all GPU painting, including separate mirrored
passes. The supported-GPU job then executed CPU `bltFixed` serially. Diagnostic
logs confirmed `GPU brush staging budget reached`; new stroke cases originally
failed their assertion that GPU dab batches actually completed. This reproduces
a backlog source, but does not prove every real-app 200-300px lag has this cause.

For supported RGBA32F GPU brushes, `KisBrushOp::doAsynchronousUpdate` passes
a 32 MiB source-byte budget through `KisDabRenderingExecutor` to
`KisDabRenderingQueue::takeReadyDabs`. It uses actual completed-device bounds
and pixel size, conservatively counting shared dabs separately. It checks
before making mutable copies or advancing average-opacity/painted-job state.
Remaining dabs stay ready in the queue, and existing asynchronous/forced-end
updates drain them. Order, cache ownership and average-opacity progression are
preserved. Space remains for destination tables, clipped records and selection
coverage. A single dab always makes progress even if larger than the budget;
normal CPU fallback remains if GPU staging still cannot fit it. CPU/unsupported
paths keep the unlimited-byte default and existing count limit.

`KRITA_GPU_BRUSH_DEBUG=1` now reports compositor recording refusal reasons and
updates taking at least 20 ms, with dab count, average dab-generation time,
combined-mirror status, remaining prepared work, mode and channel flags.
Update elapsed time includes job scheduling; it is not pure GPU kernel time.

Six queue cases cover mutable/cached dabs, zero/undersized/exact budgets,
progress, remaining-work flags, pixel data, offsets, flow and average opacity.
Full stroke tests add dense 300px and large 1024px Alpha Lock Wash/Buildup cases,
requiring GPU dab use as well as CPU layer/projection parity and Undo/Redo.
The stroke test explicitly registers the brush factory from its linked static
library: registry initialization previously loaded the installed plugin, so
pre-install tests could inadvertently exercise an older brush implementation.

Five samples after warm-up, validation disabled, RTX PRO 6000, dense 300px
Alpha Lock mirrored queued stroke, median completed stroke time:

| Mode | Previous installed brush | Byte-bounded brush |
| --- | ---: | ---: |
| Wash | 254.82 ms | 172.28 ms |
| Buildup | 146.46 ms | 93.41 ms |

The temporary baseline test used the still-installed phase 4.11 brush factory
and allowed its known zero-GPU-batch fallback; that test override was removed
after measurement. The final test always requires the current linked brush.
CPU controls measured 86.94/88.39 ms Wash and 64.76/68.26 ms Buildup. These are
queued strokes in a 1024-square, four-layer image with four workers; they
exclude canvas/tablet input and untimed comparison readbacks. GPU remains
slower than CPU in these cases, and smaller batches increase projection work
and submission count. Do not claim the real-app lag is resolved yet.

Final validation on 2026-10-03: all nine GPU suites and the rendering queue
passed with Vulkan validation (10/10). The queue has 12 passing cases and the
stroke suite 18, including initialization/cleanup. Installed with Krita closed;
all five related DLL SHA-256 hashes match the build. No configuration changed.

Manual result (2026-10-03): lag remains. The user considers mirror painting
with brushes this large a rare case and explicitly requested deferring further
optimization. Keep the validated changes and treat the residual latency as a
known limitation, not a resolved issue or a blocker for other GPU work.

Only if the user requests resuming this investigation, use the same document/preset with Alpha Lock and both mirror
axes, 200-300px and larger, Wash and Buildup. Compare how long the line keeps
drawing after release, plus small-brush control, Undo/Redo and save/reopen.
If lag remains, use slow-update/refusal logs to distinguish dab generation,
CPU fallback, projection/readback work and display scheduling before changing
another path.

## Erase dab compositing (phase 4.13)

`KisGpuDabCompositor::CompositeMode::Erase` and `paint_dabs.comp` implement
`KoCompositeOpErase` for RGBA32F: multiply source alpha by selection coverage,
then opacity, and multiply destination alpha by one minus that result. RGB
is unchanged, including hidden RGB at zero alpha. Flow/average-opacity are
ignored by Erase, matching the CPU op. `KisGpuBrushPainter::supports` accepts
Erase only with all channels enabled; Alpha Lock and RGB restrictions retain
the original CPU path, without changing the CPU eraser's channel behavior.
The debug path counter accommodates the fourth shader variant.

Existing owning-image/profile/LOD/wrap-around gates, source-byte batching,
selection snapshot, per-fragment clipping, mirror ordering and source sharing
all apply. This extends brush compositing, not GPU dab generation or layer
blend modes. Wash still paints its Alpha Darken temporary target on GPU, then
uses CPU Erase for preview and final merging; those operations are unchanged.
The work does not reopen the deferred large-brush mirror-latency investigation.

Brush tests add Erase to CPU parity/COW/Undo/CPU-after-GPU, rejected-channel
and failure handling, soft/inverted/empty/translated selection, clipping and
mirrored/sparse destinations. A dedicated test checks every RGB value remains
exactly unchanged, alpha never increases, and all 15 restricted channel masks
refuse without writes. Real brush-job tests cover all mirror combinations,
selected Erase and failed combined submission. Six complete eraser stroke
cases use the canvas resource snapshot's effective Erase mode for Buildup/Wash,
mirrored drawing and restricted-channel CPU fallback, comparing layer and
projection pixels and Undo/Redo with CPU reference strokes.

On the RTX PRO 6000, validation disabled, five completed iterations after
warm-up, 32 dabs of 256px: Erase compositing averaged 3.304 ms CPU / 1.720 ms GPU;
with selection, 4.329 / 1.828 ms. This excludes generation and canvas display.
The complete 128px eraser stroke (five samples, median) measured 4.81/15.56/13.28
ms Buildup and 7.23/18.67/17.96 ms Wash for CPU / GPU projection only / GPU
projection+brush. Short complete GPU strokes remain slower than CPU; do not
present the compositor microbenchmark as a full input-latency improvement.

Automated validation on 2026-10-03: all nine GPU suites and the rendering queue
passed with Vulkan validation enabled (10/10 suites). The brush suite passed
220 cases and the brush-job suite 23, including initialization/cleanup. Installed
with Krita closed; all five related DLL SHA-256 hashes match the build. No
configuration changed. The user subsequently reported that the manual test passed.

Manual regression checklist: launch the usual GPU brush script, use an RGBA32F document
with existing painted pixels, toggle the pixel brush to Eraser (E), and check
soft/opaque erasing, selection edges, ordinary mirror painting, Buildup/Wash,
Undo/Redo and save/reopen. Alpha Lock/channel restrictions should behave as
before via CPU fallback. No special large-mirror performance test is needed.
`GPU brush: batch` should identify mode `erase`, variant 3, for direct erasing.

## Wash Erase GPU preview (phase 4.14)

`paintWashPreview` accepts Erase as well as Normal when there is no selection
or channel restriction. The layer compositor has an internal Erase operation
that changes only destination alpha, matching `KoCompositeOpErase`. It is not
added to `blendOpForCompositeOp`, so layer blending eligibility is unchanged.
The existing RGBA32F/profile/default-pixel/alignment and owning-float-image
gates remain; final Wash merging remains on CPU. No new scheduling or memory
ownership rules are introduced. Diagnostic preview messages include the mode.

The indirect-merge test now covers both Normal and Erase with independent
selection/channel restrictions, failed submission, unaligned sources, integer
owning images and constrained readback. Erase checks HDR/negative and hidden
RGB exactly, plus alpha reduction, preview/final CPU parity and Undo/Redo.
Complete unrestricted Wash eraser strokes now require successful GPU previews,
while restricted strokes must still avoid them. The preview benchmark has
separate Normal and Erase rows and measures completed preview work only.

On the RTX PRO 6000, validation disabled, five iterations after warm-up,
12 dabs of 256px with four mirror passes: Erase preview median was 1.720 ms
with CPU preview and 2.019 ms with GPU preview. Normal control was 1.912/
1.904 ms. Both paths receive freshly GPU-painted temporary pixels; timing
includes preview completion but excludes brush generation and canvas display.
This expands GPU residency coverage, but does not establish a speedup for
these previews or end-to-end drawing latency.

Validation on 2026-10-03: all nine GPU suites plus the rendering queue passed
with Vulkan validation enabled (10/10, 56.62 s). Targeted indirect-merge checks
passed 18 rows and full eraser strokes passed six rows, plus init/cleanup.
Installed with Krita closed and verified matching SHA-256 hashes for all five
related DLLs. No user configuration was changed.

Manual result: the user reported no issues with the requested phase 4.14 check.
This confirms interactive correctness, not a measured latency improvement.

Manual regression checklist: in an RGBA32F document, use a normal-sized pixel brush
in Wash mode with Eraser (E). Check the soft eraser preview while drawing and
its appearance after pen release, with and without ordinary mirror painting.
Also check selected/restricted erasing, Undo/Redo and save/reopen. The deferred
large-brush mirror case does not need retesting for this stage.

## GPU final Wash merge (phase 4.15)

`KisPaintLayer::writeMergeData` now tries `KisGpuBrushPainter::mergeWash` before
the existing indirect-painting CPU fallback. This is a paint-layer override;
colorize masks and other indirect-painting subclasses retain their existing
merge implementations. The owning image must use a GPU float color space,
and `KRITA_GPU_BRUSH=1` plus the GPU projection gate remain required.

Preview and final merge share the same Normal/Erase compositor and eligibility
checks: RGBA32F, matching profiles, zero-alpha source default pixel, no
selection/channel restrictions, no wrap-around or LOD, finite opacity and
aligned source/destination tile grids. The final path additionally requires
the rectangle origin and dimensions to be multiples of 64. Existing final
jobs enumerate disjoint rectangles of the source tile region; these checks
ensure separate jobs also own disjoint destination tiles. Unaligned regions
keep CPU processing. Do not relax alignment without addressing concurrent
CPU/GPU jobs touching the same destination tile.

The original write lock, barrier, concurrent rectangle jobs and sequential
transaction begin/end remain intact. GPU writes register COW/mementos through
`KisGpuTileAccess`; the painter records its dirty rectangle. Submission
publishes tile state before the job returns. Tile retirement retains in-flight
source storage even when final cleanup releases the temporary target. A refused
or failed rectangle is unchanged and falls back to batched CPU readback and
`bitBlt`; other successfully submitted rectangles are not composited again.

The indirect-merge tests disable GPU brushes for their CPU reference, assert
successful GPU final-merge counts, check source-region submission failure and
low-budget recovery, retain an unchanged COW snapshot and compare Undo/Redo.
Erase includes hidden RGB. A dedicated refusal test covers partial tile
rectangles. Full Normal/Erase Wash strokes require actual GPU final merges
only on the unrestricted path; selected/locked strokes require CPU fallback.
The isolated final-merge benchmark includes transaction and GPU completion,
but excludes dab generation and canvas display.

Measurements on the RTX PRO 6000, validation disabled, five samples after
warm-up, 12 dabs of 256px with four mirror passes: final-merge median CPU/GPU
was 2.550/4.091 ms Normal and 2.365/4.074 ms Erase. The isolated benchmark
starts with GPU-painted temporary pixels and a CPU-filled destination.
Completed 1024-square, four-layer strokes with four image workers measured
5.07/16.43/18.92 ms (64px Normal Wash), 9.96/24.09/32.06 ms (256px Normal Wash),
and 9.14/21.76/22.46 ms (128px Erase Wash), for CPU / GPU projection only /
GPU projection+brush. These results do not establish a speedup; submission,
upload and transaction costs still matter. They are not tablet/display latency
measurements or a controlled before/after comparison with phase 4.14.

Validation on 2026-10-03: targeted merge/refusal tests passed 25 cases and
complete stroke tests passed 24 cases, including init/cleanup. All nine GPU
suites plus the rendering queue passed with Vulkan validation (10/10).
The 17 selected image/layer/merger/painter regression suites passed with
`KRITA_GPU_BRUSH=1` and GPU projection both disabled and enabled (17/17 each).
Installed with Krita closed, including rebuilt image, impex, UI and paint-op
libraries; all seven related DLL SHA-256 hashes match the build. No user
configuration was changed. The user subsequently reported that the requested
manual test passed. This confirms interactive correctness, not a measured
latency improvement.

Manual regression checklist: use RGBA32F, a normal-sized pixel brush, Wash mode and
Normal painting, then Eraser (E). Compare the appearance before/after pen
release, ordinary mirroring, Undo/Redo and save/reopen. Check selected and
Alpha Lock strokes still behave as before via CPU fallback. Further tuning
of the deferred large-brush mirror case remains out of scope.

## Selected Wash compositing (phase 4.16)

`KisGpuBrushPainter::compositeWash` now snapshots selection projection bytes
over the requested rectangle intersected with `selectedRect()`. Empty
intersections succeed without submission; masks larger than 16 MiB use CPU
fallback. The final-merge tile-exclusivity check still applies to the original
job rectangle before selection clipping, so separate jobs cannot acquire
overlapping destination tiles. Channel restrictions still refuse GPU Wash.

An optional `KisGpuLayerCompositor::Mask` passes image-coordinate bytes through
`KisGpuProjectionCompositor`, which translates its bounds to the destination
tile grid. Masked compositing is restricted to one RGBA32F Normal/Erase layer
without alpha lock. The layer compositor copies/pads the bytes into its table
buffer; the caller's snapshot can be destroyed after recording. The work
context waits for its previous submission before overwriting that buffer.
The shader push constants are now 96 bytes, including the mask address,
origin and size. An unmasked call resets the address to zero.

Normal computes `(sourceAlpha * opacity) * coverage`; Erase computes
`(sourceAlpha * coverage) * opacity`, matching their CPU operations. RGB is
preserved by Erase, including at zero alpha. Layer blend-mode eligibility is
unchanged. Mask generation and selection rasterization remain CPU work.

Indirect-merge tests compare preview/final pixels, COW and Undo/Redo for soft,
inverted, moved, empty and outside selections, including masked submission
failure in both preview and final merge. A reuse test changes then removes
the selection, with odd dimensions to cover padded mask-word reads. Four
complete Normal/Erase Wash strokes, with/without mirroring, require GPU
preview and final-merge counters and compare CPU pixels and Undo/Redo.

Measurements on the RTX PRO 6000, validation disabled, five samples after
warm-up, 12 dabs of 256px with four mirror passes and a 173/255 selection:
preview median CPU/GPU was 2.300/2.261 ms Normal and 2.190/2.232 ms Erase;
final merge was 3.111/4.570 ms Normal and 3.420/4.502 ms Erase. Mask snapshot
and upload are included, brush generation and canvas display excluded.
These results do not establish a latency improvement; final GPU merging
remains slower in this isolated workload.

Validation on 2026-10-03: targeted indirect merge passed 36 cases and selected
full strokes passed six (including init/cleanup). The final Vulkan-validation
run passed all nine GPU suites plus the rendering queue (10/10, 59.81 s),
including the selection-change/removal test. Installed with Krita closed;
all eight related DLL SHA-256 hashes match the build. No user configuration
was changed. The user subsequently confirmed the requested manual check passed.
This confirms interactive correctness, not a measured latency improvement.

Manual regression checklist: RGBA32F, Wash, a normal-sized pixel brush, Normal and
Eraser (E). Paint across a feathered selection edge; invert/move the selection,
then deselect and paint again. Check ordinary mirroring, appearance after pen
release, Undo/Redo and save/reopen. Alpha Lock/individual channel restrictions
still use CPU Wash processing; deferred large-mirror performance tuning is
not part of this stage.
