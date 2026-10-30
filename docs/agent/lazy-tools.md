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
- Released in [0.5.1](wiki/history/release-0.5.1.md) (PR #33): Fast Image
  Export registered folders, Name, Export Region and icon, and the Rename
  Layer Presets editor. Remembered format options followed in
  [0.5.2](wiki/history/release-0.5.2.md) (PR #35).
- Open issue (found 2026-10-28 by the test control scenario
  `lazy-selection-masks` on Windows): Create Selection Mask Alternative
  raises the safe assert `!m_d->parent || !parent` in
  `KisPaintDevice::setParentNode()` once or twice per use. The mask, undo and
  redo still behave as the scenario expects; release packages hide safe
  asserts, development builds show a dialog. Not investigated yet.

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
- General > Custom > Rename Layer Presets (2026-10-24) ports the Python
  Name List tab. `intRenameGridColumns` (1-12), `intRenameDialogWidth`
  (240-4000) and `intRenameDialogHeight` (200-4000) edit the existing
  `Solstice/RenameGridColumns` (3), `RenameDialogWidth` (500) and
  `RenameDialogHeight` (800) keys on OK. The dialog keeps writing its size on
  close, so the spin boxes show the last size. `txtRenamePresets` loads the
  whole file through `KisLazyToolsRenamePresets::readText()` and writes it
  back with `writeText()` only when the document is modified, so an untouched
  file keeps its bytes; `writeText` creates the folder and a failure shows a
  warning. Writing uses the same UTF-8 text mode as `append()`.
- The Custom tab is wrapped in `customScrollArea` (like Miscellaneous) because
  the two groups make it taller than the dialog. Its contents get the same
  transparent-background hack as `scrollAreaWidgetContents`.
- `KisLazyToolsFastExport::Settings` preserves the four root-group keys
  `Solstice/FastExportFolder`, `Solstice/FastExportPngCompression` (6),
  `Solstice/FastExportPngAlpha` (true), and `Solstice/FastExportJpegQuality` (90).
  Only these values are saved; format, scope, destination and the Name field
  still start at PNG, active document, source folder and empty. The dialog
  keeps its existing numeric ranges and trims the configured folder before
  saving.
- `fast_image_export` uses the `document-export` icon, like File > Export
  (2026-10-24).
- Export Region (2026-10-24) is an `ActionRole` button next to Export.
  `KisLazyToolsFastExport::exportRegion()` copies the masking and cropping of
  `KisMainWindow::slotExportRegion()` (projection under `KisImageBarrierLock`,
  `applyAlphaU8Mask` with the global selection, transparent default pixel,
  crop and move to the origin) into a temporary `KisDocument` with the source
  resolution, color space and title, then exports it through
  `exportDocumentSync` with the same options, path and name as `run()`.
  Unlike File > Export Region it does not fall back to the active layer's
  bounds: without a global selection it returns `NoSelection`. The temporary
  document takes the source document's batch mode, so the format's options
  dialog appears exactly when it would for Export (and tests stay
  non-interactive). The button shares `prepare()` (destination, name check,
  preference save) with Export, resolves only the active document, and is
  disabled for "All open documents". The source document's path, modified
  flag, image and selection are unchanged.
- The optional Name field (2026-10-24) is created after Export folder, so
  `findChild<QLineEdit *>()` still returns the folder field. Export passes
  `normalizedBaseName()` (trimmed, one trailing `.png`/`.jpg`/`.jpeg` or
  export extension removed) to `run()`/`outputPath()` as `baseName`; empty
  keeps `completeBaseName()`. `isValidBaseName()` refuses separators,
  Windows-reserved characters, control characters, `.`/`..` and a trailing
  dot or space; an invalid name warns and returns before preferences are
  saved. Reserved device names such as `CON` are not checked; the export then
  fails and is reported as skipped. The field and Clear are disabled for "All
  open documents" because one name cannot serve several documents, and the
  name is ignored there.
- Registered Explorer folders (2026-10-24) are a separate root-group string
  list, `Solstice/FastExportExplorerFolders`, read and written only through
  `KisLazyToolsFastExport::explorerFolders()` / `setExplorerFolders()`. They
  are deliberately not part of `Settings`: the modeless export dialog saves
  `Settings` on Export, and must not overwrite a list edited meanwhile in
  Preferences. Stored paths use `/` separators; normalization trims, drops
  empty entries and removes duplicates in order (case-insensitive on
  Windows). General > Custom (`grpFastExportFolders`, `lstFastExportFolders`)
  shows native separators and writes the list on OK; Restore Defaults does not
  clear it, like the foreground slots.
- The export dialog lists the registered folders in a fourth combo box
  (created after the destination combo, so the first three keep their order).
  Browse… sits beside it, not beside Export folder (user decision,
  2026-10-24, replacing a first "Open in Explorer" button). Browse opens
  `QFileDialog::getExistingDirectory` in the selected registered folder,
  falling back to the Export folder text when the list is empty or the folder
  is missing. The registered folder is only the starting point; the chosen
  folder fills Export folder and selects the "Configured export folder"
  destination. The "Choose a folder now" destination still starts at the
  Export folder text. An empty list disables the combo, not Browse. The
  dialog reads the list when it is created and does not refresh while open.
- The Export folder field is a `QLineEdit` subclass that accepts a dropped
  local directory URL (the first directory among the URLs; files are ignored
  and fall back to the default text drop). A folder drop sets the field and
  selects the "Configured export folder" destination. Typing does not change
  the destination.
- The modeless export-dialog factory resolves documents only on Export through
  the supplied scope callback. Parentage and `WA_DeleteOnClose` are unchanged;
  callbacks capture the same view manager that the original dialog held.
  Invalid destination/chooser cancellation returns before saving preferences.
- Format options (fixed 2026-10-24, after 0.5.1): `exportOptions()` starts
  from the format's remembered configuration,
  `KisConfig::exportConfigurationXML("image/png" | "image/jpeg")` (the
  `ExportConfiguration-<mime>` key File > Export uses), and then sets only
  `compression`/`alpha` or `quality`. Before, it passed a new configuration
  with just those keys, so options such as `forceSRGB` reset to their
  defaults in every export dialog. `KisImportExportManager` copies the options
  dialog's configuration back and saves it under the same key (non-batch
  only), so a choice made there is reused by the next Fast Image Export and
  File > Export. Both Export and Export Region use it.
  On a first export nothing is saved, so the PNG options dialog falls back
  to its own values; `forceSRGB` falls back to on, matching the exporter
  (fixed 2026-10-30, it showed off and saved that).
- Export execution keeps ordered `QPointer<KisDocument>` snapshots, skips null
  and unnamed documents, waits for each named document's image through the
  originating view manager, and calls `exportDocumentSync`. Source folder vs.
  selected folder, `completeBaseName()` default naming, PNG compression/alpha,
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
9. Register two folders in General > Custom (one with subfolders), press OK,
   open Fast Image Export, select each and press Browse…: the native chooser
   must open in it. Pick a subfolder; it must appear in Export folder,
   Destination must switch to Configured export folder and Export must write
   there. Remove a folder, rename another on disk, and confirm the list and
   the fallback to the Export folder.
10. In General > Custom, scroll to Rename Layer Presets: the file path is
    shown and the text matches `name_color_list.txt`. Press OK without
    editing (file unchanged), then add a colored line, change columns and
    size, press OK and open Rename Alternative. Save a manual preset from the
    popup and reopen Settings to see it appended.
11. In Fast Image Export, type a name and export PNG and JPEG for the active
    document to each destination; the files use the typed name. Clear the
    field, switch Documents to All open documents (Name and Clear disabled)
    and confirm each document keeps its own name.
12. Check the Export icon on Fast Image Export in Keyboard Shortcuts and the
    menu. Make an irregular selection, press Export Region with PNG and JPEG
    and compare with File > Export Region; then deselect and confirm the
    warning.
13. Export a PNG with Fast Image Export, check **Force convert to sRGB** in
    the PNG options dialog and press OK. Export again: the option must still
    be checked, and File > Export to PNG must show it checked too.

Automated through [test control](test-control.md) scenarios (2026-10-27,
Linux), as far as state decides them; run them with
`dev-tools/test-control/run_scenarios.py` in a scratch profile:

| Check | Scenario | Covered |
| --- | --- | --- |
| 1 | `plugins/dockers/layerdocker/tests/scenarios/color-label-controls.json` | Labels go to the selected nodes only (a group, not its child; two selected layers); each choice of a visibility color toggles every matching layer, inside groups too |
| 2 | `libs/ui/tests/scenarios/lazy-selection-masks.json` | Save, undo/redo, restore from the popup |
| 4, 11, 12 (part) | `libs/ui/tests/scenarios/lazy-fast-export.json` | Active document, same-folder destination, typed name, PNG and JPEG, Export Region against the selected area, the invalid-name and no-selection warnings, unchanged file name and modified state |
| 5 (part) | `libs/ui/tests/scenarios/lazy-foreground-slots.json` | The nine actions with the default colors |
| 8 (part) | `libs/ui/tests/scenarios/lazy-rename-layer.json` | Manual name and color, Save, a saved preset applied to another layer, undo of the rename |

Still manual: shortcuts and restarts (3), all-documents scope, configured and
chosen folders, and unnamed documents (4), slots changed in Preferences (5),
the hotkey (6), menus (7), screen edges and child layers (8), registered
folders (9), the Preferences page (10), the Name field with all documents
(11), the icon (12) and the format options dialog (13). Observed while
writing the scenarios, unchanged: the color label set by Rename Alternative
is not part of the undo step (undo restores only the name), restoring a
saved selection adds an undo step without a name, and creating a mask makes
`Selection_Mask_Group` the active node.

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

Registered folders (2026-10-24): `explorerFolders` covers the key,
normalization and independence from `Settings::save`; `registeredFolderBrowse`
covers the empty state, listed folders, the chooser's start folder (selected,
missing and none), the chosen folder filling Export folder and switching the
destination, cancellation, and a synthetic folder drop. It uses Qt's
non-native chooser (`AA_DontUseNativeDialogs`).
`explorerFolders` builds its backslash input with `QDir::toNativeSeparators()`
(2026-10-27): a backslash is a separator only on Windows, and on Linux it is
part of a file name, so the Windows-only input made the case fail there. On
other systems it also checks that such names stay as they are and that
duplicates are compared case-sensitively. 21/21 cases pass on Linux.
`kritaui` and the test built without new warnings; offscreen QtTest passed
15/15 and the UI DLL was installed with matching build/install SHA-256
hashes. Manual check 9 is still pending.

