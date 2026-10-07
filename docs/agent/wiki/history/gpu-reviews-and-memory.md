---
type: history
topic: gpu-engine
status: archived record (moved from docs/agent/gpu-engine.md on 2026-10-07)
sources: [docs/agent/gpu-engine.md, git history]
---

# GPU engine history: reviews and memory budgets

Canvas interop investigation, earlier phase reviews, tile memory budget and eviction, canvas transfer buffers. Moved verbatim from `docs/agent/gpu-engine.md`; current state, decisions and
invariants stay there ([gpu-engine.md](../../gpu-engine.md)). Each section records what was true
when it was written; later sections and the current document take precedence.

## Canvas interop investigation (2026-10-03)

Reported in the real application: an RGBA32F sRGB-elle-V2-g10 document
remains gray, while thumbnails and Vulkan patch readbacks are correct.
Direct GL buffer reads and canvas textures were zero. The older tests used
an isolated offscreen context and missed the application environment.

On Windows 11 / RTX PRO 6000 Blackwell / driver 596.86 / Qt 6.8, enabling
`Qt::AA_ShareOpenGLContexts` and joining the global share group reproduced
a memory-import failure (`GL_OUT_OF_MEMORY`, 0x505) in the new probe.
Keeping the exported Win32 memory and semaphore handles alive with the
shared buffer fixes this test. Reinstating immediate `CloseHandle()` calls
alone makes it fail again. This is consistent with deferred driver handling
of imported handles; the driver's internal cause is not proven. Both APIs
still use dedicated allocations. Changing both
to non-dedicated did not resolve the test failure, so that experiment was
reverted. A mismatched dedicated flag was not tried: the
[GL extension specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_external_objects.txt)
requires the GL flag to match a dedicated external allocation.

The shared buffer now retains three NT handles (memory and two semaphores)
until destruction. Pooled objects reuse their handles with each buffer.
Retirement drains pending Vulkan/GL work and deletes the GL objects before
closing the handles. Temporary self-test objects also delete their GL
objects and close their handles. Do not
restore immediate handle closure without repeating the shared-context test.

A separate diagnostic pitfall: direct `glGetBufferSubData` on imported
storage returned the first pattern after the second Vulkan write, whereas
GL GPU-copy readback returned the new pattern. This is consistent with a
stale CPU shadow; it is not evidence that the GPU sees different memory.
Both the probe and `debugInspectBuffer()` now read an ordinary GL buffer
filled by `glCopyBufferSubData`. Existing opt-in diagnostics remain under
`KRITA_GPU_CANVAS_DEBUG=1`; look for `interop self-test passed` and
`GL GPU-copy reads tile`. Import errors are logged separately for memory
import and storage binding.

Regression coverage in `KisGpuCanvasUploadTest`:

- Shared global GL context enabled before QApplication. Advertised interop
  followed by incorrect bytes fails the test rather than being skipped.
- `testInteropSelfTestPreservesBinding`: two full-buffer patterns plus
  restoration of the unpack/read/write buffer bindings.
- `testSilentInteropFailureFallsBackToCpu`: inject an actual independent
  zero-filled GL buffer, with successful GL calls. The production probe
  rejects it; the builder stays on CPU uploads, subsequent checks keep it
  disabled, textures match the reference, and the GPU backend stays healthy.
- `testWidgetInteropWithSecondDevice`: run the same probe in a visible
  QOpenGLWidget sharing the global context, before and after creating another
  independent Vulkan instance/device. This does not load ggml-vulkan itself.
- Existing texture parity and failed-import readback rows run in the shared
  context too. With the fix, 27 cases passed with no validation errors; the
  handle-lifetime mutation fails initialization with 0x505.
- All six GPU suites passed with `KRITA_GPU_VALIDATION=1`, including
  `KisGpuSaveTest` through ctest's configured working directory.

