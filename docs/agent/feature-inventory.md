# Custom feature inventory — agent reference

Where Solstice diverges from upstream Krita 6 (`krita/6.0`). Use it to find
every file a feature touches, to avoid breaking another feature's hook, and to
resolve upstream merge conflicts. Update this file whenever a custom touch
point is added, moved, or removed.

Regenerate the raw list with:

```bash
git diff --stat -w --ignore-cr-at-eol $(git merge-base krita-sol krita/6.0) krita-sol -- . ':!plugins/visionml/thirdparty'
```

## Documented features

| Feature | Own directory / files | Hooks in upstream files | Docs |
| --- | --- | --- | --- |
| GitHub Actions trial build | `.github/workflows/windows-build.yml`, `build-tools/github-actions/` | `packaging/windows/package-complete.py` (application QML import path for separate prefixes) | `docs/test-builds.md`, `docs/agent/github-actions.md` |
| Quick Access Manager | `plugins/dockers/quickaccess/` | `plugins/dockers/CMakeLists.txt` | `docs/quick-access.md`, `docs/agent/quick-access.md` |
| Rest Note | `plugins/dockers/restnote/` | `plugins/dockers/CMakeLists.txt` | `docs/rest-note.md`, `docs/agent/rest-note.md` |
| Asset Library | `plugins/dockers/assetlibrary/`, `libs/ui/KisWelcomeAssetLibraryWidget.*` | `plugins/dockers/CMakeLists.txt`, `libs/ui/KisWelcomePageWidget.*`, `libs/ui/forms/KisWelcomePage.ui`, `libs/ui/CMakeLists.txt` | `docs/asset-library.md`, `docs/agent/asset-library.md` |
| Vision ML | `plugins/visionml/` (incl. vendored `thirdparty/`) | `plugins/CMakeLists.txt` | `docs/vision-ml.md`, `docs/agent/vision-ml.md` |
| Lazy Tools | `libs/ui/KisSolsticeLazyTools.*` | `libs/ui/KisViewManager.cpp`, `libs/ui/CMakeLists.txt`, `libs/ui/forms/wdggeneralsettings.ui`, `libs/ui/dialogs/kis_dlg_preferences.*`, `plugins/dockers/layerdocker/LayerBox.*`, `WdgLayerBox.ui`, `krita/kritamenu.action` | `docs/lazy-tools.md`, `docs/agent/lazy-tools.md` |
| Puppet Warp | Strategy/args/serialization inside `plugins/tools/tool_transform2/`, icons in `krita/pics/tool_transform/` | Transform Tool sources and tests, `krita/pics/tool_transform/tool-transform-icons.qrc` | `docs/puppet-warp.md`, `docs/agent/puppet-warp.md` |
| GPU Engine (branch `krita-sol-gpu`) | `libs/gpu/` (`kritagpu`, shaders, tests), `libs/image/gpu/`, `libs/image/tiles3/KisTileGpu*`, `libs/ui/opengl/KisGpuCanvasUploader.*`, `libs/ui/KisGpuEngineUi.*`, `Kis*Gpu*Test.cpp` | `libs/CMakeLists.txt`, tile engine (`libs/image/tiles3/`), `kis_async_merger.*`, `kis_updater_context.cpp`, OpenGL canvas (`libs/ui/opengl/`), `libs/ui/KisMainWindow.cpp`, `libs/ui/dialogs/kis_dlg_preferences.*`, `libs/ui/KisImportExportManager.cpp`, `plugins/impex/libkra/tests/` (`KisGpuSaveTest`), `plugins/paintops/defaultpaintops/brush/kis_brushop.cpp` (opt-in sequential GPU dab batch), CMake files; full list in `docs/agent/gpu-engine.md` ("Status and locations") | `docs/gpu-engine.md`, `docs/agent/gpu-engine.md` |
| Visual branding | `krita/pics/branding/`, splash data, `krita/pics/mimetypes/`, `krita/pics/svg/support-krita.svg`, MSIX and macOS package images (temporary gray placeholders) | `krita/CMakeLists.txt` (Windows ICO size limit), `libs/ui/kis_splash_screen.cpp`, `libs/ui/dialogs/kis_about_application.cpp`, `libs/widgetutils/config/kstandardaction_p.h` ("Configure/About Solstice"), `krita/kritamenu.action`, `krita/versioninfo.rc.in` | `docs/visual-branding.md`, `docs/agent/solstice-visual-branding-todo.md` |

