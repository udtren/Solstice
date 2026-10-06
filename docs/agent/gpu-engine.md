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
  passed. Phases 4.20-4.22 extend the existing separable layer blend modes to
  direct brush dabs and Wash preview/final merging, including selections and
  channel restrictions. The user confirmed their combined manual test passed.
  Phases 4.23-4.25 pipeline brush submissions through three reusable contexts,
  reuse upload allocations in size buckets and cap their total at 64 MiB.
  Their combined manual check passed, as confirmed by the user. Phases 4.26-4.28
  add Linear Burn, Linear Light and Pin Light to float layer compositing,
  RGBA32F Buildup dabs and Wash preview/final merging; the user confirmed their
  combined manual check passed. Phases 4.29-4.31 add the separable and HSY blend
  families described below as one bundle; the user confirmed the combined
  manual check passed.
  Phases 4.32-4.33 batch ordinary bulk CPU reads and distinguish initial,
  CPU-to-GPU transition and resident projection timings (details below).
  The subsequent manual report identified drawing catch-up with textured
  brushes at 150px and above; the screenshot also has Masked Brush enabled.
  Phases 4.34-4.35 address masked-stroke readbacks and staging reallocation
  pressure, with a new textured/masked full-stroke regression matrix.
  The user confirmed almost no lag at 150px and acceptable gradual lag above
  about 250px. Further tuning of that reported case is not currently needed.
  Phases 4.36-4.37 add per-channel layer projection in F32/F16 and align the
  established F16 generic blend math with CPU half arithmetic (details below).
  The user confirmed the combined real-app check passed.
  Phases 4.38-4.40 reduce CPU-to-GPU transition costs with shared upload
  allocations, bounded retirement outside the shared lock, and host-cached
  transfer-only staging. The user confirmed their real-app check passed.
  Phase 4.41 batches voluntary tile eviction readbacks while retaining busy
  tiles and GPU-only data after failed transfers. The user confirmed its
  real-app check passed. Details and checks are below.
  Phase 4.42 refreshes all README benchmark workloads and ordinary complete
  strokes on the current build, with a strengthened canvas benchmark.
  It changes measurement coverage only; no application DLL changes.
  Phases 4.43-4.45 reduce ordinary-stroke tile costs: GPU COW snapshot copying,
  whole-tile sharing without readback, and batched partial-clear edge reads.
  Controlled short-stroke results and regression coverage are documented below;
  the user confirmed this bundle's real-app test passed.
  Phase 4.46 adds RGBA16F Normal/Erase Buildup with selections and channel
  locks; the user confirmed its real-app test passed. Phases 4.47-4.48 add
  F16 Alpha Darken and Normal/Erase Wash preview/final merge; the user confirmed
  this bundle passed. Phases 4.49-4.50 extend F16 Buildup/Wash to basic generic
  modes through Pin Light; the user confirmed this bundle passed. Phases
  4.51-4.52 add Soft Light SVG, Color Dodge/Burn and HSY color modes in F16
  Buildup/Wash; the user confirmed the real-app checks passed. Other extended
  F16 brush modes retain CPU fallback. Phase 4.53 reuses completed projection
  work contexts first and permits three pending serial submissions; details below.
  The user confirmed phase 4.53 and the CPU filter/FFT readback bundle
  (4.54-4.55). Phases 4.56-4.57 batch affine transform and layer-flip readbacks;
  details and verification are below. Filters and transforms still execute
  their calculations on the CPU.
  Phases
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
  `kis_tiled_data_manager.cc` (automatic interleaved/planar bulk readback),
  `libs/ui/tool/strokes/kis_painter_based_stroke_strategy.cpp` (batch prefetch
  before CPU masking-brush patch jobs),
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
Dodge, Subtract, Darken, Lighten, Difference, Overlay, Hard Light, Exclusion,
Linear Burn, Linear Light, Pin Light, Soft Light SVG/Photoshop, Color Dodge,
Color Burn, Divide, Vivid Light, Hard Mix/default/Photoshop/Softer Photoshop,
Grain Merge/Extract, Negation and Allanon
(`KoCompositeOpGenericSC` with the `cf*` functions and their clamp policies,
including the alpha-locked branch and `KoCompositeOpBase`'s clearing of
alpha-0 destination pixels). HSY Hue, Saturation, Color, Luminosity, Darker
Color and Lighter Color use `KoCompositeOpGenericHSLFunctor` semantics.
Separately named HDR variants and HSI/HSL/HSV families remain on the CPU.
All other modes run on the CPU.

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

The order of work after phase 4.57 is set in
`docs/agent/gpu-work-priorities.md`; record measurements and phase details
here and keep that document as the ordering reference.

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
| 4.20 | Separable GPU blend modes for RGBA32F Buildup dabs. | Bundled with 4.21-4.22; CPU pixel parity, ordered mirrors, COW/Undo/Redo and failure tests; user confirmed the combined manual test passed. |
| 4.21 | The same blend modes for GPU Wash preview and final merge. | Shared shader math, full strokes and GPU-path counters; user confirmed the combined manual test passed. |
| 4.22 | Selection/channel-lock parity for the added brush modes, including sparse dab tile regions. | All 16 channel masks, hidden RGB, empty/moved/inverted selection, source-region parity and failed-submit recovery; user confirmed the combined manual test passed. |
| 4.23 | Pipeline brush submissions without a completion wait after each batch. | Three contexts retain in-flight source/mask/table bytes; real blocked-queue parity and CPU-read tests; combined manual check passed (user confirmed). |
| 4.24 | Reuse brush upload allocations in 256 KiB buckets. | Variable-size dabs reuse existing capacity; completed-work benchmarks include explicit waits; combined manual check passed (user confirmed). |
| 4.25 | Bound the whole brush staging ring to 64 MiB. | Drain before slot reuse or reclamation; pressure, oversized refusal, failure/CPU fallback and Undo/Redo tests; combined manual check passed (user confirmed). |
| 4.26 | Linear Burn, Linear Light and Pin Light for RGBA32F/RGBA16F layer compositing. | CPU parity and direct GPU clamp-boundary tests; bundled validation below. |
| 4.27 | The same three blend modes for RGBA32F Buildup dabs. | Selections, all channel masks, mirrors, COW/Undo/Redo and submit fallback; bundled validation below. |
| 4.28 | The same three blend modes for RGBA32F Wash previews and final merging. | Selected/restricted preview/final parity and complete strokes; bundled validation below. |
| 4.29 | Soft Light, Dodge/Burn, Divide, Vivid Light, Hard Mix and related separable modes. | Shared F32/F16 layer and F32 brush shaders; bundled with 4.30-4.31. |
| 4.30 | HSY Hue/Saturation/Color/Luminosity and Darker/Lighter Color. | Dedicated non-separable alpha/clamp behavior; bundled with 4.29/4.31. |
| 4.31 | Selection, channel-lock and complete stroke coverage for the new major modes. | Representative Buildup/Wash parity and Undo/Redo; user confirmed the combined manual check passed. |
| 4.32 | Automatically batch bulk CPU pixel reads and bound explicit prefetch storage. | F32/F16 interleaved and planar reads, padding, sparse/negative/offset regions, batch boundaries and failed-transfer retry. |
| 4.33 | Separate initial, CPU-to-GPU transition and resident projection measurements. | GPU completion, repeated CPU/GPU medians, actual GPU-path use and full pixel parity checked. |
| 4.34 | Batch masked-brush stroke readback before parallel CPU mask patches. | Complete masked/textured 150px and 300px strokes match CPU layers/projections and Undo/Redo. |
| 4.35 | Reuse fitting brush staging contexts under budget pressure. | Two large buffers remain reusable within 64 MiB instead of rotating through repeated third-slot allocations; deterministic context-count and pixel regression. |
| 4.36 | Individual RGB channel flags for F32/F16 layer projection. | All 16 channel masks, full refresh and partial updates; eight representative blend modes. User confirmed the combined manual check passed. |
| 4.37 | CPU-compatible half alpha tests and intermediate rounding in established F16 blend modes. | Transparent RGB, near-zero/near-one alpha and Hard Light/Overlay midpoint boundaries; bundled with 4.36. User confirmed the combined manual check passed. |
| 4.38 | Share projection upload buffers across layers in one submission. | Geometric growth up to 16 MiB chunks; F32/F16 offsets, rollover, oversized uploads, lifetime and failed-submit parity. |
| 4.39 | Bound automatic retired-resource reclamation and destroy outside its shared lock. | Slow destructors cannot block other retirement; new tile allocation reclaims at most 16 resource entries, explicit flush drains completed entries. |
| 4.40 | Host-cached memory for transfer-only CPU snapshots. | Original direct shader Upload memory stays unchanged; three-process transition/initial timing and CPU parity checks. |
| 4.41 | Batch GPU tile eviction under memory pressure. | Up to 256 tiles per batch; F32/F16, mixed sizes, busy locks, failed transfers, actual released bytes and Undo/disk-swap/concurrent eviction checks. |
| 4.42 | Refresh the performance baseline and validate measured canvas paths. | Three fresh processes per workload, completed GPU work, CPU parity and rejection of silent CPU fallback; ordinary short strokes remain slower than CPU. |
| 4.43 | Copy retained CPU snapshots directly during GPU COW. | F32/F16 ReadWrite/WriteOnly, submission failure, shared source and Undo/Redo coverage. |
| 4.44 | Share whole current/old tiles without CPU synchronization in exact/rough copies. | F32/F16 no-download assertions, partial-copy parity, independent later writes and Undo/Redo. |
| 4.45 | Batch partial-clear edge downloads; discard fully cleared tiles without reading. | Aligned, negative-coordinate, one-row and failed-batch cases; combined stroke comparison below. |
| 4.46 | RGBA16F Normal/Erase Buildup with per-dab half rounding. | Selections, channel flags, mirrors, async ring lifetime, rollback and complete-stroke checks; F16 Wash remains CPU. |
| 4.47 | RGBA16F hard/creamy Alpha Darken dabs. | CPU variant parity, fractional flow and masks, async ring lifetime. |
| 4.48 | RGBA16F Normal/Erase Wash preview and final merge. | Scalar half arithmetic, selection/channel flags, fallback and complete-stroke checks. |
| 4.49 | RGBA16F basic generic dab modes through Pin Light. | Per-dab half rounding, Exclusion intermediate correction, all channel masks, alpha boundaries and async batches. |
| 4.50 | RGBA16F basic generic Wash preview/final merge. | Soft selections, all channel locks, CPU fallback, Undo/Redo and actual brush jobs. |
| 4.51 | RGBA16F extended major dab modes. | Soft Light SVG, Color Dodge/Burn and HSY color modes, with CPU half intermediates. |
| 4.52 | RGBA16F extended major Wash preview/final merge. | Selection/channel/lifecycle matrix and actual mirrored strokes. |
| 4.53 | Reuse completed projection contexts before waiting for busy ones. | Three pending serial submissions, oldest-first reuse, queue-gated lifetime/fallback/Undo tests. |
| 4.54-4.55 | Batch CPU filter inputs/destinations and FFT cache reads. | 24 actual-filter rows, CPU parity and failure/Undo checks; user confirmed real-app operation. |
| 4.56-4.57 | Batch affine transform and layer-flip readbacks. | F32/F16 exact CPU parity, full/partial transforms, transfer counts, failed download and Undo checks. |
| 4.58 | Opt-in CPU timeline from input receipt through Qt frame swaps. | Infrastructure in progress; event-to-pixel attribution and real-app baseline remain open. |
| 4.59 | Stable input/update IDs and per-widget upload/frame command coverage. | Partial lineage only; compressed-input/job/projection attribution remains open. |
| 4.60 | Scheduled job identities and explicit creation ancestry across worker threads. | Unit/scheduler/stroke regressions pass; batch input sets and projection lineage remain open. |
| 4.61 | Logical dab requests, cache-hit identity and explicit paint-batch membership. | Queue and stroke regressions pass; dirty/projection/frame dependency chain remains open. |
| 4.62 | Dirty-group IDs through projection splitting/merging to canvas preparation. | All 28 regression batches reach executed projection; all 21 input-linked real-app batches reach swapped command descendants. |
| 4.63 | Per-input request audit, stroke-start condition snapshots and tracing-cost check. | Automated checks pass; conditions installed, real-app condition capture pending. Full-region coverage and interactive baseline remain open. |
| 4.64 | Audit every recorded projection/presentation branch, not only a successful descendant. | Python tests pass; existing real-app capture passes for 21 batches/64 request-producing inputs. Geometric coverage remains open. |
| 4.65 | Record image-space projection and canvas-notification rectangles; exact LOD-0 containment checks. | 100 regression requests covered by executed walker request regions; no-canvas stage explicitly unverified. Upload/presentation geometry remains open. |
| 4.66 | Record uploaded patches and logical-widget regions; invalidate pending coverage on view changes. | Automated checks pass; combined real-app capture verifies four condition records, 33 input-linked batches and all 450 upload geometries/swap chains. |
| 4.67 | Explicit tool input-to-stroke membership and joined per-input recorded checks. | Installed; analysis tests and stroke regressions pass. Combined real-app capture verifies membership for all 106 accepted inputs and joined checks for all 94 request-producing inputs; full latency baseline remains open. |
| 4.68 | Dirty collection/submission and compressed-update-to-upload geometry. | Installed; 28 ordinary and six masked dirty groups pass; analysis tests 24/24. Combined real-app capture passes for 94 request-producing inputs, 40 input-linked batches and all 472 uploads. |
| 4 | Brush engine: GPU dab rendering and compositing for the pixel brush (mask generation, alpha darken, indirect painting), then color smudge. | Stroke parity tests; input-to-pixel latency measured lower than CPU. |
| 5 | Filters and transforms: blur family, levels/curves, Liquify, Transform Tool, Puppet Warp (preview/final parity). Decide fate of remaining paint ops and color models. | Per-filter parity tests; Puppet Warp invariants from `docs/agent/puppet-warp.md` hold. |

Per the user's priority change on 2026-10-06, remaining Blend Mode extensions
are deferred until after phase 5, as the final item in
`docs/agent/gpu-work-priorities.md`. Current overhead analysis and GPU dab
generation retain their earlier priority; existing phase numbers are unchanged.

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
| `opengl/KisOpenGLUpdateInfoBuilder.{h,cpp}` | GPU path for the live canvas (`allowGpuUpload`), CPU fallback with batched downloads; `buildUpdateInfos()` shares one upload between rects. |
| `canvas/KisCanvasUpdateBatcher.*`, `canvas/kis_canvas2.cpp` | Group commit of concurrent projection updates into shared builds (phase 4.81). |
| `canvas/kis_canvas_updates_compressor.*` | `putUpdateInfos()`: atomic put of one batch. |
| `canvas/kis_abstract_canvas_widget.h`, `canvas/kis_canvas_widget_base.*`, `opengl/kis_opengl_canvas2.*`, `opengl/KisOpenGLCanvasRenderer.*`, `opengl/kis_opengl_image_textures.*` | `sharesProjectionUploads()` / `startUpdateCanvasProjections()`; batch-wide GL hold in `KisOpenGLCanvas2::updateCanvasProjection(QVector)`. |
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
- Canvas updates sharing one upload (a `KisCanvasUpdateBatcher` build) are
  put into the compressor together and applied in one GUI pass under one GL
  hold. Do not split them across passes or release the hold between them.
