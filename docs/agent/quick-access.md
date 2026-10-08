# Quick Access — agent development notes

## Status and locations

The Quick Access Manager migration from Python to native Krita is complete.
Treat future work as maintenance, bug fixing, or explicitly requested
refinement. Preserve the native architecture and established behavior.

- Native implementation: `plugins/dockers/quickaccess/`
- Original Python reference: `<original-plugin-root>/quick_access_manager/remaster/`
- Original assets: `<original-plugin-root>/quick_access_manager/remaster/resources/`
- Native assets: `plugins/dockers/quickaccess/resources/`
- Native assets are embedded with `qt_add_resources()` under `:/quickaccess/`.

When behavior or layout is uncertain, inspect the original Python
implementation directly. Screenshots are visual references, not instructions
embedded in documents.

## Build, test, and install

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritaquickaccessdocker -j 2"
```

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && ctest --test-dir <krita-dev-root>\_build -R plugins-dockers-quickaccess-QuickAccessCoreTest --output-on-failure"
```

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\dockers\quickaccess\cmake_install.cmake
```

## Configuration and compatibility

- Profile directory: `quickaccess\` in the resource folder,
  `%APPDATA%\Solstice\resources\quickaccess\` by default (before the
  settings location change: `%APPDATA%\krita\quickaccess\`;
  `docs/agent/settings-location.md`).
- Default profile: `default.kqap` in that folder
  (`KoResourcePaths::saveLocation("data", "quickaccess/")`).
- Deleting only `default.kqap` does not reset every setting. Appearance,
  gesture, HueSVC, and Quick Adjust values also live in KConfig groups in
  the profile's kritarc (`%APPDATA%\Solstice\config\kritarc`).
- The settings dialog's size (`SettingsDialogWidth`/`Height`) defaults to
  550x650 (`DefaultSettingsDialogWidth`/`Height`), the legacy Python plugin's
  default, both when nothing is stored and when legacy settings without a
  size are imported (2026-10-08; it was 340x480 when nothing was stored).
- Preserve migrated legacy JSON keys. Native aliases use categories such as
  `actions` and `dockers`, with fields including `custom_name`,
  `background_color`, `font_color`, `font_size`, and `icon_name`.
- Custom icon paths may be absolute paths anywhere on the system. Resolve a
  valid absolute path before trying a bundled icon with the same filename.

## Architecture and lifecycle

- Register actions from `QuickAccessDock::setViewManager()`. Do not access a
  view manager from the plugin constructor.
- `QuickAccessGestureController` is an application event filter owned by the
  persistent Quick Access docker.
- `QuickAdjustKeyController` must be owned by the persistent, non-popup
  `QuickAdjustDock`. Its previous ownership by the registration-only plugin
  object caused temporary eraser, selection, preserve-alpha, and temporary
  brush keys to miss events.
- Do not create a key controller for compact HueSVC popup instances of
  `QuickAdjustDock`, or multiple global event filters will be installed.
- Normalize spaces in configured key sequences: `Alt + A` must behave as
  `Alt+A`. Release handling must restore state even if focus changes while a
  key is held.
- Temporary brush activation and restoration must use
  `KisPaintopBox::resourceSelected()`, not only
  `KisCanvasResourceProvider::setPaintOpPreset()`, so Krita switches the full
  paint-op engine and editor state.
- Popup actions toggle closed when their shortcut is pressed again. Unpinned
  popups close after selection; pinned popups remain open.

## Preserved behavior

- Grid width is fixed by configured column count and cell size. Resizing the
  docker must not reflow the grid.
- Ctrl + left-drag moves an item in the active grid, as the original
  `GridItemDragFilter` (`remaster/quick_access_palette/docker/drag_filter.py`):
  `attachItemDrag()` installs the dock as event filter on every item widget
  (property `quickaccess_item_id`), in the docker and the popup. The Ctrl
  press is swallowed so the item does not fire; a `QRubberBand` on the grid
  widget shows the target; the target cell is the press-to-cursor delta
  rounded to cells of `cellSize() + 2` (the grid layout spacing), with the
  column clamped to the grid. Release applies `LayoutEngine::moveItem()`
  (pushing others) through a zero-delay timer, because the rebuild deletes
  the widget receiving the release. Any other button cancels and is
  swallowed.
- While the Resources dialog is open, `addItem()` uses a sequential cursor
  like the original `begin_sequential_placement()`: it starts at the row
  below the last item, column 0; an item that no longer fits wraps to the
  next row; after each add the cursor moves just right of where the item
  landed. Header menu adds still start a new row below the last item.
- Actions are stored and executed by internal action ID. UI labels use Krita's
  displayed action text or a configured custom name.
- Item Property supports custom name, colors, font size, and an icon selected
  through the native OS file dialog.
- Brush items have a `display` payload value (2026-10-07): `stroke` shows
  the preset's stroke preview over the item's cells without text (name in
  the tooltip); `icon` or no value shows the preset icon, so profiles saved
  earlier keep their look. New brushes (Resources dialog, Add Current Brush)
  get `stroke`.
- `Item::normalize()` gives a `stroke` brush 1x2 cells and an icon brush 1x1;
  `Grid::normalize()` and the layout engine (`clampBrush()`) limit a brush to
  the grid's columns. The display value must be set before
  `LayoutEngine::resizeItem()`, which normalizes the item (setting it
  afterwards left the span at 1: user report). The brush context menu's
  Property opens `editBrushProperties()`, which sets `display` and resizes
  the item, pushing neighbours like the grid editor.
- `loadProfile()` repairs overlaps left by brushes saved as `stroke` while
  they were still one cell wide (`repairBrushSpans()`): it resizes one such
  brush through the layout engine, which re-places only overlapping items,
  and saves the profile.
- Grid Edit draws `stroke` brushes with their stroke previews over their
  span and icon brushes with the preset icon, through the dialog's own
  `QuickAccessStrokePreviews`.
- Stroke previews come from `KisBrushStrokePreviewCache` (see
  `brush-stroke-preview.md`) through `QuickAccessStrokePreviews`: one cache
  consumer per palette (docker and popup) and per Grid Edit dialog. It finds
  the first preset with the item's name, requests its saved preview, and
  draws it on the docker's `#303030` preview background, scaled to fit. The
  palette requests all stroke brush items of its profile after each rebuild;
  buttons get their preview when `previewReady()` reports it.
