# Lazy Tools — agent development notes

## Status and locations

This is a native port of selected features from `krita-lazy-tools`. It is not a
docker plugin: layer controls extend the existing Layers docker, while actions
and dialogs are owned by each `KisViewManager`.

- Action/dialog implementation: `libs/ui/KisSolsticeLazyTools.*`
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
  removing ampersands, allowing the setting to be reversed without restarting.
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
   Alt-key behavior are restored correctly.
8. Trigger Rename Alternative near each screen edge, apply plain and colored
   presets, save a manual preset, and verify the layer name change can be
   undone without affecting child layers.
