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
- Responsibility separation (2026-10-16): `QuickAccessSettings.h` (value type),
  `QuickAccessSettingsStore.*` (KConfig read/write and legacy migration),
  `QuickAccessProfileLoader.*` (startup profile/repair/import flow),
  `QuickAccessItemProperties.*` (item/brush property dialogs), and
  `QuickAccessItemPresentation.*` (shared alias/display/icon interpretation).
- Grid Edit separation (2026-10-16): `QuickAccessGridEditOperations.*` (group
  placement in the widget-independent core), `QuickAccessGridItemButton.*`
  (appearance, previews and drag/resize input), `QuickAccessGridCanvas.*`
  (grid painting, child widgets, drop highlight and rectangle selection), and
  `QuickAccessGridEditMetrics.h` (the existing fixed geometry constants).
  `QuickAccessGridEditDialog.cpp` now has 360 lines instead of 962 and retains
  document/selection ownership, per-tab history, menus and dialog acceptance.
- Quick Adjust separation (2026-10-16): `QuickAdjustToolOptionsPad.*` owns the
  borrowed Tool Options widget's pad, geometry, application visibility and
  restoration; `QuickAdjustDockHistory.cpp` holds the unchanged color/brush
  history methods. The coordinator is 602 lines instead of 916.
- Palette/gesture separation (2026-10-16): `QuickAccessDockWidgets.cpp` holds
  item rendering, tab layout and preview refresh; `QuickAccessDockPopups.cpp`
  holds popup construction, pinning and header dragging. These remain methods
  of the dock, retaining its canvas/document/preview and popup ownership.
  `QuickAccessItemDragController.*` owns per-palette Ctrl-drag input and feedback.
  The coordinator is 813 lines instead of 1,499. `QuickAccessGesture.cpp` now
  contains only the input controller (170 lines instead of 623);
  `QuickAccessGestureDialog.*`, `QuickAccessGesturePreview.*` and
  `QuickAccessGesturePresentation.*` separate settings UI, the overlay and
  shared direction/resource presentation.

When behavior or layout is uncertain, inspect the original Python
implementation directly. Screenshots are visual references, not instructions
embedded in documents.

