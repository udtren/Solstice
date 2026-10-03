# GPU Engine — agent development notes

Design document and technical reference for moving Solstice's image engine
(compositing, filters, transforms, and painting) from the CPU to Vulkan
compute on the GPU. User-facing status is in `docs/gpu-engine.md`.

## Status and locations

- Branch: `krita-sol-gpu`, created from `krita-sol` at `890aadc2ba`.
  Upstream Krita synchronization is intentionally abandoned on this branch.
- Phase: **4 (brush prototype), opt-in; phases 4.1–4.5 manually checked,
  phases 4.6 and 4.8 add brush-job and full stroke-strategy coverage.** Phase
  4.7 is also manually confirmed, with combined four-pass painting recorded
  for Normal/Alpha Darken, selection and alpha lock. Phase 4.8 batches Wash
  readbacks; the user confirmed correctness but reported Wash mirror lag
  (Buildup appeared unaffected). Phase 4.9 accelerates unrestricted Wash
  preview; the user confirmed improved response near the mirror intersection,
  but distant mirror lag remains in both Wash and Buildup. Phase 4.10 combines
  distant passes using sparse destination tiles; the user confirmed large
  improvements in ordinary Wash and Buildup. Alpha Lock still causes a long
  delay after pen release in both modes. Phase 4.11 addresses speculative CPU
  clone readbacks after stroke commits, but did not resolve the reported lag.
  The user clarified that strokes keep catching up after release at 200-300px
  and above. Phase 4.12 bounds brush batches by actual source bytes to avoid
  GPU staging refusals. The user confirmed residual lag and explicitly deferred
  further large-brush mirror optimization on 2026-10-03 as a rare use case.
  Retain the tested improvements; do not resume this investigation without a
  new request. This deferral applies only to this performance issue. Phase 4.13
  adds GPU Erase dab compositing for unrestricted-channel RGBA32F pixel brushes;
  the user confirmed the manual eraser check. Phase 4.14 extends unrestricted
  RGBA32F Wash eraser preview compositing to the GPU; the user confirmed the
  manual check without issues. Phase 4.15 adds GPU final Wash merging for
  unrestricted Normal/Erase RGBA32F paint layers; the user confirmed the manual
  test passed. Phase 4.16 adds selection coverage to Normal/Erase Wash previews
  and final merges; the user confirmed the manual selection check passed.
  Phases 4.17-4.19 are implemented together: Wash Alpha Lock, Wash RGB channel
  restrictions with selections, and CPU-compatible Erase channel handling in
  both direct and Wash painting. The user confirmed the combined manual check
  passed. Phases
  0–3 are implemented, including real-app canvas and memory-budget checks;
  the phase 3.3 dialog checklist remains documented separately. Layer stacks of RGBA float
  images are composited on the GPU inside `KisAsyncMerger`, directly into
  GPU-resident projection tiles (3.1), the canvas textures are filled from
  those tiles on the GPU through a buffer shared with OpenGL (3.2), and the
  engine has a user setting, document conversion, and failure warnings
  (3.3). The GPU engine is **off by default** (Preferences → Performance →
  GPU Engine, `Solstice/GpuEngine`; `KRITA_GPU_PROJECTION=1/0` overrides
  it); with it off, Krita's runtime behavior is unchanged.
- Implementation:
  - `libs/gpu/` (target `kritagpu`): Vulkan foundation, shaders in
    `libs/gpu/shaders/`, tests in `libs/gpu/tests/`;
  - `libs/image/gpu/` (part of `kritaimage`): `KisGpuTileBackend`,
    `KisGpuTileAccess`, `KisGpuProjectionCompositor`, `KisGpuMergeBatch`;
    tests `libs/image/tests/KisGpuPaintDeviceTest.cpp`,
    `KisGpuProjectionTest.cpp`;
  - `libs/image/tiles3/KisTileGpuState.h`, `KisTileGpuHooksStub.cpp`.
  - `plugins/paintops/defaultpaintops/brush/tests/KisGpuBrushJobsTest.cpp`:
    real brush generation, cache and asynchronous update jobs on the image
    worker scheduler, linked with `kritadefaultpaintops_static`.
- Hooks in upstream files: `libs/CMakeLists.txt` (`add_subdirectory( gpu )`),
  `libs/image/CMakeLists.txt` (sources, `kritagpu` link,
  `HAVE_KRITA_GPU_ENGINE`), `libs/image/tests/CMakeLists.txt`, and the tile
  engine: `tiles3/kis_tile_data_interface.h`, `kis_tile_data.h`,
  `kis_tile_data.cc`, `kis_tile.h`, `kis_tile.cc`, `kis_tile_data_store.cc`,
  the merger: `kis_async_merger.h`, `kis_async_merger.cpp`, the scheduler:
  `kis_updater_context.cpp`, and the canvas: `libs/ui/opengl/KisOpenGLUpdateInfoBuilder.{h,cpp}`,
  `kis_texture_tile.{h,cpp}`, `kis_texture_tile_update_info.h`,
  `kis_opengl_image_textures.cpp`, `libs/ui/CMakeLists.txt`,
  `libs/ui/tests/CMakeLists.txt`, and the user interface (3.3):
  `libs/ui/KisMainWindow.cpp` (`KisGpuEngineUi::install()` in the
  constructor, `offerConversion()` in `slotLoadCompleted()`,
  `confirmSave()` in `saveDocument()`), `libs/ui/dialogs/kis_dlg_preferences.{h,cc}`
  (`KisGpuEngineSettingsWidget` in the Performance tab),
  `libs/ui/KisImportExportManager.cpp` (`mayFinishExport()` in
  `doExportImpl()`), `plugins/impex/libkra/tests/CMakeLists.txt`.

## Decisions approved by the user

| Topic | Decision |
| --- | --- |
| Direction | Move the engine as a whole to the GPU. Upstream sync is given up on this branch. |
| GPU API | Vulkan. |
| Working color space | RGBA float only (32-bit, optionally 16-bit storage). Other color models (8/16-bit integer, CMYK, Lab, gray) are converted on import/export, not painted in. |
| Hardware | NVIDIA Blackwell only (RTX 50 series, RTX PRO Blackwell). Other GPUs are untested and have no compatibility guarantee. |
| Priorities | All of brush latency, many-layer documents, and filters matter; the phase order below is chosen by dependency, not by priority. |
| Implementation workflow | Bundle several related implementation tasks and their automated checks into one manual-test handoff, as requested by the user. |

## Current CPU architecture (what is being replaced)

Line numbers refer to `890aadc2ba`.

- **Tiles.** 64x64 pixels (`libs/image/tiles3/kis_tile_data_interface.h:24`).
  `KisPaintDevice` → `KisPaintDeviceData` → `KisDataManager`
  (= `KisTiledDataManager`) → `KisTile` → shared `KisTileData` (raw bytes,
  `pixelSize` from the color space). `KisTileDataStore`, `KisTileDataPooler`
  (pre-cloning), `KisTileDataSwapper` (LZF swap to a memory-mapped file).
  Animation frames and LOD planes are separate `KisPaintDeviceData` objects.
- **Undo.** `KisTransaction` → `KisMementoManager`. Tile-level copy-on-write:
  `KisTile::lockForWrite` clones shared tile data and registers the old data as
  a memento (`kis_tile.cc:221`). Undo cost is O(changed tiles).
- **Projection.** `KisImage` update → `KisUpdateScheduler` →
  `KisSimpleUpdateQueue` (splits into 512x512 patches) → `KisUpdaterContext`
  thread pool → `KisAsyncMerger::startMerge` → `KisLayerProjectionPlane::apply`
  → `KisPainter::bitBlt` → `KoColorSpace::bitBlt` → `KoCompositeOp::composite`.
  Adjustment layers and filter masks call `KisFilter::process` inside merge
  jobs.
