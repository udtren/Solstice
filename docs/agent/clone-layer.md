# Clone Layer performance — agent reference

Clone Layer is upstream Krita's layer that shows another layer's pixels
(`KisCloneLayer`). Solstice keeps its behavior and file format and only
changes how it is updated with the GPU engine. This document records the
update path, the measurements behind the change, the GPU copy (GPU engine
phase 4.110), its limits and the remaining improvement candidates.

User-facing scope: `docs/gpu-engine.md` ("What runs on the GPU", "What stays
on the CPU").

## Update path (upstream, unchanged)

- `libs/image/kis_clone_layer.{h,cpp}`: `original()` is the source's
  projection (`COPY_PROJECTION`, the default) or original
  (`COPY_ORIGINAL`). The clone has its own projection only when it has an
  offset (`needProjection()`) or effect masks (`KisLayer::projection()`).
  Without either, the merge composites the source device directly.
- `copyOriginalToProjection()` copies the source into the clone's projection,
  shifted by the offset. `KisLayer::applyMasks()` calls it into the projection
  (no masks, or masks whose rects do not vary) or into a temporary device
  (masks whose need rect varies, e.g. blur).
- **A source change updates its clones with a second walker.** The source's
  walker records a `CloneNotification` (`kis_base_rects_walker.h`); after the
  merge `KisAsyncMerger` calls `KisLayer::updateClones()`, which calls
  `KisCloneLayer::setDirtyOriginal()` → `KisLayer::setDirty()`: a separate
  walker from the clone up to the root. Every source stroke therefore
  recomposites the root (and the clone's parents) twice.
- `KisBaseRectsWalker::addCloneSourceRegenerationJobs()` regenerates source
  areas a clone needs that the walker does not already prepare.

## GPU copy of offset clones (phase 4.110)

With the GPU engine, a GPU brush leaves the source's tiles GPU-valid and
CPU-stale. The CPU copy in `copyOriginalToProjection()` then downloaded
every source tile under the dirty rect on every update.

- `KisCloneLayer::copyOriginalToProjection()` first tries
  `KisGpuProjectionCompositor::copyShifted(projection, rect, original,
  offset)`, when the clone **has no effect masks** and
  `KisGpuMergeBatch::mayCompositeOnGpu(this)` (engine enabled, RGBA F32/F16
  image: the tile-exclusive merge scheduling the GPU write relies on). On
  refusal or failure it falls back to `KisPainter::copyAreaOptimized()`.
- `copyShifted()` (`libs/image/gpu/KisGpuProjectionCompositor.cpp`) checks:
  both devices RGBA F32/F16 in the same color space, distinct devices, equal
  default pixels, destination offset a multiple of 64. When neither the
  source sample nor the destination has data in the rect it does nothing,
  like `copyAreaOptimized()`. It prepares the destination read-write and the
  source sample read-only (`KisGpuTileAccess`, shared upload arena), and
  records `KisGpuShiftedCopyPass` in a leased work context. It does not wait.
  `shiftedCopyCount()` counts successful calls.
- `KisGpuShiftedCopyPass` (`libs/gpu`, `shaders/copy_shifted.comp`): one
  workgroup per destination tile. Each pixel in the clip rect reads the
  source pixel at `position + shift` across source tile boundaries, or the
  source's default pixel where there is no source tile. Pixels are copied
  unconverted (F16 stays F16).
- **Parity.** Non-transparent pixels equal the CPU copy bit for bit. The CPU
  Copy op for RGBA F32 (`KoOptimizedCompositeOpCopy128`) clears the color of
  some alpha-0 pixels depending on SIMD batches; the GPU keeps the source's
  color (see `wiki/concepts/krita-copy-semantics.md`). Alpha-0 colors are
  unspecified in Krita and ignored by the parity tests.
- **Why not with masks.** Effect masks run on the CPU and read the copy. A
  GPU copy only moved the download from the source tiles to the copied
  tiles and added a submission: transparency masks became up to 1.4 ms per
  dab slower and downloads rose (measured before the restriction; numbers
  in the benchmark page below). A mask that ran on the GPU (e.g. a future
  GPU path for filter masks) would make the GPU copy worthwhile there too.

## Measurements

[Benchmark page](wiki/benchmarks/clone-layer-updates.md): update time and
GPU-to-CPU downloads per dab for offsets, masks, RGBA8/F32 and nesting,
before and after phase 4.110. Summary:

- CPU (RGBA8, F32 without the GPU engine): a clone costs about one extra
  walker (+0.4 to +0.8 ms per 256 px dab on a 2480x3508 image); an offset
  adds 0.1-0.9 ms; a blur filter mask dominates (+5 to +12 ms). Nesting
  depth (3 groups) and a group as the source add little.
- GPU before 4.110: offset clones downloaded about 25-30 source tiles per
  dab (3.6-5.1 ms). After: offset clones without masks drop to 1.6-2.8 ms,
  and 0.7-1.1 ms with tile-aligned dabs (no source downloads).

## Remaining candidates (not done)

1. ~~**Partial-tile projection clears (all GPU merges, not clone-specific).**~~
   Done in GPU phase 4.111 (2026-10-16): `KisAsyncMerger::setupProjection()`
   called `parentOriginal->clear(rect)` on the CPU, which downloaded the
   partly covered border tiles of every GPU-resident projection for an
   unaligned dirty rect (about 15 of a 256 px dab's 25 tiles, per
   projection, per walker; a clone doubled it through its second walker).
   The clear now runs with the first GPU composite of the batch
   (`docs/agent/gpu-engine.md`, "Partial-tile projection clears (phase
   4.111)"). Unaligned dabs of clones without masks download nothing and
   take 0.25-0.40 ms instead of 1.6-2.8 ms per dab; masked clones keep the
   downloads of their CPU masks
   ([benchmark page](wiki/benchmarks/clone-layer-updates.md)).
2. **One walker for source and clones.** Merging the clone's update into
   the source's walker would save the second recomposite of the root
   (about 0.4-0.8 ms per dab on the CPU). The walker code is intricate
   (`kis_base_rects_walker.*`, `kis_merge_walker.*`); high risk.
3. **GPU effect masks on clones** (see above).

## Tests

- `libs/image/tests/KisGpuProjectionTest.cpp`:
  - `testShiftedCopyMatchesCpu`: `copyShifted()` against
    `copyAreaOptimized()` for F32/F16, positive, negative, aligned and zero
    offsets, a source device at (10, 7), rects partly or wholly outside the
    source, content outside the rect preserved; refusals for RGBA8 and for
    the same device.
  - `testCloneLayerMatchesCpu`: an image with a Multiply clone (opacity
    200) at four offsets, with no mask, a transparency mask or a blur filter
    mask: full GPU refresh against the CPU; three GPU-written source dabs;
    the GPU copy runs exactly when there are no masks, and the source tiles
    stay CPU-stale.
- `libs/image/tests/KisCloneLayerBenchmark.cpp` (opt-in,
  `KRITA_CLONE_BENCHMARK=1`): the scenario matrix of the benchmark page.
  `KRITA_CLONE_BENCHMARK_GPU_ONLY=1` runs only the GPU engine rows;
  `KRITA_CLONE_BENCHMARK_ALIGNED=1` uses tile-aligned dabs. QtTest routes
  `qInfo()` into its log: pass `-o <file>,txt` and read the `RESULT` lines.
- `KisGpuTileBackend::downloadedTileCount()` counts tiles downloaded by
  successful readbacks (used by the benchmark).
- `libs/image/tests/kis_clone_layer_test` (upstream) still passes.

Build and run (paths from `AGENTS.md`):

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritagpu kritaimage KisGpuProjectionTest KisCloneLayerBenchmark kis_clone_layer_test -j 16"
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && set KRITA_GPU_VALIDATION=1&& set VK_LAYER_PATH=<Vulkan SDK>\Bin&& <krita-dev-root>\_build\bin\KisGpuProjectionTest.exe testShiftedCopyMatchesCpu testCloneLayerMatchesCpu"
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && set KRITA_CLONE_BENCHMARK=1&& <krita-dev-root>\_build\bin\KisCloneLayerBenchmark.exe -o clone-bench.txt,txt"
```

Install `libs/gpu` and `libs/image` (`cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P
<krita-dev-root>\_build\libs\<lib>\cmake_install.cmake`) before an
interactive check.

## Manual checks

User-checked on 2026-10-16. On an RGBA 32-bit float document with the GPU engine on (also listed in
`gpu-engine.md`, manual regression checklist):

1. Clone a paint layer, move the clone with the Move Tool by an odd amount
   (not a multiple of 64), paint on the source with a GPU brush: the clone
   follows during the stroke; the clone looks like the source moved by the
   offset; Undo/Redo.
2. The same with a clone of a group, and with the clone inside nested
   groups.
3. Add a transparency mask and a filter mask to the clone: it still follows
   the source (CPU copy path).
4. Toggle the GPU engine off (restart): the same results.
5. Save, reopen: the clone keeps its source and offset.

## Invariants

- Do not use the GPU copy where `mayCompositeOnGpu()` is false: the GPU
  write depends on `KisUpdaterContext`'s tile-exclusive scheduling.
- `copyShifted()` must leave the destination unchanged when it returns
  false (CPU fallback follows).
- Keep the no-mask condition unless masks gain a GPU path; re-measure with
  the benchmark before changing it.