Rename Layer Presets (2026-10-24): `presetTextEditor` covers a missing file,
nested folder creation, Unicode and raw text round trips, parsing of the
written text, sharing the file with `append()`, an empty list and a failed
write. The preferences page itself is checked manually (check 10). Offscreen
QtTest passed 16/16; the UI DLL was installed with matching hashes.

Export Name (2026-10-24): `exportNames` covers normalization, rejected names
and `outputPath` with a base name; `exportDialogName` covers Clear, the
disabled state for all documents, the invalid-name warning and a real PNG
export under a Unicode name that leaves the document path unchanged.
Offscreen QtTest passed 18/18; the UI DLL was installed with matching hashes.

Export Region (2026-10-24): `exportRegion` covers unnamed/null documents, no
selection, a masked 5x4 PNG crop that keeps partial alpha, a JPEG crop next
to the source, an unchanged source document and a failed path;
`exportDialogRegion` covers the disabled state for all documents, the
no-selection warning and a named export from the dialog. Offscreen QtTest
passed 20/20. The UI DLL was installed with matching hashes and
`kritamenu.action` was copied to `_install/share/krita/actions`.

Remembered format options (2026-10-24): `exportOptionsKeepSavedFormatOptions`
covers PNG and JPEG with and without a saved configuration: saved keys such as
`forceSRGB`, `saveSRGBProfile` and `progressive` are kept, and the dialog's
compression, alpha and quality override the saved ones. Offscreen QtTest
passed 21/21; the UI DLL was installed with matching hashes. The options
dialog itself is checked manually (check 13).
