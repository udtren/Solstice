# Asset Library

Asset Library is a native docker for browsing configured folders, opening
assets, and inserting them as paint, vector, or file layers. It preserves the
original folder and layout configuration while using cached asynchronous
thumbnail loading for large libraries.

The same configuration and asset operations are available through the
independent **Asset Library** tab on Krita's welcome page.

Native implementations:

- [`../plugins/dockers/assetlibrary`](../plugins/dockers/assetlibrary/)
- [`../libs/ui/KisWelcomeAssetLibraryWidget.cpp`](../libs/ui/KisWelcomeAssetLibraryWidget.cpp)