GPU phase 4.12 also touches `KisDabRenderingQueue.{h,cpp}` and
`KisDabRenderingExecutor.{h,cpp}` under `plugins/paintops/defaultpaintops/brush/`
to bound GPU brush batches by completed source pixel bytes, with queue tests.

GPU phase 4.32 adds bulk-read hooks to
`libs/image/tiles3/kis_tiled_data_manager.cc` and deferred synchronization to
`kis_tile_data{,_interface}.h`. Both interleaved and planar CPU reads batch
stale GPU tiles while holding swap read locks; existing copy/stride behavior
and individual iterator synchronization remain intact.

GPU phase 4.34 adds a sequential GPU readback before the concurrent CPU
masking patches in `libs/ui/tool/strokes/kis_painter_based_stroke_strategy.cpp`.
The masking formulas and patch partitioning remain unchanged. Phase 4.35
changes the brush staging-context choice in `KisGpuBrushPainter.cpp` to reuse
existing large buffers without exceeding the existing memory cap.

GPU phases 4.36-4.37 forward per-channel layer flags in
`libs/image/gpu/KisGpuMergeBatch.*`, allow F16 channel masks in
`KisGpuProjectionCompositor.cpp`, and align the established F16 generic blend
arithmetic in `libs/gpu/shaders/composite_blend.glsl`. Projection tests cover
major modes, all channel masks, partial updates and transparent boundaries.

GPU phases 4.38-4.40 add per-submission upload arenas in `KisGpuTileAccess.*`
and `KisGpuProjectionCompositor.cpp`, bounded resource reclamation outside
the retirement lock in `KisGpuTileBackend.*`, and host-cached transfer-only
staging in `libs/gpu/KisGpuBuffer.*`. Paint-device tests cover suballocation,
GPU lifetime, failed submissions and concurrent retirement.

GPU phase 4.41 adds `tryEvictGpuTileDataBatch` to
`libs/image/tiles3/kis_tile_data_store.{h,cc}`, a batch hook in
`KisTileGpuState.h`/`KisTileGpuHooksStub.cpp`, and bounded F32/F16 eviction
readbacks in `KisGpuTileBackend.*`. No slot is released until CPU content is
valid; failed transfers preserve GPU copies. See the dedicated eviction tests.

GPU phases 4.43-4.45 add `duplicateCpuSnapshot` in `kis_tile_data_store.{h,cc}`
for GPU COW without redundant initialization, `KisTile::cloneShared` in
`kis_tile.{h,cc}` for whole-tile `bitBlt` sharing without CPU synchronization,
and batched partial-clear boundary reads in `kis_tiled_data_manager.cc`.
`KisGpuTileAccess.cpp` uses the snapshot copy under its existing residency or
swap protection. F32/F16 tests cover stale snapshots, current/old exact/rough
copies, partial copies, clear boundaries, submit failure and Undo/Redo.

GPU phase 4.46 adds RGBA16F Normal/Erase dab composition in
`libs/gpu/KisGpuDabCompositor.*`, `shaders/paint_dabs.comp` and shader CMake
entries. `KisGpuBrushPainter.cpp` passes the storage size and restricts F16
modes; `kis_brushop.{h,cpp}` admits both floating image depths while retaining
the owning-image scheduling gate. `KisGpuBrushTest` covers half arithmetic,
asynchronous lifetime and rollback; `KisGpuStrokeTest` covers actual F16
Buildup jobs. Phases 4.47-4.48 add F16 hard/creamy Alpha Darken and Normal/Erase
Wash preview/final merging. `shaders/composite_half_brush.glsl` shares scalar
half Normal/Erase arithmetic between dabs and `composite_layers.comp`.
The layer descriptors in `KisGpuLayerCompositor.*` and
`KisGpuProjectionCompositor.*` carry explicit half-brush/flag semantics so
ordinary layer projection retains its existing arithmetic. Tests cover both
Alpha Darken CPU variants, every Wash channel mask, failure/budget fallback
and actual Wash jobs with GPU counters and Undo/Redo. Phases 4.49-4.50 extend
F16 dabs and Wash to the basic generic modes through Pin Light. They reuse
`compositeGeneric` with half channel masks and per-dab storage rounding,
extend single-layer F16 coverage in `KisGpuProjectionCompositor`, and correct
Exclusion's half intermediate product in `composite_blend.glsl`. The separate
half-brush arithmetic flag remains specific to Normal/Erase. Phases 4.51-4.52
add a lazy extended F16 dab pipeline and enable Soft Light SVG, Color Dodge,
Color Burn and HSY Color/Hue/Saturation/Luminosity for Buildup/Wash. The shared
F16 brush support predicate in `KisGpuLayerCompositor.h` also gates Wash mask
coverage. Half Dodge/Burn round CPU inversion/clamping intermediates in
`composite_blend.glsl`; the other extended F16 brush modes retain CPU fallback.