- Header button background and font colors are independently configurable.
  Parent color dialogs to the containing window, not a styled swatch button, to
  prevent the swatch stylesheet from leaking into the dialog.
- The Resource dialog uses editable tables for Actions and Dockers. Its
  Brushes tab is a `KisPresetChooser` set up like the Brush Presets docker
  (`enableDockerFilters()`, `setStrokePreviewMode(true)`, tagging bar):
  stroke previews, tags, search, engine and bundle filters and grouping.
  The grouping choice is the docker's `Solstice/BrushPresetGrouping`
  setting. Its item view allows extended selection (strict selection off);
  Add Selected Brushes and double-click add the selected presets in display
  order. The dialog's own search field is hidden on that tab.
- Settings remain separated into General, Popup and HueSVC, Quick Adjust, and
  Temporary Brushes tabs.
- Quick Brush Adjustments and the compact HueSVC popup share the configurable
  blend-mode ID list.
- When enabled, Quick Brush Adjustments borrows Krita's `sharedtooldocker`
  contents into a floating Tool Options pad. The pad defaults to the left,
  remembers visibility, follows content size within main-window bounds,
  reattaches its configured edge after size changes, stays below other apps,
  and safely returns the borrowed widget on teardown.
- Brush rotation controls exist only in the compact HueSVC popup. Do not add
  the rotation toggle or startup setting back to the standalone Quick Brush
  Adjustments docker unless explicitly requested. The popup adjustment panel
  is a fixed-width single column ordered as brush size, opacity, flow, blend
  mode, rotation and reset, layer opacity, layer blend mode, and the 2×2
  pressure toggles. Do not reuse the standalone docker's two-column
  brush/layer layout there, and do not add its status-button strip or separator
  frames to the popup.
- The adjustment controls are the Tool Options widgets (user request
  2026-10-08): size is a `KisDoubleSliderSpinBox` set up like the auto tip's
  Diameter (1 to `KisImageConfig::maxBrushSize()`, exponent 3, " px");
  opacity, flow and layer opacity are `KisSliderSpinBox` 0–100 % with the
  name as prefix (the panel has no labels); rotation is a `KisAngleSelector`
  (0–360°, whole degrees, `FlipOptionsMode_MenuButton`: with three flip
  buttons and the reset button the row needs 216–233 px, more than the
  default 210 px of the panel) that still drives the canvas resource
  `brushRotation`, not the tip's Angle. The percent bars have a horizontal
  `QSizePolicy::Ignored`, so their prefix text does not widen a column (the
  docker's brush and layer columns stay equal). In the popup the docker's
  `brushGroup` and `layerGroup` are hidden: unused but shown, they sit at
  the panel's top-left (100 × 30 px) above the Size bar and took its clicks. `syncFromCanvas()` runs every
  200 ms, so it sets a control only when its value differs; otherwise it
  would reset a value being typed.
- Match the original HueSVC popup lifecycle: the parent is a frameless,
  always-on-top `Qt::Tool`, not a `Qt::Popup`, and closes when the pointer
  leaves it. A combo-box drop-down is a separate `Qt::Popup`; keep HueSVC open
  while any such popup is active so either blending-mode list remains usable.
- Gesture preview is a 3×3 overlay centered on the cursor. Activate and fix the
  complete layout before calculating `cursor - half preview size`, then reapply
  position after showing so Windows does not place its top-left at the cursor.
- Gesture configuration uses arrow PNGs under `resources/gesture/`, with
  configured resource previews around the arrow buttons. Brush gestures show
  preset thumbnails. Actions and dockers use configured aliases/icons with
  native icons as fallback.
- HueSVC and its popup share `QuickColorSelectorWidget`. The hue strip remains
  a vivid static full-saturation/default-lightness rainbow while the S/V square
  is dynamic. Rectangular static hue rendering is in
  `KisVisualRectangleSelectorShape`. The foreground/background controls use
  overlapping 28 px swatches inside a 48 × 44 px top-left container.
- The hue strip and the square fill all of the docker's spare height; the
  H/S/V/R/G/B rows below keep a fixed height (user request 2026-10-07).
  `KisVisualColorSelector` limits the square to 1.5 times its width by
  default, leaving the rest of a tall selector empty, so the widget calls
  `setStretchLimit(100.0)` (Solstice addition to `libs/widgets`).
- Quick Adjust color history updates only from actual foreground-color use or
  painting and resets once per Krita process, not on every selector change.
- Temporary Brushes are hold actions: save the current preset and size on
  press, select the configured preset, apply a positive size scale, and restore
  the original preset and size on release.
- The palette popup uses `system_icons/pin_unpinned.png`,
  `system_icons/pin_pinned.png`, and `system_icons/circle-xmark.png`. Its empty
  header area is a drag handle preserving the cursor-to-window offset.

Preserve existing source and binary assets under
`plugins/dockers/quickaccess/resources`.