After installation, the user's real-application debug log also reports a
successful self-test, `interop 1`, and matching Vulkan / GL GPU-copy /
texture samples. For example, tile (6,6) has
`0.910378 0.879572 0.879568 1` on all three paths; white samples are
`0.999997 1 0.999997 1`. The log includes ggml-vulkan startup on the same
GPU, so this run did not require disabling Vision ML. The user confirmed
that display is correct and closed Krita after the reproduction steps.
The original gray-canvas symptom is resolved in this run on the GPU canvas
path, without CPU fallback. The broader zoom/undo/redo checklist remains
separate from this confirmation.

Manual follow-up: use the user's debug launcher, create an RGBA32F linear
sRGB document, paint, and close Krita. Confirm a white background and visible
strokes, then compare Vulkan / GL GPU-copy / texture samples in the log.
Check zoom, undo/redo, and an 8-bit document too. Only the user starts/closes
Krita; do not change `Solstice/GpuEngine*` settings. If the self-test fails,
confirm CPU canvas display is correct and retain the logged failure reason.
If it passes but the canvas is still gray, inspect the later buffer/texture
samples before attributing the failure to ggml or allocation mode.

## Earlier phase reviews

An external review (Codex) of phases 0–2 found: an exit crash of
`KisGpuProjectionTest` under the validation layer, GPU writes to unshared
CPU-locked tiles not being refused, failed downloads leaving both copies
invalid, stale uploads able to overwrite newer GPU content, an informal F16
tolerance, and test gaps (modes only tested through CPU fallback, optional
adjustment layer, no filter mask, save only after `syncToCpu`, no failure
injection). All of these are fixed and covered by tests.

A second review found that a failed download still discarded the newest
content silently (and GPU copy-on-write clones had zeroed CPU buffers), that
CPU writes could interleave with publishing an upload, and that retries
re-requested a full batch buffer. Fixed by the `ContentLost` state with
engine shutdown, CPU-buffer copies for GPU clones, the single atomic
flags/generation word, and exactly sized readback buffers; covered by
`testPersistentDownloadFailureIsReported`,
`testGpuValidCannotBePublishedAfterCpuWrite`, and
`testRetryAllocatesOnlyWhatItNeeds`. A third review found that only
`isSupported()` checked the failed state; `prepare()` and
`submitAndFinish()` now check it too (`testNoSubmissionAfterFailure`). A
fourth review found that a failed `submit()` led to a second
`vkEndCommandBuffer()`; `KisGpuCommandList` now tracks its recording state
and `abandon()` ends a recording only once (`testFailedSubmissionKeepsContent`
with `KisGpuContext::injectSubmitFailuresForTesting`; reintroducing the
double end makes that test fail through the validation layer).

A review of phases 3.1/3.2 found: the preamble command buffer had no
leading barrier, so uploads in it could race with earlier GPU work that
still read the slot (now both buffers of a list start with a full barrier);
`tryAdd()` accepted RGBA float groups inside non-float images, where the
scheduler does not keep tile-sharing jobs apart (now the same
`mayCompositeOnGpu` condition; `testFloatGroupInIntegerImageStaysOnCpu`);
a failed GL import still let tiles read an unbound buffer (now the
patches are read back through Vulkan and uploaded from the CPU, see
"Canvas from the GPU"; `testGLImportFailureFallsBackToCpu` compares the
resulting textures with the CPU path for every conversion row of
`testTexturesMatchCpu`, F16 included, also for edge patches and for a
merged update that mixes a read-back upload with one GL imports). Re-reviews rejected two earlier versions of that fix: a CPU
rebuild on the GUI thread (raced with merges) and a refresh request
(its rect needed the current level of detail, which can switch before the
scheduler queues the job);
colorants of profiles with different white points did not share the PCS
(`testTexturesMatchCpu` LargeRGB/CIE RGB rows); and profiles with both
matrix-shaper and LUT tags used the matrix (`testLutProfilesUseCpu`; it
checks the tag detection with a renamed tag entry, not a real LUT profile
through the whole CPU fallback).
The white-point rows, `testFloatGroupInIntegerImageStaysOnCpu`, and the
readback test (by breaking the margin offsets) were checked to fail with
their fix removed. Not covered by a test: the preamble barrier (the tests
pass with `VK_LAYER_VALIDATE_SYNC=1`, but the race was never reproduced
with the old code, so that proves nothing about it); the lines of
`recalculateCache()` that call `acquire()`/`readBackFailedUploads()` (the
test calls them directly, in the same order); and the last-resort full
refresh after a failed readback.