GPU phase 4.53 updates `KisGpuProjectionCompositor.*` to track timeline values
per leased context, prefer completed/oldest contexts, and allow three pending
serial submissions. Resource creation and waits run outside the pool mutex;
failed waits leave command/table data untouched. `KisGpuBrushTest` adds gated
projection coverage for F32/F16, masks, Alpha Lock, source lifetime, bounded
reuse, failed-submit CPU replay and exact Undo/Redo.

GPU phases 4.54-4.55 add batched CPU readback in
`libs/image/filter/kis_filter.cc` before filter input conversion/processing
and selected or separate-destination copies. `kis_convolution_worker_fft.h`
also batches the FFT cache region and the row span locked by repeat-border
iterators. Both hooks are guarded by `HAVE_KRITA_GPU_ENGINE`.
`KisGpuPaintDeviceTest` covers actual Invert/Gaussian filters, F32/F16,
fractional selections, separate destinations, download failure recovery,
retained snapshots and exact Undo/Redo.

GPU phases 4.56-4.57 add readback hooks in
`libs/image/kis_transform_worker.cc`: full and partial affine transforms,
explicit-axis mirrors and centered mirror helpers prefetch existing device
tiles before CPU iteration. `KisGpuPaintDeviceTest` covers F32/F16 scaling,
shear, rotations, translation, both mirrors, failed downloads, snapshots and
exact Undo/Redo. Transform math and the final default-pixel purge are retained.

`KisGpuPaintDeviceTest::testTransformSequenceUndo` additionally covers
separate flip/rotation transactions and intermediate Undo/Redo states.
The Transform Tool Undo investigation is closed; temporary tool diagnostics
were removed without changing its established history behavior. See
`docs/agent/gpu-engine.md` for the real-app findings.

Brush Stroke Preview adds `KisBrushStrokePreviewRenderer.*` and
`KisBrushStrokePreviewCache.*` under `libs/ui/widgets/`, sharing the F5
stroke/background implementation with `kis_preset_live_preview_view.cpp`.
It touches `kis_preset_chooser.*`, `kis_paintop_presets_chooser_popup.*` and
`plugins/dockers/presetdocker/presetdocker_dock.cpp` for the docker-only layout;
`libs/resourcewidgets/KisResourceItemChooser.*` for the opt-in bottom controls
and restoration from an already active horizontal strip;
`KisPresetDockerFilters.*` for per-docker multi-select engine/bundle menus and
`KisTagFilterResourceProxyModel.*` for additional per-view metadata/storage facets;
`KisResourceLocator.*` for independent saved snapshots; `kis_image.{h,cc}`
for stroke-start notification; stroke random sources and `freehand_stroke.*`
for preview-only seeding; and `MyPaintPaintOp.cpp` for tool-independent preview
tracking. Build/test entries are in
`libs/ui/CMakeLists.txt` and `libs/ui/tests/`, including
`KisBrushStrokePreviewTest`. See `docs/brush-stroke-preview.md` and
`docs/agent/brush-stroke-preview.md` for lifecycle and compatibility rules.

GPU phase 4.11 changes `libs/image/tiles3/kis_tile_data_pooler.cc` to avoid
speculative CPU-clone downloads of GPU-only tiles after commits, and adds
GPU test friend access to the existing pooler suspend/resume helpers in
`kis_tile_data_store.h`.

