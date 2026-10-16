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

Native implementations:

- [`../plugins/dockers/assetlibrary`](../plugins/dockers/assetlibrary/)
- [`../libs/ui/KisWelcomeAssetLibraryWidget.cpp`](../libs/ui/KisWelcomeAssetLibraryWidget.cpp)
