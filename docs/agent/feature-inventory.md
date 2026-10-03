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
| Quick Access Manager | `plugins/dockers/quickaccess/` | `plugins/dockers/CMakeLists.txt` | `docs/quick-access.md`, `docs/agent/quick-access.md` |
| Rest Note | `plugins/dockers/restnote/` | `plugins/dockers/CMakeLists.txt` | `docs/rest-note.md`, `docs/agent/rest-note.md` |
| Asset Library | `plugins/dockers/assetlibrary/`, `libs/ui/KisWelcomeAssetLibraryWidget.*` | `plugins/dockers/CMakeLists.txt`, `libs/ui/KisWelcomePageWidget.*`, `libs/ui/forms/KisWelcomePage.ui`, `libs/ui/CMakeLists.txt` | `docs/asset-library.md`, `docs/agent/asset-library.md` |
| Vision ML | `plugins/visionml/` (incl. vendored `thirdparty/`) | `plugins/CMakeLists.txt` | `docs/vision-ml.md`, `docs/agent/vision-ml.md` |
| Lazy Tools | `libs/ui/KisSolsticeLazyTools.*` | `libs/ui/KisViewManager.cpp`, `libs/ui/CMakeLists.txt`, `libs/ui/forms/wdggeneralsettings.ui`, `libs/ui/dialogs/kis_dlg_preferences.*`, `plugins/dockers/layerdocker/LayerBox.*`, `WdgLayerBox.ui`, `krita/kritamenu.action` | `docs/lazy-tools.md`, `docs/agent/lazy-tools.md` |
| Puppet Warp | Strategy/args/serialization inside `plugins/tools/tool_transform2/`, icons in `krita/pics/tool_transform/` | Transform Tool sources and tests, `krita/pics/tool_transform/tool-transform-icons.qrc` | `docs/puppet-warp.md`, `docs/agent/puppet-warp.md` |
| GPU Engine (branch `krita-sol-gpu`) | `libs/gpu/` (`kritagpu`, shaders, tests), `libs/image/gpu/`, `libs/image/tiles3/KisTileGpu*`, `libs/ui/opengl/KisGpuCanvasUploader.*`, `libs/ui/KisGpuEngineUi.*`, `Kis*Gpu*Test.cpp` | `libs/CMakeLists.txt`, tile engine (`libs/image/tiles3/`), `kis_async_merger.*`, `kis_updater_context.cpp`, OpenGL canvas (`libs/ui/opengl/`), `libs/ui/KisMainWindow.cpp`, `libs/ui/dialogs/kis_dlg_preferences.*`, `libs/ui/KisImportExportManager.cpp`, `plugins/impex/libkra/tests/` (`KisGpuSaveTest`), `plugins/paintops/defaultpaintops/brush/kis_brushop.cpp` (opt-in sequential GPU dab batch), CMake files; full list in `docs/agent/gpu-engine.md` ("Status and locations") | `docs/gpu-engine.md`, `docs/agent/gpu-engine.md` |
| Visual branding | `krita/pics/branding/`, splash data | `libs/ui/kis_splash_screen.cpp`, `libs/ui/dialogs/kis_about_application.cpp`, `libs/widgetutils/config/kstandardaction_p.h` ("Configure/About Solstice"), `krita/kritamenu.action`, `krita/versioninfo.rc.in` | `docs/agent/solstice-visual-branding-todo.md` |

GPU phase 4.12 also touches `KisDabRenderingQueue.{h,cpp}` and
`KisDabRenderingExecutor.{h,cpp}` under `plugins/paintops/defaultpaintops/brush/`
to bound GPU brush batches by completed source pixel bytes, with queue tests.

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