GPU phase 4.8 also touches `libs/image/kis_paint_layer.cc` and
`libs/image/kis_indirect_painting_support.cpp` to batch readbacks before Wash
preview/final CPU compositing. Phase 4.9 also routes unrestricted Wash preview
through `KisGpuBrushPainter::paintWashPreview` from `kis_paint_layer.cc`.
Phase 4.15 overrides `writeMergeData` in `kis_paint_layer.{h,cc}` to GPU-merge
unrestricted Normal/Erase Wash rectangles inside the existing final-merge
transaction and barrier jobs; other indirect-painting subclasses are unchanged.
Full-stroke parity and timing coverage lives in
`plugins/paintops/defaultpaintops/brush/tests/KisGpuStrokeTest.cpp`.

Lazy Tools menu suppression also uses `libs/ui/utils/KisMenuMnemonicFilter.*`
and `libs/ui/tests/KisMenuMnemonicFilterTest.cpp`, registered in the UI and
test CMake files. The menu-bar-owned observer handles late XMLGUI menus/title
updates without intercepting Alt key events. See `docs/agent/lazy-tools.md`.

## Undocumented custom changes

These have no dedicated documents yet. When modifying one, create its user and
agent documents first (see "Adding a new feature" below).

| Change | Files |
| --- | --- |
| Export Region (`dninosores_export_region`, `Alt+Shift+E`): exports the selection, or the active layer bounds | `libs/ui/KisMainWindow.*` (`slotExportRegion()`), `krita/kritamenu.action`, `krita/krita5.xmlgui` |
| Desktop-only cleanup: Android sources, donation dialog, supporter bundles, logcat dumper removed | `libs/global/CMakeLists.txt`, `krita/data/CMakeLists.txt`, `libs/ui/dialogs/`, `libs/ui/animation/`, `plugins/extensions/buginfo/`, `plugins/extensions/resourcemanager/`, `krita/krita5.xmlgui`, `packaging/android/` |
| Small behavior fixes | `libs/ui/kis_clipboard.cc` + `libs/ui/tests/kis_clipboard_test.*` (empty clipboard handling), `libs/ui/KisImportExportManager.cpp` (dialog parent), `libs/ui/KisWidgetWithIdleTask.h`, `libs/widgets/KisVisualRectangleSelectorShape.*`, `plugins/dockers/layerdocker/NodeDelegate.cpp` (tooltip hit area) |
| Development launcher | `run-krita.bat` |
| Generated `*_ui.py` inspection artifacts (not built; see `docs/agent/coding-rules.md`) | `libs/ui/forms/`, `libs/ui/animation/`, `plugins/impex/png/`, `plugins/dockers/storyboarddocker/`, `plugins/dockers/widegamutcolorselector/`, `plugins/tools/tool_transform2/` |

## Cross-feature hot spots

Several features share these upstream files. Keep each feature's edit small,
self-contained, and commented with the feature name so merges and reviews stay
tractable:

- `libs/ui/KisViewManager.cpp` — per-window feature construction/action setup.
- `libs/ui/KisMainWindow.*` — window-level actions; GPU engine load/save hooks.
- `libs/ui/forms/wdggeneralsettings.ui` and `libs/ui/dialogs/kis_dlg_preferences.cc`
  — General > Custom settings shared by several features.
  The GPU engine adds its group to the Performance tab from code.
- `krita/kritamenu.action`, `krita/krita5.xmlgui` — global actions and menus.
- `plugins/dockers/layerdocker/` — Layers docker extensions.
- `plugins/tools/tool_transform2/` — Puppet Warp and upstream transform modes.

## Adding a new feature

1. Choose the extension point (`docs/agent/codebase-map.md`,
   `docs/agent/extension-points.md`).
2. Create the implementation, preferring a new plugin directory.
3. Create `docs/<feature>.md` (user workflow, scope, limitations, screenshots
   in `images/`).
4. Create `docs/agent/<feature>.md` using the template below.
5. Add a README entry that contains only the feature name and a link to
   `docs/<feature>.md`.
6. Add the agent document to the index in `AGENTS.md` and a row to the
   inventory table above.

### Agent document template

```markdown
# <Feature> — agent development notes

## Status and locations
- Implementation: `plugins/...`
- Hooks in upstream files: ...
- Original reference (if ported): ...

## Build, test, and install
(targets, ctest names, cmake_install.cmake paths)

## Configuration and compatibility
(kritarc keys, data file paths, legacy formats, persisted ids)

## Architecture and lifecycle
(ownership, observers, event filters, threading, undo)

## Invariants
(behavior that must not change without user approval)

## Manual regression checklist
1. ...

## Known limitations / improvement plan
```
