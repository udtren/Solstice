---
type: pitfall
updated: 2026-11-07
sources:
  - libs/ui/assetlibrary/KisAssetLibrarySettings.cpp (save(), saveLayout(), KisAssetLibrarySettingsNotifier)
  - plugins/dockers/assetlibrary/AssetLibraryDock.cpp (saveRuntimeSettings(), reloadSharedSettings())
  - libs/ui/KisWelcomeAssetLibraryWidget.cpp (reloadSettings())
  - libs/ui/tests/KisAssetLibraryTest.cpp (testViewsDoNotOverwriteEachOther)
  - ../../asset-library.md
related:
  - build-format-test.md
---

# Two views writing one settings file

When two views edit the same settings file and each keeps a copy in memory,
a view that saves its whole copy writes back whatever it loaded earlier. Any
change another view made in between is lost, silently.

## Where it happened

The Asset Library docker and the welcome page's Asset Library tab share
`krita_asset_library/config.json`. The docker read it only at construction
and saved its complete settings, folder list included, after every resize,
splitter move, Hide/Show and close. A folder added from the welcome page did
not appear in the docker, and the next docker resize removed it from the
file. Only the docker → welcome direction worked, because the welcome page
rereads the file (mtime check) whenever it is shown.

## How to avoid it

- Frequent, automatic saves (geometry, splitters) must update only the keys
  the view owns: read the current file, replace those keys, write it back
  (`KisAssetLibrarySettings::saveLayout()`).
- An explicit save from a view should also start from the current file when
  it edits only part of the schema (Welcome `save()`).
- Tell the other views about a save in-process (a notifier signal, queued so
  that no view reloads inside the saving view's handler) and let them compare
  the values they show before rebuilding, so layout-only saves stay cheap.
- An mtime check alone does not help a view that never rereads the file.

Details and the manual check are in
[Asset Library](../../asset-library.md#configuration-and-compatibility).