- A batched canvas update's caller returns only after its build submitted and
  put it, preserving the order relative to later conflicting walkers.
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

### Separable brush blend bundle (phases 4.20-4.22)

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

### Bounded asynchronous brush submissions (phases 4.23-4.25)

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

### Extended blend coverage (phases 4.26-4.28)

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

### Blend-family bundle (phases 4.29-4.31)

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

### Bulk readback and projection measurements (phases 4.32-4.33)

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

### Textured and masked brush catch-up (phases 4.34-4.35)

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

### Layer channel flags and F16 blend arithmetic (phases 4.36-4.37)

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

### CPU-to-GPU transition preparation (phases 4.38-4.40)

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

### Batched voluntary tile eviction (phase 4.41)

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

### Current-build benchmark baseline (phase 4.42)

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

### Ordinary-stroke tile costs (phases 4.43-4.45)

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

### RGBA16F Normal/Erase dabs (phase 4.46)

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

### RGBA16F Alpha Darken and Wash (phases 4.47-4.48)

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

### RGBA16F basic blend brushes (phases 4.49-4.50)

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

### RGBA16F extended major blend brushes (phases 4.51-4.52)

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

### Projection context reuse (phase 4.53)

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

### Batched CPU filter and FFT readback (phases 4.54-4.55)

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

### Batched affine transform and layer-flip readback (phases 4.56-4.57)

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

### Closed Transform Tool Undo investigation (October 4, 2026)

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

### Paint pipeline trace foundation (phase 4.58)

Started October 5, 2026, as priority 1 of `gpu-work-priorities.md`.
This is measurement infrastructure only. Priority 1 is not complete:
per-event pixel/frame attribution and the 64px/256px Buildup/Wash three-path
baseline still need implementation and real-application capture. Do not move
on to dab generation or more blend modes on the strength of these traces.

`KisPaintTrace` is enabled only when `KRITA_PAINT_TRACE` contains an output
filename prefix. Normal process teardown writes `<prefix>.<pid>.json` using
QSaveFile. A bounded 262,144-event buffer uses one steady-clock timebase across
threads. Overflow is counted in metadata and makes a capture unusable for
comparisons. No file I/O, GPU fences, queue changes, or configuration writes
are added to the measured path. Disabled hooks return without acquiring the
recording lock, reading the clock, or allocating a recorder. Enabled recording
does take a mutex; its overhead must be measured before drawing conclusions.
Do not call `flush()` during a timed capture: export snapshots allocate and
perform I/O. Abnormal termination may lose the buffered trace.

Captured boundaries:

| Event | Meaning |
| --- | --- |
| `input.mouse_*`, `input.tablet_*` | Arrival at the canvas input manager, before its filtering/compression; includes hover and rejected events |
| `tool.begin/move/end` | Freehand helper dispatch on the GUI thread |
| `stroke.enqueue`, `stroke.end_requested` | Image scheduler submission requests |
| `stroke.run`, `stroke.dirty` | Freehand callback and dirty-notification CPU spans |
| `dab.generate_and_postprocess` | Executed CPU dab generation/postprocessing; cache hits need not run this span |
| `brush.cpu_composite`, `brush.gpu_attempt_and_fallback` | CPU painting job or GPU-path attempt including possible CPU fallback, not GPU execution time |
| `projection.merge` | Merger CPU span; Vulkan work may outlive it |
| `canvas.prepare`, `canvas.upload` | Canvas data preparation and compressed update upload spans |
| `canvas.paint`, `canvas.frame_swapped` | OpenGL widget paint and Qt frameSwapped callback; not physical scanout |

Each record includes thread ID and opaque `owner`/`related` identities.
Enqueue/run share a job-data identity, but pointer identities can be reused
after destruction and are not persistent event IDs. Canvas records use their
canvas/widget identities. Parallel or nested spans cannot be added to obtain
elapsed latency. Critically, the next swap after an input may still show old
pixels; the summary deliberately does not report that difference as latency.
Future event lineage must survive input compression, asynchronous dab batches,
dirty-rectangle/projection compression and update-info upload before a frame
can be attributed to that input. Multiple documents/views and canceled/no-op
inputs must be accounted for. QPainter/QtQuick presentation is not covered.

User-invoked capture (agent must not start or stop the real application):

```bat
build-tools\paint-trace\run.cmd <krita-dev-root> brush
```

The second argument is `cpu`, `projection` or `brush` (default). It sets only
process-local overrides: projection/brush 0/0, 1/0 or 1/1, disables Vulkan
validation for timing, and prepares the same dependency/Python environment
as the working debug launcher. An existing Krita/Solstice process blocks
launch, avoiding forwarding to a process with different flags. `--check` as
the third argument validates setup without starting an application. Output
goes to `%TEMP%/solstice-paint-trace-<mode>.<pid>.json`; startup stderr is in
the adjacent `.launch.log`. Flags in metadata describe requested paths, not
proof of successful GPU execution. Never change `kritarc` for the comparison.

For initial capture validation, open a single RGBA32F document, use a plain
pixel brush without smoothing/texture/masks, draw a few short strokes with
pauses, and close normally. First establish that input, job and frame events
are all present. This smoke capture is not the performance baseline.

```bat
python build-tools/paint-trace/summarize.py <trace.json>
python -B -m unittest discover -s build-tools/paint-trace -p test_summarize.py
```

The summary validates schema/units and rejects overflow, then reports counts
and median/p95 CPU span durations per event name. It does not sum overlapping
spans, infer pixel causality, or report a drawing speedup. The raw format uses
Chrome trace-event JSON for timeline inspection. Tests cover disabled mode in
a fresh process, four concurrent writers, export, and bounded overflow;
timing values are not pass/fail performance thresholds.

After lineage is validated, the planned baseline remains 64px/256px ×
Buildup/Wash × all three paths, with warmup, at least three fresh processes,
explicit sample counts and fixed document/preset/view conditions. Include
tracing-disabled comparisons to quantify measurement overhead. Re-run the
README operation workloads at that point; do not relabel old numbers as new.

Validation on October 5, 2026: `KisPaintTraceTest` passed 5/5 including
init/cleanup; Python summary tests passed 3/3. The four ordinary 64px/256px
Buildup/Wash rows of `KisGpuStrokeTest` passed with tracing enabled and again
disabled (6/6 each including init/cleanup). Each row compares all three
painting paths and verifies pixels, Undo/Redo and Vulkan validation errors.
These single-sample regression timings are not a performance baseline.
The enabled run exported a parseable trace with no dropped events; as this
fixture has no UI, it cannot verify input or frame-swap hooks. That remains
a real-app smoke check at that stage. Both launcher environment checks passed without
starting Solstice. A local convenience launcher is
`%TEMP%/solstice-paint-trace.bat` (brush path).

The rebuilt version/image/brush/UI/paintop libraries and default paintop
plugin were installed with the application closed; all six build/install
SHA256 hashes matched. Logs: `%TEMP%/solstice-paint-trace-build.log`,
`solstice-paint-trace-test.txt`, `solstice-paint-trace-strokes-{enabled,disabled}.txt`
and `solstice-paint-trace-install.log`. No application preferences changed.

### Paint trace identity and frame coverage (phase 4.59)

The first real-app capture passed: `%TEMP%/solstice-paint-trace-brush.39868.json`
contains seven freehand begin/end pairs, 697 moves, 726 projection spans,
397 upload spans and 1,485 Qt frame swaps, with zero dropped events.
Both tablet and mouse input streams are present. Counting their combined
arrivals as brush samples, or pairing each with the next swap, is invalid.
This is a smoke result, not a latency measurement or a three-path baseline.

Schema 2 adds process-unique numeric IDs serialized as strings. Dispatch
scopes in the input manager expose a thread-local input identity to the
freehand tool. Nested non-input events clear it temporarily; nested pointer
events replace it and restore the outer identity on return. Worker threads
cannot inherit it. `input.accepted_*` means synchronous freehand dispatch,
not a promise that the event creates a dab. Delayed/compressed input dispatched
outside the original scope is deliberately unattributed. Propagating lineage
through compression, brush jobs, dirty notifications and projection is still
required before reporting input-to-pixel latency.

Each `KisUpdateInfo` has an ID (zero with tracing off). `update.ready` marks
preparation; compression records `update.superseded`, and successful animation
update merging records `update.merged`. Neither means that old pixels survive.
`update.upload_issued` assigns a separate occurrence ID after the nonempty
texture upload loop returns successfully, so reuse of a cached update is not
mistaken for a single upload. Early exits are not marked successful. This
records command issuance, not GPU completion or a GL error check.

The OpenGL widget owns a tracing-only `KisCanvasPaintTrace`. It tracks remaining
render/blit regions in widget coordinates for the visible part of each upload.
Decoration-only paints cannot consume unrendered image regions. A cached image
render can be followed by a later blit; partial coverage remains pending.
`frame.covered_upload` connects a covered occurrence to `frame.submitted`.
`frame.replaced` connects paints occurring before the previous swap; only this
widget's `frameSwapped` signal acknowledges the pending frame. Extra swaps and
uploads arriving between paint and swap cannot acknowledge a future frame.
Resize clears tracking; uploads outside the viewport remain unassociated.
Tracking is bounded to 4,096 pending occurrences; overflow marks the whole
trace incomplete so the summary rejects it.

Coverage is a conservative record of render/blit commands, not proof that an
upload's pixels survived later overwrites or became physically visible. Capture
with a fixed view (no pan/zoom/rotation, resize, animation, wrap-around or LOD
changes); coordinate changes and non-OpenGL canvases are not attribution targets
yet. The summary accepts schemas 1/2, counts only explicit same-widget frame
chains and reports no latency or speedup. Merged/superseded updates remain raw
graph evidence, not extra successful samples. No waits or repaint requests
were added, and the tracking object is absent when tracing is disabled.

Automated coverage: `KisPaintTraceTest` 6/6 (including init/cleanup),
`KisCanvasPaintTraceTest` 5/5, and Python summary tests 4/4. Tests exercise
nested input dispatch, thread isolation, stable ID export, partial render and
delayed blit, late uploads, duplicate swaps, separate widgets, reset and bounded
state. The summary regression rejects implicit nearest-swap attribution and
cross-widget matches. `KisGpuStrokeTest` passed the 64px/256px Buildup/Wash rows
with tracing on and off (6/6 each), and `KisGpuCanvasUploadTest` passed 28/28 with Vulkan
validation enabled. Timing values from these regressions are not benchmarks.

Real-app schema-2 verification passed on October 5, 2026:
`%TEMP%/solstice-paint-trace-brush.19084.json` contains nine freehand strokes,
657 linked freehand dispatches (9 begins, 639 moves, 9 ends), and 772 upload
occurrences. All 772 have render/blit coverage and an explicit same-widget
swapped-frame chain. No events were dropped. There are 1,190 submitted frames
and 1,187 attributed swaps; the 1,206 raw Qt swaps must not be treated as 1,206
new image updates. This confirms coverage bookkeeping in the real app, not
input-to-pixel latency. Priority 1 remains in progress.

The six rebuilt version/image/brush/UI/paintop libraries and default paintop
plugin were installed with Solstice closed; build/install SHA256 hashes match.
Logs use `%TEMP%/solstice-paint-lineage-` with suffixes `build.log`, `test.txt`,
`canvas-test.txt`, `strokes.txt`, `strokes-disabled.txt`, `upload.txt` and
`install.log`. Both GPU regression traces exported schema 2 with no dropped
events; these fixtures do not exercise real freehand input or the canvas widget
frame hooks. No preferences were changed and the application was not launched.

### Scheduled job trace lineage (phase 4.60)

`KisStrokeJob` now assigns a process-unique ID at construction and records
`job.created`, `job.started`, `job.finished` and `job.destroyed`. The creation
parent is the executing job's ID, or the synchronous input-dispatch ID when
there is no executing job. Mutation-generated jobs therefore retain their
creating context even when they run later on a different worker. Queued jobs
destroyed without execution remain distinct from completed jobs. IDs belong
to the wrapper, not the reusable data pointer or copied LOD data.

`KisPaintTrace::JobScope` sets/restores a thread-local job identity around the
strategy call. CPU scopes and instant records created there include `args.job`;
`job.run` covers the strategy call including its nested work. No scheduler
ordering, queue ownership, dependency or wait behavior changed. The disabled
path reads neither clocks nor trace locks and allocates no recorder; the wrapper
adds one ID field and cheap disabled checks. Enabled-trace overhead is still
unquantified and must be measured before publishing a performance baseline.

The summary reports creation-to-start intervals for explicitly matching job IDs
(including queue insertion and scheduling), counts unexecuted destroyed jobs,
and counts jobs with a freehand-input ancestor. **Creation ancestry is not a
complete pixel dependency graph:** timer-generated work may have no input parent,
and a batch can consume dabs from several earlier inputs. Never label all batch
pixels as belonging to the context that created its flush job. Delayed input,
dab-batch input sets, dirty rectangles and compressed projection requests still
need attribution. No input-to-display latency is reported yet.

Validation: `KisPaintTraceTest` 7/7, including actual `KisStrokeJob` execution on
separate threads, child creation, context restoration, canceled/unrun jobs and
CPU-span job tags; existing `kis_stroke_test` 8/8 and `kis_strokes_queue_test`
21/21 (cancellation, LOD, mutated jobs and concurrent/barrier ordering). Python
summary tests pass 6/6, including repeated execution/cyclic graph rejection.
The 64px/256px Buildup/Wash rows of `KisGpuStrokeTest` pass with tracing on and
off, 6/6 each, with Vulkan validation enabled. The enabled regression trace
contains 3,044 created/executed jobs and 8,116 job-tagged spans, no dropped
events; it has no real UI input, as expected. These are correctness checks,
not benchmark samples. Logs: `%TEMP%/solstice-paint-jobs-{build.log,test.txt,
stroke-test.txt,queue-test.txt,enabled.txt,disabled.txt}`.
The six rebuilt libraries/plugin were installed with the app closed; all
build/install SHA256 hashes match (`solstice-paint-jobs-install.log`). No
preferences were changed and the real application was not started by the agent.

For the next real-app capture use `%TEMP%/solstice-paint-trace.bat`, a fixed
RGBA32F view and plain pixel brush. Draw short 64px/256px Buildup/Wash strokes
with pauses, then close normally. Verify job ancestry/intervals and frame
coverage before requesting the three-path repeated performance baseline.

Real-app job capture passed: `%TEMP%/solstice-paint-trace-brush.15916.json`
records 14 strokes, 1,005 linked freehand dispatches, 25,025 created/executed/
finished/destroyed jobs and 71,276 job-tagged CPU spans. All 2,551 upload
occurrences reach an explicit swapped-frame chain; nine prepared updates were
superseded before upload. There are 208,922 events and none were dropped.
Only 8,018 jobs have a freehand-input creation ancestor. This is not evidence
that the remaining jobs are unrelated to drawing: timer-driven batch flushes
need explicit source membership, motivating phase 4.61. Aggregate scheduling
intervals include all job types in this single process, not a controlled
CPU/GPU performance comparison or input-to-pixel latency.

### Logical dab and paint-batch trace membership (phase 4.61)