- **Composite ops.** `libs/pigment/compositeops`. Over, alpha-darken, and copy
  for RGBA F32 use xsimd kernels (`KoOptimizedCompositeOpOver128.h`).
- **Painting.** `FreehandStrokeStrategy` / `KisPainterBasedStrokeStrategy`
  open a transaction, paint ops render dabs (`KisDabRenderingExecutor`), and
  `KisPainter::bltFixed` composites them into the indirect-painting temporary
  target, which is merged into the layer at stroke end.
- **Filters.** `KisFilter::processImpl(device, rect, config)`, in place on one
  device; parallelism comes only from patch splitting.
- **Canvas.** `KisOpenGLImageTextures::updateCache` reads projection bytes per
  256x256 texture tile, converts them with LCMS to the display profile on the
  CPU, and uploads them with `glTexSubImage2D`. OCIO runs in the fragment
  shader. The Windows renderer is desktop OpenGL or ANGLE
  (`KisOpenGL::OpenGLRenderer`); this machine uses `OpenGLRenderer=desktop`.

## Target architecture

```
krita (canvas, tools, dockers)            libs/ui
   │  OpenGL canvas samples shared textures (KisGpuGLSharedImage)
   ▼
GPU image backend (planned)               libs/image  (phase 1+)
   │  GPU-resident paint devices, undo, projection, strokes, filters
   ▼
kritagpu                                   libs/gpu    (phase 0, exists)
      device, queue, timeline, buffers, tile pool, pipelines, interop
```

### Data model (implemented in phase 1)

GPU residency is a property of `KisTileData`, not a separate GPU device. The
original plan (a parallel sparse *tile → slot* map per device with its own undo)
was replaced because Krita's tile engine already provides everything a GPU
device needs: sparse tiles, a shared default tile, copy-on-write, mementos,
frames, and LOD planes. Attaching a GPU copy to each tile data reuses all of
it unchanged.

- A GPU tile is a 64x64 slot in `KisGpuTilePool` (RGBA32F 64 KiB, RGBA16F
  32 KiB). The size matches `KisTileData`, so a CPU tile maps 1:1 onto a slot.
- `KisTileData::gpuState()` is `nullptr` until the tile data is first used on
  the GPU; then it points to a `KisTileGpuState` (`tiles3/KisTileGpuState.h`):
  slot, one atomic word holding the `CpuValid`/`GpuValid`/`ContentLost`
  flags and the CPU write generation, and the timeline value of the last GPU
  use. Non-GPU tile data pays one pointer and one predictable branch.
- The generation is bumped by `KisTile::lockForWrite` **and**
  `KisTile::unlockForWrite`, so a snapshot taken while a CPU write was in
  progress (a stroke painting into a layer that a merge reads) is never
  published as the tile's GPU copy.
- Flags and generation are updated only by compare-and-swap on that word:
  `notifyCpuWrite()` bumps the generation and clears `GpuValid` in one step,
  and an upload is published with `markGpuValidIfGeneration()`, which fails
  if any CPU write happened since the upload was recorded. A check followed
  by a separate store (the earlier implementation) could publish stale GPU
  content.
- Synchronization rules:
  - CPU read or write lock (`KisTile::lockForRead/Write` →
    `KisTileData::blockSwapping`) downloads the GPU copy first if the CPU copy
    is stale (`KisTileGpuHooks::ensureCpuValid` → `KisGpuTileBackend`);
  - `KisTile::lockForWrite` marks the GPU copy stale
    (`KisTileData::notifyCpuWrite`);
  - a GPU write (`KisGpuTileAccess::finish`) marks the CPU copy stale.

  All existing CPU code (iterators, `readBytes`, painters, `.kra` saving,
  scripting) therefore keeps working without modification.
- GPU-resident tile data keeps its CPU buffer even when stale. An idle slot
  can be downloaded and evicted; `KisTileDataStore::trySwapTileData` does
  this before ordinary disk swapping. Prepared or in-flight GPU accesses
  prevent eviction (see "Tile memory budget and eviction").
- Only RGBA F32 (pixel size 16) and RGBA F16 (pixel size 8) devices are
  eligible (`KisGpuTileAccess::isSupported`). The tile engine itself stays
  color-space agnostic; eligibility is checked at the access level.
- Converting documents in other color models to RGBA float moves to phase 3,
  together with the user-visible GPU mode switch.

### GPU access API

`KisGpuTileAccess(device, rect, ReadOnly | ReadWrite | WriteOnly)`:

1. `prepare(commands)` fetches the tiles covering the rect, performs
   copy-on-write for writers (`KisTile::detachForExternalWrite`, the GPU twin
   of the COW branch of `lockForWrite`, including memento registration),
   allocates slots, records GPU slot-to-slot copies for COW clones into the
   caller's command list, and **snapshots** the CPU content of tiles that
   need an upload into a staging buffer (it does not record the upload).
   A COW clone for a read-write access is always made by a GPU slot copy;
   a source that is not GPU-current is uploaded once by the access (many
   cleared tiles share one default tile data, so clearing a projection costs
   one upload, not one per tile).
2. The caller records compute work on `addresses()` (one device address per
   tile, row-major over `tileGrid()`).
3. `KisGpuTileAccess::submitAndFinish(commands, {accesses...})`, under the
   backend's residency mutex: decides which snapshots are still uploaded
   and records them into `commands.preamble()` (a second command buffer
   submitted before the main one in the same batch), submits, and publishes
   flags. A snapshot is **skipped** when the tile is already GPU-valid
   (GpuValid means the slot holds the current generation, which is at least
   as new as the snapshot); a snapshot that overlapped a CPU write is used
   for this submission but **not published** (`markGpuValidIfGeneration`
   fails). Outside the mutex it then releases replaced tile data and retires
   the staging buffer. This replaced an earlier rule that refused to submit
   when an uploaded tile had changed: during painting that rule sent most
   merges back to the CPU.
4. The caller calls `device->setDirty(rect)`, as CPU writers do.

Failure handling:

- `prepare()` fails if any tile is locked by a CPU user (shared or not), if a
  slot cannot be allocated, or if staging memory cannot be allocated. Fresh
  copy-on-write clones whose content only the GPU would have produced get the
  previous content on the CPU (`restorePendingContent`), so a failed or
  refused GPU write never changes the device.
- Readback buffers are sized for the tiles actually downloaded, so the
  tile-by-tile retry of a failed batch needs one tile of memory (64 KiB
  for RGBA F32), not a full batch.
- A failed download is retried tile by tile. If it still fails (device loss,
  out of memory), the latest content of that tile is **lost**. The tile then
  shows the content the CPU last had, which can be older than several GPU
  writes, but it is never presented as normal data: the tile is marked
  `ContentLost`, `contentLossCount()` is incremented, an error is logged,
  and the backend enters the failed state (`hasFailed()`): no further GPU
  work is accepted. `isSupported()` and `prepare()` fail, and
  `submitAndFinish()` re-checks the state under the residency mutex (which
  the backend also holds while entering the failed state), so work prepared
  before the failure is not submitted and its written tiles keep their
  previous content. The merger and all other users fall back to the CPU. Phase 3 must surface the
  loss to the user (and warn before saving).
- Copy-on-write clones made for GPU writes copy the source's CPU buffer, so
  their CPU buffer is never meaningless (it holds what the CPU last had).

`KisGpuTileAccess::syncToCpu(device, rect)` downloads stale tiles in batches
of 256 per submission (use before bulk CPU reads such as saving).

### Undo

