# Asset Library — agent development notes

## Status and locations

The Asset Library migration from Python to native Krita is complete. Treat
future work as maintenance or explicitly requested refinement, preserve the
native architecture and visible layout, and keep compatibility with Python-era
configuration.

- Native docker: `plugins/dockers/assetlibrary/`
- Welcome-page implementation: `libs/ui/KisWelcomeAssetLibraryWidget.*`
- Shared implementation (2026-10-16): `libs/ui/assetlibrary/`
  (`KisAssetLibrarySettings`, `KisAssetLibraryBrowser`,
  `KisAssetLibraryOperations`), exported by `kritaui` for the docker.
- Regression tests: `libs/ui/tests/KisAssetLibraryTest.cpp`.
- Original reference: `<original-plugin-root>/krita-asset-library/asset_library/`

Inspect the Python implementation directly when behavior is uncertain.

## Build and install

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritaassetlibrarydocker KisAssetLibraryTest -j 2"
```

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\dockers\assetlibrary\cmake_install.cmake
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\libs\ui\cmake_install.cmake
```

Changes under `libs/ui` may also require rebuilding and installing the affected
shared Krita library.

## Configuration and compatibility

Configuration is resolved through `KoResourcePaths::saveLocation("data",
"krita_asset_library/", true)`, followed by `config.json`. The Solstice profile
root is described in [settings-location.md](settings-location.md); do not
hard-code the old Krita profile path.
Preserve `paths`, layout sizes, display settings, and compatibility aliases such
as `nested` to `include_subfolders` and `font_size` to the split font-size
settings. If the JSON file does not exist, import the older KConfig fallback
from group `asset_library`, key `settings_json`.

`KisAssetLibrarySettings` owns the common schema and I/O. Its explicit `Usage`
retains the two pre-refactor consumers' edge cases:

- Docker falls back for out-of-range column/font values and retains current
  values for absent layout/column fields on reload. Welcome clamps column/font
  values and reloads display defaults for absent fields. Thumbnail sizes clamp
  to 48-512 in both views.
- Docker's Settings dialog writes its complete known schema. Docker resize,
  splitter, Hide/Show and close use `saveLayout()`, which rereads the file and
  replaces only the six layout keys. Welcome rereads the file on save and
  replaces only path/display fields, preserving docker layout and unknown
  fields (its loaded JSON is used only when the file is unreadable). Both
  retain the old `nested`/`font_size` read aliases and write modern keys.
- Before 2026-11-07 the docker read the file only at construction and wrote
  everything on each resize, so folders added from Welcome were not shown in
  the docker and were later overwritten by its stale list. Do not reintroduce
  full-schema runtime saves.
- An unreadable/missing JSON file uses the KConfig fallback. A readable invalid
  JSON file becomes an empty object, without importing stale fallback data.
- Explicit-path I/O and JSON conversion are available to tests; tests use
  temporary files and never modify the user's preferences or asset folders.

## Architecture and behavior

- `AssetLibraryDock` is a `KisMainwindowObserver`. Retain the view manager
  supplied through `setViewManager()` for document opening and layer insertion;
  do not access a view manager in the plugin constructor.
- The welcome page's Asset Library tab is independent. Do not embed the docker
  widget there. It shares JSON/KConfig settings and user-visible asset
  operations and loads configuration and thumbnails lazily when selected.
- Live sync: every successful save emits
  `KisAssetLibrarySettingsNotifier::sigSaved()` (process-wide, GUI thread).
  Each docker and the welcome page connect to it with a queued connection,
  reload the file, and act only when `hasSameSharedValues()` reports a change
  in folders or display settings. The docker adopts them with
  `copySharedValues()` and keeps its own layout; a hidden or not-yet-loaded
  welcome page catches up on the next show through the mtime check. Folder
  reloads keep the selected folder when it still exists. Edits made outside
  Solstice are not watched.
- Preserve the horizontal split layout: folder list and
  Refresh/Settings/Hide buttons on the left; status and scrollable thumbnail
  sections on the right.
- Folder entries retain alias, path, recursive-folder flag, and per-folder
  extension list. With recursion enabled, group assets by containing folder.