The pixel-brush rendering queue assigns an ID to each accepted logical request,
including cached copies and postprocessed dabs. `dab.request`,
`dab.cache_request` and `dab.postprocess_request` record its creating job/input
context. Copies of the internal rendering-job value preserve the request ID;
separate queue requests always receive new IDs even when they share pixels.
Generation spans include the logical request ID and executing job ID.

The asynchronous brush update allocates a batch ID and passes it through
`KisDabRenderingExecutor::takeReadyDabs` to the rendering queue. `dab.in_batch`
is recorded only when a completed request is actually returned, after count/
byte-limit checks. Retained cache entries, requests not ready yet and already
consumed requests do not create extra membership records. This keeps input
sources distinct when one timer-driven flush consumes multiple earlier inputs.
`batch.ready` records the prepared group; `batch.paint_job` associates scheduled
GPU-attempt/CPU painting jobs; `batch.dirty_recorded` marks the final sequential
callback recording the painter's dirty rectangles. Mirroring/wrapping can reuse
the group; these records do not claim that every member affects every rectangle
or that a skipped mirrored job wrote pixels. No `KisRenderedDab` ABI change,
queue ordering change, new waits or scheduling changes were needed.

The summary reports request kinds, explicit memberships, generation coverage,
batch counts and batches with multiple traced source inputs. It rejects duplicate
request IDs and repeated consumption records. It deliberately does not use the
timer job's creation ancestor as the only source of a batch. Source ancestry is
still a creation relationship; interpolation can depend on earlier input samples.
Dirty recording is not GPU completion, projection completion or presentation.
The remaining priority-1 work includes dirty/projection compression lineage,
delayed input handling and controlled repeated CPU/projection/brush measurements.

Validation: `KisPaintTraceTest` 7/7, `KisDabRenderingQueueTest` 13/13 and Python
summary tests 7/7. The new queue test covers two input contexts, ordinary/cached/
postprocessed requests, pending dependencies, count-limited splitting, empty
fetches and copied IDs; existing byte-budget and mutable-dab tests also pass.
The 64px/256px Buildup/Wash `KisGpuStrokeTest` rows pass with tracing enabled and
disabled (6/6 each) and Vulkan validation. The enabled trace
`solstice-paint-batches-regression.31100.json` contains 1,836 logical requests,
all with generation spans and batch membership, 28 prepared/dirty-recorded
batches and 284 paint-job links, with no dropped events. The no-UI fixture has
no freehand-input source IDs, as expected. These are correctness checks, not
performance samples.

The first GPU test launch loaded an old installed default-paintop DLL against
the new trace API and failed plugin initialization. Installing the rebuilt
library/plugin set resolved it; both reruns passed. Logs:
`%TEMP%/solstice-paint-batches-{build.log,test-build.log,trace-test.txt,
queue-test.txt,enabled.txt,disabled.txt,install.log}`. Real-app batch membership
has not been verified yet. Defer another manual capture until the remaining
projection linkage is ready, instead of repeating the same smoke test after
each instrumentation step. Tracing remains disabled by default; the 262,144
event bound is unchanged, so long enabled captures can be rejected as incomplete.

### Dirty groups and projection lineage (phase 4.62)

Each completed pixel-brush batch records its ID in its `KisPainter` alongside
the existing dirty rectangles. The final sequential batch callback is the only
writer of this diagnostic list. `takeDirtyRegion()` drains both together and
emits `batch.to_dirty` links into the active explicit dirty-dispatch context.
The list is bounded to 4,096 batches; overflow marks the trace incomplete so
summaries reject it. No new locking or waits were added to painter operations.

`FreehandStrokeStrategy::issueSetDirtySignals()` supplies the dirty-group ID
while collecting all masked painters and calling `setDirty`. When masked-brush
updates defer that call to a job, the lambda explicitly captures/restores the
same ID. `KisPaintTrace::FlowScope` is thread-local and restores outer state;
it is separate from job creation ancestry and is not implicitly inherited by
arbitrary jobs or timers. Dirty drains outside a known dispatch remain
unattributed rather than being assigned to a nearby timestamp.

`KisSimpleUpdateQueue` records each projection request and its source context.
Recursive patch splitting retains the parent-request link. A new walker gets
a stable ID; requests merged into an existing walker link to that same ID.
Queue collection/optimization records removed-walker to surviving-walker edges.
Rectangle/checksum recalculation preserves the walker ID. All existing checks
for node, crop, update type, clone invalidation and LOD still determine whether
updates can merge; instrumentation does not change that decision.

The executing merge job installs the walker context around both the merge and
the existing `continueUpdate` notification. The existing direct image-to-canvas
connection carries it synchronously into `update.ready`, which links prepared
canvas updates to that walker. Existing upload/frame links complete the graph.
Deferred/suppressed UI notifications or projection filters that replay requests
outside this context are intentionally unlinked. Do not assume the graph covers
animation, delayed input, every filter or every rendering backend.

The summary follows explicit input/job/dab/batch/dirty/request/walker/update/
upload/frame edges. It checks frame/upload owner identities and reports whether
**at least one command descendant** reaches each stage. Superseded updates and
merged walkers preserve command ancestry; they do not prove that an earlier
input's pixels survive. Counts do not assert that all split regions have been
displayed, and no input-to-pixel latency is published. Stronger per-sample
coverage remains required before the three-path baseline. The trace's extra
overhead must also be measured.

Validation: `KisPaintTraceTest` 8/8; `kis_simple_update_queue_test` 9/9;
Python summary tests 8/8. Coverage includes nested/thread-isolated flow state,
multiple painter batches drained exactly once, recursive splitting, ordinary
merging, optimize-time merging, checksum recalculation and rejection of a
cross-canvas or missing-link shortcut. The trace test now uses the standard
test resource/plugin paths and QApplication; its first QCoreApplication-based
attempt lacked those paths and was stopped after unnecessary plugin searching.
This was a test-harness issue, fixed before the successful rerun.

The 64px/256px Buildup/Wash GPU stroke rows pass with tracing on/off (6/6 each,
including pixel and Undo/Redo comparisons); canvas upload tests pass 28/28.
Vulkan validation is enabled for these regressions. The enabled stroke trace
`%TEMP%/solstice-paint-projection-regression.40148.json` contains 1,836 requests
in 28 batches: all 28 have projection requests and executed merge descendants,
with zero dropped events. This fixture has no canvas widget or tablet input,
so its input/frame coverage correctly remains zero. The synthetic Python graph
test covers the complete explicit chain, not interactive frame delivery.

Logs use `%TEMP%/solstice-paint-projection-` plus `build.log`, `test-build.log`,
`trace-test.txt`, `queue-test.txt`, `enabled.txt`, `disabled.txt`, `upload.txt`
and `install.log`. The rebuilt libraries/plugin are installed with the app
closed. For the combined real-app check, run the existing launcher and draw
one short stroke each at 64px/256px Buildup/Wash in an RGBA32F document with a
plain pixel brush and fixed viewport, then close normally. Keep the capture
short because the unchanged 262,144-event bound now includes more lineage.
Verify input/batch descendants through projection and swapped frames, no
dropped events and no suspicious cross-document associations before collecting
performance baselines. Priority 1 remains in progress; priorities 2–4 have
not started.

Real-app follow-up (October 5): `%TEMP%/solstice-paint-trace-brush.41716.json`
contains four accepted stroke beginnings/ends and 68 accepted moves, with zero
dropped events. All 21 input-linked batches have projection-request, executed
merge, canvas-update and swapped-command descendants. All 234 upload occurrences
have render/blit coverage and a swapped-frame chain. These are command ancestry
checks, not a guarantee of pixel survival or complete coverage of every input.
Only 64 of the 76 accepted dispatches have a swapped-command descendant; the
remaining dispatches must not be treated as measured latency samples or declared
lost without examining whether they generated paint work. Preset, size and
painting-mode metadata are not recorded, so the requested four conditions cannot
be independently verified from this capture.

There are also 80 batches without input links. Brush stroke previews use their
own RGB8 image and `FreehandStrokeStrategy`, so background preview rendering is
a source-supported explanation for this population, not a proven per-batch
classification. Do not report 80 lost canvas updates or compare process-wide
CPU-span aggregates against GPU brush timings. The offline analyzer now reports
input-linked batches separately at every pipeline stage, including those without
a swapped descendant. Its eight tests pass, including an unrelated same-time
batch, a wrong-canvas swap and a missing projection edge. This analysis-only
change needs no DLL replacement. Priority 1 is still incomplete: per-sample
coverage, workload identification, tracing overhead and the three-path baseline
remain before publishing latency results.

### Input audit and workload conditions (phase 4.63)

`summarize.py` now audits each accepted input through job creation, logical dab
requests and batch membership. Traversal stops at batches, so a shared batch or
merged projection does not assign another input's requests to this input. Each
row reports requests lacking a ready batch and batches lacking any swapped
command descendant. An input with one presented batch and another missing batch
does not pass the all-request check. Inputs without requests are reported by
begin/move/end kind; they are not assigned a zero latency or declared lost.
This is still command ancestry, not coverage of all rectangles within each batch.

Reanalysis of capture 41716: all 64 inputs that created requests have all their
requests in batches with swapped-command descendants. No request from these
inputs lacks batch membership. The other 12 accepted inputs comprise four begins,
four moves and four ends with no recorded request descendants. This resolves the
earlier 64/76 count at the request level; it does not prove those inputs could
not influence later interpolation or delayed drawing.

When tracing is enabled, `KisToolFreehand::initStroke` snapshots the GUI preset
name, existing stored MD5 (without generating one), engine ID, nominal brush
size, incremental flag, image dimensions, color model and depth. The incremental
flag corresponds to Buildup/Wash for the pixel engine. `KisPaintTrace` stores these
under `metadata.stroke_conditions`, keyed by accepted begin input ID and canvas
identity. At most 1,024 snapshots are kept; overflow marks the capture incomplete.
No disk access, preset serialization, configuration change or GPU wait is added
to stroke start. Disabled tracing does not construct the snapshot. An unscoped
begin has no invented input ID and remains without conditions. The analyzer
rejects duplicate or wrong-input/canvas condition records; older captures remain
readable and report missing conditions.

These are GUI settings at stroke start, not pressure-adjusted dab sizes or proof
of GPU execution. Stored MD5 does not identify unsaved preset edits. Conditions
are not yet propagated as a stroke ID onto every later input/job; do not assign
them to later samples merely by nearest timestamp. Selection, mirrors, smoothing,
view transform and other workload conditions still need control in the manual
protocol. Automated coverage does not verify this GUI hook in an actual stroke.

Validation: `KisPaintTraceTest` 9/9 (including snapshot immutability, zero-input
suppression, bounded storage and disabled-process behavior); Python tests 10/10
(including partial batches, unbatched requests, mixed input sources and condition
identity mismatches). The 64px/256px Buildup/Wash stroke rows pass with tracing
enabled and disabled, 6/6 each, with Vulkan validation. Build and installation
completed; hashes of all six installed library/plugin files match build outputs.
Logs use `%TEMP%/solstice-paint-conditions-` followed by `build.log`, `test.txt`,
`enabled.txt`, `disabled.txt` and `install.log`. The first compile found a const
smart-pointer access to the non-const `paintIncremental()` API; the local handle
was corrected before the successful build. No painting algorithm changed.

Tracing-cost check on October 5, same Windows/Blackwell development environment:
`KisGpuStrokeTest` with `KRITA_GPU_STROKE_REPEATS=3`, validation disabled. Three
fresh processes per tracing state, ordered off/on, on/off, off/on. Each process
warms up each workload/path, then records three samples; the table is the median
of the three process medians in milliseconds. Rows test 1024x1024, four RGBA32F
layers, four image workers, no canvas/input dispatch, and untimed verification
readback. Pixel and Undo/Redo checks passed in all six processes.

| Workload | CPU off / on | GPU projection off / on | GPU projection + brush off / on |
| --- | ---: | ---: | ---: |
| 64px Buildup | 4.781 / 4.760 | 8.014 / 8.179 | 8.785 / 9.241 |
| 64px Wash | 5.416 / 5.437 | 8.846 / 8.724 | 9.297 / 9.563 |
| 256px Buildup | 7.792 / 7.938 | 11.249 / 11.097 | 10.023 / 10.723 |
| 256px Wash | 10.113 / 10.772 | 13.770 / 13.905 | 15.652 / 14.753 |

Raw results: `%TEMP%/solstice-trace-overhead-{1,2,3}-{off,on}.txt`. Enabled traces
have PIDs 7216, 40672 and 42292, each with 51,992 events and no drops. Across rows,
median differences range from about -5.7% to +7.0%; several ranges overlap, and
the 256px GPU-brush Wash row is faster with tracing in this run. This is evidence
that run variation matters, not evidence that tracing improves performance or
that one correction factor is valid. This fixture does not execute GUI condition
snapshots, pointer dispatch or frame tracing. Do not subtract these numbers from
interactive measurements or replace README's representative benchmarks with them.

Priority 1 remains in progress. Next: complete per-sample split-region coverage,
explicit stroke identity across accepted inputs, and controlled interactive
CPU/projection/brush captures with the new condition records. Defer another
identical manual smoke test until that capture protocol is ready. Priorities 2–4
remain deferred in accordance with `gpu-work-priorities.md`.

### Recorded branch completeness (phase 4.64)

The offline pipeline summary now includes `recorded_branch_audit`. Starting at
input-linked batches, every recorded dirty/request/walker/update/upload branch
must reach a swapped frame; one successful sibling no longer hides a pending
split request or second canvas update. Superseded updates and merged walkers
retain their explicit downstream obligations. Update replacement/merge edges
are accepted only between known, matching canvas owners. Shared downstream
work can conservatively keep several inputs unresolved.

An iterative reverse-topological pass evaluates the graph in linear time and
rejects cycles, including cycles with a swapped frame. It avoids recursion-limit
failures on long chains. Unresolved leaf counts identify the stage where links
stop: missing dirty dispatch, projection child/walker, canvas update, upload,
frame coverage or swap. The report includes up to 16 terminal IDs for inspection.
These counts describe recorded paths, not proof of lost drawing: offscreen work,
resets, suppressed notifications and missing instrumentation remain unresolved.

The request audit's earlier `all_requested_dabs_have_swapped_batch_command`
field remains available and keeps its weaker meaning (some descendant per
batch). The new `all_requested_dabs_have_all_recorded_branches_swapped` field
also requires every recorded downstream branch. Neither field certifies
rectangular coverage: dirty/projection coordinates are not recorded, so an
unrecorded split or a rectangle mismatch cannot be detected by this graph.
Do not publish input-to-pixel latency from either field.

Validation: Python tests 12/12, including one missing split sibling, multiple
canvas updates, merged/superseded resolution, cross-canvas replacement rejection,
a cycle and a 3,000-node chain. Reanalysis of real-app capture 41716 passes the
stronger check for all 21 input-linked batches and 64 request-producing inputs,
with zero unresolved terminals. The no-canvas regression capture 40148 correctly
has no input-linked population; zero unresolved rows there is not a presentation
pass. This change is offline analysis/documentation only: no DLL rebuild or
new interactive capture is needed. Next remains geometric coverage and explicit
stroke identity, followed by the controlled interactive comparison.