GPU writes reuse `KisMementoManager` unchanged: `detachForExternalWrite`
clones shared tile data exactly like `lockForWrite` and registers the change,
so a GPU write inside a `KisTransaction` is undone and redone by swapping
tile data, with no GPU-specific undo code. A clone of GPU-only data is made
by a GPU slot copy; a clone of CPU-valid data is a normal CPU clone that is
uploaded. Slots are returned to the pool when their tile data is destroyed,
deferred until the timeline passes the slot's `lastUse`
(`KisGpuTileBackend::destroyState` / `collectGarbage`).

Note for tests: `KisTransactionData` ignores its first `redo()` (the undo
stack calls it on push). Call `redo()` once after `endAndTake()` before
testing undo/redo.

### Projection (implemented in phase 2)

`KisAsyncMerger` composites the children of a group one by one onto
`m_currentProjection` (`compositeWithProjection`). Phase 2 batches those
composites:

- `KisGpuMergeBatch::tryAdd()` defers a leaf when it is GPU-composable: plain
  `KisLayerProjectionPlane` (no layer style), a supported blend mode, channel
  flags either empty or RGB with the alpha channel locked, the same RGBA
  F32/F16 color space as the projection, and a device offset that differs
  from the projection's by a multiple of 64 pixels, inside an image whose
  own color space is RGBA F32/F16 (`mayCompositeOnGpu`; a float group in an
  RGBA8 image stays on the CPU because the scheduler below would not keep
  its jobs apart). Anything else flushes the batch and is composited on the
  CPU exactly as before.
- The merger flushes before every step that reads the projection composited
  so far: root and `N_EXTRA` leaves, leaves that depend on lower nodes
  (adjustment layers), `writeProjection()`, and the end of `startMerge()`.
  Adjustment layers and filter masks are therefore split points, and the
  result equals immediate compositing.
- A flush runs `KisGpuProjectionCompositor::composite()`: one dispatch of
  `composite_layers.comp` iterates over all deferred layers per pixel, inside
  the clip rect only. Layer tiles are read through `KisGpuTileAccess`
  (resident; uploaded only when stale); tiles outside a layer's extent are
  skipped, like `KisLayerProjectionPlane` clips to the extent.
- **Since 3.1 the projection is GPU-authoritative.** The batch composites
  in place into the projection's tiles (`KisGpuTileAccess` read-write, clip
  rect), which become GPU-valid and CPU-stale; nothing is read back by the
  merge. CPU readers (writeProjection's partial copies, adjustment layers,
  the canvas, thumbnails, saving) download on demand, ordered after the
  merge's submission. (Phase 2 composited into scratch tiles and wrote the
  result back with `writeBytes()`.)
- Concurrent merge jobs used to be allowed when their rects were disjoint,
  even if they shared a 64x64 tile. In-place GPU writes of whole tiles make
  that unsafe, so `KisUpdaterContext::walkerIntersectsJob` compares
  tile-aligned rects (`KisGpuMergeBatch::tileAligned`) when
  `KisGpuMergeBatch::mayCompositeOnGpu(startNode)` (GPU projection enabled
  and an RGBA F32/F16 image). `tryAdd()` uses the same condition, so GPU
  merges never run without this exclusivity. The GPU path only composites
  into projections whose offset is a multiple of 64, so the image grid is
  the tile grid.
- When the canvas cannot use the GPU path (below), it reads the projection
  on the CPU: `KisOpenGLUpdateInfoBuilder::buildUpdateInfo` calls
  `KisGpuTileAccess::syncToCpu` for the update rect first, so stale tiles
  are downloaded in batches instead of one submission per tile.

### Canvas from the GPU (implemented in phase 3.2)

Before enabling uploads, `checkGLInterop()` runs
`KisGpuGLSharedBuffer::testGLInterop()` once in the actual current canvas
context. A temporary 1 MiB buffer follows the production allocation, lazy
import, and semaphore cycle. Vulkan fills it with the bit patterns of 1.0f,
then 0.5f; GL copies it to an ordinary buffer and reads back every word.
Failure (including wrong bytes with GL error 0) disables canvas interop for
the session, so the builder uses its existing batched CPU download path.
Projection compositing remains enabled and kritarc is untouched. Probe GL
objects are deleted with their context current, and pixel-unpack/copy
bindings are restored. The persistent canvas pool is not used for the probe.

The canvas keeps its OpenGL textures (`KisTextureTile`, 256 px with a
16 px border, mipmaps, filters, OCIO shaders) unchanged. Only where the
texture data comes from changes:

1. `KisOpenGLUpdateInfoBuilder::buildUpdateInfo(rect, image, …)` (the live
   canvas; the animation frame cache keeps the CPU path because it stores
   CPU pixel data) builds the tile infos as before, then asks
   `KisGpuCanvasUploader::upload()` to produce their pixels.
2. The uploader reads the projection tiles (`KisGpuTileAccess` read-only,
   current LOD plane), and `canvas_patches.comp` writes every patch into a
   `KisGpuGLSharedBuffer`: a Vulkan buffer whose memory is imported into GL
   as a pixel unpack buffer. Each patch has exactly the layout that
   `KisTextureTile::update()` uploads (`KisTextureTileUpdateInfo::
   uploadGeometry()`, now shared by both paths): at image edges the side
   margins repeat the edge rows/columns, and the corner blocks are zero,
   like the CPU code.
3. On the GUI thread `KisOpenGLImageTextures::recalculateCache()` acquires
   the shared buffers (GL waits for the Vulkan semaphore), each tile's
   `update()` calls `glTexSubImage2D`/`glTexImage2D` with the buffer bound
   as `GL_PIXEL_UNPACK_BUFFER`, and the buffers are released (GL signals a
   semaphore that the next Vulkan write of that buffer waits for).
4. Shared buffers come from a bounded pool (normally power-of-two sizes,
   ≥ 8 MiB, exact-sized when rounding would exceed the budget) and return to
   it when the last tile info referencing them dies. Idle buffers can be
   reclaimed with the importing GL share group current; see the memory
   management sections below.
   A buffer whose content GL never consumed (the update was compressed
   away) is reused after Vulkan waits for its own previous signal.
5. If GL cannot import a shared buffer (`KisGpuGLSharedBuffer::glAcquire()`
   returns false), `KisGpuCanvasUploader::acquire()` fails: the GPU canvas
   path is disabled for the session, the buffer is kept out of the pool,
   and no tile reads it through GL (`KisTextureTile::update()` asserts
   `isAcquired()`). `recalculateCache()` then calls
   `readBackFailedUploads()`: each such buffer is copied to the CPU through
   Vulkan (the copy waits for the Vulkan write and consumes its signal,
   `finishVulkanRead()`), and its tiles get the patch centers as CPU pixels
   (`KisTextureTileUpdateInfo::replaceGpuUploadWithPixels()`), so the same
   update is applied on the CPU upload path. This reads neither the
   projection (which a merge may be writing; the GUI thread must never
   read it) nor needs any rect or level-of-detail conversion: the patches
   are exactly the ones computed for this update. Only if the readback
   fails too (the GPU engine has failed) is the update dropped and the
   whole image refreshed (`refreshGraphAsync()`; the full-resolution
   bounds cover the dropped area at any level of detail).

Display conversion on the GPU (`KisGpuCanvasUploader::conversionFor`):

