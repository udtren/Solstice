# GPU Engine — maintenance reference

This document owns the current GPU architecture, compatibility, failure handling,
source map and regression checks. See the [user guide](../gpu-engine.md) for
workflows and visible limitations, and [work priorities](gpu-work-priorities.md)
for unfinished work and deferred investigations.

## Status and locations

The initial extraction was checked against main at
`cc2445cc4a521d58ae0904079ee67354667b3eb6` on 2026-10-16. Phase 4.110
is integrated from PR #5; its tests and recorded user acceptance are in
[Clone Layer](clone-layer.md). Work in another checkout is not included until
merged; documentation changes alone do not establish interactive acceptance.

- The engine accelerates RGBA F32/F16 tiles, layer projection, supported pixel
  brushes, selected transforms and Gaussian convolution. Unsupported conditions
  use the CPU. GPU brush painting is on by default; see the settings below.
- Masking composite, alpha-only textures and all four predefined image-tip
  applications support both float depths. The mask device itself is painted on
  the CPU. Per-operation gates and numerical limits are documented below.
- The engine's historical branch was `krita-sol-gpu`; follow the
  [repository branch policy](../../AGENTS.md#branch-policy) for new work.
- Dated implementation results and manual acceptance belong to the
  [phase history](#phase-history). The original mixed reference, initial CPU
  survey, phase plan, measurements and opening progress diary are preserved in
  the [development snapshot](wiki/history/gpu-engine-development.md).
- The former priority diary is preserved in the
  [work-order snapshot](wiki/history/gpu-work-priorities-development.md).
  Old next steps, PIDs and opt-in defaults there are historical.
- Manual failure-warning/save-dialog acceptance was still pending in the
  archived phase plan. Retain that outstanding check; automated save tests
  do not establish interactive acceptance.

## Decisions approved by the user

| Topic | Decision |
| --- | --- |
| Direction | Move the engine as a whole to the GPU, with CPU fallbacks. Upstream intake follows the global repository policy. |
| GPU API | Vulkan. |
| Working color space | GPU acceleration supports RGBA F32/F16 only. Other models remain editable on the CPU; opening conversion is optional and undoable (see the feature gate below). |
| Hardware | NVIDIA Blackwell (RTX 50 series, RTX PRO Blackwell) is the only tested target. Other GPUs are untested and have no compatibility guarantee, but nothing Blackwell-specific is required: device selection accepts any GPU with the Vulkan 1.3 features in "Technology and device requirements", and `isBlackwell()` only logs a warning. Docs say "untested", never that other GPUs fall back to the CPU (user decision, 2026-10-14). |
| Priorities | All of brush latency, many-layer documents, and filters matter; current work order is maintained in [GPU priorities](gpu-work-priorities.md). |
| Implementation workflow | Bundle several related implementation tasks and their automated checks into one manual-test handoff, as requested by the user. |

## Target architecture

The implemented integration retains Krita's tile engine, transactions, scheduler
and OpenGL canvas:

```text
Canvas and tools (libs/ui)
  OpenGL uploads shared Vulkan buffers into existing canvas textures
  KisGpuCanvasUploader / KisGpuGLSharedBuffer
        |
Image engine (libs/image)
  GPU state on KisTileData, projection batches, brush/transform/filter workers
  Existing CPU paths handle unsupported operations and synchronize stale tiles
        |
kritagpu (libs/gpu)
  Vulkan device, queue/timeline, buffers, tile pool, compute pipelines, interop
```

The original CPU survey and proposed shared-image canvas are in the development
snapshot. Production canvas transfer uses shared buffers; the shared-image
primitive remains separately available for interop tests.

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
  prevent eviction (see
  [Tile memory budget and eviction](wiki/history/gpu-reviews-and-memory.md#tile-memory-budget-and-eviction)).
- Only RGBA F32 (pixel size 16) and RGBA F16 (pixel size 8) devices are
  eligible (`KisGpuTileAccess::isSupported`). The tile engine itself stays
  color-space agnostic; eligibility is checked at the access level.
- Opening-document conversion and its Keep/Ask policies are implemented in
  `KisGpuEngineUi`; see the feature gate and compatibility sections below.

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
  previous content. The merger and all other users fall back to the CPU.
  `KisGpuEngineUi` surfaces the loss and guards saving (see the feature gate below).
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
  flags empty (all channels) or any four-channel RGBA mask, the same RGBA
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
Color and Lighter Color use `KoCompositeOpGenericHSLFunctor` semantics, and
so do (phase 4.107) HSY Tint and Increase/Decrease Saturation/Luminosity and
the HSI, HSL and HSV families (Color, Hue, Saturation, Increase/Decrease
Saturation, Intensity/Lightness/Value, Increase/Decrease of it).
Phase 4.108 adds every other mode of Krita's RGBA float spaces except
Dissolve, Marker, Destination In/Atop, Divisive Modulo, Divisive Modulo
Continuous and Modulo Continuous ("All other blend modes").

### Canvas

- Production transfer uses `KisGpuGLSharedBuffer` and the existing OpenGL
  texture upload path, as detailed in "Canvas from the GPU". Display conversion
  uses supported identity or matrix/TRC paths; LUT profiles and soft proofing
  retain CPU conversion. Existing GL texture handling generates mipmaps.
- Interop needs desktop OpenGL on the Vulkan GPU. ANGLE disables canvas interop;
  it does not by itself disable Vulkan projection or painting.
- On Windows, "Auto" prefers desktop OpenGL when
  `KisGpuMergeBatch::isEnabled()` (`KisOpenGL::selectSurfaceConfig()`,
  `preferredAutoRenderer`; ranked after the driver blacklist).
  `krita/main.cc` falls back to "auto" when no renderer is set.

### Threading and submission

- `KisGpuContext` owns one graphics+compute queue. Submission is serialized by
  a mutex; every submission signals the context timeline semaphore.
- Stroke and merge jobs keep running on `KisUpdaterContext` threads, but they
  *record* GPU work (per-thread `KisGpuCommandList`) instead of touching
  pixels. Completion is tracked by timeline values, not CPU waits.
- The GUI thread imports shared buffers and uploads canvas textures through GL;
  it must not read the live image while applying a prepared update.

### Feature gate and user interface (implemented in phase 3.3)

- `Solstice/GpuEngine` (bool; embedded default on, code fallback off; `KisGpuEngineSettings`) enables
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
  Ask (default; Convert to RGBA 16-bit float / Convert to RGBA 32-bit float /
  Keep, with "Do not ask again" storing the answer including depth), Convert
  to RGBA 16-bit float, Convert to RGBA 32-bit float, or Keep. The conversion is
  `KisImage::convertImageColorSpace()` to
  `KisGpuEngineSettings::conversionTarget(colorSpace, policy)`: RGBA F16 for
  `Convert16`, otherwise F32, with the same profile for RGB documents (F16
  rounds values to half precision), or the selected depth with the
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

### Pixel brush integration

- `KisGpuBrushPainter` accelerates supported pixel-brush Buildup dabs and
  Wash preview/final merge, including selections, channel masks, Erase and
  mirrors. Alpha Darken with partial channel masks retains CPU fallback.
  `KisBrushOpResources::gpuDabs` decides description generation once per
  stroke. Unsupported dabs can still be uploaded as materialized CPU pixels.
- `Solstice/GpuBrush` defaults to true and is read once per process;
  `KRITA_GPU_BRUSH=1/0` overrides it. It also requires GPU projection.
  Do not confuse this gate with the embedded engine default and its false
  code fallback.
- Generated circle kinds (default/Gaussian/Soft) and image applications use
  verification counters before skipping CPU generation (16 successful
  comparisons per kind/application). Retain materialization, cache reuse and
  rollback on refused/failed GPU batches.
- GPU brush submissions use three contexts and a bounded upload pool
  (64 MiB). Masked strokes that cannot use GPU masking use CPU Wash preview/final merge
  via `setTemporaryTargetPaintedOnCpu()`; supported GPU masking is below.
- GPU minimum/start update periods default to -1 (immediate), CPU minimum
  to 0 with the adaptive period retained. Overrides include
  `KRITA_GPU_BRUSH_MIN_UPDATE_MS` and `KRITA_CPU_BRUSH_MIN_UPDATE_MS`.
  The early first update and after-batch retry apply to both paths.
  Masked GPU strokes keep the adaptive period.
- Keep full-stroke and asynchronous job coverage (`KisGpuStrokeTest`,
  `KisGpuBrushJobsTest`, dab queue and freehand stroke tests), not only the
  compositor microtests. See the phase records for trace evidence and the
  still-deferred large-brush mirror investigation.

### Masking brush (phase 4.101)

A brush with a masking brush paints two devices per stroke
(`KisMaskingBrushRenderer`): the stroke device (the main brush, same color
space as the target) and a GrayA8 mask device (the masking brush). Each
update copies the stroke into the target and composites the mask into the
target's alpha (`KisMaskingBrushCompositeOp`). Until phase 4.101 this
composite ran on the CPU, so masked strokes stayed on the CPU brush.

- `libs/gpu/shaders/masking_composite.comp` + `KisGpuMaskingCompositePass.*`:
  one workgroup per 64x64 tile; inside the rect each pixel becomes the
  stroke pixel (zero for an absent stroke tile) with
  `a = composite(mode, uint8ToFloat[mask], a)`. Modes 0-9 follow
  `KisMaskingBrushCompositeFuncTypes` without strength: Multiply, Darken,
  Overlay, Color Dodge, Color Burn, Linear Burn, Linear Dodge, Hard Mix
  (Photoshop), Hard Mix Softer (Photoshop), Subtract.
- Parity: `KoColorSpaceMathsTraits<float>::compositetype` is float, so the
  shader uses `precise float` for products and sums (no double chains:
  Overlay, Linear Burn and Hard Mix Softer differed by 1 ulp with double).
  Divisions are computed in double and cast, which rounds them correctly.
  Dodge/Burn reproduce the CPU's NaN/inf handling; the fuzzy tests follow
  `qFuzzyIsNull` (<= 0.00001) and `qFuzzyCompare`. The mask byte is
  `multiply(gray, alpha)` computed on the CPU, and the byte-to-float table is
  uploaded from `KoColorSpaceMaths<quint8, float>::scaleToA` (`KoLuts`).
  Requires `shaderFloat64`.
- `libs/image/gpu/KisGpuMaskingWorker.*`: `supports(colorSpace, opId)` (GPU
  brush enabled, RGBA F32/F16, a supported mode, float64, `KRITA_GPU_MASKING` not
  `0`); `apply(stroke, mask, dst, rect, opId)` reads the mask bytes, takes
  the stroke ReadOnly and the target ReadWrite (their tile grids must match),
  records, submits and waits. It returns false without changing the devices
  when it cannot run; the caller then composites on the CPU. `runCount()`
  for tests.
- `KisPainterBasedStrokeStrategy`: `m_gpuMasking` is decided once per stroke
  (LOD 0, `paintbrush` preset, `supports()`); it marks the temporary target
  as GPU-painted (`setTemporaryTargetPaintedOnCpu(!m_gpuMasking)`, also on
  resume). `doMaskingBrushUpdates()` then runs one sequential job over the
  union of the rects; on failure it falls back to
  `updateProjection(rc)` per rect.
- `KisBrushOp`: a masked stroke keeps the GPU brush only when `supports()`
  holds for the masking composite op. It retains the adaptive update period:
  each batch also costs a masking composite and a projection merge. This protects
  CPU-generated dabs (Lightness/Gradient textures and sharpness); the dated
  comparisons that motivated this choice are in the development snapshot.
- Tests: `KisGpuStrokeTest::testMaskingCompositeMatchesCpu` (all ten modes,
  bit-exact against `KisMaskingBrushCompositeOpFactory`, pixels outside the
  rect kept) and the masked rows of `testTexturedMaskedStroke` (RGBA32F masked
  rows on the GPU brush path must run the worker; others must not).
- RGBA16F (phase 4.106, 2026-10-12, user request): the `TILE_F16` build
  `masking_composite_rgba16f` (half tiles) computes
  `KisMaskingBrushCompositeOp<half, mode>` with the half rules of the
  textured dabs ("RGBA16F image tips and textures"): `mul` and `div` in
  double, `half +- half` in float, every half result double -> float ->
  half, the half fuzzy thresholds (0.002, 0.001); the mask byte is scaled
  with `KoColorSpaceMaths<quint8, half>` (the worker passes that table).
  `KisGpuMaskingWorker::supports()` accepts RGBA F16, so masked RGBA16F
  strokes use the GPU brush. Tests: RGBA16F rows of
  `testMaskingCompositeMatchesCpu` (every mode bit-identical, including
  alphas at the half thresholds) and of `testTexturedMaskedStroke`.
- Not covered: LOD previews. (The strength modes, Height, Linear Height and
  their Photoshop variants, are texture modes only: the masking brush never
  offers them, `supportedCompositeOpIds()`; textures run on the GPU since
  phases 4.102 and 4.105.) Painting the mask itself stays on
  the CPU (an alpha device; GPU painting was measured and deferred on
  2026-10-11, `gpu-work-priorities.md`).
- Recorded manual acceptance: F32 on 2026-10-10, F16 on 2026-10-12.
  See the work-order snapshot; these are past results.

### Textured dabs (phase 4.102)

Pattern-textured circle dabs (default, Gaussian, Soft) and supported predefined
image tips can describe their texture for GPU generation at either float depth.

- Scope: the 14 alpha-only texture modes (Multiply, Subtract, Darken, Overlay,
  Color Dodge, Color Burn, Linear Dodge, Linear Burn, Hard Mix and Hard Mix
  Softer (Photoshop), Height, Linear Height, Height and Linear Height
  (Photoshop)), hard/soft texturing and pressure strength. Lightness and Gradient
  textures and sharpness keep CPU-generated pixels. Image-tip applications named
  Lightness/Gradient Map are distinct and supported; see the next section.
- Description: `KisProceduralDabTexture` (`libs/image/KisProceduralCircleDab.h`),
  attached as `KisProceduralCircleDab::texture`: the alpha8 pattern texels
  (`KisTextureMaskInfo::alphaBytes()`, computed once in `recalculateMask()`),
  the patch origin `offset % size - effectiveOffset` of `apply()`, the
  `KisMaskingBrushCompositeFuncTypes` mode, the soft flag, the composite
  function constants as their constructors compute them in float (strength,
  `0.99 * strength` for Height and Linear Height, its inverse, the
  10x/9x Photoshop weights), and the brush tip's reflection flips.
  `KisTextureOption::gpuDescription()` builds it; `apply()` now shares
  `applyAlphaMask()` with the description's CPU applier.
- Jobs (`KisDabRenderingJob.cpp`): `buildCircleDab()` accepts a dab whose
  postprocessing is a GPU texture (`gpuTexture()`). While a mask kind is
  being verified (phase 4.85), textured dabs are generated and textured on the
  CPU, and the circle is verified against the original (untextured) pixels.
  Once the kind is verified, a textured dab skips both the CPU generation and
  the CPU texturing: the postprocess branch replaces the job's description
  with the circle plus that dab's texture (`texturedDescriptionCount()`).
  Postprocess jobs (repeated dab shapes) build their own texture description.
- Reflections: the brush tip's mirroring (`mirrorFlips(di)`) happens before
  texturing, canvas mirroring after it. `proceduralFlips` holds both; the
  shader samples the texture at the generation coordinate reflected by
  `textureBrushFlips`. `KisRenderedDab::materialize()` renders the circle with
  the brush flips, applies the CPU texture (`CpuApplier`, the real
  `KisTextureOption` code), then mirrors by the remaining flips.
- GPU: `KisGpuDabCompositor::Circle` is 224 bytes including image-tip fields (texture and table
  addresses, origin, size, mode, soft, strength, inverted strength, weight,
  brush flips). The pattern bytes and the 256-entry byte-to-float table
  (`KoColorSpaceMaths<quint8, float>::scaleToA`) are uploaded once per batch
  and distinct pointer. `paint_dabs.comp`: `textureTexel()` wraps like
  `KisFillPainter::fillRect()` (positive modulo), `texturedAlpha()` repeats
  the strength composite functions of `KisMaskingBrushCompositeOp.h` with
  `precise` float operations and double divisions, including qMin/qMax/
  kisBoundFast NaN behaviour.
- Separate shader builds: the texture code is compiled into `paint_dabs_textured` and
  `paint_dabs_textured_extended` (`DAB_TEXTURE`) and their RGBA16F builds; the compositor
  uses them only for batches with a textured dab, and the untextured
  variants keep their previous source. Pitfall: with the (unused) texture
  code in the existing variants, RGBA16F dabs changed by one half ULP
  (`KisGpuBrushTest::testHalfDabs` and others) and the RGBA32F Soft circle
  stopped being bit-exact (`testGeneratedCircleDabsExact`): extra code
  changes how the driver compiles the existing exact paths.
- Tests: `KisGpuStrokeTest::testTextureModes` (14 modes x hard/soft at
  strength 0.6 with a texture offset, some mirrored, plus Lightness as the
  CPU case; layer and projection against the CPU path, generated, skipped and
  textured-description counters) and the textured rows of
  `testTexturedMaskedStroke` (now expected on the GPU).

### Image tip dabs (phase 4.103)

Before phase 4.103 every dab of a predefined (image) brush tip (GBR, PNG,
SVG, ABR) was transformed with QPainter on the CPU and uploaded as pixels.
QPainter could not be repeated on the GPU: Qt replaces its pixel
conversions at run time with AVX2/SSE4 versions that round differently
(the SSE4 unpremultiply uses `rcpps`, whose result differs between CPU
vendors), so even the CPU output depended on the machine.

- Decision (user, 2026-10-11, "A2"): replace QPainter in
  `KisQImagePyramid::createImage()` with a deterministic implementation
  shared by the CPU and the GPU brush. Up to one 8-bit step per channel of
  difference from upstream Krita is accepted.
- Sampler: `KisImageTipSampler` (`libs/image/KisImageTipSampler.{h,cpp}`,
  so that the dab description in kritaimage can use it) with the
  description `KisImageTipGeometry` (bordered ARGB32 pyramid level, its
  premultiplied QRgba64 copy computed once per level, dab size, identity
  flag, 16.16 increments and one row per dab row: first pixel, length and
  the source position at the first pixel). The arithmetic follows Qt 6.8's
  raster engine for `drawImage()` into ARGB32 (KDE qtbase fe3936871d):
  `QSpanData::setupMatrix()` with its 1/65536 delta,
  `fetchTransformedBilinear64_uint32()` with Qt's edge clamping,
  `QRgba64::premultiplied()`, the 16-bit `_mm_mulhi_epu16` interpolation of
  `interpolate_4_pixels_rgb64()`, `unpremultiplied_64bit()` (a 65536-entry
  factor table on the CPU) and `toArgb32()`. Scale-only transforms use
  Qt's rounded target rectangle; rotated dabs cover whole rows of pixels
  whose centres map into the bordered level, with the position computed at
  each row start like `QRasterizer` spans (accumulating from x = 0 gave
  differences of 4).
- `KisQImagePyramid`: `describeImage()` builds the geometry (false outside
  Qt's fast fixed-point range; `createImage()` then keeps QPainter, now
  `createImageWithQPainter()`), `renderDescribed()` and `describedPixel()`
  forward to the sampler. The CPU path is separable for scale-only rows
  (the vertical blend once per source column) and uses SSE2.
- Accuracy against QPainter on this machine
  (`KisQImagePyramidDescribeTest`, about 100 million pixels): at most one
  step of the alpha-mask value `UINT8_MULT(255 - red, alpha)` and of the
  alpha; no coverage differences for scale-only dabs, 2 pixels in 13.6
  million for rotated dabs.
- CPU speed (2026-10-11, after phase 4.103): `render()` has AVX2 kernels
  (four pixels per step, chosen at run time with
  `__builtin_cpu_supports("avx2")`, functions compiled with
  `__attribute__((target("avx2")))`; the SSE2 path remains the fallback and
  the reference). They pair-load each row's two neighbouring texels instead
  of gathering (edge pixels keep a gather path). Gray levels
  (`KisImageTipSampler::isGray()`, every visible texel red == green ==
  blue, stored per pyramid level as `KisImageTipGeometry::gray`)
  unpremultiply one channel for all three, which gives the same bytes.
  Only the pixels outside each row are cleared. Pitfall: interpolating an
  opaque tip gives alpha 0xfffe (both mulhi terms round down, as in Qt), so
  nearly every pixel takes the unpremultiply path; that arithmetic, not
  the factor table lookup, was the cost (an exact double division for the
  factor was slower than the table on Zen 5).
  `KisQImagePyramidDescribeTest` checks AVX2 against SSE2 and the gray path
  against three channels on every case, and its benchmark reports both.
  400 dabs of a 256px tip (Ryzen 9 9950X): scaled QPainter 28-34 ms, AVX2
  40-46, SSE2 53-59; rotated QPainter 53-63, AVX2 66-75, SSE2 96-107
  (before: 66-80 and 110-131 ms).
- Description: `KisProceduralCircleDab::ImageTip` (kind 3) with
  `imageTip` (the geometry); the alpha is
  `Uint8ToFloat(UINT8_MULT(255 - red, alpha))` of the sampled ARGB32 pixel,
  as `KoColorSpaceTrait::fillGrayBrushWithColor()` computes it for RGBA F32
  (no SIMD specialisation exists for float). `render()` uses
  `KisImageTipSampler::render()`; F16 table/rounding rules are described below.
- `KisBrush::proceduralImageDab()` describes the dab of
  `generateMaskAndApplyMaskOrCreateDab()` (the same pyramid and
  `KisDabShape(scale * d->scale, ratio, -normalizeAngle(rotation +
  d->angle))`), including the applications described below;
  auto, image pipe (`.gih`) and text brushes are refused (they
  generate their dabs differently).
- Jobs: `buildCircleDab()` tries the image tip when the brush is not an auto
  brush; the description's size must equal the dab bounds. The kind gets its
  own verification counter (`s_verifiedDabs[3]`), so CPU generation is
  skipped after 16 verified image tip dabs; textures (phase 4.102),
  mirroring and materialization work unchanged.
- GPU: `KisGpuDabCompositor::Circle` is 224 bytes (level and row
  table addresses, level size, increments, identity flag). The ARGB32 level
  and each dab's row table are uploaded once per batch and distinct pointer,
  with the byte-to-float table. `paint_dabs.comp` evaluates kind 3 only in
  the `DAB_TEXTURE` builds (the compositor selects them for batches with an
  image tip); it computes only the red and alpha channels in 32- and 64-bit
  integers (the unpremultiply divides in uint64).
- Memory and transfer: each pyramid level keeps its premultiplied copy
  (8 bytes per texel, besides the 4-byte ARGB32 level), and every GPU batch
  uploads the levels its dabs use. A level larger than the 64 MiB staging
  budget makes the batch fall back to materialized CPU pixels.
- Tests: `KisQImagePyramidDescribeTest` (accuracy against QPainter, the
  described image equals `createImage()`, a benchmark),
  `KisGpuBrushTest::testGeneratedImageTipDabs` (identity, scaled, rotated and
  out-of-level rows; GPU alpha equals the CPU alpha bit for bit, and
  `render()` equals `alphaAt()`), `KisGpuStrokeTest::testImageTipStroke`
  (a GBR tip at 48 and 160px, rotation, Wash and Buildup, mirrors, a
  textured row, scale 1; layer and projection against the CPU path,
  generated and skipped counters).
- Applications (phase 4.104, 2026-10-11, user request): the image stamp,
  Lightness map and Gradient map tips are described as well
  (`KisProceduralCircleDab::imageApplication`). Each output channel depends
  on a few 8-bit channels of the sampled tip pixel only, so
  `KisBrush::proceduralImageDab()` captures the CPU's per-pixel work in a
  table made with the CPU code itself, and the GPU looks it up:
  - Lightness map: `fillGrayBrushWithColorAndLightnessWithStrength()` on
    256 pixels `qRgba(v, 0, 0, v)` (the color depends on red, the alpha on
    alpha); made per dab (paint color, strength).
  - Gradient map: the 256 colors `cachedAt(qGray / 255.0)` and the 256
    doubles `alpha / 255.0`; the alpha is
    `float(double(gradient alpha) * maskOpacity)` (a double product on the
    GPU, correctly rounded). Upstream change (`kis_brush.cpp`, gradient
    branch of `generateMaskAndApplyMaskOrCreateDab()`): fully transparent
    tip pixels now take their own gradient color instead of the previous
    pixel's, so that a pixel depends on its tip pixel only (their alpha
    stays 0; invisible).
  - Image stamp (`paintDevice()`, `KisFixedPaintDevice::convertFromQImage()`
    from 8-bit sRGB): the conversion per channel, from a gray ramp, used
    only if the color space's conversion is per channel (checked once per
    color space on a 16-level grid with four alphas and 65536 random
    pixels; true for the default float profiles); otherwise the dabs keep
    their pixels.
  - Verification and skipping (phase 4.85) count each application
    separately (kind index 3 + application).
  - GPU: `Circle.imageApplication` and `Circle.imageTable` (in the former
    padding, still 224 bytes); the shader samples all four channels
    (`imageTipPixel()`) and applies the table (`imageTipValue()`); the alpha
    mask keeps its own path (`imageTipAlpha()`; the unused channels are dead
    code).
  - Tests: `KisImageTipDescriptionTest` (every application, color and gray
    tips, 60 shapes: the description renders exactly the CPU dab of
    `mask()` or `paintDevice()`), `KisGpuBrushTest::
    testGeneratedImageTipDabs` (every application on every geometry, all
    channels bit for bit), `KisGpuStrokeTest::testImageTipStroke` (stamp and
    Lightness rows; the Gradient map needs the canvas gradient, which the
    stroke harness does not provide).
  - Mirrors: dabs of different colors do not commute, and the CPU and GPU
    paths order the reflections of a batch differently, so mirrored colour
    strokes may differ slightly between the paths where dabs overlap
    (unchanged: colour dabs were already painted by the GPU brush as
    uploaded pixels).
- Not covered: image pipe (.gih) and text brushes, image stamps in
  color spaces whose conversion is not per channel, Gradient maps without a
  gradient, and dabs with other postprocessing (sharpness,
  Lightness/Gradient textures) keep their pixels.

### RGBA16F image tips and textures (phase 4.105)

2026-10-11, user request. Image tip dabs (every application) and textured
dabs are described on RGBA16F layers too; before, only their RGBA32F dabs
were (phases 4.102-4.104), and RGBA16F dabs with an image tip or a texture
kept their pixels.

- Image tips: `KisBrush::proceduralImageDab()` accepts 8-byte RGBA F16 color
  spaces; the tables are made with the same CPU calls in the F16 color space
  and hold exact half values as floats (`channelValue()`). The alpha mask
  uses `KoColorSpaceMaths<quint8, half>` (`imageTipHalfAlpha()`; on the GPU
  `uint8ToHalfTable()` as the 256-entry table). The Gradient map's opacity
  goes through `KoColor::setOpacity()`: double -> float -> half
  (`roundToHalf(float(alpha))` in the shader). `render()` and
  `matchesPixel()` write and compare halves.
- Textures: `KisTextureOption::gpuDescription()` accepts RGBA F16 dabs and
  computes the constants with the half instantiation
  (`StrengthCompositeFunctionBase<half>`, `Arithmetic::inv<half>`, the
  double weight, exact in float). The texels go through
  `KoColorSpaceMaths<quint8, half>` (the same 256-entry table). The shader's
  `texturedAlphaHalf()` repeats `KisMaskingBrushCompositeOp<half, ...>`:
  `KoColorSpaceMathsTraits<half>::compositetype` is double, and Imath's
  `half` has no arithmetic operators, so `half +- half` is computed in float
  (`inv`, the soft Linear Burn and Linear Dodge sums), `mul`, `div` and
  `unionShapeOpacity` in double, and every value returned as a half rounds
  double -> float -> half (`hd()`); the fuzzy checks are the half
  specializations (0.002 and 0.001 thresholds).
- Shaders: `paint_dabs_textured_rgba16f` and
  `paint_dabs_textured_extended_rgba16f` (`TILE_F16` + `DAB_TEXTURE`); the
  compositor uses them for RGBA16F batches with an image tip or a texture.
  The existing RGBA16F variants are unchanged.
- Tests: `KisImageTipDescriptionTest` (RGBA16F rows: the description is
  the CPU dab, byte for byte), `KisGpuBrushTest::testGeneratedImageTipDabs`
  (RGBA16F runs: all channels as halves), `KisGpuStrokeTest::
  testHalfTextureModesExact` (14 modes x hard/soft x 3 strengths, the GPU
  alpha equals `KisMaskingBrushCompositeOp<half>` on every dab alpha and
  texel bit for bit), and RGBA16F rows of `testImageTipStroke` and
  `testTextureModes`.
- Still on the CPU: Lightness and Gradient textures, sharpness. RGBA16F
  masking composite is supported; see "Masking brush".

### Remaining blend modes (phase 4.107)

2026-10-12, user request ("6", priority 5 of `gpu-work-priorities.md`).

- RGBA16F brushes: `kisGpuSupportsHalfBrushBlend()` accepts every
  `KisGpuBlendOp`, and `KisGpuDabCompositor::record()` no longer refuses F16
  modes, so Soft Light (Photoshop), Divide, Vivid Light, Hard Mix (all
  three), Grain Merge/Extract, Negation, Allanon, Darker/Lighter Color and
  the new modes below use the GPU brush on RGBA16F too.
- New modes (`KisGpuBlendOp` 34-62, dab modes +2): HSY Tint, Increase/
  Decrease Saturation and Luminosity; HSI, HSL and HSV Color, Hue,
  Saturation, Increase/Decrease Saturation, Intensity/Lightness/Value and
  Increase/Decrease of it. `composite_blend.glsl` decodes HSI/HSL/HSV as
  `OpColorHsi + 8 * family + function` (`blendHSY()`), and one generic
  implementation (`hsxLightness`, `hsxSaturation`, `hsxSetSaturation` with
  the CPU's sort, `hsxToneMapping` with HSV's per-channel fallback) now
  serves the HSY modes too.
- Arithmetic, following `KoColorSpaceMathsTraits<T>::compositetype`
  (float for float, double for half):
  - `exactBlend()` modes (Color Burn, Vivid Light, Hard Mix Softer, every
    HSX mode) use `precise` alpha, weighted colour and interpolation and an
    IEEE division (`exactQuotient()`);
  - RGBA16F: `inv()` results round to half before Vivid Light's and Hard
    Mix Softer's arithmetic; Vivid Light's `1 - a / b` is rounded once
    (`oneMinusQuotient()`); the weighted colour of `exactBlend()` modes
    rounds each `mul(a, b, c)` once (`mul3()`);
  - `roundedProduct()` for every product that feeds an addition in these
    paths: the NVIDIA driver fused `l + t * s` into an fma despite
    `NoContraction`, which changed HSL results at rounding ties
    ([wiki](wiki/concepts/cpu-gpu-bit-parity.md), techniques 9-11).
- Fixed on the way (already on the GPU, not covered before): RGBA32F
  brushes in Vivid Light, Hard Mix Softer and Lighter Color differed from
  the CPU over repeated dabs (up to 4e-5, 0.009 for Lighter Color).
- Tests: the brush lists of `KisGpuBrushTest` (F32 matrices with
  selections, channel flags, mirrors; `testHalfDabs`; `testHalfWashBlendModes`),
  `KisGpuStrokeTest::testBlendModes`/`testHalfBlendModes` and the two lists
  of `KisGpuProjectionTest` include every mode; the existing tolerances
  (2e-5 for F32, 1/1024 for F16 dabs) hold. Darker Color with alpha locked
  leaves the stroke test's darker background unchanged on every path.
- Current exclusions are listed in "All other blend modes (phase 4.108)".

### All other blend modes (phase 4.108)

2026-10-13, user request ("4 のブレンドモード変更はすべてまとめて").

- New `KisGpuBlendOp` 63-116 (dab modes +2), mapped in
  `KisGpuProjectionCompositor::blendOpForCompositeOp()`:
  - separable (`blendChannelExtra()`, clamp policies in
    `extraClampsSource/Destination/Bottom()`): the HDR variants of Hard Mix,
    Color Dodge and Vivid Light, Hard Overlay (and HDR), Geometric Mean,
    Parallel, Interpolation (and 2x), Penumbra A-D, Soft Light (IFS
    Illusions, Pegtop-Delphi), Gamma Light/Dark/Illumination, Flat Light,
    P-Norm A/B, Super Light, Tint/Shade/Fog Lighten/Fog Darken (IFS
    Illusions), Easy Dodge/Burn, Inverse Subtract, Modulo, Modulo Shift (and
    Continuous), Arc Tangent, Equivalence, Additive-Subtractive, Reflect,
    Glow, Freeze, Heat, Glow-Heat, Heat-Glow, Reflect-Freeze,
    Freeze-Reflect, Heat-Glow-Freeze-Reflect;
  - HSL functors (`blendHslExtra()`, through `compositeHSY()`): Lambert
    Lighting (and Gamma 2.2), Tangent Normal Map, Combine Normal;
  - own composite functions (`compositeSpecial()`): Luminosity SAI
    (`KoCompositeOpGenericSCAlpha`, no normalization), Behind, Greater,
    Copy Red/Green/Blue.
- Kept on the CPU: Dissolve (random), Marker (needs Alpha Darken's flow
  parameters), Divisive Modulo, Divisive Modulo Continuous and Modulo
  Continuous (their quotient's fractional part, and the source-0 branch of
  `KoColorSpaceMaths<double>::modulus()` at 2^52 scale, turn any one-ulp
  difference into an O(1) error), and Destination In/Atop: they change pixels
  where the source has no tiles (`KisLayerProjectionPlane` applies them
  beyond the layer's extent, and a Wash merge beyond the stroke), which the
  tile-based GPU compositor skips. Logical modes exist only for integer
  spaces.
- CPU semantics reproduced:
  - `clamp<T>()` is the identity for float and half (`KoColorSpaceMaths`
    specializations); `kisBoundFast()` gives the lower bound for NaN
    (`kbound()`); `T::max` is FLT_MAX or HALF_MAX (`FltMax`), epsilon
    FLT_EPSILON or HALF_EPSILON;
  - std::pow() edge cases (`cpow()`: negative bases, zero) and accurate
    pow (`accurateLog()` with an atanh series and split ln2,
    `accurateExp()` with range reduction): GLSL `log2()` has an absolute
    error that ruined large exponents near 1;
  - double modulus near integers (`modOne()` on an exact hi + lo sum);
  - RGBA16F: Arithmetic results round to half at every step where the CPU
    returns a T (`tv()`), so half overflows to infinity as on the CPU;
  - infinities survive `roundedProduct()`, `mul3()` and `exactQuotient()`.
- Accuracy: modes whose CPU functions compute pow/exp/cos/atan2/sqrt in
  double are tolerance-based (`modeTolerance()` in `KisGpuBrushTest`: 1e-3
  F32, 4 half ulps; Greater and Easy Burn 5e-3, they amplify). Glow, Heat
  and their combinations divide by 1 - dst: on exact inputs
  (`testExtendedBlendBoundaries`) they hold 2e-5, after other layers
  (`testBlendModesMatchCpu`) 1e-2. F32 comparisons are relative above 1.
- Tests: the mode lists of `KisGpuBrushTest`, `KisGpuStrokeTest` and
  `KisGpuProjectionTest` include the new modes; NaN and equal infinities
  compare equal (`sameNonFinite()`, bitwise `PixelValues`), since several
  modes give NaN for negative HDR channels on the CPU too; boundary failures
  print the source, both destinations and both results.

### Precise contraction audit (phase 4.109)

2026-10-13, user request (task 1 after phase 4.108).

- `KisGpuEngineTest::testPreciseContraction` (`libs/gpu/tests/
  precise_probe.comp`, built optimized and not, float, double and with the
  float16 extensions) runs `a + b * c` and longer shapes on triples whose
  fused and unfused results differ: on driver 596.86 every precise form
  keeps the unfused result; it records the counts per driver (qInfo) and
  checks `roundedProduct()` and an explicit fma.
- `KisGpuEngineTest::testBlendProbe` (`blend_probe.comp`, the real
  `composite_blend.glsl`) reproduces phase 4.108's case: with the plain
  precise multiply-add in `hsxToneMapping()` the Increase Saturation (HSL)
  green came out one float above the CPU's tie (0.980712890625), with every
  input bit-identical and NoContraction on every operation; an inline copy
  of the same code was exact. The test now checks the value bit for bit.
- Changed to `roundedProduct()`: the half lerps of
  `composite_half_brush.glsl` (`halfLerp()`, `overHalf()`), the RGBA16F
  Alpha Darken colour of `paint_dabs.comp`, and in `grid_warp.comp`
  (`roundedProductD()`) the quadratic of `backwardMap()`, the mapped
  coordinates, the fallback distance and the integer weights (truncated
  after + 0.5, where one bit can change a weight). `transform_pass.comp`
  has no precise product feeding a sum; `separable_convolution.comp` is
  tolerance-based; `masking_composite.comp` rounds double results to half.
- Not changed: precise products that feed a multiplication, a division or
  a comparison only, and explicit `fma()` calls that reproduce the CPU's
  AVX2 kernels (`softFade()`).

### Clone Layer offsets (phase 4.110)

2026-10-16, user request (after measuring Clone Layer costs).

- An offset Clone Layer without effect masks copies its source on the GPU:
  `KisCloneLayer::copyOriginalToProjection()` →
  `KisGpuProjectionCompositor::copyShifted()` → `KisGpuShiftedCopyPass`
  (`shaders/copy_shifted.comp`), any pixel shift, the source's default
  pixel where it has no tile, pixels unconverted. Before, the CPU copy
  downloaded the GPU-written source tiles on every update.
- Conditions, parity (alpha-0 colors), the reason masked clones stay on
  the CPU, tests and the remaining candidates: `docs/agent/clone-layer.md`.
  Measurements: [Clone Layer updates](wiki/benchmarks/clone-layer-updates.md).
- Found while measuring, not changed: `KisAsyncMerger::setupProjection()`
  clears the dirty rect on the CPU, which downloads the partly covered
  border tiles of GPU-resident projections for rects that are not
  tile-aligned (all GPU merges).

## Technology and device requirements

| Topic | Decision | Reason |
| --- | --- | --- |
| API level | Vulkan 1.3 core: synchronization2, timeline semaphores, buffer device address, shaderInt64, shaderFloat16, 16-bit storage. | All present on Blackwell (driver 596.86 reports Vulkan 1.4.329). |
| Loader | `vulkan-1.dll` loaded at runtime (`KisGpuVulkanFunctions`, X-macro tables). No link-time dependency. | Krita still starts without a Vulkan driver. The SDK has no volk. |
| Memory | One dedicated `VkDeviceMemory` per buffer; tiles sub-allocated from 256 MiB chunks (4096 RGBA32F tiles). No VMA. | Tiles are fixed-size, so a slab allocator is trivial; the SDK does not ship VMA. |
| Tile storage | Storage buffers addressed by 64-bit device address (`GL_EXT_buffer_reference`), not images. | Any number of tiles per dispatch through address tables; no descriptor sets, layouts, or image transitions; sub-allocation is trivial. |
| Shader input | Push constants only (addresses + counts). | No descriptor management in the engine. |
| Shaders | GLSL compute, compiled at build time by the SDK's `glslc` (`--target-env=vulkan1.3 -O -mfmt=num`), included as word arrays (`kis_gpu_add_shader()` in `libs/gpu/CMakeLists.txt`). | No runtime compiler dependency; build fails on shader errors. |
| Precision | RGBA F32/F16 storage; arithmetic and rounding follow each CPU operation, including double and intermediate half rounding where required. | Preserve operation-specific parity; F16 halves tile storage. |
| Display | GL/Vulkan shared-buffer interop with exported memory, binary semaphores and device UUID check. | Retain the existing OpenGL canvas, OCIO and decorations; shared-image interop is not the production upload path. |
| Device choice | Highest score: discrete GPU, then NVIDIA vendor; requires a graphics+compute queue with timestamps. `KisGpuDeviceInfo::isBlackwell()` (NVIDIA device id ≥ 0x2B00) is informational and logs a warning when false. | Blackwell-only target without hard-failing development on other GPUs. |
| Validation | `KRITA_GPU_VALIDATION=1` enables `VK_LAYER_KHRONOS_validation` plus a debug messenger that counts errors (`validationErrorCount()`). | Tests assert zero validation errors. |

### Numerical parity

The color of a pixel whose alpha is exactly 0 is **unspecified** in Krita:
`KoOptimizedCompositeOpOver128` copies the source color into lanes that stay
transparent, while the scalar paths keep the old color. GPU parity tests
may compare such pixels by alpha only. The initial 1e-5 F32 compositing rule
is not a universal tolerance: current projection, brush and extended-mode
suites define their own thresholds above. Transform and generated-dab tests
include bit-exact checks; FFT-vs-spatial Gaussian convolution uses a tolerance.
See [parity techniques](wiki/concepts/cpu-gpu-bit-parity.md).

## Transforms and Gaussian convolution

These implemented paths are distinct from merely batching CPU readback.
They require supported RGBA F32/F16 tiles, a healthy backend, `shaderFloat64`,
LOD 0 and no wrap-around; refusing a path retains CPU processing.

- Affine: `KisTransformWorker` plans the two resampling passes;
  `KisGpuTransformWorker::runPlannedPasses()` uses a temporary device for the
  first pass and writes the final union for the second, then batches CPU sync
  before CPU default-tile purging. Fixed-point weights and CPU rounding are
  reproduced bit for bit. Reduced editing previews remain CPU.
- Liquify and mesh Puppet Warp: `KisGpuGridWarpWorker` records CPU polygon/copy
  operations and uses ordered ownership (`atomicMax`) before rendering.
  Source/destination must differ but match color space, default pixel and
  offsets. The sampled warp is bit-exact; Puppet Warp's multi-group compositing
  remains on the CPU and can vary by a few ulps. Preserve the separate
  [Puppet Warp contract](puppet-warp.md), including preview/final consistency
  and the unresolved deeply folded-joint limitation. Legacy MLS stays CPU.
- Gaussian: `KisGaussianKernel::applyGaussian()` calls
  `KisGpuConvolutionWorker::applySeparable()`. The worker reads a source
  snapshot, computes horizontal/vertical passes in double into temporary
  storage, then copies back. Eligibility includes border/channel handling
  and a minimum source read area (about 180 x 180 pixels); small regions remain
  CPU. `KisFilter::prefersSingleCall()` avoids splitting suitable large filter
  applications. Gaussian Blur/High Pass and the blur part of Unsharp Mask
  benefit; sharpening itself remains CPU.
- Parity/Undo tests and dated acceptance are in
  [the transform/filter phase record](wiki/history/gpu-phases-4.93-.md).
  Affine/Liquify use exact comparisons; Gaussian's approved tolerances are
  relative 1e-5 F32 and 1e-3 F16 against the FFT CPU path. A measured one-step
  difference is a result for that workload, not a universal guarantee.

## Historical plans and measurements

The [development snapshot](wiki/history/gpu-engine-development.md) preserves
the complete phase 0-3 measurement tables and original phase plan.
[GPU work priorities](gpu-work-priorities.md) owns remaining work.
For trace procedures and run provenance use
[paint trace protocol](wiki/history/gpu-phases-4.58-4.81.md) and [baseline runs](paint-trace-baseline-runs.md);
the [user guide](../gpu-engine.md#current-benchmarks) holds representative
published results. Historical queued-stroke timings exclude input/display and
must not be interpreted as current input latency.

## Build, test, and install

`kritagpu` is built only when CMake finds the Vulkan headers and `glslc`
(the private SDK is passed as described in `docs/agent/vision-ml.md`).

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritagpu KisGpuEngineTest KisGpuGLInteropTest -j 8"
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && ctest --test-dir <krita-dev-root>\_build -R libs-gpu --output-on-failure"
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\libs\gpu\cmake_install.cmake
```

- Historical test observations from the development snapshot (identical with and without
  `KRITA_GPU_PROJECTION=1`; not re-checked against a pre-GPU build, and not
  re-run during this extraction; not a standing waiver for new failures):
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
- Conversion UI: `KisGpuEngineUiTest` checks preference round trips (including
  legacy numeric policies), Ask choices with/without remembering, Escape,
  automatic conversion, profile retention, and Undo/Redo. It runs offscreen
  with a usable GPU and creates no canvases. `KisGpuProjectionTest::
  testConvertedDocumentUsesGpu` covers both F16 and F32 conversion targets,
  including Lab input and the GPU projection after converting an RGB8 image.
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
| `KisGpuSeparableConvolutionPass.*` + `shaders/separable_convolution.comp` | Phase-4.98 separable convolution in bands: horizontal pass into a double intermediate, vertical pass with the FFT worker's write rules. |
| `KisGpuShiftedCopyPass.*` + `shaders/copy_shifted.comp` | Phase-4.110 copy between tile grids with any pixel shift (Clone Layer offsets); the default pixel where the source has no tile. |
| `KisGpuMaskingCompositePass.*` + `shaders/masking_composite.comp` | Phase-4.101 masking brush composite: stroke pixels into the target with the mask composited into alpha, ten modes, CPU-exact. |

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
| `gpu/KisGpuProjectionCompositor.*` | Composites batches in place into GPU-authoritative projection tiles; layer accesses and reusable work contexts. |
| `kis_clone_layer.cpp` | Phase 4.110: `copyOriginalToProjection()` tries `copyShifted()` for clones without effect masks. |
| `kis_async_merger.{h,cpp}` | `m_gpuBatch` member, `tryAdd` in `compositeWithProjection`, flush points. |
| `kis_updater_context.cpp` | Tile-aligned job exclusivity when `KisGpuMergeBatch::mayCompositeOnGpu`. |
| `gpu/KisGpuConvolutionWorker.*` | Phase-4.98 Gaussian convolution: old-data snapshot, one GPU submission into a temporary device, CPU copy into the rect. Called by `KisGaussianKernel::applyGaussian()`. |
| `gpu/KisGpuMaskingWorker.*` | Phase-4.101 masking brush composite for `KisPainterBasedStrokeStrategy::doMaskingBrushUpdates()`; `supports()` is also used by `KisBrushOp`. |

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

- kritarc (default group): `Solstice/GpuEngine` (bool; the embedded default
  `krita/data/kritarc` sets true since 2026-10-07, the code fallback is
  false) and
  `Solstice/GpuEngineConvertDocuments` (int: 0 Ask, 1 Convert to F32, 2 Keep,
  3 Convert to F16; values 0/1/2 retained for compatibility, 2026-10-15;
  default 0). See "Feature gate and user interface".
- Environment `KRITA_GPU_PROJECTION=1` or `0` overrides `Solstice/GpuEngine`
  (development switch; `KisGpuMergeBatch::setEnabled()` in tests).
- Device selection refuses `VK_PHYSICAL_DEVICE_TYPE_CPU` devices (software
  Vulkan such as lavapipe or SwiftShader, typical in virtual machines; they
  are slower than the CPU paths), 2026-10-11 at the user's request:
  `KisGpuContext::acceptsDeviceType()`, rejection reason "software (CPU)
  device" in the "GPU engine unavailable" log line. `KRITA_GPU_ALLOW_SOFTWARE_DEVICE=1`
  accepts them (diagnostics). Integrated and virtual GPUs are still
  accepted when they meet the feature requirements (discrete GPUs are
  preferred). Test: `KisGpuEngineTest::testSoftwareDevicesRefused`.
- With the engine on, `KRITA_GPU_TRANSFORM=0`, `KRITA_GPU_LIQUIFY=0`,
  `KRITA_GPU_PUPPET=0` and `KRITA_GPU_CONVOLUTION=0` keep the affine passes,
  Liquify (and with it Puppet Warp, which shares the grid warp), Puppet Warp
  alone and the Gaussian convolution on the CPU (development switches).
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
- The GPU Gaussian convolution reads a snapshot of the device's old data and
  writes the device only through a CPU copy of its own rect. Concurrent
  filter-stroke patches rely on this; do not make it write the device's
  tiles from the GPU directly.
- `KisFilter` has the Solstice virtual `prefersSingleCall()`. Changing
  `KisFilter`'s virtual functions requires rebuilding and installing every
  filter plugin; tests load the installed plugins.
- GL objects of a shared image are created and destroyed with its GL context
  current; the image stays in `VK_IMAGE_LAYOUT_GENERAL`.

## Manual regression checklist

Run relevant checks after rebuilding/installing changed components and restarting.
Record the build, format, workload and result separately from earlier acceptance.

1. Open, display, zoom, rotate, and mirror a multi-layer document in GPU mode.
2. Paint, undo, redo; compare with the CPU mode result.
3. Save, reopen in CPU mode and in upstream Krita.
4. Enable Preferences → Performance → GPU Engine, restart: the status shows
   the active GPU after opening an RGBA float document.
5. Open an RGBA 8-bit PNG: the conversion question offers F16 and F32;
   each produces the selected depth, and undo restores 8-bit; Keep leaves it on the CPU path;
   "Do not ask again" is honored on the next open and changeable in
   Preferences, including the chosen depth. Existing RGBA F16/F32 documents
   keep their depth regardless of this setting.
6. Disable the setting and restart: no question, no GPU use
   (`KRITA_GPU_PROJECTION=0` likewise).
7. Gaussian Blur, Unsharp Mask and Gaussian High Pass on RGBA32F and RGBA16F
   layers (phase 4.98): apply time and result, with a selection, Undo/Redo,
   the dialog preview, and a filter mask while painting below it.
8. Puppet Warp apply on RGBA32F and RGBA16F layers (phase 4.96): one pin,
   several pins with a turned joint and pin orders, Undo/Redo; the result
   matches the preview.
9. Masking brush on the GPU brush (phase 4.101) on an RGBA32F layer: a
   textured masked preset (e.g. `z) 线稿 纹理`) with both mirrors at about
   40px and at a large size, a masked preset in Wash and Buildup, another
   masking mode (e.g. Darken or Subtract), Undo/Redo; the stroke looks like
   the CPU brush (GPU brush off) and does not lag behind it.
10. Textured dabs on the GPU brush (phase 4.102) on an RGBA32F layer: a
    textured round brush (Multiply, then Subtract and a Height mode, hard and
    soft texturing, strength on pressure) at small and large sizes, with
    mirrors, in Wash and Buildup; the texture lines up between dabs and with
    the CPU brush (GPU brush off); Undo/Redo; a Lightness-mode texture
    still paints (CPU).
11. Image tip dabs on the GPU brush (phase 4.103) on an RGBA32F layer: a
    predefined GBR or PNG tip and an imported ABR tip at small and large
    sizes, with rotation (fixed angle and Drawing Angle), mirrors, a texture,
    Wash and Buildup; the stroke matches the CPU brush (GPU brush off) and
    the brush outline; Undo/Redo. Phase 4.104: a colour tip as Image
    Stamp, as Lightness Map (change the color and the Lightness strength)
    and as Gradient Map (change the gradient), with rotation, a texture,
    Wash and Buildup; each looks like the CPU brush. On an 8-bit layer image
    tips look as before.
12. RGBA16F image tips and textures (phase 4.105) on an RGBA 16-bit float
    layer: a gray tip, a colour tip as Image Stamp and Lightness Map, a
    textured round brush (Multiply, Overlay, a Height mode, hard and soft)
    and a textured image tip, Wash and Buildup; each looks like the CPU
    brush (GPU brush off); Undo/Redo.
13. RGBA16F masked strokes (phase 4.106) on an RGBA 16-bit float layer: a
    masked preset (e.g. `z) 线稿 纹理`) at small and large sizes, another
    masking mode (Darken or Subtract), with a texture, Wash and Buildup;
    the stroke looks like the CPU brush and does not lag; Undo/Redo.
14. Blend modes (phase 4.107): on an RGBA 16-bit float layer, a brush in
    Soft Light (Photoshop), Vivid Light, Hard Mix (Softer Photoshop) and
    Lighter Color; on both float formats, HSV Hue, HSL Saturation, HSI
    Intensity, Tint and Decrease Value, as brush modes (Buildup, Wash,
    mirrors, alpha lock) and as layer blend modes; each looks like the CPU
    (GPU brush off, or GPU Engine off); Undo/Redo.
15. Other blend modes (phase 4.108), on RGBA 32-bit and 16-bit float layers,
    as layer modes and brush modes (Buildup, Wash): Glow, Heat, Gamma Light,
    Hard Overlay, Penumbra A, Easy Dodge, Modulo Shift, Lambert Lighting,
    Behind, Greater, Luminosity SAI and Copy Red; each looks like the CPU;
    Destination In still works (CPU); Undo/Redo.
16. Outstanding failure UI acceptance: with a reproducible test fault, confirm
    that content loss produces one non-modal warning and stops GPU use. Check
    Save/Save As/export/save-on-close choices (Save as New File default,
    Save Anyway, Cancel), a later loss during export preserving the existing
    target, and the documented autosave/`supportsIO() == false` exclusions.
    Do not induce a real driver reset or use valuable documents to exercise
    this check. Record acceptance separately from `KisGpuSaveTest`.
17. Clone Layer offsets (phase 4.110): the
    [Clone Layer manual checks](clone-layer.md#manual-checks).

## Phase history

The review notes and phase records (phases 4.1 and later) are in the wiki history pages,
moved verbatim on 2026-10-07. Add new phase records to the newest page, and summaries of
cross-cutting findings to `docs/agent/wiki/` (see `wiki/index.md`).

- [Reviews and memory budgets](wiki/history/gpu-reviews-and-memory.md): Canvas interop investigation, earlier phase reviews, tile memory budget and eviction, canvas transfer buffers.
- [Pixel brush and Wash (phases 4.1-4.16)](wiki/history/gpu-phases-4.01-4.16.md): GPU brush prototype, selections, channel locks, mirrors, Wash preview and final merge.
- [Blend modes, RGBA16F and transfers (phases 4.17-4.57)](wiki/history/gpu-phases-4.17-4.57.md): Blend-mode coverage, asynchronous submissions, RGBA16F brushes, context reuse, batched readbacks.
- [Paint trace and latency analysis (phases 4.58-4.81)](wiki/history/gpu-phases-4.58-4.81.md): Paint pipeline trace, capture protocol, real-app stage and lock analysis, shared canvas builds.
- [Generated dabs and brush latency (phases 4.82-4.92)](wiki/history/gpu-phases-4.82-4.92.md): GPU circle dabs, update period, Wash latency, shared pipelines, immediate canvas uploads.
- [Filters and transforms (phases 4.93-)](wiki/history/gpu-phases-4.93-.md): CPU filter/transform baseline, GPU affine passes, GPU Liquify grid warp, GPU Gaussian blur, GPU Puppet Warp rendering.

Section index:

- [Canvas interop investigation (2026-10-03)](wiki/history/gpu-reviews-and-memory.md#canvas-interop-investigation-2026-10-03)
- [Earlier phase reviews](wiki/history/gpu-reviews-and-memory.md#earlier-phase-reviews)
- [Tile memory budget and eviction](wiki/history/gpu-reviews-and-memory.md#tile-memory-budget-and-eviction)
- [Canvas transfer buffer budget and retirement](wiki/history/gpu-reviews-and-memory.md#canvas-transfer-buffer-budget-and-retirement)
- [Pixel brush prototype (phase 4.1)](wiki/history/gpu-phases-4.01-4.16.md#pixel-brush-prototype-phase-41)
- [Selection coverage for pixel brushes (phase 4.2)](wiki/history/gpu-phases-4.01-4.16.md#selection-coverage-for-pixel-brushes-phase-42)
- [Normal brush channel locks (phase 4.3)](wiki/history/gpu-phases-4.01-4.16.md#normal-brush-channel-locks-phase-43)
- [Mirrored pixel-brush compositing (phase 4.4)](wiki/history/gpu-phases-4.01-4.16.md#mirrored-pixel-brush-compositing-phase-44)
- [Combined mirror submissions (phase 4.5)](wiki/history/gpu-phases-4.01-4.16.md#combined-mirror-submissions-phase-45)
- [Brush job integration coverage (phase 4.6)](wiki/history/gpu-phases-4.01-4.16.md#brush-job-integration-coverage-phase-46)
- [GPU clipping to CPU paint rectangles (phase 4.7)](wiki/history/gpu-phases-4.01-4.16.md#gpu-clipping-to-cpu-paint-rectangles-phase-47)
- [Queued stroke measurements and Wash readback batching (phase 4.8)](wiki/history/gpu-phases-4.01-4.16.md#queued-stroke-measurements-and-wash-readback-batching-phase-48)
- [GPU Wash preview (phase 4.9)](wiki/history/gpu-phases-4.01-4.16.md#gpu-wash-preview-phase-49)
- [Sparse mirror destinations (phase 4.10)](wiki/history/gpu-phases-4.01-4.16.md#sparse-mirror-destinations-phase-410)
- [Post-stroke speculative CPU clones (phase 4.11)](wiki/history/gpu-phases-4.01-4.16.md#post-stroke-speculative-cpu-clones-phase-411)
- [Byte-bounded brush updates (phase 4.12)](wiki/history/gpu-phases-4.01-4.16.md#byte-bounded-brush-updates-phase-412)
- [Erase dab compositing (phase 4.13)](wiki/history/gpu-phases-4.01-4.16.md#erase-dab-compositing-phase-413)
- [Wash Erase GPU preview (phase 4.14)](wiki/history/gpu-phases-4.01-4.16.md#wash-erase-gpu-preview-phase-414)
- [GPU final Wash merge (phase 4.15)](wiki/history/gpu-phases-4.01-4.16.md#gpu-final-wash-merge-phase-415)
- [Selected Wash compositing (phase 4.16)](wiki/history/gpu-phases-4.01-4.16.md#selected-wash-compositing-phase-416)
- [Channel-locked Wash and Erase bundle (phases 4.17-4.19)](wiki/history/gpu-phases-4.17-4.57.md#channel-locked-wash-and-erase-bundle-phases-417-419)
- [Separable brush blend bundle (phases 4.20-4.22)](wiki/history/gpu-phases-4.17-4.57.md#separable-brush-blend-bundle-phases-420-422)
- [Bounded asynchronous brush submissions (phases 4.23-4.25)](wiki/history/gpu-phases-4.17-4.57.md#bounded-asynchronous-brush-submissions-phases-423-425)
- [Extended blend coverage (phases 4.26-4.28)](wiki/history/gpu-phases-4.17-4.57.md#extended-blend-coverage-phases-426-428)
- [Blend-family bundle (phases 4.29-4.31)](wiki/history/gpu-phases-4.17-4.57.md#blend-family-bundle-phases-429-431)
- [Bulk readback and projection measurements (phases 4.32-4.33)](wiki/history/gpu-phases-4.17-4.57.md#bulk-readback-and-projection-measurements-phases-432-433)
- [Textured and masked brush catch-up (phases 4.34-4.35)](wiki/history/gpu-phases-4.17-4.57.md#textured-and-masked-brush-catch-up-phases-434-435)
- [Layer channel flags and F16 blend arithmetic (phases 4.36-4.37)](wiki/history/gpu-phases-4.17-4.57.md#layer-channel-flags-and-f16-blend-arithmetic-phases-436-437)
- [CPU-to-GPU transition preparation (phases 4.38-4.40)](wiki/history/gpu-phases-4.17-4.57.md#cpu-to-gpu-transition-preparation-phases-438-440)
- [Batched voluntary tile eviction (phase 4.41)](wiki/history/gpu-phases-4.17-4.57.md#batched-voluntary-tile-eviction-phase-441)
- [Current-build benchmark baseline (phase 4.42)](wiki/history/gpu-phases-4.17-4.57.md#current-build-benchmark-baseline-phase-442)
- [Ordinary-stroke tile costs (phases 4.43-4.45)](wiki/history/gpu-phases-4.17-4.57.md#ordinary-stroke-tile-costs-phases-443-445)
- [RGBA16F Normal/Erase dabs (phase 4.46)](wiki/history/gpu-phases-4.17-4.57.md#rgba16f-normalerase-dabs-phase-446)
- [RGBA16F Alpha Darken and Wash (phases 4.47-4.48)](wiki/history/gpu-phases-4.17-4.57.md#rgba16f-alpha-darken-and-wash-phases-447-448)
- [RGBA16F basic blend brushes (phases 4.49-4.50)](wiki/history/gpu-phases-4.17-4.57.md#rgba16f-basic-blend-brushes-phases-449-450)
- [RGBA16F extended major blend brushes (phases 4.51-4.52)](wiki/history/gpu-phases-4.17-4.57.md#rgba16f-extended-major-blend-brushes-phases-451-452)
- [Projection context reuse (phase 4.53)](wiki/history/gpu-phases-4.17-4.57.md#projection-context-reuse-phase-453)
- [Batched CPU filter and FFT readback (phases 4.54-4.55)](wiki/history/gpu-phases-4.17-4.57.md#batched-cpu-filter-and-fft-readback-phases-454-455)
- [Batched affine transform and layer-flip readback (phases 4.56-4.57)](wiki/history/gpu-phases-4.17-4.57.md#batched-affine-transform-and-layer-flip-readback-phases-456-457)
- [Closed Transform Tool Undo investigation (October 4, 2026)](wiki/history/gpu-phases-4.17-4.57.md#closed-transform-tool-undo-investigation-october-4-2026)
- [Paint pipeline trace foundation (phase 4.58)](wiki/history/gpu-phases-4.58-4.81.md#paint-pipeline-trace-foundation-phase-458)
- [Paint trace identity and frame coverage (phase 4.59)](wiki/history/gpu-phases-4.58-4.81.md#paint-trace-identity-and-frame-coverage-phase-459)
- [Scheduled job trace lineage (phase 4.60)](wiki/history/gpu-phases-4.58-4.81.md#scheduled-job-trace-lineage-phase-460)
- [Logical dab and paint-batch trace membership (phase 4.61)](wiki/history/gpu-phases-4.58-4.81.md#logical-dab-and-paint-batch-trace-membership-phase-461)
- [Dirty groups and projection lineage (phase 4.62)](wiki/history/gpu-phases-4.58-4.81.md#dirty-groups-and-projection-lineage-phase-462)
- [Input audit and workload conditions (phase 4.63)](wiki/history/gpu-phases-4.58-4.81.md#input-audit-and-workload-conditions-phase-463)
- [Recorded branch completeness (phase 4.64)](wiki/history/gpu-phases-4.58-4.81.md#recorded-branch-completeness-phase-464)
- [Image-space rectangle coverage (phase 4.65)](wiki/history/gpu-phases-4.58-4.81.md#image-space-rectangle-coverage-phase-465)
- [Upload and widget geometry (phase 4.66)](wiki/history/gpu-phases-4.58-4.81.md#upload-and-widget-geometry-phase-466)
- [Explicit stroke membership and joined checks (phase 4.67)](wiki/history/gpu-phases-4.58-4.81.md#explicit-stroke-membership-and-joined-checks-phase-467)
- [Dirty and compressed-update geometry bridges (phase 4.68)](wiki/history/gpu-phases-4.58-4.81.md#dirty-and-compressed-update-geometry-bridges-phase-468)
- [Qt command-presentation timing (phase 4.69)](wiki/history/gpu-phases-4.58-4.81.md#qt-command-presentation-timing-phase-469)
- [Explicit execution-path evidence (phase 4.70)](wiki/history/gpu-phases-4.58-4.81.md#explicit-execution-path-evidence-phase-470)
- [Projection reuse and skip evidence (phase 4.71)](wiki/history/gpu-phases-4.58-4.81.md#projection-reuse-and-skip-evidence-phase-471)
- [Nine-process software-timing baseline (phase 4.72)](wiki/history/gpu-phases-4.58-4.81.md#nine-process-software-timing-baseline-phase-472)
- [Upload-boundary wall-interval analysis (phase 4.73)](wiki/history/gpu-phases-4.58-4.81.md#upload-boundary-wall-interval-analysis-phase-473)
- [Direct update-ready to upload-issue analysis (phase 4.74)](wiki/history/gpu-phases-4.58-4.81.md#direct-update-ready-to-upload-issue-analysis-phase-474)
- [Projection-end to prepared-update analysis (phase 4.75)](wiki/history/gpu-phases-4.58-4.81.md#projection-end-to-prepared-update-analysis-phase-475)
- [Native preparation stage probes (phase 4.76)](wiki/history/gpu-phases-4.58-4.81.md#native-preparation-stage-probes-phase-476)
- [Focused real-app stage capture (phase 4.76 follow-up)](wiki/history/gpu-phases-4.58-4.81.md#focused-real-app-stage-capture-phase-476-follow-up)
- [Tile preparation and submission substage probes (phase 4.77)](wiki/history/gpu-phases-4.58-4.81.md#tile-preparation-and-submission-substage-probes-phase-477)
- [Real-app submission substage findings (phase 4.77 follow-up)](wiki/history/gpu-phases-4.58-4.81.md#real-app-submission-substage-findings-phase-477-follow-up)
- [Residency lock-holder tracing (phase 4.78)](wiki/history/gpu-phases-4.58-4.81.md#residency-lock-holder-tracing-phase-478)
- [Real-app residency overlap (phase 4.78 follow-up)](wiki/history/gpu-phases-4.58-4.81.md#real-app-residency-overlap-phase-478-follow-up)
- [End main command recording before residency acquisition (phase 4.79)](wiki/history/gpu-phases-4.58-4.81.md#end-main-command-recording-before-residency-acquisition-phase-479)
- [Early-finish real-app result: no demonstrated improvement](wiki/history/gpu-phases-4.58-4.81.md#early-finish-real-app-result-no-demonstrated-improvement)
- [Queue-lock and driver-call timestamps (phase 4.80)](wiki/history/gpu-phases-4.58-4.81.md#queue-lock-and-driver-call-timestamps-phase-480)
- [Real-app driver-call overlap (phase 4.80 follow-up)](wiki/history/gpu-phases-4.58-4.81.md#real-app-driver-call-overlap-phase-480-follow-up)
- [Shared canvas update builds (phase 4.81)](wiki/history/gpu-phases-4.58-4.81.md#shared-canvas-update-builds-phase-481)
- [Real-app capture of shared canvas builds (phase 4.81 follow-up)](wiki/history/gpu-phases-4.58-4.81.md#real-app-capture-of-shared-canvas-builds-phase-481-follow-up)
- [Byte-limited GPU brush batches continue at once (phase 4.82)](wiki/history/gpu-phases-4.82-4.92.md#byte-limited-gpu-brush-batches-continue-at-once-phase-482)
- [GPU-evaluated circle dabs (phase 4.83, priority 3 step 1)](wiki/history/gpu-phases-4.82-4.92.md#gpu-evaluated-circle-dabs-phase-483-priority-3-step-1)
- [Exact Gaussian and fused default circle dabs (phase 4.84)](wiki/history/gpu-phases-4.82-4.92.md#exact-gaussian-and-fused-default-circle-dabs-phase-484)
- [Skipped CPU generation of described dabs (phase 4.85)](wiki/history/gpu-phases-4.82-4.92.md#skipped-cpu-generation-of-described-dabs-phase-485)
- [RGBA16F generated dabs (phase 4.86)](wiki/history/gpu-phases-4.82-4.92.md#rgba16f-generated-dabs-phase-486)
- [Soft (curve) circle generated dabs (phase 4.87)](wiki/history/gpu-phases-4.82-4.92.md#soft-curve-circle-generated-dabs-phase-487)
- [GPU brush update period (phase 4.88)](wiki/history/gpu-phases-4.82-4.92.md#gpu-brush-update-period-phase-488)
- [Wash latency breakdown and chained brush updates (phase 4.89)](wiki/history/gpu-phases-4.82-4.92.md#wash-latency-breakdown-and-chained-brush-updates-phase-489)
- [GPU base copy in the Wash preview (phase 4.90)](wiki/history/gpu-phases-4.82-4.92.md#gpu-base-copy-in-the-wash-preview-phase-490)
- [Shared and precompiled compute pipelines (phase 4.91)](wiki/history/gpu-phases-4.82-4.92.md#shared-and-precompiled-compute-pipelines-phase-491)
- [Immediate canvas uploads on the shared GPU path (phase 4.92)](wiki/history/gpu-phases-4.82-4.92.md#immediate-canvas-uploads-on-the-shared-gpu-path-phase-492)
- [Priority 4 baseline: CPU filter and transform costs (phase 4.93)](wiki/history/gpu-phases-4.93-.md#priority-4-baseline-cpu-filter-and-transform-costs-phase-493)
- [GPU affine transform passes (phase 4.94)](wiki/history/gpu-phases-4.93-.md#gpu-affine-transform-passes-phase-494)
- [GPU Liquify grid warp (phase 4.97)](wiki/history/gpu-phases-4.93-.md#gpu-liquify-grid-warp-phase-497)
- [GPU Gaussian blur family (phase 4.98)](wiki/history/gpu-phases-4.93-.md#gpu-gaussian-blur-family-phase-498)
- [GPU Puppet Warp mesh rendering (phase 4.96)](wiki/history/gpu-phases-4.93-.md#gpu-puppet-warp-mesh-rendering-phase-496)
- [GPU masking brush composite (phase 4.101)](wiki/history/gpu-phases-4.93-.md#gpu-masking-brush-composite-phase-4101)
- [GPU textured dabs (phase 4.102)](wiki/history/gpu-phases-4.93-.md#gpu-textured-dabs-phase-4102)
- [GPU image tip dabs (phase 4.103)](wiki/history/gpu-phases-4.93-.md#gpu-image-tip-dabs-phase-4103)
- [GPU colour, Lightness and Gradient image tips (phase 4.104)](wiki/history/gpu-phases-4.93-.md#gpu-colour-lightness-and-gradient-image-tips-phase-4104)
- [RGBA16F image tips and textures (phase 4.105)](wiki/history/gpu-phases-4.93-.md#rgba16f-image-tips-and-textures-phase-4105)
- [RGBA16F masking brush composite (phase 4.106)](wiki/history/gpu-phases-4.93-.md#rgba16f-masking-brush-composite-phase-4106)
- [Remaining blend modes (phase 4.107)](wiki/history/gpu-phases-4.93-.md#remaining-blend-modes-phase-4107)
- [All other blend modes (phase 4.108)](wiki/history/gpu-phases-4.93-.md#all-other-blend-modes-phase-4108)
- [Precise contraction audit (phase 4.109)](wiki/history/gpu-phases-4.93-.md#precise-contraction-audit-phase-4109)
- [Clone Layer offsets (phase 4.110)](wiki/history/gpu-phases-4.93-.md#clone-layer-offsets-phase-4110)

## Risks and open questions

- Interop requires desktop OpenGL. With the engine on, "Auto" picks it on
  Windows unless the driver is blacklisted; an explicit ANGLE choice still
  disables interop.
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
- Every GPU blend mode has F16 brush and layer parity coverage since phase
  4.107; phase 4.108 added the rest except the modes listed in "All other
  blend modes", whose results the GPU cannot reproduce.
- Python scripting (`libkis`) and file export need CPU pixel access. Accesses
  to stale CPU tiles download synchronously; already CPU-current tiles do not.