### Image-space rectangle coverage (phase 4.65)

`KisPaintTrace::rectangle` records signed x/y/width/height and explicit LOD in
the existing bounded event buffer. JSON rectangle construction occurs at export;
the active recording path stores a `QRect` value. A negative LOD marks the trace
incomplete rather than inventing a coordinate scale. Existing schema-2 captures
remain readable. The extra rectangle/LOD fields enlarge in-memory event records
when tracing is enabled; phase-4.63 tracing-cost numbers are not a calibration
for this version. The 262,144-event limit and opt-in behavior remain unchanged.

Hooks:

- `KisSimpleUpdateQueue::addJob`: `projection.request_rect` for every requested
  rectangle, including empty split pieces, in the request's image/LOD space.
- `KisUpdateJobItem::runMergeJob`: after the merger returns, record the walker's
  final `requestedRect()` and `changeRect()` as `projection.executed_request_rect`
  and `projection.change_rect`, with its node owner and LOD. This includes the
  effects of queue merging/recalculation; it does not certify individual writes.
- `KisCanvas2::startUpdateCanvasProjection`: `update.request_rect` records the
  notification's image rectangle. `KisImage::notifyProjectionUpdated` has already
  upscaled it to LOD 0. The canvas identity stays attached to each update.

`build-tools/paint-trace/geometry.py` follows explicit request splitting and
walker merging, then checks whether the union of executed requested rectangles
covers each requested rectangle. Bounding boxes are insufficient: subtraction
preserves holes and uses half-open boundaries derived from x/y/width/height.
It separately checks a walker's change rectangle against canvas notification
rectangles **per canvas**, never by combining areas from different views.
Only LOD 0 is evaluated. Missing coordinate records, unknown/mismatched owners,
other LODs and excessive fragmentation are reported as unverified. Rectangle
lists are capped at 1,024 for analysis and subtraction at 100,000 piece checks;
exceeding those budgets does not become a successful coverage result.

These are declared image-space regions, not pixel survival or whole-pipeline
display coverage. A walker's requested region does not prove each pixel was
modified. Canvas notification coverage does not establish which tiles were
uploaded or which viewport pixels reached a swap. Dirty-group geometry, tile
transfer coverage, image-to-widget mapping and view changes still require work
before an input-to-pixel baseline. Old captures without coordinates correctly
produce unverified results, rather than being retroactively certified.

Validation: `KisPaintTraceTest` 10/10, including signed/empty rectangles and LOD
serialization; `kis_simple_update_queue_test` 9/9 with tracing enabled. The first
queue run had tracing unset and skipped the trace-only row; rerunning with the
flag passed all rows. Python tests 17/17 include holes, touching edges, negative
coordinates, overlapping unions, per-view isolation, wrong owner/LOD, missing
coordinates, bounded analysis and 100 deterministic comparisons against an
independent small-pixel-set oracle. Run all analysis tests with:

```bat
python -B -m unittest discover -s build-tools/paint-trace -p test_*.py
```

The four 64px/256px Buildup/Wash GPU stroke rows pass with tracing on/off and
Vulkan validation (6/6 each, including initialization/cleanup). Trace
`%TEMP%/solstice-paint-regions-regression.2684.json` has zero dropped events;
all 100 projection requests pass rectangle containment. Its 293 executed walkers
have no canvas links because the fixture has no window; that stage is explicitly
unverified, not passed. Build/install completed and all six installed DLL/plugin
hashes match the build. Logs use `%TEMP%/solstice-paint-regions-` plus `build.log`,
`test.txt`, `queue.txt`, `enabled.txt`, `disabled.txt` and `install.log`.

Actual GUI condition/rectangle capture remains pending. Combine it with the
remaining display-geometry work rather than requesting another identical smoke
test at this intermediate stage. No application launch, termination or preference
change was performed. Priority 1 remains in progress.

### Upload and widget geometry (phase 4.66)

For completed `updateCanvasProjection` calls with `paintTraceUploadIssued`,
`KisOpenGLCanvas2` records the dirty image rectangle clipped to current image
bounds, every tile's `realPatchRect()` clipped to those bounds, the corresponding
logical-widget rectangles and the actual dirty widget rectangle passed to the
existing render/blit coverage tracker. Geometry is keyed by the unique upload
occurrence ID, not a reusable buffer/pointer or update ID. The patch list is read
only after the renderer has issued all upload commands; it is command geometry,
not a new GPU completion check.

Supported mapping is invertible affine scaling/translation with zero cross-axis
terms, including negative scale (mirror display), LOD 0 and no wrap-around view.
Use `QRectF` mapping followed by `toAlignedRect()` and widget clipping, matching
the renderer's logical-pixel coordinate convention. Image expectations are
clipped before mapping, while the tracked dirty rectangle includes the renderer's
existing two-pixel growth. The analyzer checks that this tracked rectangle covers
the visible expectation; it does not demand that image patches cover that border.
Rotated/sheared/projective views, wrap-around and nonzero LOD record
`update.geometry_unsupported`; they are not accepted as verified geometry.

`KisCanvasPaintTrace::setView` compares the transform, widget rectangle, device
pixel ratio and wrap mode before upload tracking and before painting. Changes
clear pending upload/frame tracking and emit `frame.reset`. This is conservative:
it prevents acknowledgments against stale coordinates and can leave pending work
unattributed, but does not alter actual painting, upload or widget scheduling.
The tracing object and these checks exist only with opt-in tracing enabled.

The offline `geometry.transfers` report combines three containment checks
(image patch union, mapped widget patch union, tracked render/blit rectangle)
with same-widget `frame.covered_upload` and swapped/replaced frame links.
It rejects acknowledgments preceding the upload and treats an intervening view
reset as unverified. Successful status is `covered_to_swapped_commands`, not
pixel visibility or physical scanout. `outside_image`/`outside_view` are exclusions,
not successful presentation. Missing metadata, patches, unsupported mapping,
nonzero LOD, uncovered regions and absent swaps have separate statuses.

Validation: mapping/coverage tests 6/6, including negative coordinates, clipping,
mirror display, offscreen regions, rejected rotation/wrap/singular transforms,
pan/scale/DPR/viewport/wrap invalidation, delayed blits and partial renders.
Vulkan-validation-enabled `KisGpuCanvasUploadTest` passes 28/28. Analysis tests
19/19 include image/widget/tracking holes, wrong-widget swaps, replacement
chains, stale acknowledgments, resets, offscreen exclusions and old metadata.
The real OpenGL widget integration still requires the combined manual capture.
Build and install completed; logs use `%TEMP%/solstice-paint-display-` followed by
`build.log`, `trace-test.txt`, `upload.txt` and `install.log`.

Combined real-app check: run `%TEMP%/solstice-paint-trace.bat`, create RGBA32F,
use a plain pixel brush without texture/masked tip, keep zoom/pan/rotation fixed,
and draw one short stroke each at 64px Buildup, 64px Wash, 256px Buildup and 256px
Wash. Close normally. Inspect condition records, projection rectangle containment,
per-view notification coverage and each upload's transfer geometry/swap status,
with no dropped events. Keep it short to stay below the event bound. No app is
launched or terminated by the agent, and no preferences are changed.

Priority 1 remains incomplete. These stage checks are not yet a joined guarantee
of every input's visible pixels: explicit stroke identity, dirty-group geometry,
notification compression/transfer coverage linkage and controlled repeated
three-path captures still need completion. Superseded pixels and offscreen work
also require explicit sample inclusion rules before publishing latency.

Combined real-app follow-up (October 5, capture
`%TEMP%/solstice-paint-trace-brush.2572.json`, 22,257,961 bytes): no dropped events.
All four accepted begins have condition records for `paintbrush`, preset
`b) Basic-4 Flow Opacity`, RGBA/F32, 2480x3508. Actual nominal sizes/modes were
64.2358px Wash, 64.24px Buildup, 256.1px Wash and 256.1px Buildup. These satisfy
the approximate-size integration check, not an exact-size controlled benchmark.

There are 91 accepted dispatches: four begins, 83 moves and four ends. The 80
request-producing inputs have all requests assigned to batches; all recorded
branches from their 33 batches reach swapped commands. Eleven dispatches (four
begins, three moves and four ends) have no recorded request descendants. Another
68 batches are not input-linked and are excluded from this painting population.

Geometry results: all 37 projection requests have covered requested regions;
466 walker/canvas notification checks pass. Another 70 executed walkers have no
canvas link and remain unverified for that stage. All 70 finish within the first
9.759ms of the trace, before the first stroke at 11,269.060ms; their change
rectangles comprise two full-image sets of 35 patches. This supports startup
refresh as their context, rather than lost stroke updates; do not certify those
unnotified walkers as presented. There are 467 canvas updates in total, including
one without a merge parent, and 16 superseded updates.

All 450 upload occurrences pass image patch containment, mapped widget patch
containment, tracked-widget containment and same-widget swapped-command coverage.
No upload has unsupported mapping, intervening view invalidation, an uncovered
region or a missing acknowledgment. Seven `frame.reset` events exist globally;
none invalidates an upload's acknowledged interval. This validates the installed
condition/geometry hooks for this fixed-view real-app workload. It does not
establish pixel survival, physical display timing, or a three-path latency
baseline. No native changes or reinstall were needed for this log analysis.

### Explicit stroke membership and joined checks (phase 4.67)

`KisToolFreehand` records `stroke.input` edges from accepted input IDs to the
begin input ID. A tracing-only dynamic QObject property, `solsticePaintTraceStroke`,
holds the current ID on that tool instance; no exported class layout changes.
Every begin replaces the value, including an unscoped begin with zero. Move/end
read it; end emits `stroke.ended` and removes it before scheduling the helper's
end processing. Tool destruction also destroys the property. Deactivation's
existing `endStroke()` path clears it. These are diagnostic operations only when
tracing is enabled, with no persistent configuration or painting changes.
An unscoped input remains unassociated; no nearby timestamp is substituted.

`stroke_summary` validates that the input and its begin belong to the same
canvas, rejects conflicting memberships and makes the condition snapshot
available via the explicit begin ID. Begins themselves are already explicitly
identified by their accepted event/condition record. Older logs retain unknown
membership for later moves/end events. `stroke.ended` describes tool finalization,
not asynchronous job completion or presentation.

The pipeline input audit now follows each input's recorded downstream branches
and joins their projection-request geometry, executed-walker notification geometry
and upload/display geometry. Shared downstream branches are checked conservatively
in full, so a failure can exclude several inputs. `sample_readiness` combines
those checks with complete request/batch membership, all recorded branches reaching
swaps, known stroke conditions and a recorded stroke end. Its exclusions state
which prerequisite is missing; inputs without requests are excluded rather than
assigned zero latency. `recorded_checks_passed` is deliberately not named
"latency valid": dirty-group and compressed-update geometry, input interpolation
dependencies and pixel survival are not yet established. No timing metric is
published by this change.

Validation: all Python analysis tests 21/21, including a complete synthetic
joined chain, removal of a required rectangle, missing old-log membership,
conflicting strokes and cross-canvas association. Reanalysis of real-app capture
2572 finds no joined-stage geometry failures among the 80 request-producing
inputs. It correctly produces zero passing readiness rows because later-input
membership and stroke-end records were not instrumented in that version; this
does not invalidate the earlier stage-by-stage integration result.

`kritaui` rebuilt and installed. Vulkan-validation-enabled 64px/256px Buildup/Wash
stroke rows pass with tracing enabled and disabled, 6/6 each. These headless
regressions do not exercise the GUI membership hook; its interactive verification
is pending. Logs use `%TEMP%/solstice-paint-samples-` followed by `build.log`,
`install.log`, `enabled.txt` and `disabled.txt`. No application launch, termination
or preferences change was performed. Defer another identical manual capture until
the remaining dirty-group and compressed-update geometry linkage is ready, then
verify membership and the complete protocol together before three-path sampling.

### Dirty and compressed-update geometry bridges (phase 4.68)

`FreehandStrokeStrategy::issueSetDirtySignals` records `dirty.source_rect` after
collecting all painter dirty regions, before masking normalization/partitioning.
`dirty.submitted_rect` records the actual rectangles immediately before calling
the target node's `setDirty`, including inside the delayed masked-brush job.
Both use the dispatch ID, target-node owner and projection's current LOD. Empty
vectors produce an explicit empty rectangle rather than absent metadata. Existing
job order, masking partitioning and painting behavior are unchanged.

`geometry.bridges` checks source-region containment in submitted regions and
submitted-region containment in the directly linked projection request regions.
This complements the existing request-to-executed-walker check. It is group-level
provenance; it does not claim that each member dab affects every group rectangle.
Only the established LOD-0 coordinate convention is accepted; unknown owners,
missing records and other LODs remain unverified. Intentional clipping or other
unmodeled coordinate changes must not be interpreted as lost pixels solely from
a gap report.

Each supported upload also records `update.upload_bounds_rect`. The analyzer
follows same-canvas `update.merged`/`update.superseded` edges to actual upload
occurrences, verifies widget-to-canvas identity via `canvas.created`, and checks
that the original notification rectangle (clipped to the image bounds) is
covered by the union of descendant upload expectations. All descendant bounds
must agree; resizing, cross-canvas edges, missing metadata and absent transfers
remain unverified. Existing per-upload patch/widget/frame checks then verify the
upload expectations. Outside-image notifications are exclusions, not presentation
successes. The input-level readiness report now requires both new bridge checks.

Validation: all Python tests 24/24. Cases include holes after masking partitioning,
truncated projection requests, chained replacement/merge, clipping to image bounds,
cross-node/canvas rejection, image-bound changes and missing bridge records despite
otherwise successful frame ancestry. Vulkan-validation-enabled ordinary 64/256px
Buildup/Wash tests pass with tracing on/off (6/6 each); the masked-150-texture0
row passes with tracing enabled (3/3 including initialization/cleanup).

Trace `%TEMP%/solstice-paint-bridges-regression.45344.json` verifies both dirty
bridges for all 28 ordinary groups. The masked trace
`%TEMP%/solstice-paint-bridges-masked.39864.json` verifies all six masked groups,
including delayed submission. Both have zero dropped events. These fixtures have
no canvas and cannot verify real update compression or tool membership. `kritaui`
rebuilt/installed; logs use `%TEMP%/solstice-paint-bridges-` plus `build.log`,
`install.log`, `enabled.txt`, `disabled.txt` and `masked.txt`.

Combined real-app capture `%TEMP%/solstice-paint-trace-brush.45816.json` passed
after normal closure. Four RGBA32F strokes on a 2480x3508 image record the Pixel
engine and `b) Basic-4 Flow Opacity`, approximately 64px/256px in Wash/Buildup.
All 106 accepted inputs have explicit stroke membership; all four stroke ends
are recorded. All 94 request-producing inputs pass the joined checks, including
dirty bridges and compressed-update coverage. The other 12 inputs have no
recorded dab requests and remain excluded. All 40 input-linked batches reach
their recorded swapped-command branches; all 472 upload geometry/frame checks
pass. There are zero dropped events.

This does not certify the entire process: 58 batches without input links,
70 walker records without canvas links and one notification without an upload
remain outside the verified drawing-input population. They are not automatically
lost painting updates. Older captures intentionally lack the new records and
must not be retroactively accepted by the stricter gate.