- equal profiles: only the storage format changes (F32/F16);
- two RGB matrix-shaper profiles (colorants and TRCs, no LUT tags, intent
  other than absolute colorimetric): linearize with the source TRC,
  multiply by `inverse(dst colorants) · src colorants`, delinearize with
  the destination TRC. The colorants must be the PCS (D50) values LCMS
  uses: `LcmsColorProfileContainer` stores them Bradford-adapted to the
  profile's media white point, so `colorantMatrix()` undoes that
  adaptation, and refuses the profile if the columns then do not sum to
  the D50 white (±0.01). Without this, profiles with different white
  points (LargeRGB D50, CIE RGB E, sRGB D65) were off by up to 0.32. Non-linear TRCs are sampled with
  `KoColorProfile::linearizeFloatValue/delinearizeFloatValue` (LCMS
  `cmsEvalToneCurveFloat`) into 4096-entry tables over [0, 1] (input
  clamped); linear TRCs are exact and unbounded. Conversion flags
  (HighQuality, black point compensation) do not change matrix-shaper
  results for these profiles.

Everything else uses the CPU path: soft proofing, channel selection,
non-RGBA-float projections or displays, profiles whose ICC tag table has
any AToB*/BToA*/DToB*/BToD* tag (LCMS would use the LUT; checked on
`rawData()`, unreadable data counts as LUT), absolute
colorimetric intent, no GL interop (ANGLE, another GPU), or any GPU
failure.

Limitation: values outside [0, 1] with a non-linear TRC are clamped by the
tables, while LCMS extends parametric curves. Linear-light documents (the
usual RGBA float case) and HDR displays with linear profiles are exact.
- If the GPU fails, the batch falls back to the CPU composites.

Parity criteria (`KisGpuProjectionTest`):

- RGBA F32: every channel within 2e-5 of the CPU projection (Krita computes
  intermediates in double, the shader in float). Colors of pixels with alpha
  exactly 0 are not compared (they are unspecified in Krita itself).
- RGBA F16: the shader rounds the result to half after every layer, like
  Krita compositing into an F16 device. Every channel within 4 half ULPs at
  max(|value|, 1), i.e. 4/1024 for normalized values; measured maximum 3.