- Tiles support open, show in file explorer, insert as paint/vector layer,
  insert as file layer, duplicate, rename, and delete. Show in File Explorer
  (2026-11-13) calls `KisShowInFileExplorer::showFile()` in
  `libs/ui/utils/KisShowInFileExplorer.*`, shared with the welcome page's
  Recent Images menu (`KisWelcomePageWidget::slotRecentDocContextMenuRequest()`).
  On Windows it runs `explorer.exe /select,"<path>"` through
  `QProcess::setNativeArguments()` (Qt's argument quoting would put the quote
  before `/select`, which Explorer rejects); elsewhere, or when the file is
  gone, it opens the folder with `QDesktopServices`. Layer insertion must use native undo-aware and
  view-manager paths.
- For `.kra` thumbnails, read `preview.png`, then
  `Thumbnails/thumbnail.png`, and use `mergedimage.png` only as fallback.
  Loading `mergedimage.png` first severely stalls large libraries.
- Keep the bounded in-memory cache keyed by path, size, modification time,
  thumbnail size, and device-pixel ratio. Resizing or changing column count
  must reuse cached thumbnails rather than decode every asset again.
- Automatic columns use actual scroll viewport width. Retain the viewport
  resize event filter and deferred startup relayout because initial dock
  geometry is not final.
- Do not rebuild the thumbnail grid on every resize; relayout only when the
  computed column count changes.

## Shared implementation boundaries

- `KisAssetLibraryBrowser` contains extension normalization, recursive grouping
  and locale-aware filename ordering, column calculation, and thumbnail cache
  code. Each view still owns its own bounded 64 MiB cache and GUI-thread access.
  The key includes path, file size/mtime, logical thumbnail size, device ratio,
  and placeholder style. Docker's bold extension placeholder and Welcome's
  default-font placeholder deliberately remain different.
- `KisAssetLibraryOperations` contains undo-aware layer insertion and the
  duplicate/rename filename prompt/validation. It receives the current view
  manager and dialog parent at call time; it owns no global window state.
- Keep display widgets, settings dialogs, menus, timers, and refresh decisions
  in their existing views. They have intentional differences (including the
  docker's Hide/splitter controls, Welcome's lazy load, and cancellation refresh).
  Neither view embeds or depends on the other. Do not make `kritaui` depend on
  the loadable docker plugin.
- User-visible strings were moved unchanged; translation keys remain stable.

## Regression checks

Run `ctest --test-dir <krita-dev-root>\_build -R ^libs-ui-KisAssetLibraryTest$
--output-on-failure --timeout 90` inside `env.bat`, with
`QT_QPA_PLATFORM=offscreen` and `QT_QPA_FONTDIR=C:\Windows\Fonts`.

The focused test covers legacy settings, per-view bounds/reload behavior,
Welcome save scope and docker round trips, the two views not overwriting each
other's changes (layout-only saves, save notifications), JSON/fallback precedence, recursive
listing and extension filters, preview/thumbnail/merged-image fallback order,
HiDPI caching and invalidation, missing-document dialogs, and filename extension
and cancellation handling.

After installing and restarting, check in both views:

1. Existing folders, aliases, recursion, extension filters and font sizes load.
2. Resize automatic columns; thumbnails remain correct. Docker Hide/Show and
   splitter sizes survive restart. Welcome stays lazy until selected.
3. Change display settings from Welcome; saved docker layout is retained.
   With the docker visible next to the welcome page, add a folder from
   Welcome: the docker lists it at once and still has it after resizing the
   docker and restarting. Add one from the docker: Welcome lists it.
4. Open an asset; insert PNG/SVG and file layers, then undo/redo insertion.
5. Duplicate, rename and delete a disposable asset; cancel each dialog once.
   Show in File Explorer opens Explorer with the asset selected, including a
   file whose name has spaces or non-ASCII characters; do the same from the
   Recent Images menu, and for a recent file that was deleted.
6. Open a second window and close views after resizing, checking that deferred
   timers and document operations remain associated with the proper window.

2026-10-16 validation: `kritaassetlibrarydocker` and `KisAssetLibraryTest` built;
the focused CTest passed, with 14 QtTest passes including init/cleanup and the
four KRA preview data rows (zero failures/skips). `kritaui` and the docker were
installed, together with the rebuilt `kritaversion` and `kritawidgetutils`
dependencies. All four installed DLLs match their build outputs by SHA-256.
No Solstice/Krita process was running during installation. Real canvas
insertion/undo and the above interactive layout checks remain for user
verification. Logs are `%TEMP%/solstice-assetlibrary-{final-build,final-test,
install}.log` and `%TEMP%/solstice-assetlibrary-results.txt`.

The user confirmed Asset Library works correctly after this refactor on
2026-10-16. Interactive verification is complete.
