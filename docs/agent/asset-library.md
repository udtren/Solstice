# Asset Library — agent development notes

## Status and locations

The Asset Library migration from Python to native Krita is complete. Treat
future work as maintenance or explicitly requested refinement, preserve the
native architecture and visible layout, and keep compatibility with Python-era
configuration.

- Native docker: `plugins/dockers/assetlibrary/`
- Welcome-page implementation: `libs/ui/KisWelcomeAssetLibraryWidget.*`
- Original reference: `<original-plugin-root>/krita-asset-library/asset_library/`

Inspect the Python implementation directly when behavior is uncertain.

## Build and install

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritaassetlibrarydocker -j 2"
```

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\dockers\assetlibrary\cmake_install.cmake
```

Changes under `libs/ui` may also require rebuilding and installing the affected
shared Krita library.

## Configuration and compatibility

Configuration lives at `%APPDATA%\krita\krita_asset_library\config.json`.
Preserve `paths`, layout sizes, display settings, and compatibility aliases such
as `nested` to `include_subfolders` and `font_size` to the split font-size
settings. If the JSON file does not exist, import the older KConfig fallback
from group `asset_library`, key `settings_json`.

## Architecture and behavior

- `AssetLibraryDock` is a `KisMainwindowObserver`. Retain the view manager
  supplied through `setViewManager()` for document opening and layer insertion;
  do not access a view manager in the plugin constructor.
- The welcome page's Asset Library tab is independent. Do not embed the docker
  widget there. It shares JSON/KConfig settings and user-visible asset
  operations and loads configuration and thumbnails lazily when selected.
- Preserve the horizontal split layout: folder list and
  Refresh/Settings/Hide buttons on the left; status and scrollable thumbnail
  sections on the right.
- Folder entries retain alias, path, recursive-folder flag, and per-folder
  extension list. With recursion enabled, group assets by containing folder.
- Tiles support open, insert as paint/vector layer, insert as file layer,
  duplicate, rename, and delete. Layer insertion must use native undo-aware and
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