## Build, test, and install

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritaquickaccessdocker QuickAccessCoreTest QuickAccessConfigTest QuickAccessItemPropertiesTest QuickAdjustKeyControllerTest QuickAccessGridEditTest QuickAdjustDockTest QuickAccessInteractionTest -j 2"
```

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && set QT_QPA_PLATFORM=offscreen&& set QT_QPA_FONTDIR=C:\Windows\Fonts&& ctest --test-dir <krita-dev-root>\_build -R plugins-dockers-quickaccess- --output-on-failure --timeout 120"
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

- `QuickAccessDock` coordinates the document, canvas, gestures, popups and grid
  widgets. Its child controllers own input event filters, while it retains
  document mutations, rebuilding and preview consumers. The extraction
  introduces no additional application event filters or top-level window owners.
- `QuickAccessSettings` is a widget-independent value type; the settings dialog
  keeps `Settings` as an alias. `QuickAccessSettingsStore` takes an injectable
  `KSharedConfigPtr`. Production uses the existing shared config, while tests
  use temporary INI files. It preserves all four KConfig groups, key names,
  color encodings and JSON temporary-brush arrays. Columns remain in `.kqap`,
  not KConfig. Non-dialog keys such as selector shape, gesture threshold and
  Tool Options startup visibility are not overwritten by a settings save.
- The formerly duplicated initial-import and settings-upgrade mappings now use
  `QuickAccessSettingsStore::importLegacy()`. Empty legacy blend-mode lists keep
  the stored value; legacy absent dialog sizes still use 550x650. The raw value
  type's older 340x480 defaults are retained, but palette settings always come
  through the store and its established 550x650 fallback.
- `QuickAccessProfileLoader` performs the original startup sequence: prefer the
  native profile; repair overlapping stroke-preview items; upgrade legacy
  settings only below migration version 2; otherwise import or create defaults.
  Initial migration settings/version are written only after saving the imported
  profile. A corrupt existing profile is not overwritten. Warnings are delivered
  synchronously to the caller, which owns their dialog parent. The pre-existing
  silent behavior for repair-save and optional settings-upgrade failures remains.
- `QuickAccessItemProperties` receives the active grid/document, resolved label,
  and parent widget. It does not save profiles or rebuild widgets; it returns
  whether the dock should do so. Cancellation changes no item/alias values.
  Brush display is set before layout normalization; unchanged brush display
  returns false. Color dialogs remain parented to the property dialog window.
- `QuickAccessItemPresentation` shares legacy/native alias lookup, brush display
  constants and icon resolution. Its source remains in the same directory so
  `__FILE__`-relative browsing of original icon assets is unchanged. Absolute
  existing custom icons take priority over bundled icons with the same basename.
- `kritaquickaccessconfig` is a private static support library linking the core,
  Qt Gui and KConfig; it has no Qt Widgets or `kritaui` dependency. The loadable
  docker and focused tests link it. No new installed DLL or persisted ID is added.
- Grid Edit keeps a copy of the document and commits it only when its caller
  receives Accepted. Each tab still has at most 20 item snapshots; undo clears
  that tab's selection. Copy/move creates new IDs, preserves payloads and appends
  below the target's occupied rows through the existing layout engine. Cross-tab
  moves retain independent source/target undo histories.
- Group movement clamps the whole selection at the top and horizontal bounds,
  then places remaining items in visual order, with ID as the tie-breaker.
  `QuickAccessGridEditOperations::moveGroup()` is the original algorithm; it
  assumes normalized input items that fit their grid.
- The canvas/button split introduces no event filters or new window owners.
  Rebuilds remain zero-delay callbacks with the canvas as context, after mouse
  release returns; destroying the dialog cancels pending rebuilds. The dialog
  still owns one stroke-preview consumer. The canvas handles preview refresh and
  rectangle hit testing against the actual child-button geometries.
- Preserve Grid Edit's existing presentation rules separately from the palette:
  its bundled-icon lookup precedes the absolute-file fallback, and its alias
  lookup does not apply the palette's legacy-category fallback. This refactor
  deliberately retains both behaviors rather than changing existing profiles'
  appearance or which items have resize handles.

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
- Each main window has its own persistent controller. Accept presses only
  when its owning `KisMainWindow` is active; capture that window and its
  `KisViewManager` with `QPointer` for the entire hold. Restore through that
  captured manager, never through `currentMainwindow()` on release.
  Restore on application/window deactivation and controller destruction too.
  The manager needs its own weak pointer: `KisMainWindow` deletes its manager
  before its dockers, while the window's `QPointer` can still be non-null.
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
  `QuickAccessItemDragController::attach()` installs the controller as event filter on every item widget
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
  and saves the profile. The implementation now lives in `QuickAccessProfileLoader`.
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
  overlapping 16 px swatches inside a 24 × 24 px top-left container, the size
  of the Wide Gamut Color Selector's `KisColorSourceToggle` (user request
  2026-10-11; they were 28 px in a 48 × 44 px container).
- Selector shape (user request 2026-10-11): HueSVC sets its own
  `KisColorSelectorConfiguration` with `KisVisualColorSelector::setConfiguration()`,
  which also stops it from following `[advancedColorSelector]
  colorSelectorConfiguration`. Before, HueSVC showed whatever shape the
  Advanced Color Selector used, so a new profile showed Krita's default
  triangle and ring. Two shapes: `slider` (Square + Slider, SV + H, default)
  and `ring` (Square + Ring, SV + H), stored as `[QuickAccessHueSVC]
  SelectorShape`. The `view-choose` header button opens a menu with two
  rendered previews (drawn like `WGSelectorConfigGrid::generateIcon()`); a
  file-local `ShapeNotifier` updates the docker and an open popup together.
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

## Hold-key regression checks (2026-10-15)

Build `QuickAdjustKeyControllerTest` and run it alongside `QuickAccessCoreTest`.
It creates two main windows and checks eraser and alpha-lock holds, release in
another window, application deactivation, docker destruction and main-window
destruction. Use Qt's offscreen platform for automated runs.
The test uses `KisApplication`: `KisInputManager` assumes that application type
when querying extended modifiers. Initialize the test OpenGL surface format
before constructing it; otherwise main-window creation opens an internal-error
dialog and an offscreen run waits indefinitely. It supplies clipboard MIME data for the
offscreen platform and an independent preset per window (eraser mode is a
derived preset resource). It disables installed controllers' event filters
before adding the compiled test controllers, so each window has only one.

For interactive verification, open two windows and repeat holds for eraser,
alpha lock, selection and temporary brushes. Check ordinary release, switching
windows, Alt+Tab and closing a window while holding a key. Confirm restoration
in the original window, and confirm editable fields do not activate holds.

## Responsibility-separation regression checks

`QuickAccessConfigTest` uses temporary INI/profile/legacy files to check defaults,
four-group migration and round trips, unrelated-key preservation, corrupt JSON,
initial versus existing-profile migration, migration idempotence, save failure,
and historical stroke-preview overlap repair. `QuickAccessItemPropertiesTest`
checks accept/cancel for all eight non-brush item kinds, legacy aliases and
unchanged action/docker IDs, brush display/one-column layout/neighbour pushing,
custom/bundled icon precedence, and the settings-dialog value round trip.
Neither focused test loads the real user's profile. Existing core and hold-key
tests remain part of the regression run.

Validation on 2026-10-16: the docker and all four test targets built; all four
CTest suites passed offscreen (11.74 seconds). The docker was installed into
the development test installation, and its SHA-256 matched the build output.
The user confirmed the refactored Quick Access works correctly on 2026-10-16.
The checklist below remains the regression checklist for future changes.

After installation and a complete restart, verify:

1. Existing palette tabs, appearance, gesture settings, HueSVC and Quick Adjust
   values load, and changes survive restart.
2. Edit and cancel each item kind, including aliases, colors, a custom absolute
   icon path and a script path. Color-picker windows retain normal colors.
3. Change brush Icon/Stroke Preview in both the docker and palette popup; confirm
   neighbour movement and Grid Edit agree, including a one-column grid.
4. Ctrl-drag an item, cancel with another button, then use ordinary clicks.
   Resources-dialog additions still fill sequentially; header additions start
   a new row.
5. Toggle popup shortcuts and pin/unpin behavior, then repeat the multi-window
   hold-key checks above, including focus loss and editable text fields.

## Grid Edit regression checks (2026-10-16)

`QuickAccessGridEditTest` uses temporary configuration/resources, never the user's
profile. It tests group clamping and collision placement, Ctrl multi-selection,
rectangle selection, drag previews without document mutation, release followed by
deferred rebuilding, horizontal and vertical resizing, icon-item movement without
resizing, remove/copy/move menus, independent tab undo, the 20-entry history limit,
Save/Cancel, payload/alias preservation and destruction with a rebuild pending.
The original 24 widget method bodies were compared ignoring formatting and are
unchanged; all literal tokens in the editor are preserved. The two child-widget
traversals now live in the canvas; selection results and preview delivery retain
the same semantics. No serialized fields, profile version, settings keys or
action IDs changed.

Validation: the plugin and all five test targets built. All five Quick Access
CTest suites passed offscreen (19.99 seconds), including the new Grid Edit suite
(11 QtTest cases including setup/cleanup). The plugin was installed and its
build/install SHA-256 hashes matched. Logs under `%TEMP%`: `solstice-grid-edit-`
`build.log`, `test-build.log`, `ctest.log`, and `install.log`.

The user confirmed the Grid Edit split works correctly on 2026-10-16. For future
changes, check an existing profile: Grid Edit drag/group selection, label/separator resize,
tab copy/move and Undo, Save versus Cancel, and brush stroke/icon previews. Repeat
in a second window and close the editor after dragging; palette popup and ordinary
action activation should retain their existing behavior.

## Quick Adjust ownership and history (2026-10-16)

`QuickAdjustDock` explicitly deletes its `QuickAdjustToolOptionsPad` in its
destructor body, before its members and child controls are destroyed. The pad
is also a QObject child of the dock. Its application-state connection and queued
geometry callbacks use the pad controller as context, and its borrowed/source/pad
widgets remain guarded by QPointer. Restoration occurs at the same teardown point
as before: reparent the borrowed content to `sharedtooldocker`, enable its toggle
action, restore its previous visibility, and defer deletion of the placeholder
and floating pad. Merely hiding the Quick Adjust dock hides the pad without
returning its content or changing the saved startup preference.

The dock supplies a synchronous context callback for the current canvas/window
and Tool Options mode. The pad never retains a view manager. Losing or switching
canvases does not release an already borrowed widget; maximum-size calculation
uses the current window or the existing screen fallback. Keep the original
compact-popup exclusion, hidden-toggle check, fixed 5-pixel gap, position strings,
screen clamping and scroll-area size calculation. Startup visibility is still
written even when no pad can be shown, including compact instances. Tests inject
an independent KSharedConfig; production retains the existing shared config.

The nine extracted history methods are unchanged. The session-wide color reset
stays in `setCanvas()`, colors are still added only on actual use and ignored in
eraser mode, and brush history is deduplicated by preset name and truncated to
the configured button count. `unsetCanvas()` clears color history but retains
brush history. Pressure toggle updates, provider connections, native hold-key
controller ownership and the 200 ms synchronization timer remain with the dock.
No settings keys, defaults, action IDs or profile fields changed.

`QuickAdjustDockTest` compiles the current dock, history, pad and key-controller
sources. It uses a temporary Solstice profile and temporary INI files. It checks
borrowing/restoring initially visible and hidden dockers, application-state
visibility, compact/hidden/no-canvas/toolbar/missing-docker guards, saved visibility
and unrelated-key preservation, content growth/shrink and three pad positions,
canvas loss, source/owner destruction, pending callbacks, brush-history order and
limit, and normal versus compact controls and key-controller counts.

Interactive checks after restarting Solstice: enable the Tool Options pad and
try all three positions, switch tools and documents, hide/show Quick Adjust,
switch to another application and back, then close the window. Check color and
brush histories and the compact HueSVC layout, and repeat temporary hold keys in
two windows. The user confirmed that Quick Brush Adjustments works correctly
after this refactor on 2026-10-16; keep these checks for future changes.

Validation on 2026-10-16: the plugin and all six test targets built; the new
Quick Adjust suite passed all 12 QtTest cases (including setup/cleanup). All six
Quick Access CTest suites passed offscreen (24.12 seconds). The plugin was
installed, and its build/install DLL SHA-256 hashes matched. The nine moved
history methods and all original string literals were checked against the prior
implementation. Logs under `%TEMP%`: `solstice-quick-adjust-build.log`,
`solstice-quick-adjust-test.txt`, `solstice-quick-adjust-ctest.log` and
`solstice-quick-adjust-install.log`.

## Palette and gesture separation (2026-10-16)

`QuickAccessDockWidgets.cpp` and `QuickAccessDockPopups.cpp` implement existing
dock methods; they add no owners, retained view managers or independent document
copies. Item rendering retains the palette-specific aliases, fixed cell spacing,
icon precedence, stroke-preview cache consumer, contextual menus and click
connections. Popup flags, shortcuts, pinning, pointer-leave handling (including
HueSVC combo-box popups) and header drag offset are unchanged.

`QuickAccessItemDragController` installs filters only on the palette's item
widgets. It obtains the active grid and cell size synchronously from the dock,
retains only the item ID/press position/weak highlight pointer, and invokes the
dock's move callback on a zero-delay timer. The dock still applies the layout
engine, saves and rebuilds. This preserves the original active-grid lookup at
delivery time and prevents deletion of the receiving widget inside mouse release.
The dock explicitly deletes the controller before its document members are
destroyed, cancelling queued moves. Plain clicks, Ctrl presses, double-clicks,
other-button cancellation and row/column clamping retain their previous rules.

`QuickAccessGestureController` remains the persistent dock's application event
filter. Settings reload, text/modal focus exclusions, key matching, auto-repeat
handling, threshold clamping, direction boundaries and execution payload mapping
are unchanged. The controller owns and deletes `QuickAccessGesturePreview`;
queued repositioning uses that preview as context. The dialog edits its own page
copies and only its caller commits accepted settings. Presentation is shared by
the dialog and overlay without adopting the palette's different legacy alias
fallback rules. This is a responsibility split, not a change to gesture focus,
multi-window routing or cancellation policy.

`QuickAccessInteractionTest` compiles the plugin's production sources except
factory registration, with the same resources and Python build option. It uses
an isolated Solstice profile and temporary KConfig. It exercises actual palette
and popup widgets, deferred Ctrl-drag save/rebuild, collision movement, unchanged
payloads/aliases/gestures, plain clicks, other-button cancellation, span clamping,
pending-callback destruction, popup pin flags/position/header dragging, all eight
gesture directions and the center/threshold boundary, F-key and repeat handling,
disabled/text/modal guards, controller teardown, preview geometry/reuse/teardown,
and settings page edits and Save/Cancel isolation.

For interactive verification after restarting Solstice, use an existing profile:
Ctrl-drag items in the docker and popup, cancel with another button, use ordinary
clicks and context menus, pin/unpin and drag the popup, and open both HueSVC blend
mode lists. Test configured gesture keys and all directions with preview on/off;
edit/cancel/save gesture pages and restart to check persistence. Repeat with two
windows. Automated offscreen checks do not establish native focus or tablet
behavior. The user confirmed this split works correctly on 2026-10-16; retain
these interactive checks for future changes.

Validation: the plugin and all seven test targets built. All seven Quick Access
CTest suites passed offscreen (34.04 seconds); the new integration suite passed
18 QtTest cases including setup/cleanup. The plugin was installed and build/install
SHA-256 hashes matched (`141b8259f324aa081c4dea42f9cb6145de15ed03eb59ff742eb95c3551c001cb`).
Comparison with the pre-split working copy confirmed 46 dock methods and all nine
gesture-dialog methods unchanged, item construction unchanged except drag-filter
attachment, and the drag algorithm unchanged except its callback boundaries.
The original string-literal multiset is preserved across the extracted sources.
Logs under `%TEMP%`: `solstice-palette-gesture-build.log`, `test-build.log`,
`test.txt`, `ctest.log` and `install.log` (all with the same prefix).

The regression assertions retain two existing Qt details: the original pin/unpin
flag sequence leaves window type `Window` after unpinning because `Tool` and
`Popup` share flag bits; preview placement uses the layout's size hint before
showing, which can differ from the final frame size. Neither behavior was changed.