Priority 1 remains open until controlled three-path sampling and its timing
analysis are complete. The recorded checks still describe command/region
provenance, not all interpolation dependencies, pixel survival or physical scanout;
any timing report must name its actual Qt presentation boundary and inclusion rules.

### Qt command-presentation timing (phase 4.69)

`build-tools/paint-trace/timing.py` consumes the joined readiness gate. Only an
input with exactly one finite, nonnegative mouse/tablet receipt timestamp and
verified geometry for every downstream upload is timed. The endpoint is the
latest required same-widget swap acknowledgment from `summarize_transfers`,
including frame replacement. An earlier successful sibling or unrelated later
swap cannot substitute for this endpoint. Missing, ambiguous, nonfinite or
reversed timestamps exclude the input. The analyzer reports per-stroke sample
counts, median, nearest-rank p95 and maximum in milliseconds; it does not pool
different strokes or processes. Shared batches/frames correlate samples.

All 29 Python tests pass, including the full joined-chain timing case, latest
required upload, unrelated swaps, invalid/duplicate receipts, incomplete geometry,
invalid/reversed acknowledgments and separate stroke populations. This phase is
offline analysis only: no native rebuild, installation or additional runtime
instrumentation cost. Existing command coverage remains distinct from physical
scanout, pixel survival and complete interpolation dependencies.

Reanalysis of `solstice-paint-trace-brush.45816.json` yields 94 timed inputs and
12 exclusions without dab requests. Exploratory single-process results follow;
these are not a controlled baseline or evidence that GPU beats CPU:

| Recorded condition | Inputs | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: |
| ~64px Wash | 24 | 24.08 | 40.90 | 48.62 |
| ~64px Buildup | 26 | 18.66 | 39.65 | 43.02 |
| ~256px Wash | 22 | 55.48 | 64.53 | 66.29 |
| ~256px Buildup | 22 | 53.00 | 90.27 | 94.75 |

CPU-requested feasibility capture `solstice-paint-trace-cpu.43504.json` records
`projection_env=0`, `brush_env=0`, zero dropped events and four ended strokes.
All 95 accepted inputs have stroke membership. All 83 request-producing inputs,
22 input-linked batches and 275 uploads pass the joined checks and timing gate;
the other 12 inputs have no dab requests. Outside this population, 81 unlinked
batches, 70 walkers without canvas links and one notification without an upload
remain unverified. Recorded conditions are the same preset/hash and RGBA32F
2480x3508 image, with nominal sizes 64.4551/64.46px and 255.96px:

| Recorded condition | Inputs | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: |
| ~64px Buildup | 21 | 21.48 | 43.13 | 48.29 |
| ~64px Wash | 21 | 18.66 | 31.50 | 43.11 |
| ~256px Buildup | 19 | 18.94 | 42.98 | 42.98 |
| ~256px Wash | 22 | 19.52 | 37.56 | 40.91 |

This verifies that the timing/geometry gate also works with the CPU-requested
configuration. Sizes, stroke order and hand-drawn inputs differ from the earlier
brush-requested capture, and each has only one process: do not calculate speedups
or treat these as a controlled baseline.

Projection-requested feasibility capture `solstice-paint-trace-projection.31336.json`
records `projection_env=1`, `brush_env=0`, zero dropped events and four ended
strokes. All 97 accepted inputs have explicit membership. All 76 request-producing
inputs, 23 input-linked batches and 241 uploads pass the joined checks and timing
gate. The other 21 inputs (four begin, 13 move, four end) have no recorded dab
requests. Outside this population, 15 unlinked batches, 70 walkers without canvas
links and one notification without upload remain unverified. All four snapshots
record exactly 64px/256px, the same preset/hash, RGBA32F and 2480x3508 dimensions.

| Recorded condition | Inputs | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: |
| 64px Buildup | 17 | 20.43 | 46.29 | 46.29 |
| 64px Wash | 18 | 16.80 | 32.30 | 32.30 |
| 256px Buildup | 20 | 49.19 | 76.08 | 80.80 |
| 256px Wash | 21 | 43.67 | 78.00 | 78.12 |

These single-process observations validate the analysis in the third requested
configuration, not GPU execution or a speed comparison. The longer 256px intervals
are a candidate for controlled reproduction, not evidence of their cause.

The subsequent brush-requested capture `solstice-paint-trace-brush.45824.json`
records both environment flags as 1, zero dropped events and four ended strokes.
All 98 accepted inputs have membership. All 81 request-producing inputs, 27
input-linked batches and 275 uploads pass the joined checks and timing gate;
17 inputs without dab requests are excluded. Outside this population, 13 unlinked
batches, 70 walkers without canvas links and one notification without upload
remain unverified. Sizes are exactly 64px/256px, in Buildup/Wash order, with the
same preset/hash and RGBA32F 2480x3508 image as the projection-requested capture.

| Recorded condition | Inputs | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: |
| 64px Buildup | 22 | 23.97 | 45.72 | 49.55 |
| 64px Wash | 18 | 23.11 | 38.06 | 38.06 |
| 256px Buildup | 18 | 41.74 | 52.94 | 52.94 |
| 256px Wash | 23 | 36.40 | 51.91 | 51.93 |

Feasibility now passes in all three requested configurations. These are still
single-process hand-drawn observations, with differing input counts and slightly
different CPU brush sizes. Do not calculate speedups or update README benchmarks.
The launch logs contain ggml Vulkan initialization, which is not evidence of the
painting engine's effective GPU path. Before repeated baseline captures, establish
per-workload projection/brush/interop success or CPU fallback attribution; the
current environment flags and region trace alone do not establish this. Retain
all feasibility captures separately from the eventual controlled baseline.

#### Controlled capture protocol

Track the current series, selected installed binary hashes and capture acceptance
in [paint-trace-baseline-runs.md](paint-trace-baseline-runs.md). CPU run 1 passed
trace checks: 16 strokes, 302 timed inputs, zero dropped events. Excluding the
four explicit warm-up strokes leaves 12 strokes / 222 inputs. Run 2 (projection)
also passed, leaving 12 measured strokes / 218 inputs, with CPU brush and shared
buffer uploads observed. Run 3 (brush) passed with 12 measured strokes / 228 inputs,
successful brush submission and shared-buffer evidence. Round 1 is complete;
run 4 (projection) also passed with 12 measured strokes / 220 inputs and the same
observed paths as run 2. Run 5 (brush) passed with 12 measured strokes / 236 inputs
and the same observed paths as run 3. Run 6 (CPU) passed with 12 measured strokes /
232 inputs and the same observed paths as run 1. Run 7 (brush) passed with 12
measured strokes / 231 inputs and the same observed paths as runs 3 and 5.
Run 8 (CPU) passed with 12 measured strokes / 255 inputs and the same observed
paths as runs 1 and 6. Run 9 (projection) passed with 12 measured strokes / 236
inputs. All nine captures are complete; see phase 4.72 and the run sheet's
completed comparison. The observed scene reuses child images rather than doing
multilayer stack composition; retain that distinction when reporting results.

1. Use the same installed build, machine, tablet/mouse, display refresh rate and
   window geometry. Close other heavy workloads. Record build revision and local
   diff, driver/display settings and preset edits alongside the captures.
2. Prepare one RGBA32F 2480x3508 document with a white background and one empty
   paint layer. Reopen this same document for every fresh process; never save the
   measured strokes into it. Use `b) Basic-4 Flow Opacity`, Normal blend, full
   opacity, no selection, channel locks, mirror, texture or masked brush. Keep
   smoothing, pressure behavior, zoom, pan and rotation identical. Use a fixed
   axis-aligned LOD-0 view and keep the complete strokes within the viewport.
3. The user starts `build-tools\paint-trace\run.cmd <devroot> <mode>` from Command
   Prompt. Modes are `cpu`, `projection`, `brush`; they request CPU only, GPU
   projection with CPU brush, and GPU projection with GPU brush respectively.
   The agent must not launch or terminate the app or alter `kritarc`.
4. Each process uses exactly 64px Buildup, 64px Wash, 256px Buildup, 256px Wash,
   in that order. For each condition make one warm-up stroke, wait for completion,
   then three short strokes of approximately equal length and duration in clean,
   separate regions. Leave a pause between strokes. Do not pan/zoom mid-stroke.
   Record deviations; normal closure flushes the trace. If overflow occurs,
   discard the run and shorten/split the protocol equally for every mode.
5. First capture one process per mode as a feasibility pass, using just one short
   stroke per condition (four strokes total, no warm-up). These are feasibility
   captures only and must not be pooled with the eventual baseline. For that
   baseline, use three fresh processes per mode in rounds `cpu/projection/brush`,
   `projection/brush/cpu`, `brush/cpu/projection`. Do not mix earlier exploratory
   captures or native instrumentation revisions into the baseline. Label warm-up
   stroke IDs explicitly in the analysis notes; the summarizer does not guess
   warm-up status or discard the first input automatically.
6. Run `python -B build-tools/paint-trace/summarize.py <trace files...>` and retain
   its JSON with the original logs. Check every condition snapshot, exclusion
   count, membership and geometry gate before comparing timing. Report per-stroke
   counts/median/p95 and per-process variation, with warm-up strokes separated;
   samples within a stroke are not independent trials. Hand drawing differences
   must be reported rather than attributed entirely to GPU changes.

Environment flags express requested configurations only. Effective GPU brush,
projection and interop/fallback use still needs corroboration before publishing
an actual GPU-path baseline; do not relabel CPU fallback as GPU success. This
protocol and timing metric prepare that comparison but do not complete priority 1
or the physical input-to-pixel acceptance criterion. README benchmark figures
remain unchanged until comparable results exist.

### Explicit execution-path evidence (phase 4.70)

Trace-only markers now distinguish actual submission from API success/no-op:

- `KisGpuBrushPainter` emits `path.brush.submitted` with the executing job ID
  only after a nonzero `submitAndFinish` result. Empty successful operations
  cannot produce this marker. BrushOp records CPU and failed-attempt CPU branches
  as `path.brush.cpu` / `path.brush.cpu_fallback`, skipping already-combined mirrors.
- `KisGpuProjectionCompositor` emits `path.compositor.submitted` only after
  successful submission, using the current flow and job IDs. Within a merger
  this identifies the executing walker. The shared compositor also serves other
  operations, so only explicitly matched walker IDs count as projection evidence.
  `KisAsyncMerger` records its direct CPU apply branch; `KisGpuMergeBatch` records
  CPU replay after a failed batch. A walker may legitimately contain both paths.
- After a renderer reports uploads issued, `KisOpenGLCanvas2` records whether
  its tile list still uses GPU shared buffers or CPU pixels, keyed by the unique
  upload ID. Interop-failure readback replaces GPU references before this point,
  so those uploads are counted as CPU-source uploads. Mixed lists keep both markers.

`build-tools/paint-trace/paths.py` associates these records with each input's
explicit batch jobs, executed walkers and uploads. It reports evidence counts
and identities without evidence. It never infers CPU execution from absent GPU
markers, nor GPU execution from environment flags. Shared identities are deduped
within each input, but rows across inputs must not be summed as independent work.
These markers add no waits, queries, persistent settings or painting decisions.
Submission is not completion, and CPU branches can have no pixel effect. Wash
preview/final merge and internal leaf work are not fully classified: this is
evidence for the observed stages, not an all-GPU stroke certification. Missing
records in earlier captures remain unknown and do not invalidate their geometry
checks. Do not use this evidence report alone as a new latency inclusion gate.

Validation: Python analysis tests 32/32, including mixed paths, unrelated events,
missing/zero identities and shared jobs, plus prior timing/geometry coverage.
Vulkan-validation-enabled 64px/256px Buildup/Wash regressions pass with tracing
on/off (6/6 each). Trace `solstice-paint-path-regression.41940.json` has no dropped
events and records 12 brush submissions, 233 compositor submissions, 272 CPU brush
branches and 368 CPU projection branches across the fixture's CPU/GPU runs.
Those process-wide counts are instrumentation evidence, not timing samples.
Canvas upload regressions pass 28/28 with Vulkan validation. `kritaimage`,
`kritaui`, version library and default paintops plugin are installed and match
their build outputs by SHA-256; the user had already closed the application.
Build/test/install logs use `%TEMP%/solstice-paint-path-` prefixes. Real-app
association of path markers with the four measured strokes remains to be checked.

Real-app capture `solstice-paint-trace-brush.46604.json` subsequently verifies
explicit brush and canvas attribution. Both requested flags are 1, with no dropped
events; all four strokes ended and all 96 accepted inputs have membership. All
83 request-producing inputs, 26 input-linked batches and 298 upload occurrences
pass the joined geometry/timing checks. Thirteen inputs without dab requests are
excluded. Conditions are exactly 64px/256px Buildup/Wash with the same Pixel preset
and RGBA32F 2480x3508 image. For every one of the 83 included inputs, all linked
brush job identities have successful submission evidence and all descendant
uploads have shared-buffer evidence, with no recorded CPU branch in those stages.

Projection attribution is only partial: all 43 Wash inputs have compositor
submission evidence for their walkers, while all 40 Buildup inputs have no
projection-path evidence. Do not classify the latter as CPU fallback or GPU
projection. `KisAsyncMerger::setupProjection` has an oblige-child reuse path that
leaves `m_currentProjection` empty, intentionally skipping composition; other
recalculation/no-op paths also remain unclassified. This is a candidate explanation
from code inspection, not yet established by the trace. Before another baseline
capture, distinguish explicit reuse/skip decisions from missing instrumentation.

Process-wide records include 26 brush submissions, 170 compositor submissions,
45 CPU brush branches and 298 shared-buffer uploads. The CPU branches are outside
the included input-linked jobs and must not be attributed to these strokes. As in
earlier captures, 15 unlinked batches, 70 walkers without canvas links and one
notification without upload are not certified. Exploratory timing medians/p95
in ms are 25.53/47.69 (64 Buildup, 21 inputs), 20.30/42.42 (64 Wash, 23),
52.29/74.50 (256 Buildup, 19), and 41.76/60.23 (256 Wash, 20). These use the new
native markers and are not pooled with older instrumentation or used as speedups.

### Projection reuse and skip evidence (phase 4.71)

`KisAsyncMerger` now records `path.projection.child_reused` in the existing
oblige-child setup branch, `no_target` when the corresponding composition call
has no destination, and `invisible` when an invisible leaf is skipped. An empty
walker records `empty_walk`. Root and extra-node recalculation record
`root_recalculated` / `extra_recalculated` after the existing call returns.
`KisLayer::updateProjection` records `original_reused` after releasing an unneeded
projection device, `masks_applied` after its mask branch, and `recalculate_skipped`
for its existing early return. All use the current walker flow ID; node ownership
is retained where available. Conditions and painting behavior are unchanged.

The path analyzer reports these decisions separately from CPU apply and GPU
submission. Recalculation alone proves neither backend; reuse of a layer original
does not prove reuse of the entire group. One known walker cannot hide another
walker without evidence. These markers explain skipped work without claiming all
internal processing is classified. Older captures remain unknown; do not infer
reuse retrospectively from the absence of submission markers.