A review of phase 3.3 found that a save during which GPU content is
lost for the first time still replaced the target file (the pre-save
check runs before the background save; now `mayFinishExport()`,
`KisGpuSaveTest`), and that the one-time `isEnabled()` decision could be
made by several threads with different results (now `std::call_once`).

Still not verified: the full interactive checklist beyond the confirmed
gray-canvas reproduction with `KRITA_GPU_PROJECTION=1`; a build without the
Vulkan SDK; real device loss and VRAM exhaustion (injected download failures
and constrained tile budgets are tested); undo across animation frames and
LOD switches with GPU-written tiles.

## Tile memory budget and eviction

`KisGpuTileBackend` bounds the combined F32/F16 tile pool reservations to
one quarter of device-local memory, capped at 8 GiB (minimum 1 MiB).
`KRITA_GPU_TILE_BUDGET_MIB` overrides this at backend creation for development;
valid values are 1 through 1048576. No preferences or `kritarc` keys change.
This is a limit on tile buffer sizes, not process-wide VRAM usage: Vulkan
allocation overhead, staging/readback, GL textures, shared canvas buffers,
and Vision ML allocations are outside it. It is not a live driver budget.

When a pool cannot grow, allocation attempts one eviction pass and retries.
The pass prefers historical tile data, then CPU-current data within each
group. GPU-only tiles are downloaded before their slots are released.
Empty chunks are freed and their indices reused; a live slot's address does
not move. Partial chunks allow budgets smaller than the normal chunk size.
An unsuccessful voluntary download leaves the slot and latest GPU pixels
intact. If no safe slots can be reclaimed, GPU preparation fails and callers
use their existing CPU fallback, without stopping the engine.

`KisGpuTileAccess` pins each slot while recording, including COW sources.
Submission publishes the last-use timeline before releasing the pins.
Eviction skips pinned slots and submissions not yet completed. The state
object stays attached after eviction with an invalid slot and valid CPU copy;
the next GPU access loads any disk-swapped CPU data and assigns a new slot.
Do not use the existence of `gpuState()` as proof of GPU residency.

The store iteration lock stabilizes candidate lifetimes. Eviction takes the
tile swap lock for writing with a try-lock, then the residency mutex, then
state/transfer locks. Never acquire the store iteration lock while holding
a tile swap lock or the residency mutex. In particular, allocation drops
the swap lock before scanning for eviction candidates. Pins require the
caller to keep tile data alive and pair every successful `pinState()` with
`unpinStates()`. The normal swapper can now download and evict a GPU tile,
then swap it to disk without deleting its GPU state.

Regression coverage:

- `KisGpuEngineTest::testTilePoolAllocation`: budget-sized partial chunks,
  refusal at the limit, slot reuse, empty-chunk release, live address stability.
- `KisGpuPaintDeviceTest`: repeated allocation under a two-tile budget;
  prepared access and COW-source protection; a failed eviction download;
  GPU-to-disk-to-GPU round trip; Undo/Redo after eviction; concurrent writes,
  reads and eviction. Existing deferred-slot tests cover destroyed tiles.
- `KisGpuProjectionTest::testMemoryBudgetFallsBackToCpu`: a one-tile budget
  causes partial preparation rollback, produces pixels identical to the CPU
  projection, and permits GPU compositing again after the budget is restored.

