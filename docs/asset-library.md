# Asset Library

Asset Library is a native docker for browsing configured folders, opening
assets, and inserting them as paint, vector, or file layers. It keeps the
folder and layout settings of the Asset Library plugin it replaces, and caches
thumbnails for large libraries.

For details, see the original plugin repository, [Asset
Library](https://github.com/udtren/krita-asset-library). This guide covers
Solstice's native version and where it differs.

The same configuration and asset operations are available through the
independent **Asset Library** tab on the welcome page.

Both views use shared settings, folder scanning, thumbnail loading and layer
insertion code. Their existing layouts and controls remain independent. Existing
JSON settings and older plugin settings continue to load without conversion by
the user; changing settings on the welcome page retains the saved docker layout.

Folders and display settings are one shared list. A folder added or removed in
either view appears in the other at once, including the docker in every open
window; each docker keeps its own size, splitter position and Hide state.
Changes made to the settings file outside Solstice are picked up after a
restart.

Right-click an asset for **Open**, **Show in File Explorer**, the insert
commands, **Duplicate**, **Rename** and **Delete**. **Show in File Explorer**
opens the asset's folder with the file selected. The same command is in the
right-click menu of the welcome page's **Recent Images** tab, next to
**Forget**. If the file was moved or deleted, its folder opens instead; if the
folder is gone too, a warning names the missing path.

Native implementations:

- [`../plugins/dockers/assetlibrary`](../plugins/dockers/assetlibrary/)
- [`../libs/ui/KisWelcomeAssetLibraryWidget.cpp`](../libs/ui/KisWelcomeAssetLibraryWidget.cpp)