Validation: 34 Python tests pass, including separate reuse/skip and CPU/GPU
evidence, mixed processing and an unknown sibling walker. The four ordinary
64px/256px Buildup/Wash regression rows pass with tracing on/off and Vulkan
validation (6/6 each including setup/cleanup). Trace
`solstice-paint-reuse-regression.19132.json` has zero dropped events and records
873 original-reuse decisions, 385 root recalculations, 364 CPU apply branches
and 89 mask branches across the fixture's runs. This fixture does not exercise
the oblige-child branch, so the real-app Buildup diagnosis remains pending.
`kritaimage` and version DLLs are installed and match build SHA-256 values;
logs use `%TEMP%/solstice-paint-reuse-` prefixes.

One combined real-app capture is requested using the existing `brush` launcher:
same RGBA32F document, fixed viewport and exact 64px Buildup, 64px Wash, 256px
Buildup, 256px Wash short strokes, then normal closure. Check the Buildup walker
decision records alongside successful brush submissions and shared-buffer
uploads before scheduling controlled baseline repetitions.

Real-app capture `solstice-paint-trace-brush.23828.json` completes this check.
Both environment flags are 1, with zero dropped events. All 96 accepted inputs
have membership and all four strokes ended. All 80 request-producing inputs,
26 input-linked batches and 306 uploads pass the joined geometry/timing checks;
16 inputs without dab requests are excluded. Sizes are exactly 64px/256px in
Buildup/Wash order, with the same preset/hash and RGBA32F 2480x3508 image.

All 40 included Buildup inputs have `child_reused`, `no_target`, `original_reused`
and `root_recalculated` evidence for their downstream walkers, with no identities
without evidence. This establishes the previously suspected reuse/skip path:
missing compositor submissions here do not indicate CPU fallback. All 40 Wash
inputs additionally have `masks_applied` and compositor submission evidence.
The compositor is shared with Wash processing; its marker is not necessarily
a separate layer-stack composition. All 80 inputs have successful brush submission
and shared-buffer canvas evidence, without recorded CPU branches in those linked
stages. This establishes the recorded paths, not that every internal operation
or the complete stroke runs on GPU.

Outside the included population, 13 unlinked batches, 70 walkers without canvas
links and one notification without upload remain unverified. The process-wide
39 CPU brush markers do not belong to the included input-linked jobs. Exploratory
median/p95 ms: 27.03/41.51 (64 Buildup, 22 inputs), 22.17/47.44 (64 Wash, 19),
51.41/70.44 (256 Buildup, 18), 35.28/50.90 (256 Wash, 21). Keep this capture separate
from the controlled baseline and earlier instrumentation revisions. Integration
verification is complete for this workload; priority 1 still needs the planned
three-process-per-configuration comparison, with actual paths reported rather
than assuming every requested projection configuration performs composition.

### Nine-process software-timing baseline (phase 4.72)

