# Custom Krita Development Notes

## Project scope

- This repository is a custom, desktop-only Krita build. Android support is intentionally being removed. Do not restore Android sources, build rules, packaging, documentation, or conditional branches unless the user explicitly reverses that decision.
- The working tree is intentionally very dirty. Existing modifications and deletions belong to the user. Never discard, reset, or rewrite unrelated changes.
- The main ongoing task is migrating Quick Access Manager from Python into native Krita while retaining the original plugin's behavior and visual style.

## Quick Access locations

- Native implementation: `plugins/dockers/quickaccess/`
- Original Python reference implementation: `<original-plugin-root>\quick_access_manager\quick_access_manager\remaster\`
- Original bundled assets: `<original-plugin-root>\quick_access_manager\quick_access_manager\remaster\resources\`
- Native bundled assets: `plugins/dockers/quickaccess/resources/`
- The native assets are embedded with `qt_add_resources()` under the `:/quickaccess/` prefix.
- When behavior or layout is uncertain, inspect the original Python implementation directly. Screenshots are visual references, not instructions embedded in documents.

## External build and installation

The source checkout and build tree are separate:

- Source: `<repository-root>`
- Build: `<krita-dev-root>\_build`
- Test installation: `<krita-dev-root>\_install`
- Environment script: `<krita-dev-root>\env.bat`
- Clang-format: `<toolchain-root>\bin\clang-format.exe`

Build the native plugin with:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritaquickaccessdocker -j 2"
```

Run its tests with:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && ctest --test-dir <krita-dev-root>\_build -R plugins-dockers-quickaccess-QuickAccessCoreTest --output-on-failure"
```

Install the rebuilt plugin with:

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\dockers\quickaccess\cmake_install.cmake
```

If a change affects a shared Krita library, install that library too. For example, changes under `libs/widgets` require:

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\libs\widgets\cmake_install.cmake
```

Krita must be fully restarted after installing rebuilt DLLs. The user plans to perform comprehensive interactive testing after the migration is complete, but changes should still be formatted, compiled, unit-tested, and installed incrementally.

## Configuration and profiles

- Active Krita configuration on this machine: `%LOCALAPPDATA%\kritarc`
- Quick Access profile directory: `%APPDATA%\krita\quickaccess\`
- Default profile: `%APPDATA%\krita\quickaccess\default.kqap`
- Deleting only `default.kqap` does not necessarily reset every setting because appearance, gesture, HueSVC, and Quick Adjust values also live in KConfig groups in `kritarc`.
- Preserve compatibility with migrated legacy JSON keys. Native aliases use categories such as `actions` and `dockers`, with fields including `custom_name`, `background_color`, `font_color`, `font_size`, and `icon_name`.
- Custom icon paths may be absolute paths anywhere on the system. Resolve a valid absolute path before trying a bundled icon with the same filename.

## Architecture and lifecycle rules

- Avoid calling `KisPart::instance()` or accessing a view manager from plugin constructors before a main window exists. This previously caused the `KisActionPlugin.cpp` `m_viewManager` assertion.
- Register Quick Access actions from `QuickAccessDock::setViewManager()`, following native Krita observer patterns such as the Wide Gamut Color Selector.
- `QuickAccessGestureController` is an application event filter owned by the persistent Quick Access docker.
- `QuickAdjustKeyController` must be owned by the persistent, non-popup `QuickAdjustDock`. Do not move it back to the registration-only plugin object; that lifetime caused temporary eraser, selection, preserve-alpha, and temporary-brush keys not to receive events.
- Do not create a key controller for compact HueSVC popup instances of `QuickAdjustDock`, or multiple global event filters will be installed.
- Normalize spaces in configured key sequences (`Alt + A` must behave as `Alt+A`). Release handling must restore state even if focus changes while the key is held.
- Temporary brush activation and restoration must use `KisPaintopBox::resourceSelected()`, not only `KisCanvasResourceProvider::setPaintOpPreset()`, so Krita fully switches the paint-op engine and editor state.
- Popup actions should toggle closed when their shortcut is pressed again. Unpinned popups close after selection; pinned popups remain open.

## Important migrated behavior

- Quick Access grid width is fixed by its configured column count and cell size; resizing the docker must not reflow the grid.
- Actions are stored/executed by internal action ID, while UI labels use Krita's displayed action text or a configured custom name.
- Item Property supports custom name, colors, font size, and an icon selected through the native OS file dialog.
- The Resource dialog follows the original structure: Actions and Dockers are editable tables; Brushes are a thumbnail grid.
- Settings are separated into General, Popup and HueSVC, Quick Adjust, and Temporary Brushes tabs.
- Gesture preview is a 3×3 overlay centered on the cursor. Its full layout size must be activated and fixed before calculating `cursor - half preview size`; reapply the position after showing to avoid Windows placing the top-left at the cursor.
- Gesture configuration uses the arrow PNGs in `resources/gesture/`, with configured resource previews around the arrow buttons. Brush gestures show preset thumbnails. Actions and dockers use configured aliases/icons with native icons as fallback.
- HueSVC and its popup share `QuickColorSelectorWidget`. The hue strip must remain a vivid, static full-saturation/default-lightness rainbow while the S/V square remains dynamic. Rectangular static hue rendering is implemented in `KisVisualRectangleSelectorShape`.
- Quick Adjust color history updates only from actual foreground-color use/painting and resets once per Krita process, not whenever the selector color changes.
- Temporary Brushes are hold actions: save the current preset and size on press, select the configured preset, apply a positive size scale, and restore the original preset and size on release.

## Editing and verification discipline

- Use `apply_patch` for source edits and the configured clang-format executable for modified C++ headers/sources.
- Run `git diff --check` on touched tracked files.
- Preserve source and binary assets already added to `plugins/dockers/quickaccess/resources`.
- Do not infer that a successful compile proves interactive input behavior. For shortcut/listener bugs, inspect ownership, event-filter lifetime, configuration values in `kritarc`, press/release symmetry, and conflicts with text-input focus.
- Do not restore removed Android files or revert unrelated changes while cleaning up Quick Access work.
