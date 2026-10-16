# Lazy Tools — agent development notes

## Status and locations

This is a native port of selected features from `krita-lazy-tools`. It is not a
docker plugin: layer controls extend the existing Layers docker, while actions
and dialogs are owned by each `KisViewManager`.

- Action/dialog implementation: `libs/ui/KisSolsticeLazyTools.*`
- Rename/export implementation (separated 2026-10-16): `libs/ui/lazytools/`
  (`KisLazyToolsRenamePresets.*`, `KisLazyToolsRenameDialog.*`,
  `KisLazyToolsFastExport.*`, `KisLazyToolsFastExportDialog.*`). The coordinator
  shrank from 733 to 420 lines; the new implementation files are below 160 lines.
- Top-menu lifecycle: `libs/ui/utils/KisMenuMnemonicFilter.*`
- Focused regression: `libs/ui/tests/KisMenuMnemonicFilterTest.cpp`
- Rename/export regression: `libs/ui/tests/KisLazyToolsTest.cpp`
- View-manager lifecycle: `libs/ui/KisViewManager.cpp`
- General > Custom UI: `libs/ui/forms/wdggeneralsettings.ui`
- Settings persistence: `libs/ui/dialogs/kis_dlg_preferences.cc`
- Layers docker controls: `plugins/dockers/layerdocker/LayerBox.*` and
  `plugins/dockers/layerdocker/WdgLayerBox.ui`
- Action metadata: `krita/kritamenu.action`
- Original Python reference: `<krita-plugin-root>/krita-lazy-tools/lazy_tools/`

## Build and installation

Changes span the shared `kritaui` library, Krita shell data, and the layer
docker. Build Krita and the layer docker, then install both the shared library
and docker before interactive testing. A running Krita process may lock the
DLLs on Windows.