Completed on October 6, 2026 without changing the phase-4.71 native binaries.
The [run sheet](paint-trace-baseline-runs.md#completed-comparison-october-6-2026)
records all nine hashes, explicit warm-up IDs, per-stroke values, sample counts,
hierarchical aggregation and observed paths. Its local archive contains original
traces, launch logs, full summaries and `comparison.json`.

All 2,799 request-producing inputs passed joined checks, with zero dropped events;
683 accepted inputs without dab requests were excluded. Removing 36 warm-up
strokes leaves 108 measured strokes and 2,078 inputs. This completes the scheduled
manual software-boundary baseline, not the stronger physical input-to-pixel goal.
The user is not asked to repeat this nine-run series. CPU/GPU process ranges
overlap at 64px; 256px CPU summary medians are smaller. Shared-buffer configurations
are candidates for examining transfer preparation, waits, dirty area and scheduling.
No causal attribution follows from these observations alone. The scene reuses
child images, so this is not a GPU multilayer-stack comparison.

Next: priority 2 overhead decomposition, using existing traces for initial
analysis before adding narrowly scoped CPU/GPU measurements. Preserve actual
submission/reuse classification, do not sum overlapping spans, and do not infer
GPU execution time from CPU recording spans. The published software-timing
results retain manual-input, missing display-context and uncalibrated trace-cost
limitations; they do not establish a general GPU speedup. Earlier synthetic
benchmark values are retained separately; README links the new results.

### Upload-boundary wall-interval analysis (phase 4.73)

Priority 2 begins with offline reanalysis; no native binary or setting changes.
`build-tools/paint-trace/overhead.py` consumes only already-verified timing inputs
and their explicit descendant upload IDs. Each required upload must have exactly
one finite, nonnegative issue timestamp within the verified input/swap interval.
It partitions that interval at the **latest required upload-issued timestamp**:
input receipt to that boundary, and that boundary to the last required Qt swap.
Unrelated uploads are ignored. Missing, ambiguous or reversed timestamps exclude
the input rather than inventing an association. The partitions add to total wall
time for each input; independently aggregated medians/p95 do not necessarily add.

This is a chronological partition, not a GPU timer or causal critical path.
The first part includes job queues, dab generation, brush processing, projection,
canvas preparation/upload and any waits before the marker; the second includes
remaining canvas/presentation work and the Qt acknowledgment. Asynchronous stages
overlap; do not sum CPU spans or call the first partition transfer-only cost.

Validation: all Python tests 39/39. New cases cover last-required versus unrelated
uploads, exact per-input partition, missing/duplicate/nonfinite timestamps,
out-of-interval timestamps, empty verified population and nonadditive medians.
All nine raw baseline logs reanalyzed successfully with zero additional timing
exclusions; original timing reports are unchanged. All 2,078 measured inputs
remain after the same explicit warm-up exclusions. Original archived summaries
are untouched; each run adds `upload-boundary.json`, with the aggregate in
`%TEMP%/solstice-paint-baseline-471/upload-boundary-comparison.json`.

Aggregation follows the baseline hierarchy independently per component: median
of three measured stroke medians per process, then median of three processes.
The values below are milliseconds (before / after latest required upload issue):

| Condition | CPU | CPU brush + shared buffer | GPU brush + shared buffer |
| --- | ---: | ---: | ---: |
| 64px Buildup | 16.55 / 3.26 | 16.56 / 2.98 | 17.33 / 1.43 |
| 64px Wash | 15.91 / 3.76 | 14.77 / 3.24 | 16.68 / 2.42 |
| 256px Buildup | 19.77 / 2.31 | 31.20 / 2.74 | 23.01 / 1.08 |
| 256px Wash | 15.85 / 2.92 | 23.10 / 0.91 | 19.73 / 2.71 |

For 256px Buildup, process ranges before the boundary are 17.10–21.38 (CPU),
24.49–31.52 (CPU brush/shared buffer), and 18.95–26.63 (GPU brush/shared buffer).
After it, ranges are 2.25–3.23, 0.51–2.99, and 0.66–1.56 respectively. For 256px
Wash, before ranges are 14.68–17.64, 22.96–27.16, 19.60–22.06; after ranges are
2.46–3.24, 0.65–1.73, 2.02–3.54. Preserve overlap/variation and manual-input
limitations. The larger shared-buffer-mode intervals appear predominantly before
upload issuance, not as an increased trailing Qt acknowledgment interval.

Next investigation is the pre-upload region: distinguish queue delay, canvas
preparation, source staging and actual synchronization, correlating update IDs
instead of matching nearby timestamps. Existing broad `canvas.prepare` and
`canvas.upload` spans are not sufficient to uniquely assign every sub-operation
or GPU wait. Vulkan timestamp measurements, submission counts and transfer byte
accounting remain open priority-2 work. No runtime optimization or causally
established bottleneck is claimed; no further manual capture is requested for
this offline step.

### Direct update-ready to upload-issue analysis (phase 4.74)

2026-10-06: offline analysis only; no native rebuild or installation.
`overhead.summarize_ready_to_issue()` adds `ready_to_issue_timing` to the normal
paint-trace summary. It selects uploads linked to verified timing inputs,
deduplicates shared uploads across inputs, and joins each upload's explicit
parent to exactly one `update.ready` event. Missing/duplicate markers and
nonfinite, negative, boolean or reversed timestamps are excluded. It never
matches by timestamp proximity or substitutes an older compressed update.

Reanalysis of the nine archived phase-4.71 captures produced these exploratory
process summaries (all verified inputs, including warm-ups and all conditions):

| Run | Unique uploads | Median ms | P95 ms |
| --- | ---: | ---: | ---: |
| 01 CPU | 734 | 2.164 | 3.822 |
| 02 projection | 692 | 3.033 | 8.427 |
| 03 brush | 757 | 1.764 | 10.029 |
| 04 projection | 681 | 2.630 | 9.459 |
| 05 brush | 739 | 1.818 | 5.143 |
| 06 CPU | 701 | 2.107 | 3.691 |
| 07 brush | 750 | 1.754 | 5.281 |
| 08 CPU | 941 | 1.714 | 8.800 |
| 09 projection | 790 | 2.364 | 7.800 |

No selected upload was excluded. These are upload-weighted mixed-condition
diagnostics, not replacements for the measured-stroke baseline or evidence of
a speedup. The interval includes event-loop/compressor waiting and upload
processing. It excludes residence of earlier merged/superseded updates and
can start before a later input joins that update. Therefore it must not be
subtracted from per-input latency or summed across overlapping updates.
The existing native `update.ready` marker occurs after preparation; preparation
and the earlier drawing/projection scheduling remain unresolved. GPU execution
timestamps, transfer volume and submission overhead remain open.

Validation: 42 Python tests pass, including direct-parent selection despite an
older merged update, deduplication, missing/ambiguous/invalid timestamps, and
empty verified input sets. Run `python -B -m unittest discover -s
build-tools/paint-trace -p 'test_*.py'` from the repository root. Running the
ordinary `summarize.py` on each archived trace reproduces the new summary field.
No new interactive capture is required for this offline addition.

### Projection-end to prepared-update analysis (phase 4.75)

2026-10-06: `overhead.summarize_projection_preparation()` joins the explicit
walker ID in `projection.merge` to the parent ID of `update.ready`. Only walkers
downstream of verified timing inputs are selected. Each requires one complete
merge span and one ready marker with valid ordered timestamps; multiple ready
notifications are excluded rather than choosing the nearest. Walkers are
deduplicated globally and within each stroke. The normal summary now includes
`projection_preparation_timing`, including per-stroke medians and P95s.

All nine archived phase-4.71 captures passed without exclusions: respectively
737, 719, 767, 688, 757, 703, 756, 946 and 795 unique walkers including warm-ups.
The comparison below uses the explicit measured stroke IDs in the baseline
run sheet and archived `comparison.json`: median of three measured stroke
medians per process, then median of the three process values. Entries show
**CPU merge span / merge-end to update-ready**, in milliseconds. These are
walker-weighted intervals within a stroke, not an input-latency partition.

| Condition | CPU | CPU brush + shared buffer | GPU brush + shared buffer |
| --- | ---: | ---: | ---: |
| 64px Buildup | 0.010 / 0.258 | 0.010 / 1.067 | 0.010 / 0.143 |
| 64px Wash | 0.049 / 0.202 | 0.046 / 0.948 | 0.702 / 0.074 |
| 256px Buildup | 0.013 / 0.723 | 0.011 / 6.161 | 0.014 / 5.030 |
| 256px Wash | 0.096 / 0.601 | 0.097 / 5.744 | 2.021 / 0.251 |

For 256px Buildup, process ranges for merge-end to ready were CPU
0.608–0.747ms, CPU brush/shared buffer 5.957–7.908ms, GPU brush/shared buffer
4.214–5.120ms. For 256px Wash, the GPU brush merge CPU span was 1.775–2.489ms,
while its merge-end to ready was 0.161–0.440ms. Thus Buildup and Wash do not
justify assuming the same dominant stage. Shared batches/walkers correlate
samples; components' medians cannot be added or subtracted from input medians.
Merge spans measure host work, including submission/waiting when present, not
GPU execution. The subsequent interval includes notification and canvas
preparation/synchronization; it does not isolate a copy or queue wait.

Code inspection identifies next measurement sites in
`KisGpuCanvasUploader::upload()`: context acquisition/`commands.begin()`,
`KisGpuTileAccess::prepare()`, shared-buffer acquisition and submit/finish.
The context pool currently selects its last free entry without checking whether
its prior submission has completed; `begin()` can wait for that submission.
This is a candidate, not a demonstrated cause. Wash additionally needs its
indirect-painting/compositor span split. No synchronization or rendering behavior
has been changed based on the offline data alone.

Validation: 45 Python tests pass, including unrelated-nearer-marker rejection,
deduplication, per-stroke separation, ambiguous/missing events, invalid/reversed
timestamps and empty verified input sets. No native rebuild, installation or
new user capture was needed. Original baseline logs/summaries remain unchanged.

### Native preparation stage probes (phase 4.76)

2026-10-06: installed opt-in CPU scopes in `KisGpuCanvasUploader::upload()`:
`canvas.gpu.acquire_context`, `canvas.gpu.begin`, `canvas.gpu.prepare_source`,
`canvas.gpu.acquire_buffer`, and `canvas.gpu.submit_finish`. Added compositor
scopes for context acquisition, existing context wait, target/layer preparation
and submit/finish (`compositor.*`). Scopes carry the current walker flow ID and
the existing automatic job identity; direct test calls can legitimately have
zero flow. Multiple layer scopes per walker are expected. These are CPU spans,
not GPU timestamps or isolated transfer costs. The submit/finish compositor
scope also includes its existing result handling and success marker.

The wrappers preserve original return values, short-circuit validity checks,
failure cleanup and call order. No extra GPU wait or rendering branch was
introduced. Disabled tracing does not record timestamps/events. Existing
summary stage statistics expose these names; do not associate zero-flow spans
with nearby inputs or add overlapping scopes as input latency.

Build succeeded for `kritaui`, `KisGpuCanvasUploadTest`, `KisGpuStrokeTest`.
Validation-enabled trace-on tests exited successfully. Their logs contain
13,597 and 1,829 new stage events respectively, all finite nonnegative complete
spans, with no recorder overflow. Trace-off QtTest reports: canvas 28/28,
64/256px Buildup/Wash stroke rows 6/6. Logs/traces use the temporary prefix
`solstice-stage-476-`. Installed `libs/image` and `libs/ui`; no app was launched
or terminated by the agent.

Next manual capture is a focused diagnostic, not a repeat of the nine-process
baseline: launch `build-tools/paint-trace/run.cmd <krita-dev-root> brush` and use
the same RGBA F32 document and Basic-4 Flow Opacity preset. At exactly 256px,
draw three short Buildup strokes, then three Wash strokes, and exit normally.
The first of each group is warm-up. Inspect the new linked scopes before
choosing an optimization. This real-app check remains pending; automated
success does not establish which wait dominates in the user's document.

### Focused real-app stage capture (phase 4.76 follow-up)

2026-10-06: user closed the diagnostic process, PID 43812. Archived trace,
launcher log and full summary are under `%TEMP%/solstice-stage-476-real-43812/`.
Six ended strokes match Basic-4 Flow Opacity, exact 256px, RGBA F32 2480x3508:
Buildup warm-up 4193, measured 4959/5856; Wash warm-up 7144, measured 7904/8696.
All 112 timed inputs passed the recorded checks; 24 inputs without dab requests
are excluded. Recorder overflow is zero. This single-process diagnostic does
not replace the nine-process baseline or establish a performance improvement.

For each measured stroke, select the distinct downstream walker IDs of verified
timing inputs and collect the new CPU scopes with matching explicit `args.id`.
The table shows the two per-stroke medians in milliseconds, not summed values:

| Scope | Buildup 4959 / 5856 | Wash 7904 / 8696 |
| --- | ---: | ---: |
| canvas.gpu.acquire_context | 0.1602 / 0.2671 | 0.0001 / 0.0002 |
| canvas.gpu.begin | 0.0131 / 0.0145 | 0.0103 / 0.0108 |
| canvas.gpu.prepare_source | 0.1216 / 0.1575 | 0.0830 / 0.0698 |
| canvas.gpu.acquire_buffer | 0.1783 / 0.0003 | 0.0003 / 0.0003 |
| canvas.gpu.submit_finish | 1.1584 / 1.4042 | 0.1455 / 0.2210 |
| compositor.wait_context | not observed | 0.0080 / 0.0071 |
| compositor.prepare_target | not observed | 0.4789 / 0.5127 |
| compositor.prepare_layer | not observed | 0.1472 / 0.1307 |
| compositor.submit_finish | not observed | 0.1165 / 0.2354 |

Buildup has 119/102 matched canvas calls; Wash has 101/109 calls per listed
stage. Measured Buildup buffer acquisition has maxima 14.7683/10.9974ms, so its
occasional tail remains relevant despite low medians. Context begin medians do
not support treating reuse waiting as the main cost in this capture.

Next priority: split `KisGpuTileAccess::submitAndFinish()` into residency mutex
acquisition, recording uploads, queue submission, publishing and completion;
also inspect target preparation for Wash. Its present scope includes all these
operations, so a long value does not prove GPU execution or mutex contention.
Do not remove/reorder the residency lock: upload ordering and failed-state
publication depend on it. No runtime optimization has been made yet, and no
additional user capture is requested until narrower measurement is ready.

### Tile preparation and submission substage probes (phase 4.77)

2026-10-06: `KisGpuTileAccess.cpp` now records opt-in `tile_access.resolve_tiles`
(tile lookup, COW handling and pinning) and `tile_access.stage_uploads`
(staging allocation/reuse and CPU snapshot copies). Submission records
`tile_submit.lock`, `record_uploads`, `queue`, `publish`, and `complete`.
Names carry the `tile_submit.` prefix. Flow ID and existing job identity are
retained, and the command-list pointer identifies the submission owner.

The lock scope wraps construction of the existing RAII locker; the returned
locker remains alive across the original critical section. Upload ordering,
failed-state checks, state publication and restoration outside the lock are
unchanged. No GPU wait is added. The spans describe host intervals, including
waits inside their existing operations, not GPU execution. Trace recording
itself adds overhead, including while the residency mutex is held; these probes
are diagnostic and must not be treated as an uninstrumented performance result.
Preparation scopes apply to all tile accesses; match enclosing canvas or
compositor scopes with the same thread/flow to distinguish their callers.

Build succeeded. With Vulkan validation, canvas tests passed 28/28 and the
64/256px Buildup/Wash stroke rows passed 6/6, both tracing on and off. Trace-on
captures have 16,505/2,867 new finite nonnegative spans, zero dropped events,
and matching lock/record/queue/publish/complete counts (1,191/260 submissions).
Temporary artifacts use `solstice-stage-477-`. Installed the rebuilt image
library; no app was launched or stopped by the agent.

Real-app follow-up is pending: use the same phase-4.76 `brush` launcher and
256px Basic-4 Flow Opacity conditions, three Buildup then three Wash strokes,
with the first of each group as warm-up. Prior captures lack these substage
events. This is one diagnostic process, not another nine-process baseline.
Do not optimize lock scope or upload publication until this distinction is known.

### Real-app submission substage findings (phase 4.77 follow-up)

2026-10-06, PID 11944: six 256px strokes, zero recorder overflow, 108 verified
timing inputs; 23 inputs without recorded dab requests excluded. Buildup warm-up
3593, measured 4395/5159; Wash warm-up 6609, measured 7413/8213. Archived raw
trace, launch log and summary: `%TEMP%/solstice-stage-477-real-11944/`.

Select distinct downstream walker IDs of verified inputs per measured stroke.
Substages are assigned only to one enclosing canvas submit, compositor submit,
or compositor target-prepare span with the same flow ID and thread (timestamp
containment, not nearest-event matching). Per-stroke medians in milliseconds:

| Substage | Buildup 4395 / 5159 | Wash 7413 / 8213 |
| --- | ---: | ---: |
| Canvas submission lock acquisition | 0.9164 / 0.5936 | 0.0062 / 0.0001 |
| Canvas record uploads | 0.0012 / 0.0009 | 0.0006 / 0.0004 |
| Canvas queue submit | 0.0898 / 0.0546 | 0.0326 / 0.0406 |
| Canvas publish | 0.0003 / 0.0003 | 0.0002 / 0.0002 |
| Canvas complete | 0.0003 / 0.0003 | 0.0001 / 0.0002 |
| Compositor target resolve tiles | not observed | 0.1932 / 0.2039 |
| Compositor target staging | not observed | 0.1512 / 0.1150 |

Canvas submit counts are 95/97 for measured Buildup and 72/96 for measured
Wash; target staging is conditional and appears 56/69 times. These medians
must not be summed. The Buildup lock-acquisition interval is much longer than
the queue-submit interval in this capture. This identifies a host lock
acquisition bottleneck candidate, not GPU kernel execution time or the identity
of the lock holder. Wash does not show the same typical contention.

Next investigate all residency-lock holders: `pinState()` (including slot
allocation), `unpinStates()`, `tryEvict()`/`tryEvictBatch()` (which can download
under the lock), and concurrent `submitAndFinish()`. Existing traces cannot
uniquely identify the competing holder. Preserve upload ordering and failure
publication invariants; no lock removal is justified. No further manual capture
is requested until the next change is ready; no speedup is claimed.

### Residency lock-holder tracing (phase 4.78)

2026-10-06: added `residency.hold.*` scopes after acquisition and before release
at all current backend residency-lock sites: `pin_existing`, `pin_allocate`,
`unpin`, `evict`, `evict_batch`, `fail`, plus `submit` in tile access. RAII
destruction order preserves the original lock lifetime. `tile_submit.lock`
now uses the backend as owner and command list as related pointer, so wait and
hold records identify the same mutex. Other submission substage owners remain
the command list. Existing phase-4.77 wait owners are not compatible with this
holder join and must not be guessed by temporal proximity.

Unfiltered per-tile holds overflowed the canvas regression trace (621,250
dropped events); that initial trace is unusable. `KisPaintTrace.cpp` now omits
`residency.hold.*` spans below 10 microseconds before taking the recorder mutex.
The threshold is serialized as `residency_hold_min_us: 10`. Omitted short holds
are intentional filtering, not overflow. Other event families remain unfiltered.
Trace-on still adds clock/recording overhead; observed holds exclude unlock and
recording gaps and cannot fully account for every wait.

New `build-tools/paint-trace/residency.py` provides the summary field
`residency_contention`. It matches process/owner and different threads,
clips intersections with waits, and rejects owner groups with overlapping hold
intervals beyond a 1e-6 microsecond floating-point tolerance. Results are
observed overlap, not proof of scheduler causation. Sums count time per wait,
not elapsed process wall time, and include warm-ups unless filtered explicitly.

Validation: 50 Python tests pass, covering clipped overlap, gaps, wrong
process/owner/thread, ambiguous holds, invalid timestamps and serialization
roundoff. Native Vulkan-validation tests pass: canvas 28/28, stroke 6/6, with
tracing on and off (off run before the trace-only threshold adjustment).
Final filtered traces have zero overflow, invalid events or ambiguous groups.
Canvas: 1,191 waits, 446 with observed hold overlap; 449.46ms summed wait,
405.16ms overlap, of which 404.88ms belongs to other submit holders. Stroke:
260 waits, 45 matched; 3.91ms summed wait, 3.55ms overlapping submit holders.
These aggregate test workloads are not the user's six-stroke workload and
do not establish real-app contention ownership.

Installed rebuilt `libs/image`; logs use `solstice-stage-478-`, final trace-on
logs use `solstice-stage-478-filtered-`. Real-app follow-up remains one `brush`
process with the same 256px preset/document, three Buildup then three Wash
strokes. First stroke of each group is warm-up. No lock-order or performance
optimization has been applied before identifying the real-app holder.

### Real-app residency overlap (phase 4.78 follow-up)

2026-10-06, PID 44756: expected six 256px Basic-4 Flow Opacity strokes in
RGBA F32 2480x3508, 109 verified timing inputs; 27 no-dab inputs excluded.
No dropped events; hold threshold is 10us. Buildup warm-up 4152, measured
4891/5618; Wash warm-up 7040, measured 7754/8487. Raw trace, launch log and
summary are archived at `%TEMP%/solstice-stage-478-real-44756/`.

Select each stroke's verified downstream walkers and their enclosing
`canvas.gpu.submit_finish` scopes, then select contained same-thread/flow lock
waits. Compare against all same-backend cross-thread holds, including holders
outside that stroke's lineage. Results below are **sums over individual waits**,
not wall-clock stroke delays or medians; concurrent waits can overlap.

| Measured stroke | Canvas waits | Wait sum ms | Overlap with other submit holders ms |
| --- | ---: | ---: | ---: |
| Buildup 4891 | 74 | 68.0875 | 66.0468 |
| Buildup 5618 | 88 | 91.8102 | 89.0707 |
| Wash 7754 | 75 | 14.5011 | 13.5494 |
| Wash 8487 | 93 | 19.7232 | 18.2418 |

Thus about 97% of measured Buildup canvas lock-acquisition time overlaps other
submission holders. Pin-existing contributes just 0.113ms in the first Buildup
stroke; no other holder category overlaps those measured Buildup waits.
No ambiguous owner groups, invalid intervals or excluded waits were found.
Wash also overlaps submit holders, but has a much smaller summed wait in this
capture. This strengthens the case for investigating concurrent submissions
rather than slot allocation or eviction first. It does not prove a speedup
without tracing, nor quantify physical input-to-pixel delay.

One directly joined example: walker 4963 holds residency for 1.8486ms during
canvas submission; its `tile_submit.queue` interval is 1.8474ms. The queue
scope covers command-buffer ending and context submission, not just Vulkan
kernel work. Other calls can be shorter. Next inspect that path and safe ways
to reduce residency critical-section time while preserving upload generation,
queue ordering and failure-publication invariants. Do not simply move queue
submission outside the lock or remove locking. No further manual run is
requested until there is a concrete next change.

### End main command recording before residency acquisition (phase 4.79)

2026-10-06: `KisGpuCommandList::finishMainRecording()` ends the main command
buffer once, caches success/failure until `begin()`, and leaves the late-upload
preamble available. `KisGpuTileAccess::submitAndFinish()` calls it before
acquiring residency, traced as `tile_submit.finish_main`. Main commands are
already complete at this point; generation-dependent upload decisions and
preamble recording, queue submission, and state publication remain under the
original residency lock. Failure skips upload recording/submission and retains
the existing abandon/publish-zero/restore/complete path. `abandon()` does not
end the main buffer twice, and `begin()` clears the cached state.

After early finish, callers must not record more main commands. The existing
`isRecording()` indicates an open recording/submission cycle (including the
still-available preamble), not that the main buffer remains writable. Ordinary
`submit()` callers still work without explicitly finishing early. This changes
host recording-end timing, not GPU execution/submission order; preamble stays
first in the same submission. Queue submission itself remains in the critical
section. Improvement magnitude is unmeasured; earlier queue-scope observations
included both command-buffer ending and context submission.

Added `KisGpuEngineTest::testEarlyMainFinish`: main readback recorded and ended
before a late preamble fill, exact output for two reuse cycles, idempotent end,
abandon, injected submit failure, and successful reuse after failure. With Vulkan
validation: selected engine tests 6/6; shared upload arena, failed-state refusal,
failed-submission content preservation and older-upload ordering tests 13/13;
canvas 28/28; 64/256 Buildup/Wash stroke tests 6/6. Trace-on canvas also passes
28/28 and verifies 1,191 early-finish spans end before their matching lock spans,
with zero overflow. Artifacts: `%TEMP%/solstice-stage-479-*`.

Installed rebuilt GPU, image and UI libraries together (command-list layout
changed). Real-app follow-up: same `brush` launcher, exact 256px Basic-4 Flow
Opacity in RGBA F32, three Buildup then three Wash strokes. Compare against the
archived phase-4.78 capture, excluding first strokes, and check Undo/Redo for
visible regressions. No speedup is claimed until this check; no lock was removed.

### Early-finish real-app result: no demonstrated improvement

2026-10-06, PID 3704, phase 4.79: six expected 256px Basic-4 Flow Opacity
RGBA F32 strokes, 117 verified timing inputs, 14 no-dab inputs excluded, zero
overflow. Buildup warm-up 3907, measured 4687/5607; Wash warm-up 6974,
measured 7800/8714. Archive: `%TEMP%/solstice-stage-479-real-3704/`.

Each pair below is the two measured per-stroke medians in milliseconds.
Canvas substages use the same explicit walker/thread/enclosing-submit selection
as phase 4.78. Input timing is verified input receipt to command-swap notification.

| Metric | Before Buildup | After Buildup | Before Wash | After Wash |
| --- | ---: | ---: | ---: | ---: |
| Canvas lock acquisition | 0.7831 / 0.9466 | 0.8996 / 0.7845 | 0.0001 / 0.0077 | 0.0129 / 0.0172 |
| Early main finish | not split | 0.0015 / 0.0016 | not split | 0.0007 / 0.0007 |
| Canvas queue stage | 0.0382 / 0.0343 | 0.0397 / 0.0947 | 0.0309 / 0.0343 | 0.0347 / 0.0283 |
| Input to command swap | 30.269 / 34.326 | 35.876 / 60.148 | 34.585 / 32.662 | 48.180 / 44.333 |

No speedup is demonstrated. The moved operation is very small in these samples
and Buildup lock contention remains. End-to-end software timings are worse in
this capture, but one process per version with two hand-drawn measured strokes
per condition cannot isolate a regression caused by the change. Matched canvas
call counts also differ (before Buildup 74/88, after 115/112; before Wash 75/93,
after 110/129). Do not label this a verified optimization in README/benchmarks.
User reported closure only; explicit Undo/Redo success was not provided.

Next investigate `KisGpuContext::submit()` queue-mutex acquisition and the
driver `vkQueueSubmit2` call, especially long competing submissions. Early main
finalization is retained as a correctness-tested change with unproven latency
benefit; do not remove residency ordering protection based on this result.
No additional manual repetition requested at this analysis step.

### Queue-lock and driver-call timestamps (phase 4.80)

2026-10-06: optional `KisGpuSubmitTiming` output in the context/command-list
submission API captures steady-clock nanoseconds before/after queue-mutex
acquisition and immediately before/after `vkQueueSubmit2`. It is reset on each
call; injected refusal leaves driver timestamps zero. Null output performs no
clock sampling. GPU execution is not timed by these host timestamps.

Tile access requests timing only when paint tracing is enabled and records
saved `tile_submit.queue_lock`, `tile_submit.queue_prepare`, and
`tile_submit.driver` intervals through `KisPaintTrace::externalSpan()` after
the residency critical section and completion work. This avoids acquiring the
trace recorder mutex inside these newly measured queue/driver intervals.
Each event retains backend owner, command-list related pointer, flow, job and
originating thread. Events may appear later in the JSON array than their
timestamp; analysis must use timestamps and explicit identities, not array
adjacency. Existing surrounding scopes still incur their normal trace overhead.

New `testConcurrentSubmissionTiming` uses four worker threads, independent
command lists/output buffers and 32 submissions each. It checks all readback
words, 128 unique timeline values, ordered host timestamps, refusal without a
driver call, successful untimed reuse, and no added validation errors.

Vulkan-validation regression results: selected engine tests 7/7, shared arena /
failure / old-upload ordering 13/13, canvas 28/28, stroke 6/6; trace-enabled
canvas/stroke also 28/28 and 6/6. Python analysis remains 50/50. Trace captures
have zero overflow and one uniquely enclosing queue scope for each of the three
new intervals across 1,191 canvas and 258 stroke submissions.

Exploratory full-test medians (queue lock / preparation / driver, milliseconds):
canvas 0.0001 / 0.0008 / 0.0520; stroke 0.0001 / 0.0005 / 0.0445. Stroke P95
queue-lock and driver intervals are 0.1471 and 0.2710ms. These mixed test calls
suggest investigating driver-call tails, but are not the real-app Buildup
measurement. No rendering/locking policy or claimed performance result changed.

Artifacts use `%TEMP%/solstice-stage-480-*`. Installed GPU/image/UI libraries;
their build/install hashes match. Next focused real-app capture remains the
same six-stroke 256px Buildup/Wash protocol, to inspect the competing submit
holders' queue-lock and driver intervals. Do not repeat the nine-run baseline
until there is a demonstrated runtime improvement worth benchmarking.

### Real-app driver-call overlap (phase 4.80 follow-up)

2026-10-06, PID 24148: six expected 256px Basic-4 Flow Opacity strokes,
RGBA F32 2480x3508, 165 verified timing inputs (108 after warm-up exclusion),
52 no-dab inputs excluded, zero overflow. Buildup warm-up 4027, measured
5252/6402; Wash warm-up 7986, measured 9250/10529. Archive:
`%TEMP%/solstice-stage-480-real-24148/`, raw trace, launcher log and summary.

Select verified walkers, contained same-thread/flow canvas-submit lock waits,
and all directly timed queue/driver spans of other threads on the same backend.
The overlap sums below count time per waiter; concurrent waiters can count the
same driver interval repeatedly. They are not wall-clock stroke duration.

| Stroke | Canvas waits | Wait sum ms | Other submit hold overlap ms | Other driver-call overlap ms | Other queue-lock overlap ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Buildup 5252 | 136 | 108.5455 | 104.0951 | 103.0312 | 0.0187 |
| Buildup 6402 | 125 | 94.1548 | 90.7168 | 89.7886 | 0.0206 |
| Wash 9250 | 142 | 14.2552 | 13.1517 | 12.2399 | 0.4865 |
| Wash 10529 | 132 | 10.2298 | 9.2950 | 7.7846 | 1.1863 |

Buildup's driver-call overlap is about 95% of summed residency acquisition
time. Other-thread queue-information preparation overlap is only 0.2112 and
0.1654ms for these two strokes. This directly narrows the observed long holder
intervals to `vkQueueSubmit2`, rather than the context's queue-mutex acquisition.
It does not explain why the driver call takes time or establish a driver bug,
GPU execution duration, or physical input-to-pixel latency.

For directly selected canvas calls, Buildup lock medians are 0.5454/0.6640ms,
driver medians 0.0436/0.0482ms with maxima 2.467/2.400ms; queue-lock medians
are 0.0001ms. Wash driver medians are 0.0225/0.0228ms; queue-lock medians
are also 0.0001ms. Selected canvas call counts are larger than input counts,
so the next candidate is coalescing compatible canvas preparation/submissions,
using actual patch/tile coverage and preserving compression lineage, fallback,
color conversion, and complete image-update coverage. Keep serial queue ordering
and residency publication protection; removing those locks is not justified.

Measured input-to-command-swap medians are Buildup 22.942/24.098ms and Wash
21.101/28.408ms. Capture lengths and per-stroke update counts differ from prior
captures; no runtime change occurred since phase 4.80 and no speedup is claimed.
No further manual repeat requested at this analysis step. Next implementation
should reduce demonstrably redundant submissions before rebenchmarking.

### Shared canvas update builds (phase 4.81)

2026-10-06. Classifying the PID 24148 submit spans by enclosing scope showed
834 canvas, 428 compositor and 62 brush submissions, against only 169
GUI-thread canvas consumptions (`canvas.upload`). Canvas lock waits overlapped
other threads' canvas driver calls for 353.1ms, compositor calls for 14.1ms and
brush calls for 2.2ms (summed per waiter, whole capture including warm-ups).
The contention is therefore mainly between concurrent canvas uploads of
different projection walkers. Canvas driver calls also had the longest tail
(median 0.030ms, P95 1.09ms, maximum 4.55ms).

Implementation (all in `libs/ui`, GPU canvas path only):

- `canvas/KisCanvasUpdateBatcher.*`: group commit. `process()` enqueues a
  request with its thread's paint-trace flow and returns only after a build
  containing it has finished. A thread with no build running takes the oldest
  pending requests (at most 32 and 1 Mpx after the first, which is always
  taken) and builds them; others wait on a condition variable (`canvas.batch_wait`
  scope). Builds never overlap and complete in arrival (ticket) order. A
  same-thread re-entry from inside a build builds alone instead of deadlocking.
- `KisCanvas2::startUpdateCanvasProjection()` uses the batcher when
  `KisAbstractCanvasWidget::sharesProjectionUploads()` is true (OpenGL canvas,
  GPU canvas upload enabled and supported for the projection/display pair, no
  soft proofing or channel selection). Otherwise the original per-update path
  runs unchanged (QPainter canvas, CPU canvas path).
- The build creates **one update info per request**, unchanged rects, trace ids
  and compressor semantics. It emits each request's `update.ready` with that
  request's captured flow, `update.request_rect`, and a new informational link
  `update.batched` (later update -> first update of the batch). The infos are
  put into the compressor atomically (`KisCanvasUpdatesCompressor::putUpdateInfos()`)
  before any waiting thread returns.
- `KisOpenGLUpdateInfoBuilder::buildUpdateInfos()` collects the tiles of all
  rects and calls `KisGpuCanvasUploader::upload()` once. On failure each info
  takes the original CPU path (per-rect `syncToCpu`, retrieve, convert).
  The single-rect `buildUpdateInfo()` now delegates to it.
- `KisGpuCanvasUploader::upload()` groups patch centers into source regions,
  merging only centers closer than one 64px GPU tile, and prepares one
  read-only `KisGpuTileAccess` per region. A combined row-major address table
  over the regions' tile grids (zero between regions, never read) feeds the one
  patch-writer dispatch; all accesses go to one `submitAndFinish()`. Distant
  updates (e.g. mirrored dabs) are not widened to a bounding rect. Region grids
  are checked to be disjoint. A single update produces exactly the previous
  single access and table.
- `KisGpuCanvasUpload` GL holds nest. `KisOpenGLCanvas2::updateCanvasProjection(QVector)`
  acquires all uploads of the taken updates once, and releases after every
  update was applied; `recalculateCache()` holds still nest inside it. The last
  release signals GL completion, so no update of a batch can read a buffer after
  Vulkan may reuse it. A re-acquire after the final release is a safe assert.

Ordering argument: before, a walker thread returned from `sigImageUpdated` after
its canvas submission and compressor put; conflicting walkers start only after
that. The batcher keeps this: a waiting thread returns only after a build that
submitted its read and put its info. Builds are serialized and in ticket order,
so compressor puts remain in read order. Reads are still ordered on the single
queue before later conflicting projection writes.

Tests (Vulkan validation): `KisGpuCanvasUploadTest` 38/38, including 10 new
`testSharedUploadMatchesCpu` rows: overlapping, distant (separate accesses in one
submission), one-pixel and outside-image rects build with one upload counter
increment, one shared upload object, CPU-identical textures, and nested hold
behavior; trace-enabled run also 38/38; zero validation errors. The existing GL
import failure test now releases its own outer hold (it had relied on idempotent
acquire). New `KisCanvasUpdateBatcherTest` 8/8 (single thread, waiters sharing a
build, request/pixel limits, 8 threads x 300 requests without overlap, duplicate or
early return, re-entry), five repeated runs stable. Python analysis 50/50.
`KisGpuStrokeTest` has no canvas and does not exercise this path. Only `kritaui`
changed; its build/install SHA256 match.

Not yet measured in the real app. Expected effect: fewer canvas submissions and
less canvas-vs-canvas residency contention in 256px Buildup; Wash had less
contention. The GUI may receive more update infos per frame; texture upload
volume per update is unchanged. Do not claim a speedup before a focused capture.
Manual checks required: painting/undo/redo/mirror/zoom/LOD with the GPU canvas,
soft proofing and channel selection (unbatched CPU path), and GL import fallback.

### Real-app capture of shared canvas builds (phase 4.81 follow-up)

2026-10-06. The user reported the manual checks (painting, Undo/Redo, mirror,
zoom/rotation, soft proofing, channel selection, save/reload) without problems.
A first capture (PID 41908) combined the manual checks with the strokes and
overflowed the event limit (213,587 dropped). It is not used for performance.
It confirmed batching in the app (1,808 updates, 1,304 canvas submissions,
504 `update.batched` links) without warnings in the launcher log.

Focused capture PID 43720: six 256px strokes, zero dropped events, 129 inputs
passing recorded checks. Buildup warm-up 3056, measured 3827/4613; Wash warm-up
6102, measured 7151/8211. Archive: `%TEMP%/solstice-stage-481-real-43720/`.
Geometry: all 731 uploads `covered_to_swapped_commands`.

Whole-capture comparison with PID 24148 (both include warm-ups):

| Metric | 4.80 (24148) | 4.81 (43720) |
| --- | ---: | ---: |
| Canvas updates / canvas GPU submissions | 834 / 834 | 774 / 241 |
| All residency submissions (`tile_submit.lock`) | 1,324 | 766 |
| Summed residency lock wait ms | 459.8 | 73.0 |
| Summed canvas submit (`canvas.gpu.submit_finish`) ms | 598.6 | 42.8 |
| Summed `canvas.prepare` ms (includes batch waits) | 2,301.0 | 1,059.6 |
| Summed `canvas.batch_wait` ms | - | 916.0 |

Measured strokes, medians in ms:

| Stroke | Merge end -> ready | Input -> first merge start | Input -> swap |
| --- | ---: | ---: | ---: |
| 4.80 Buildup 5252 / 6402 | 3.47 / 4.28 | 13.43 / 13.58 | 22.94 / 24.10 |
| 4.81 Buildup 3827 / 4613 | 0.43 / 0.25 | 27.07 / 25.27 | 38.50 / 32.62 |
| 4.80 Wash 9250 / 10529 | 0.17 / 0.23 | 12.39 / 16.56 | 21.10 / 28.41 |
| 4.81 Wash 7151 / 8211 | 1.28 / 1.33 | 25.26 / 37.40 | 39.46 / 50.41 |

The targeted post-merge interval fell for Buildup, but input-to-swap rose. The rise
is before the projection merge: input to dab request stays about 0.1ms and dab
render job queue delay stays about 2ms, but a requested dab waits 21-35ms (was
10-15ms) to be taken into a brush batch. Batch cadence (median 9-15ms gaps) and
batch-ready to paint start (0.02ms) are unchanged. The strokes of this capture
were much shorter and faster: 102-132ms at 860-1,566 dabs/s, versus 151-216ms at
607-1,199 dabs/s. Batches took about 16-19 dabs, so the dab queue backlogs at these
rates (`someDabsAreStillInQueue`). The input-to-swap difference is therefore
confounded by hand-drawn stroke speed and is not attributed to the canvas change.
Neither a speedup nor a regression of input-to-swap is established. A fair
comparison needs strokes of similar length and speed. The brush batch dab limit
under fast strokes is a separate candidate for investigation.

### Byte-limited GPU brush batches continue at once (phase 4.82)

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

### GPU-evaluated circle dabs (phase 4.83, priority 3 step 1)

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

### Exact Gaussian and fused default circle dabs (phase 4.84)

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

### Skipped CPU generation of described dabs (phase 4.85)

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

### RGBA16F generated dabs (phase 4.86)

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

### Soft (curve) circle generated dabs (phase 4.87)

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

### GPU brush update period (phase 4.88)

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

### Wash latency breakdown and chained brush updates (phase 4.89)

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

### GPU base copy in the Wash preview (phase 4.90)

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

### Shared and precompiled compute pipelines (phase 4.91)

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

### Immediate canvas uploads on the shared GPU path (phase 4.92)

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

## Risks and open questions

- Interop requires desktop OpenGL; users on ANGLE must switch renderer.
- Memory management now bounds and evicts tile pools, including a path to
  disk swap. Canvas transfer buffers have a separate cap. GPU-resident tiles
  still retain CPU snapshots, other temporary allocations and GL textures
  have no shared budget. Voluntary tile eviction downloads are now batched;
  disk swap still uses the single-tile hook.
- Individual iterator reads of stale tiles still download synchronously per
  tile; bulk `readBytes`, planar reads, CPU filter inputs/destinations and FFT
  cache reads now batch automatically. Other large
  iterator-based consumers should call `KisGpuTileAccess::syncToCpu` first.
  Host-cached staging brought the tested CPU-to-GPU transition close to
  resident update time; other GPUs/drivers and larger documents still need
  measurements. Initial preparation/upload costs remain higher than resident
  updates.
- `KisGpuLayerStackCompositor`, `KisGpuLayerCompositor`, and
  `KisGpuTileFill` reuse one host-visible table buffer, so callers must wait
  between recordings (the projection compositor keeps one per work context);
  replace with a per-submission ring before overlapping submissions.
- Projection work contexts prefer completed buffers and permit three pending
  serial submissions; they still wait under pressure before reusing busy
  command/table buffers. Successful submissions return without an end wait.
  The separate bounded upload ring added in 4.23-4.25 is specific to brushes.
- Unsupported in the GPU projection (CPU fallback): layer styles, blend modes
  outside the list above, layers whose offset is
  not a multiple of 64 relative to the projection, color space mismatches.
- The whole-stack upload measured 55 GB/s with one `vkCmdCopyBuffer` region per
  tile; evaluate a compute scatter from ReBAR memory for document load.
- Additional blend modes still need CPU parity coverage. F16 projection
  already rounds after each layer and is tested. F16 Normal/Erase dabs now
  have per-dab rounding; F16 Alpha Darken and basic generic Buildup/Wash now
  have separate arithmetic and stroke coverage. Extended F16 major modes
  (Soft Light SVG, Color Dodge/Burn and HSY color modes) now have it too;
  the remaining extended brush modes still need it before enabling F16.
- Python scripting (`libkis`) and file export need CPU pixel access; every such
  call becomes a synchronous download.