Supported blend modes (`KisGpuBlendOp`, parity with Krita's RGBA float ops):
Normal (`KoOptimizedCompositeOpOver128`), Multiply, Screen, Addition / Linear
Dodge, Subtract, Darken, Lighten, Difference, Overlay, Hard Light, Exclusion
(`KoCompositeOpGenericSC` with the `cf*` functions and their clamp policies,
including the alpha-locked branch and `KoCompositeOpBase`'s clearing of
alpha-0 destination pixels). All other modes run on the CPU.

### Canvas

- The Qt build has **no Vulkan support** (`QT_FEATURE_vulkan -1` in
  `_install/include/QtGui/qtgui-config.h`), so `QVulkanInstance`/QRhi-Vulkan
  are unavailable. The OpenGL canvas stays, and Vulkan shares images with it
  through `GL_EXT_memory_object_win32` / `GL_EXT_semaphore_win32`
  (`KisGpuGLSharedImage`).
- The projection is copied (GPU→GPU) into shared canvas textures instead of
  `readBytes` + LCMS + `glTexSubImage2D`. The LCMS display conversion moves to
  the GPU (3D LUT or matrix+TRC shader); OCIO stays in the fragment shader.
- LOD/mipmaps are produced by a GPU downsample pass.
- Requirement: desktop OpenGL on the same GPU. ANGLE (D3D11) is not supported
  in GPU mode.

### Threading and submission

- `KisGpuContext` owns one graphics+compute queue. Submission is serialized by
  a mutex; every submission signals the context timeline semaphore.
- Stroke and merge jobs keep running on `KisUpdaterContext` threads, but they
  *record* GPU work (per-thread `KisGpuCommandList`) instead of touching
  pixels. Completion is tracked by timeline values, not CPU waits.
- The GUI thread only performs GL work (acquire/release shared images).

### Feature gate and user interface (implemented in phase 3.3)

- `Solstice/GpuEngine` (bool, default off; `KisGpuEngineSettings`) enables
  the engine for the whole process. `KisGpuMergeBatch::isEnabled()` decides
  once (`std::call_once`; a value set by `setEnabled()` before the first
  call wins), so a change applies after a restart: switching while merges
  run would mix GPU merges with CPU-style job exclusivity. The environment
  variable `KRITA_GPU_PROJECTION` (`1` or `0`) overrides the setting.
  Documents that are not RGBA F32/F16 always use the CPU path.
- Opening a document (`KisMainWindow::slotLoadCompleted()`, deferred until
  the view is shown) calls `KisGpuEngineUi::offerConversion()`: if the
  engine is enabled and usable (Vulkan context created, not failed) and the
  image is not RGBA float, `Solstice/GpuEngineConvertDocuments` decides:
  Ask (default; Convert / Keep, with "Do not ask again" storing the
  answer), Convert, or Keep. The conversion is
  `KisImage::convertImageColorSpace()` to
  `KisGpuEngineSettings::conversionTarget()`: RGBA F32 with the same profile
  for RGB documents (same values, same appearance), RGBA F32 with the
  default profile otherwise; internal rendering intent and flags. It is
  undoable and marks the document modified; saving keeps the new color
  space. New documents are not affected (choose RGBA float in the New
  Document dialog). Not converted: file layers (they have no color space
  conversion and stay on the CPU path through the color space mismatch),
  and layers of another color space inside an image that is already RGBA
  float (no unification).
- When the engine enters the failed state (GPU content lost),
  `KisGpuTileBackend` calls its failure listener once per process;
  `KisGpuEngineUi::install()` registers one that shows a non-modal warning
  on the GUI thread (the number of lost tiles, that the engine stopped, and
  to check documents before saving).
- `KisMainWindow::saveDocument()` calls `KisGpuEngineUi::confirmSave()`
  after the busy-image check: after a failure in this session it warns
  before every manual save, Save As, export, and save on close (not
  autosave), offering Save as New File (default), Save Anyway, or Cancel.
  Agreeing records the loss count the user accepted. The loss count is
  process-wide; the warning cannot say which document was affected.
- The background save itself can lose content: the tile compressor locks
  tiles of the cloned document, which downloads GPU-only tiles, and a
  failed download leaves the old CPU content without telling the saver.
  So `KisImportExportManager::doExportImpl()` asks
  `KisGpuEngineUi::mayFinishExport()` after `filter->convert()` and before
  the temporary file replaces the target (`QSaveFile::commit()`, or the
  copy on Windows/macOS): if more tiles were lost than the user accepted,
  the export fails with `ErrorWhileWriting` and the target is untouched;
  the next save asks first. Autosaves (`isAutosaving()`) are exempt.
  Filters that write the target themselves (`supportsIO()` false) are not
  covered.
- Preferences → Performance → General → GPU Engine
  (`KisGpuEngineSettingsWidget`): the two settings and the current status
  (active device, stopped, enabled but not started, not used, or not built
  with Vulkan).

## Phase 0 technology decisions

| Topic | Decision | Reason |
| --- | --- | --- |
| API level | Vulkan 1.3 core: synchronization2, timeline semaphores, buffer device address, shaderInt64, shaderFloat16, 16-bit storage. | All present on Blackwell (driver 596.86 reports Vulkan 1.4.329). |
| Loader | `vulkan-1.dll` loaded at runtime (`KisGpuVulkanFunctions`, X-macro tables). No link-time dependency. | Krita still starts without a Vulkan driver. The SDK has no volk. |
| Memory | One dedicated `VkDeviceMemory` per buffer; tiles sub-allocated from 256 MiB chunks (4096 RGBA32F tiles). No VMA. | Tiles are fixed-size, so a slab allocator is trivial; the SDK does not ship VMA. |
| Tile storage | Storage buffers addressed by 64-bit device address (`GL_EXT_buffer_reference`), not images. | Any number of tiles per dispatch through address tables; no descriptor sets, layouts, or image transitions; sub-allocation is trivial. |
| Shader input | Push constants only (addresses + counts). | No descriptor management in the engine. |
| Shaders | GLSL compute, compiled at build time by the SDK's `glslc` (`--target-env=vulkan1.3 -O -mfmt=num`), included as word arrays (`kis_gpu_add_shader()` in `libs/gpu/CMakeLists.txt`). | No runtime compiler dependency; build fails on shader errors. |
| Precision | RGBA32F is the default working format; RGBA16F storage is supported with float32 arithmetic. | Parity with Krita's F32 color space; F16 halves bandwidth (measured below). |
| Display | GL/Vulkan interop with exported memory and binary semaphores, `GL_LAYOUT_GENERAL_EXT`, dedicated allocation, device UUID check. | Qt has no Vulkan; interop keeps the existing canvas, OCIO, and decorations. |
| Device choice | Highest score: discrete GPU, then NVIDIA vendor; requires a graphics+compute queue with timestamps. `KisGpuDeviceInfo::isBlackwell()` (NVIDIA device id ≥ 0x2B00) is informational and logs a warning when false. | Blackwell-only target without hard-failing development on other GPUs. |
| Validation | `KRITA_GPU_VALIDATION=1` enables `VK_LAYER_KHRONOS_validation` plus a debug messenger that counts errors (`validationErrorCount()`). | Tests assert zero validation errors. |

### Parity rule found in phase 0

The color of a pixel whose alpha is exactly 0 is **unspecified** in Krita:
`KoOptimizedCompositeOpOver128` copies the source color into lanes that stay
transparent, while the scalar paths keep the old color. GPU parity tests
compare such pixels by alpha only. Everything else must match `KoCompositeOp`
within 1e-5 (F32).

## Phase 0 measurements

RTX PRO 6000 Blackwell Workstation Edition, driver 596.86, 32-thread CPU,
RelWithDebInfo. Normal blend of 8 layers of 4096x4096 (16.8 MPix per layer,
10% of tiles empty, random alpha), `KisGpuEngineTest::benchmarkCompositeStack`:

| Path | Time |
| --- | --- |
| CPU `KoCompositeOp` (xsimd), 1 thread | 290 ms |
| CPU `KoCompositeOp`, 32 threads | 82.8 ms |
| GPU RGBA32F kernel | **1.55 ms** (×53 vs. 32 threads; ≈1.5 TB/s, close to the memory bandwidth limit) |
| GPU RGBA16F kernel | **0.74 ms** (×112) |
| Upload of the whole stack (2.1 GB F32, staging → slots) | 39 ms |
| Readback of the result (268 MB F32) | 6.8 ms |
| Vulkan → GL shared image round trip | functional, no CPU wait on the GL side |

Conclusions:

1. Compositing is memory-bound; the GPU wins by 50–100× once data is resident.
2. Upload/readback cost far more than the kernel. The engine must keep pixel
   data **resident on the GPU** and transfer only on load, save, and
   unported CPU access. A hybrid that uploads per operation would lose most of
   the gain.
3. RGBA16F storage halves the kernel time; decide per document later (default
   F32 for parity).

## Phase 1 measurements

`KisGpuPaintDeviceTest::benchmarkTransfers`, 4096x4096 RGBA F32 device
(4096 tiles, 256 MiB), wall-clock times including submission and wait:

| Operation | Time |
| --- | --- |
| First GPU read (uploads every tile) | 63 ms |
| GPU read when already resident | 0.7 ms |
| GPU fill, write-only, in place | 1.6 ms |
| `readBytes` of stale tiles (one download submission per tile) | 406 ms |
| `syncToCpu` (256 tiles per submission) | 26 ms |
| `readBytes` after `syncToCpu` | 20 ms |

Conclusions: residency works as intended (re-reading resident data costs
almost nothing), and per-tile synchronous downloads are ~15x slower than
batched ones. Any bulk CPU consumer of GPU-written data must download in
batches; the canvas must eventually not download at all (phase 3).

## Phase 2 measurements

`KisGpuProjectionTest::benchmarkRefresh`, 4096x4096 RGBA F32 image, 16 full
layers (Normal/Multiply/Screen/Overlay, 86% opacity), layers already
GPU-resident, 32 CPU threads:

| Operation | CPU | GPU |
| --- | --- | --- |
| Full refresh (`refreshGraphAsync`) | 284 ms | 66 ms |
| 256x256 update of one layer (`setDirty`), average of 20 | 12.7 ms | 0.88 ms |

The full refresh is bounded by the projection round trip (fill, download,
`writeBytes` of 16.8 Mpixels): the compositing kernel itself takes a few
milliseconds. Phase 3 removes that round trip.

## Phase 3.1 measurements

Same benchmark as phase 2 (4096x4096 RGBA F32, 16 layers, 32 CPU threads):

| Operation | CPU | Phase 2 | Phase 3.1 |
| --- | --- | --- | --- |
| Full refresh | 253 ms | 66 ms | 42 ms (+ 51 ms if all of it is then read back on the CPU) |
| 256x256 update of one layer + reading the updated rect (what the canvas does) | 12.0 ms | — | 2.3 ms |

## Phase 3.2 measurements

`KisGpuCanvasUploadTest::benchmarkCanvasUpdate`: producing the canvas
texture data (`buildUpdateInfo`) for a 4096x4096 RGBA F32 image with
8 layers, linear sRGB image profile, sRGB display profile. The GPU time
includes waiting for the GPU to finish; the GL-side texture copies are not
included (GPU-to-GPU).

| Update | CPU path (download + LCMS) | GPU path |
| --- | --- | --- |
| Whole image | 1025 ms | 1.6 ms |
| 256x256 rect | 4.5 ms | 0.39 ms |

## Phase plan

Each phase ends with parity tests against the CPU implementation, validation
clean runs, and a benchmark entry in this document.

| Phase | Scope | Exit criteria |
| --- | --- | --- |
| 0 | `kritagpu` foundation, compositing spike, GL interop spike. | Done (this document). |
| 1 | GPU residency of `KisTileData`, download on CPU access, GPU copy-on-write with memento registration, deferred slot free, `KisGpuTileAccess`. | Done: `KisGpuPaintDeviceTest` (fill, composite parity, undo/redo, COW isolation, CPU-after-GPU writes, save/load with and without prior sync, F16, slot release, deferred release of in-flight slots, locked tiles, download failure retry and fallback, stale-upload refusal) and the existing tile/paint device/transaction/iterator tests pass. |
| 2 | Projection on the GPU: batched layer compositing in `KisAsyncMerger`, 11 blend modes, alpha lock, adjustment layers and filter masks as split points, CPU fallback. | Done: `KisGpuProjectionTest` (every blend mode with and without alpha lock, full refresh, partial unaligned updates, adjustment layer, filter mask, F16) matches the CPU projection within the parity criteria, validation-clean with exit code 0; existing merger/projection/walker/layer tests pass with `KRITA_GPU_PROJECTION=1`. |
| 3.1 | GPU-authoritative projection: in-place GPU compositing, tile-aligned merge job exclusivity, uploads decided at submission, COW clones by GPU copies, batched canvas downloads. | Done: `KisGpuProjectionTest` (incl. projection residency and concurrent updates sharing tiles) and all `libs/image` tests except four unrelated ones pass with and without `KRITA_GPU_PROJECTION=1`. |
| 3.2 | Canvas from the GPU: projection tiles → display-converted pixels in a Vulkan buffer shared with GL (PBO) → existing `KisTextureTile` uploads without CPU copies; GPU display conversion for matrix-shaper profiles. | Done: `KisGpuCanvasUploadTest` writes the same texture tiles through the CPU and the GPU path and compares them: identical for equal profiles, ≤ 1 half ULP for F32→F16, ≤ 1.2e-4 for matrix-shaper conversions (sRGB/Rec.2020 linear ↔ sRGB TRC, and different white points: LargeRGB/CIE RGB ↔ sRGB; F16 image to an sRGB TRC F16 texture). `testLutProfilesUseCpu` covers the LUT fallback; `testGLImportFailureFallsBackToCpu` runs the GL-import recovery for all those conversion rows (one failed upload, an edge update, and a merged update where one upload is read back and the other uploaded by GL) with the same tolerances. |
| 3.3 | `Solstice/GpuEngine` gate and Preferences UI; conversion of documents in other color spaces to RGBA float when opened; failure warning, pre-save warning, and no file replacement after a loss during the save. | Code done: `KisGpuProjectionTest::testConvertedDocumentUsesGpu` (conversion targets; an RGBA8 document converted to the target composites on the GPU with an identical 8-bit appearance), `KisGpuPaintDeviceTest::testPersistentDownloadFailureIsReported` (the failure listener fires once for two losses), `KisGpuSaveTest::testLossDuringSaveKeepsTarget` (a `.kra` save that loses the GPU-only projection fails and leaves the target file unchanged; after acknowledging, the save succeeds; fails without the guard). The dialogs themselves are not unit-tested. Open: the manual checklist below (first user-visible build). |
| 3.4 | Bounded image tile pools, idle tile eviction, and normal disk swap for evicted GPU tile data. | Done: low-budget, rollback, concurrency, disk swap, and Undo tests; the user confirmed painting, Undo/Redo, zoom, save and reopen under a 16 MiB tile budget. |
| 3.5 | Bounded canvas transfer buffers and safe GL/Vulkan retirement; cap idle canvas work contexts. | Done: automated budget/parity/lifecycle tests; the user confirmed correct operation under a 16 MiB canvas budget. Its log confirms budget fallback with interop still enabled. Other transient allocations and stale CPU snapshots remain separate work. |
| 4.1 | Opt-in RGBA32F Normal/Alpha Darken pixel-brush dab compositing, retaining CPU dab generation. | Sequential brush update job. GPU/CPU parity, COW/Undo/Redo, CPU-after-GPU writes, failed submit, and low-budget rollback tests. Normal and Alpha Darken/Wash manually confirmed, with successful GPU batches in both logs. Full stroke latency remains unverified. |
| 4.2 | Selection coverage for direct GPU dab compositing. | Soft/inverted/empty selection, translated masks, deselection, CPU fallback, Undo/Redo, and both Alpha Darken variants tested. User confirmed operation; follow-up log confirms Normal with selection on GPU. |
| 4.3 | Normal-mode alpha and RGB channel locks on GPU dabs. | All 16 channel masks, with/without selection, compared with CPU; transparent RGB, COW, Undo/Redo and fallback covered. User confirmed operation; log confirms Normal with alpha locked on GPU. |
| 4.4 | GPU compositing for the existing horizontal/vertical mirror passes. | CPU parity for reflection order, shared dab pixels, masks, alpha lock, fractional-axis clipping fallback and mixed CPU/GPU passes. User confirmed operation; logs confirm reflected Normal and Alpha Darken passes, including Normal with selection and alpha lock together. |
| 4.5 | Combine nearby mirror passes into one submission with immutable shared source pixels. | CPU parity, source immutability, COW/Undo/Redo and preparation/submit fallback tested. Warm benchmarks improved; user confirmed operation. Log confirms combined single-axis painting; four-pass combination remains automatically tested only. |
| 4.6 | Exercise actual brush generation/cache/update jobs on the image worker scheduler. | 15 integration rows plus init/cleanup passed: CPU/GPU pixels, dirty regions, exact submission counts, mixed fallback, owning-image gate, COW and Undo/Redo. No runtime behavior change. |
| 4.7 | Clip GPU dabs to the CPU painting regions, including fractional mirror axes. | CPU parity, gaps/empty regions, COW/Undo/Redo, rollback and exact combined submission counts tested. User confirmed operation; real-app log confirms four-pass combination. |
| 4.8 | Measure complete queued strokes and batch Wash preview/final-merge readbacks. | CPU, GPU projection only, and GPU projection+brush compared through FreehandStrokeStrategy and completed projection. Input/display latency remains separate; see measurements and validation below. |
| 4.9 | Keep unrestricted Normal RGBA32F Wash preview compositing on GPU. | CPU parity/fallback checks; user reports improved response near mirror intersection, but distant Wash/Buildup lag remains. |
| 4.10 | Combine distant mirror passes using sparse destination tiles. | CPU parity, no gap allocation, rollback, Undo/Redo and complete distant Wash/Buildup strokes; user confirmed major improvement without Alpha Lock. |
| 4.11 | Avoid speculative CPU-clone downloads of GPU-only tiles after stroke commits. | Residency/COW and Alpha Lock stroke tests pass; manual lag persisted, later clarified as large-brush drawing catch-up. |
| 4.12 | Bound ready GPU brush batches by source pixel bytes. | Automated parity, queue and Undo/Redo checks passed. User confirmed residual large-brush mirror lag; further optimization explicitly deferred on 2026-10-03. |
| 4.13 | Erase dab compositing on GPU for RGBA32F pixel brushes. | CPU parity including hidden RGB, selections, mirrored passes, rollback and Undo/Redo; real brush jobs and complete eraser strokes. User confirmed the manual eraser check. |
| 4.14 | Keep unrestricted RGBA32F Wash Erase previews on GPU. | CPU preview/final parity including hidden RGB, channel/selection fallback, failed submit, readback limits and complete eraser strokes; user confirmed the manual check without issues. |
| 4.15 | Final Wash merge on GPU for unrestricted RGBA32F Normal/Erase painting. | CPU parity, COW/Undo/Redo, failed submit, low-budget fallback and tile-aligned parallel final jobs; user confirmed the manual test passed. |
| 4.16 | Selection coverage for RGBA32F Normal/Erase Wash preview and final merge. | Soft/inverted/moved/empty selections, changing/removing selections, submit fallback and full selected/mirrored strokes; user confirmed the manual check passed. |
| 4.17 | Normal Wash Alpha Lock on GPU, including selected painting. | Bundled with 4.18-4.19; all channel masks, CPU parity, COW/Undo/Redo and full strokes; user confirmed the combined manual check passed. |
| 4.18 | Normal Wash RGB channel locks, alone or with Alpha Lock/selection. | Bundled with 4.17/4.19; hidden RGB and channel preservation tested; user confirmed the combined manual check passed. |
| 4.19 | CPU-compatible Erase channel handling on GPU in Buildup and Wash. | Bundled with 4.17-4.18; CPU Erase ignores channel flags, including Alpha Lock; user confirmed the combined manual check passed. |
| 4 | Brush engine: GPU dab rendering and compositing for the pixel brush (mask generation, alpha darken, indirect painting), then color smudge. | Stroke parity tests; input-to-pixel latency measured lower than CPU. |
| 5 | Filters and transforms: blur family, levels/curves, Liquify, Transform Tool, Puppet Warp (preview/final parity). Decide fate of remaining paint ops and color models. | Per-filter parity tests; Puppet Warp invariants from `docs/agent/puppet-warp.md` hold. |

## Build, test, and install

`kritagpu` is built only when CMake finds the Vulkan headers and `glslc`
(the private SDK is passed as described in `docs/agent/vision-ml.md`).

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritagpu KisGpuEngineTest KisGpuGLInteropTest -j 8"
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && ctest --test-dir <krita-dev-root>\_build -R libs-gpu --output-on-failure"
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\libs\gpu\cmake_install.cmake
```

- Known failures unrelated to the GPU engine (identical with and without
  `KRITA_GPU_PROJECTION=1`; not re-checked against a pre-GPU build):
  `kis_pattern_test` (PNG pattern load), `kis_transform_mask_test` (1 px
  rect difference), `kis_cage_transform_worker_test` (reference images),
  `KisPaintOpPresetTest` (embedded pattern load).
- Do not run the whole `libs-ui-*` suite on a desktop session: GUI tests
  such as `kis_view_signals_test` create canvases without the default
  surface format and trigger the `g_sanityDefaultFormatIsSet` safe assert
  in `KisOpenGL::initialize()`, which opens blocking "Internal Error"
  dialogs (observed with and without `KRITA_GPU_PROJECTION=1`; not caused
  by the GPU engine, not checked against a pre-GPU build). Run the GPU
  tests and specific UI tests individually instead. The same applies to
  `plugins-impex-libkra-kis_kra_saver_test`: its documents have an empty
  MIME type, so its saves fail, and without batch mode a failed load or
  save opens a blocking dialog ("File emptytest.kra does not exist").
- Phase 3.3 save test: target `KisGpuSaveTest`, ctest name
  `plugins-impex-libkra-KisGpuSaveTest` (run it from
  `<krita-dev-root>\_build\plugins\impex\libkra\tests`, as ctest does;
  it uses batch mode and the native MIME type, so it opens no dialogs).
- Phase 3.2 test: target `KisGpuCanvasUploadTest`, ctest name
  `libs-ui-KisGpuCanvasUploadTest` (needs a desktop OpenGL context on the
  Vulkan GPU; skips otherwise).
- Phase 2 test: target `KisGpuProjectionTest`, ctest name
  `libs-image-KisGpuProjectionTest`. Changes to the merger must also pass
  `kis_async_merger_test`, `kis_projection_test`, `kis_update_scheduler_test`,
  `kis_walkers_test`, `kis_*layer*_test`, and `kis_filter_mask_test`, both
  without and with `KRITA_GPU_PROJECTION=1`.
- Phase 1 test: target `KisGpuPaintDeviceTest`, ctest name
  `libs-image-KisGpuPaintDeviceTest`. Changes to the tile engine must also
  pass `libs-image-tiles3-*`, `kis_paint_device_test`,
  `kis_transaction_test`, `kis_iterator*_test`, `kis_painter_test`, and
  `kis_low_memory_tests`.
- Test output (including benchmark numbers) is only visible with
  `-o <file>,txt` when running `<krita-dev-root>\_build\bin\KisGpuEngineTest.exe`
  directly.
- Validation run: set `KRITA_GPU_VALIDATION=1` and
  `VK_LAYER_PATH=<krita-dev-root>\VulkanSDK\1.4.357.0\Bin`.
- Benchmark size: `KRITA_GPU_BENCH_SIZE` (pixels per side, default 4096) and
  `KRITA_GPU_BENCH_LAYERS` (default 8).
- Tests skip (not fail) when no suitable Vulkan device or desktop GL context
  exists.

## Source map (`libs/gpu`)

| File | Role |
| --- | --- |
| `KisGpuVulkanFunctions.*` | Runtime loader and function tables. Add every new Vulkan function to an X-macro list. Platform functions are stored untyped so headers never include `<windows.h>`. |
| `KisGpuContext.*` | Instance, device selection, queue, timeline semaphore, memory type lookup, submission, validation messenger. |
| `KisGpuBuffer.*` | Buffer + dedicated memory; `Device`, `Upload` (prefers ReBAR), `Readback` (prefers cached). |
| `KisGpuCommandList.*` | Reusable command buffer, barriers, buffer copies, timestamp queries. Tracks its recording state: `submit()` ends recording even when the submission fails; `abandon()` ends a recording that will not be submitted. |
| `KisGpuComputePipeline.*` | Push-constant-only compute pipelines. |
| `KisGpuTilePool.*` | 64x64 tile slots, chunk growth, batched upload/readback per chunk. |
| `KisGpuLayerStackCompositor.*` + `shaders/composite_over_stack.comp` | Phase-0 normal-blend stack flattening. |
| `KisGpuGLSharedImage.*` (Windows) | Vulkan image shared with a GL texture plus exported semaphores. |
| `KisGpuTileFill.*` + `shaders/fill_tiles.comp` | Fills whole tiles with one color (tests, projection background). |
| `KisGpuLayerCompositor.*` + `shaders/composite_layers.comp` | Phase-2 layer compositing: per-layer blend op, opacity, alpha lock, clip rect. Defines `KisGpuBlendOp`. |
| `KisGpuCanvasPatchWriter.*` + `shaders/canvas_patches.comp` | Phase-3.2 canvas patches: edge-extended texture blocks, display conversion (identity / matrix-shaper), F32/F16 in and out. |
| `KisGpuGLSharedBuffer.*` (Windows) | Vulkan buffer shared with a GL pixel unpack buffer, with the Vulkan/GL semaphore cycle. |
| `KisGpuGLInterop_p.h` (private) | GL_EXT_memory_object / GL_EXT_semaphore entry points and the device-UUID check shared by the interop classes. |

## Source map (`libs/image`)

| File | Role |
| --- | --- |
| `tiles3/KisTileGpuState.h` | Per-tile-data GPU state and the `KisTileGpuHooks` entry points. |
| `tiles3/kis_tile_data*.{h,cc}` | `gpuState()`, `installGpuState()`, `notifyCpuWrite()`, `dropClones()`; download hook in `blockSwapping()`; state release in the destructor. |
| `tiles3/kis_tile.{h,cc}` | `detachForExternalWrite()`; `lockForWrite()` calls `notifyCpuWrite()`. |
| `tiles3/kis_tile_data_store.cc` | The swapper downloads and evicts idle GPU slots before disk swapping; prepared/in-flight slots are skipped. |
| `gpu/KisGpuTileBackend.*` | Process-wide context, RGBA32F/RGBA16F pools, downloads, deferred slot release, retired resources. Never destroyed. |
| `gpu/KisGpuTileAccess.*` | Public GPU access to device tiles (see above). |
| `gpu/KisGpuMergeBatch.*` | Batches composites inside `KisAsyncMerger`; eligibility checks; CPU fallback. Always compiled; inert without `HAVE_KRITA_GPU_ENGINE`. |
| `gpu/KisGpuEngineSettings.*` | kritarc keys, the convert policy, GPU color spaces and the conversion target. Always compiled. |
| `gpu/KisGpuProjectionCompositor.*` | Runs a batch: scratch tiles, layer accesses, dispatch, write-back; per-thread work contexts. |
| `kis_async_merger.{h,cpp}` | `m_gpuBatch` member, `tryAdd` in `compositeWithProjection`, flush points. |
| `kis_updater_context.cpp` | Tile-aligned job exclusivity when `KisGpuMergeBatch::mayCompositeOnGpu`. |

## Source map (`libs/ui`)

| File | Role |
| --- | --- |
| `opengl/KisGpuCanvasUploader.*` (Windows, `HAVE_KRITA_GPU_CANVAS`) | GL interop check, display conversion setup, shared-buffer pool, Vulkan submission of canvas patches, acquire/release on the GUI thread. |
| `opengl/kis_texture_tile_update_info.h` | `uploadGeometry()` (shared CPU/GPU layout), `setGpuUpload()`. |
| `opengl/kis_texture_tile.{h,cpp}` | GPU branch of `update()` (upload from the bound shared buffer); class exported for tests. |
| `opengl/KisOpenGLUpdateInfoBuilder.{h,cpp}` | GPU path for the live canvas (`allowGpuUpload`), CPU fallback with batched downloads. |
| `opengl/kis_opengl_image_textures.cpp` | `checkGLInterop()` in `initGL()`; acquire/release around tile updates; readback of failed GL imports. |
| `KisGpuEngineUi.*` | Conversion offer, failure message, pre-save warning, `KisGpuEngineSettingsWidget`. |
| `KisMainWindow.cpp` | `install()`, `offerConversion()`, `confirmSave()` hooks. |
| `KisImportExportManager.cpp` | `mayFinishExport()` before the target file is replaced. |
| `dialogs/kis_dlg_preferences.{h,cc}` | The settings widget in the Performance tab. |
| `tiles3/KisTileGpuHooksStub.cpp` | Hooks for builds without `kritagpu`. |

## Configuration and compatibility

- kritarc (default group): `Solstice/GpuEngine` (bool, default false) and
  `Solstice/GpuEngineConvertDocuments` (int: 0 Ask, 1 Convert, 2 Keep;
  default 0). See "Feature gate and user interface".
- Environment `KRITA_GPU_PROJECTION=1` or `0` overrides `Solstice/GpuEngine`
  (development switch; `KisGpuMergeBatch::setEnabled()` in tests).
- No `.kra` changes or persisted ids: `.kra` files stay readable by the CPU
  path and upstream Krita; GPU mode stores RGBA F32/F16 layers with the
  normal Krita color space ids. Saving works through the CPU path: the tile
  compressor locks tiles, which downloads stale data.

## Invariants

- `kritagpu` depends only on `kritaglobal` and Qt Core/Gui. Never link the
  Vulkan loader at build time; never include `<windows.h>` from its headers.
- `kritaimage` builds and works without `kritagpu` (`HAVE_KRITA_GPU_ENGINE`
  off): no tile data then ever gets a GPU state.
- At least one of a tile data's CPU and GPU copies is valid, except between
  `KisGpuTileAccess::prepare()` and `submitAndFinish()` for tiles the access
  writes. No CPU thread may lock those tiles in that window (stroke
  discipline). A failed download never leaves both copies invalid; lost
  content is marked `ContentLost` and stops the engine instead of being
  treated as valid.
- `detachForExternalWrite` refuses (sets `isLocked`) when a CPU user holds
  the tile, whether or not its data is shared. This is a snapshot check;
  GPU writers must still not run concurrently with CPU users of the same
  tiles.
- Uploads are published only through `submitAndFinish()`, never with a
  stale CPU write generation.
- Process-wide Vulkan objects (`KisGpuTileBackend`, the projection
  compositor's work contexts) are never destroyed: static destruction can
  run after the Vulkan loader and layers are torn down (this crashed the
  tests with `0xC0000409` under the validation layer).
- Every command buffer of a command list (the main one and the preamble)
  begins with a full memory barrier; the engine's only
  GPU dependency model is submission order on the single queue. Flags change
  only after the submission exists (`finish(value)`), so a later download is
  always ordered after the write it depends on.
- Lock order: `KisGpuTileBackend::residencyMutex()`, then
  `KisTileGpuState::mutex` (several: sorted by address), then
  `KisGpuTileBackend::m_transferMutex`.
- The GPU merge batch must be flushed before anything reads
  `KisAsyncMerger::m_currentProjection`; when adding a new reader to the
  merger, add a flush before it.
- Canvas patches written by the GPU must keep exactly the layout of
  `KisTextureTileUpdateInfo::uploadGeometry()` (including zero corners);
  change both paths together.
- A `KisGpuGLSharedBuffer` is never written by Vulkan while GL may still
  read it: it returns to the pool only when no tile info references it.
- The GPU writes projection tiles in place only while merge jobs that
  share a tile are kept apart (`KisUpdaterContext::walkerIntersectsJob`)
  and only for projections on the 64 px image grid.
- Krita must start and work without Vulkan; GPU features degrade to the CPU
  path.
- Blend results match the `KoCompositeOp` RGBA float implementation (alpha-0
  color excepted). Preview and final render must stay identical (Transform
  Tool, Puppet Warp).
- Tile slots are released only after all GPU work using them has completed.
- GL objects of a shared image are created and destroyed with its GL context
  current; the image stays in `VK_IMAGE_LAYOUT_GENERAL`.

## Manual regression checklist

Phase 0 has no user-visible behavior. From phase 3 on:

1. Open, display, zoom, rotate, and mirror a multi-layer document in GPU mode.
2. Paint, undo, redo; compare with the CPU mode result.
3. Save, reopen in CPU mode and in upstream Krita.
4. Enable Preferences → Performance → GPU Engine, restart: the status shows
   the active GPU after opening an RGBA float document.
5. Open an RGBA 8-bit PNG: the conversion question appears; Convert keeps
   the appearance and undo restores 8-bit; Keep leaves it on the CPU path;
   "Do not ask again" is honored on the next open and changeable in
   Preferences.
6. Disable the setting and restart: no question, no GPU use
   (`KRITA_GPU_PROJECTION=0` likewise).

## Review status

### Canvas interop investigation (2026-10-03)

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

### Earlier phase reviews

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

### Tile memory budget and eviction

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

### Canvas transfer buffer budget and retirement

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

### Pixel brush prototype (phase 4.1)

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

### Selection coverage for pixel brushes (phase 4.2)

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

### Normal brush channel locks (phase 4.3)

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

### Mirrored pixel-brush compositing (phase 4.4)

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

### Combined mirror submissions (phase 4.5)

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

### Brush job integration coverage (phase 4.6)

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

### GPU clipping to CPU paint rectangles (phase 4.7)

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

### Queued stroke measurements and Wash readback batching (phase 4.8)

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

### GPU Wash preview (phase 4.9)

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

### Sparse mirror destinations (phase 4.10)

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

### Post-stroke speculative CPU clones (phase 4.11)

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

### Byte-bounded brush updates (phase 4.12)

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

### Erase dab compositing (phase 4.13)

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

### Wash Erase GPU preview (phase 4.14)

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

### GPU final Wash merge (phase 4.15)

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

### Selected Wash compositing (phase 4.16)

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

### Channel-locked Wash and Erase bundle (phases 4.17-4.19)

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

## Risks and open questions

- Interop requires desktop OpenGL; users on ANGLE must switch renderer.
- Memory management now bounds and evicts tile pools, including a path to
  disk swap. Canvas transfer buffers have a separate cap. GPU-resident tiles
  still retain CPU snapshots, other temporary allocations and GL textures
  have no shared budget, and eviction downloads need batching.
- A CPU read of a stale tile costs one synchronous submission per tile
  (see the phase 1 measurements); bulk readers should call
  `KisGpuTileAccess::syncToCpu` first. `KisTiledDataManager::readBytes`
  could batch this automatically.
- `KisGpuLayerStackCompositor`, `KisGpuLayerCompositor`, and
  `KisGpuTileFill` reuse one host-visible table buffer, so callers must wait
  between recordings (the projection compositor keeps one per work context);
  replace with a per-submission ring before overlapping submissions.
- Phase 2 waits for the GPU inside each merge job (one submission per
  batch). Fine for correctness; phase 3 should pipeline submissions.
- Unsupported in the GPU projection (CPU fallback): layer styles, blend modes
  outside the list above, partial RGB channel flags, layers whose offset is
  not a multiple of 64 relative to the projection, color space mismatches.
- The whole-stack upload measured 55 GB/s with one `vkCmdCopyBuffer` region per
  tile; evaluate a compute scatter from ReBAR memory for document load.
- Parity of the remaining blend modes and of 16-bit float rounding (Krita's F16
  op rounds after every layer; the GPU rounds once).
- Python scripting (`libkis`) and file export need CPU pixel access; every such
  call becomes a synchronous download.
- Merging later `krita-sol` feature work into `krita-sol-gpu` is expected to
  conflict in `libs/image` once phase 1 starts.