For the rename/export separation, only `kritaui` changes; there are no action,
form or layer-docker binary changes. Build and test:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritaui KisLazyToolsTest KisMenuMnemonicFilterTest -j 2"
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && set QT_QPA_PLATFORM=offscreen&& set QT_QPA_FONTDIR=C:\Windows\Fonts&& ctest --test-dir <krita-dev-root>\_build -R libs-ui-KisLazyToolsTest --output-on-failure --timeout 120 && ctest --test-dir <krita-dev-root>\_build -R libs-ui-KisMenuMnemonicFilterTest --output-on-failure --timeout 120"
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\libs\ui\cmake_install.cmake
```

## Architecture and invariants

- `KisSolsticeLazyTools` is constructed by `KisViewManager` and registers
  actions only after the view manager's action manager exists.
- The Windows global hotkey is process-wide. A single native event filter owns
  `RegisterHotKey`; it routes activation to the current main window's
  `screen_color_picker` action. Keep registration idempotent across multiple
  windows.
- The hotkey remains fixed at `Win+Shift+C`. The General > Custom checkbox
  controls whether it is registered.
- Top-menu mnemonic suppression stores each action's original label before
  removing mnemonic markers, allowing the setting to be reversed without
  restarting. Install `KisMenuMnemonicFilter` during `createActions()`, before
  XMLGUI adds menus. A single zero-delay startup callback is insufficient:
  startup can process events before menu construction, and later XMLGUI/plugin
  changes can restore mnemonic labels. Each menu bar owns its filter; Lazy
  Tools retains a `QPointer`. The filter synchronously handles `ActionAdded`
  and `ActionChanged` before QMenuBar registers shortcuts, with a reentrancy
  guard for its own `setText`. It never consumes key/shortcut/mouse events.
  Original and suppressed text properties distinguish external title updates
  from internal changes; toggling the setting restores the latest original.
  Preserve escaped literal `&&`, submenu mnemonics and ordinary action
  shortcuts. Keep the existing `Solstice/DisableTopMenuShortcuts` key/default.
- Selection masks are stored in the hidden `Selection_Mask_Group`. Keep the
  group name compatible with the Python plugin and create masks through the
  native node command adapter so creation is undoable.
- Saved selection activation uses `KisSetGlobalSelectionCommand` and therefore
  participates in the native undo stack.
- Fast export must not change a document's path or modified state. Use
  `exportDocumentSync`, and skip unnamed documents rather than inventing paths.
- Foreground slot colors and export options are persisted under `Solstice/` in
  `kritarc`.
- Rename Alternative reads and appends presets in the legacy-compatible
  `lazy_tools/config/name_color_list.txt` user-data path. Layer renaming uses
  `KisNodeManager::setNodeName()` so the name change is undoable; color labels
  apply only to the active node.
- `KisLazyToolsRenamePresets` owns only the existing text codec and append
  policy. Blank/empty-name lines are ignored; names split at the first comma,
  color names are trimmed/case-insensitive (`gray` and `grey` both map to 8),
  unknown colors map to 0. Appending preserves raw old lines and suppresses only
  an exact new-line match, not normalized duplicates. The path resolver, text
  encoding and silent I/O-failure behavior are unchanged.
- The rename-dialog factory takes a parent and an apply callback. The coordinator
  looks up the active node at application time and uses the same native node
  manager/label operations. The dialog stays open if no node can be renamed.
  Checked Save still appends before trying to rename. Width/height are saved on
  destruction, including cancellation; minimum size and grid fallback remain
  240x200 and at least one column. The modal caller owns the dialog.
- `KisLazyToolsFastExport::Settings` preserves the four root-group keys
  `Solstice/FastExportFolder`, `Solstice/FastExportPngCompression` (6),
  `Solstice/FastExportPngAlpha` (true), and `Solstice/FastExportJpegQuality` (90).
  Only these values are saved; format, scope and destination still start at
  PNG, active document and source folder. The dialog keeps its existing numeric
  ranges and trims the configured folder before saving.
- The modeless export-dialog factory resolves documents only on Export through
  the supplied scope callback. Parentage and `WA_DeleteOnClose` are unchanged;
  callbacks capture the same view manager that the original dialog held.
  Invalid destination/chooser cancellation returns before saving preferences.
- Export execution keeps ordered `QPointer<KisDocument>` snapshots, skips null
  and unnamed documents, waits for each named document's image through the
  originating view manager, and calls `exportDocumentSync`. Source folder vs.
  selected folder, `completeBaseName()` output naming, PNG compression/alpha,
  JPEG quality and skipped-document reporting remain unchanged. Do not toggle
  production document batch mode: format-specific options dialogs are existing
  behavior. Tests set batch mode only on temporary test documents.
- The Layers docker visibility selector intentionally changes canvas-layer
  visibility. Do not confuse it with Krita's existing filter button, which only
  filters rows shown in the docker.
- Color-label changes apply only to the explicitly selected nodes. Never
  propagate a group's label to its descendants implicitly.

## Manual checks

1. Confirm the label control updates all explicitly selected nodes without
   changing group descendants, and repeated activation of a visibility color
   toggles all matching layers.
2. Create a selection, save it, undo/redo it, and restore it from the popup.
3. Assign shortcuts to all new actions and confirm they survive restart.
4. Export PNG/JPEG for active and all open saved documents to each destination
   mode; confirm unnamed documents are reported as skipped.
5. Change each foreground slot, apply settings, and trigger all nine actions.
6. With Krita unfocused on Windows, press `Win+Shift+C` over another app and
   verify the sampled foreground color. Toggle the option off and verify the
   hotkey is released.
7. Toggle top-menu shortcut suppression on and off and verify menu labels and
   Alt-key behavior are restored correctly. With it enabled, restart without
   opening Settings and hold/release the user's Alt+E TempBrush shortcut over
   a canvas: no Edit menu, temporary brush restores on release. Repeat after
   switching documents, opening a second window and configuring toolbars.
   Mouse-opened menus must still work. Disable the assigned Alt+E action when
   checking restored Edit-menu mnemonics, to avoid an intentional conflict.
8. Trigger Rename Alternative near each screen edge, apply plain and colored
   presets, save a manual preset, and verify the layer name change can be
   undone without affecting child layers.

For a menu-only change, build `kritaui` and `KisMenuMnemonicFilterTest`; install
`_build/libs/ui/cmake_install.cmake` with `CMAKE_INSTALL_LOCAL_ONLY=1`.
Run only `libs-ui-KisMenuMnemonicFilterTest` (not the whole UI suite). This Qt
widget test uses no application preferences and covers late construction,
title updates, remove/reinsert, literal ampersands, separate windows, ownership,
actual Alt+E QAction activation, mouse menus and restored Alt+E mnemonics.
It can run with `QT_QPA_PLATFORM=offscreen`. Real TempBrush press/release and
canvas focus still require the manual check above.

2026-10-04 validation: `kritaui` and the focused test built successfully;
offscreen Qt 6.8 tests passed 6/6 (four test functions plus init/cleanup).
The UI DLL was installed with matching build/install SHA256 hashes after
confirming the application was closed. Logs under `%TEMP%`:
`solstice-menu-shortcuts-build.log`, `solstice-menu-shortcuts-test.txt`,
`solstice-menu-shortcuts-install.log`. No user preferences were modified.
The user subsequently confirmed the real-app startup/TempBrush check passed.

## Rename/export regression coverage

`KisLazyToolsTest` uses a temporary Solstice profile, an explicit temporary
KConfig path and temporary preset/output files. It covers legacy preset parsing,
exact-line duplicate handling, raw-line and Unicode preservation, all color
labels, export defaults/keys/round trips and multi-dot filenames. Widget checks
cover rename application/cancel/no-active-node handling and geometry persistence,
export scope resolution, saved options, invalid-folder warnings and chooser
cancellation. Real PNG (alpha and opaque) and JPEG exports verify source and
configured destinations, readable pixels/dimensions, and preservation of the
document path, caption and modified flag. Null/unnamed documents, cancelled
operation waits and failed output paths are checked too.

2026-10-16 validation: `kritaui` and both test targets built successfully.
The new suite passed all 13 QtTest cases (including init/cleanup), and CTest
passed both `KisLazyToolsTest` and `KisMenuMnemonicFilterTest`. The shared UI
library was installed; build/install DLL SHA-256 hashes matched. No user
preferences or documents were used by the tests.

The coordinator's OS hotkey filter, selection popup and translated strings were
compared against the pre-refactor implementation and are unchanged. The user
confirmed the rename/export refactor works correctly on 2026-10-16.
