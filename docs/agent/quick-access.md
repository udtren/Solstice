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

- Profile directory: `%APPDATA%\krita\quickaccess\`
- Default profile: `%APPDATA%\krita\quickaccess\default.kqap`
- Deleting only `default.kqap` does not reset every setting. Appearance,
  gesture, HueSVC, and Quick Adjust values also live in KConfig groups in
  `%LOCALAPPDATA%\kritarc`.
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
- Actions are stored and executed by internal action ID. UI labels use Krita's
  displayed action text or a configured custom name.
- Item Property supports custom name, colors, font size, and an icon selected
  through the native OS file dialog.
- Header button background and font colors are independently configurable.
  Parent color dialogs to the containing window, not a styled swatch button, to
  prevent the swatch stylesheet from leaking into the dialog.
- The Resource dialog uses editable tables for Actions and Dockers and a
  thumbnail grid for Brushes.
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
  mode, rotation dial/value/reset, layer opacity, layer blend mode, and the
  2×2 pressure toggles. Do not reuse the standalone docker's two-column
  brush/layer layout there, and do not add its status-button strip or separator
  frames to the popup.
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