The current pass downloads stale tiles individually and retains the stale
CPU snapshot while a tile is GPU-resident. Batched eviction and reducing
that RAM duplication remain open. Canvas transfer buffers have their own
limit below; other temporary allocations are not a unified budget.

Validation on 2026-10-03: all six GPU suites and the fourteen required
tile/paint-device/transaction/iterator/painter suites passed (20 ctest
entries, including the low-memory stress test), with Vulkan validation
enabled. After the final empty-pin cleanup guard, the engine and paint-device
suites passed again. Updated `kritagpu`, `kritaimage`, `kritalibbrush`,
`kritaui`, and `kritalibkra` libraries were installed. The real-application
memory-pressure check was subsequently completed by the user with no issues
in painting, Undo/Redo, zoom, saving, and reopening. Its log shows
`GPU tile memory budget reached` with CPU compositing fallback, a passing
interop probe, and equal GL buffer/texture samples. This is evidence for
the tile-budget change, not yet the later canvas-buffer limit.

Manual check after installation: RGBA32F painting, Undo/Redo, zoom, save and
reopen. For pressure testing, start via a temporary launcher with
`KRITA_GPU_TILE_BUDGET_MIB=16`; use a multi-layer image exceeding this budget.
Only the user starts and closes Krita; do not change their saved settings.

## Canvas transfer buffer budget and retirement

The shared Vulkan/OpenGL upload pool now defaults to 512 MiB across all
documents. `KRITA_GPU_CANVAS_BUDGET_MIB` overrides it at first allocation
(1 through 1048576). The mutex protects checking the limit and allocating
together, so concurrent builders cannot each spend the same free capacity.
Reservations include live updates, reusable buffers, and failed imports
awaiting retirement. A request that cannot fit returns a reason to the
existing CPU upload path; it does not disable interop for the session.
Power-of-two allocation falls back to the exact requested size when only
that fits. The count measures requested buffer bytes, not driver overhead.

Before the GUI thread acquires an update, it collects failed imports and
reclaims unused buffers if allocation requested trimming or the idle cache
exceeds one quarter of the budget. Live update references are never touched.
`KisGpuGLSharedBuffer::prepareForDestruction()` requires exclusive ownership
and the importing GL share group current when GL objects exist. It refuses
active GL reads and mismatched contexts. It drains the outstanding Vulkan
or GL semaphore in a Vulkan submission, waits, deletes GL objects, and
finishes GL deletion before the destructor closes the retained NT handles
and frees Vulkan resources. A failed drain keeps the allocation charged and
alive for retry. Never delete an imported pool object after a failed drain.
The wrong share group is not a reason to free resources or reduce accounting.

Unused canvas work contexts are capped at four. Excess contexts wait for
their submission before destroying writer tables. This bounds the retained
context count, not all temporary memory or active contexts. Normal GL canvas
textures and image tile staging are outside the shared-buffer limit.

`KisGpuCanvasUploadTest::testBufferBudgetAndRetirement` uses a 1 MiB budget:
one pending GPU update forces the next update onto the CPU with identical
textures, while preserving the first update. It checks refusal to reclaim
a live update or delete in another share group, retry after a failed drain
submission, deletion of the real GL buffer name, a dropped update never
imported into GL, and failed-import CPU recovery followed by reclamation.
The existing conversion, shared QOpenGLWidget/second-device, and interop
failure tests remain part of the suite.

Validation on 2026-10-03: the canvas suite passed all 28 cases, and all six
GPU ctest suites passed with Vulkan validation enabled. Manual follow-up:
use a temporary launcher setting `KRITA_GPU_CANVAS_BUDGET_MIB=16` and leaving
the tile-budget override unset. In RGBA32F, test multiple documents, painting,
zoom, document switching/closing, and save/reopen. Confirm no gray or stale
canvas after a CPU fallback or buffer retirement. The user launches/closes
Krita; saved configuration is unchanged.
