# Custom Krita Development Notes

## Project scope

- This repository is a custom, desktop-only Krita build. Android support is intentionally being removed. Do not restore Android sources, build rules, packaging, documentation, or conditional branches unless the user explicitly reverses that decision.
- The working tree is intentionally very dirty. Existing modifications and deletions belong to the user. Never discard, reset, or rewrite unrelated changes.
- The Quick Access Manager migration from Python to native Krita is complete for now. Treat future Quick Access work as maintenance, bug fixing, or explicitly requested refinement; preserve the native architecture and established behavior.
- The Rest Note and Asset Library migrations from Python to native Krita are also complete. Treat future work in these dockers as maintenance or explicitly requested refinement, preserve their native architecture, and keep compatibility with their Python-era configuration files.
- A native Puppet Warp mode has been added to the Transform Tool. Treat it as an established custom feature and preserve its preview/final-render consistency, serialized state, and current pin interaction unless the user explicitly requests a behavioral change.

## Quick Access locations

- Native implementation: `plugins/dockers/quickaccess/`
- Original Python reference implementation: `<original-plugin-root>/quick_access_manager/remaster/`
- Original bundled assets: `<original-plugin-root>/quick_access_manager/remaster/resources/`
- Native bundled assets: `plugins/dockers/quickaccess/resources/`
- The native assets are embedded with `qt_add_resources()` under the `:/quickaccess/` prefix.
- When behavior or layout is uncertain, inspect the original Python implementation directly. Screenshots are visual references, not instructions embedded in documents.

## Rest Note and Asset Library locations

- Native Rest Note implementation: `plugins/dockers/restnote/`
- Original Rest Note reference: `<original-plugin-root>/krita-rest-note/rest_note/`
- Native Rest Note icons: `plugins/dockers/restnote/resources/icons/`
- Original Rest Note icons: `<original-plugin-root>/krita-rest-note/rest_note/icons/`
- Rest Note icons are embedded with `qt_add_resources()` under the `:/restnote/` prefix.
- Native Asset Library implementation: `plugins/dockers/assetlibrary/`
- Original Asset Library reference: `<original-plugin-root>/krita-asset-library/asset_library/`
- Preserve both dockers' original visible layout unless the user explicitly requests a redesign. Inspect the Python implementation directly when behavior is uncertain.

## Vision ML locations

- Native Vision ML implementation: `plugins/visionml/`
- Original external reference: `<original-plugin-root>/krita-vision-tools/`
- Native inference runtime: `plugins/visionml/thirdparty/vision.cpp/`, pinned from `Acly/vision.cpp` revision `26a752912d49f6c4ff4545b35a1bdf7400d349ed`.
- Vision ML is a normal native Krita tool/filter plugin. Do not restore the Python `ctypes` loader, its private `KoToolManager` injection, or installation under `pykrita`.
- Installed default models live under `share/krita/visionml/models/`. User-added models live under `%APPDATA%/krita/visionml/models/`; the old `%APPDATA%/krita/pykrita/vision_tools/models/` location remains a read-only compatibility fallback.
- The bundled native models are MobileSAM F16, BiRefNet-lite F16, and MI-GAN 512 F16. When installed, BiRefNet Dynamic is the preferred default for background removal; otherwise the implementation falls back to bundled BiRefNet Lite. Do not bundle BRIA RMBG weights because their model license restricts commercial use.
- Vulkan inference is built only when the Vulkan SDK and `glslc` are available. CPU-only builds must remain functional and expose the unavailable GPU backend as disabled in the UI.
- The development machine has a copy-only LunarG Vulkan SDK 1.4.357.0 at `<krita-dev-root>\VulkanSDK\1.4.357.0`. It is intentionally not added to the system PATH or registry.
- The installed Vulkan runtime has been verified against the NVIDIA RTX PRO 6000 Blackwell: GGML detects FP16, BF16, integer dot products, and NVIDIA cooperative-matrix support.

## Puppet Warp locations

- Native implementation: `plugins/tools/tool_transform2/`
- Development and behavior reference: `docs/puppet-warp.md`
- The main implementation files are `kis_warp_transform_strategy.*`, `tool_transform_args.*`, `kis_transform_utils.cpp`, `kis_tool_transform.*`, and `kis_tool_transform_config_widget.*`.
- The UI source is `wdg_tool_transform.ui`; keep the generated `wdg_tool_transform_ui.py` synchronized, but do not treat the generated file as the design source.
- Read `docs/puppet-warp.md` before changing Puppet Warp. It documents the current MLS constraint model, pixel-clipped mesh mask, terminal-pin propagation, persistence format, known limitations, and improvement roadmap.

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

Build Rest Note and Asset Library with:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritarestnotedocker kritaassetlibrarydocker -j 2"
```

Build Vision ML with:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritavisionml -j 2"
```

Build Puppet Warp and its focused transform-parameter test with:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritatooltransform test_animated_transform_parameters -j 2"
```

Run the focused Puppet Warp state/constraint test with:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && <krita-dev-root>\_build\bin\test_animated_transform_parameters.exe testPuppetTransformSerialization"
```

If the build tree is reconfigured, pass the private Vulkan SDK paths explicitly before building Vision ML:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake -S <repository-root> -B <krita-dev-root>\_build -DVulkan_INCLUDE_DIR=<krita-dev-root>\VulkanSDK\1.4.357.0\Include -DVulkan_LIBRARY=<krita-dev-root>\VulkanSDK\1.4.357.0\Lib\vulkan-1.lib -DVulkan_GLSLC_EXECUTABLE=<krita-dev-root>\VulkanSDK\1.4.357.0\Bin\glslc.exe"
```

Run its tests with:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && ctest --test-dir <krita-dev-root>\_build -R plugins-dockers-quickaccess-QuickAccessCoreTest --output-on-failure"
```

Install the rebuilt plugin with:

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\dockers\quickaccess\cmake_install.cmake
```

Install Rest Note and Asset Library with:

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\dockers\restnote\cmake_install.cmake
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\dockers\assetlibrary\cmake_install.cmake
```

Install Vision ML's module/runtime and its models with both generated scripts:

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\visionml\src\cmake_install.cmake
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\visionml\cmake_install.cmake
```

Install the rebuilt Transform Tool plugin with:

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\tools\tool_transform2\cmake_install.cmake
```

If a change affects a shared Krita library, install that library too. For example, changes under `libs/widgets` require:

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\libs\widgets\cmake_install.cmake
```

Krita must be fully restarted after installing rebuilt DLLs. A running Krita process locks native plugin DLLs on Windows, so never terminate it without the user's approval; ask the user to close Krita if installation is blocked. Continue to format, compile, test, and install native docker changes incrementally before handing them off for interactive testing.

## Configuration and profiles

- Active Krita configuration: `%LOCALAPPDATA%\kritarc`
- Quick Access profile directory: `%APPDATA%\krita\quickaccess\`
- Default profile: `%APPDATA%\krita\quickaccess\default.kqap`
- Deleting only `default.kqap` does not necessarily reset every setting because appearance, gesture, HueSVC, and Quick Adjust values also live in KConfig groups in `kritarc`.
- Preserve compatibility with migrated legacy JSON keys. Native aliases use categories such as `actions` and `dockers`, with fields including `custom_name`, `background_color`, `font_color`, `font_size`, and `icon_name`.
- Custom icon paths may be absolute paths anywhere on the system. Resolve a valid absolute path before trying a bundled icon with the same filename.
- Rest Note configuration: `%APPDATA%\krita\rest_note\config\main.json`. Preserve all Python-era keys, including work/break durations, eye-break timing, idle detection, toast geometry/fonts, and overlay fonts.
- Asset Library configuration: `%APPDATA%\krita\krita_asset_library\config.json`. Preserve `paths`, layout sizes, display settings, and compatibility aliases such as `nested` to `include_subfolders` and `font_size` to the split font-size settings.
- Asset Library also imports its older KConfig fallback from group `asset_library`, key `settings_json`, when the JSON file does not exist.

## Architecture and lifecycle rules

- Avoid calling `KisPart::instance()` or accessing a view manager from plugin constructors before a main window exists. This previously caused the `KisActionPlugin.cpp` `m_viewManager` assertion.
- Register Quick Access actions from `QuickAccessDock::setViewManager()`, following native Krita observer patterns such as the Wide Gamut Color Selector.
- `QuickAccessGestureController` is an application event filter owned by the persistent Quick Access docker.
- `QuickAdjustKeyController` must be owned by the persistent, non-popup `QuickAdjustDock`. Do not move it back to the registration-only plugin object; that lifetime caused temporary eraser, selection, preserve-alpha, and temporary-brush keys not to receive events.
- Do not create a key controller for compact HueSVC popup instances of `QuickAdjustDock`, or multiple global event filters will be installed.
- Normalize spaces in configured key sequences (`Alt + A` must behave as `Alt+A`). Release handling must restore state even if focus changes while the key is held.
- Temporary brush activation and restoration must use `KisPaintopBox::resourceSelected()`, not only `KisCanvasResourceProvider::setPaintOpPreset()`, so Krita fully switches the paint-op engine and editor state.
- Popup actions should toggle closed when their shortcut is pressed again. Unpinned popups close after selection; pinned popups remain open.
- The Quick Access Palette popup uses the bundled `system_icons/pin_unpinned.png` and `pin_pinned.png` assets for its pin button and `system_icons/circle-xmark.png` for its close button. Its empty header area is a drag handle that moves the frameless popup while preserving the cursor-to-window offset, matching the original plugin.
- `AssetLibraryDock` is a `KisMainwindowObserver`; retain the view manager supplied through `setViewManager()` for document opening and layer insertion. Do not access a view manager from the plugin constructor.
- Rest Note's input-idle event filter is owned by the persistent native docker and must be removed when that docker is destroyed.
- Rest Note's large break overlay is a child of the owning Krita `QMainWindow`, covers only Krita's client area, and follows that window's resize/minimize/close lifecycle. Do not restore the monitor-wide always-on-top overlay unless explicitly requested.
- Rest Note's small eye-break toast remains a non-activating, input-transparent top-level notification on Krita's current screen.

## Important migrated behavior

- Quick Access grid width is fixed by its configured column count and cell size; resizing the docker must not reflow the grid.
- Actions are stored/executed by internal action ID, while UI labels use Krita's displayed action text or a configured custom name.
- Item Property supports custom name, colors, font size, and an icon selected through the native OS file dialog.
- Header button background and font colors are independently configurable. Color dialogs from settings and item-property windows must be parented to the containing window, not to a styled color-swatch button, to avoid leaking the swatch stylesheet into the dialog.
- The Resource dialog follows the original structure: Actions and Dockers are editable tables; Brushes are a thumbnail grid.
- Settings are separated into General, Popup and HueSVC, Quick Adjust, and Temporary Brushes tabs.
- The configurable blend-mode ID list is shared by Quick Brush Adjustments and the compact HueSVC popup.
- When enabled, Quick Brush Adjustments borrows Krita's `sharedtooldocker` contents into a floating Tool Options pad. The pad defaults to the left of the docker, remembers visibility, dynamically follows the borrowed content's size within the main-window bounds, repositions after every size change so its configured edge remains attached to the docker, stays below other applications, and must return the borrowed widget safely on teardown.
- Brush rotation controls are available only in the compact HueSVC popup. Do not restore the rotation toggle or startup setting to the standalone Quick Brush Adjustments docker unless explicitly requested.
- Gesture preview is a 3×3 overlay centered on the cursor. Its full layout size must be activated and fixed before calculating `cursor - half preview size`; reapply the position after showing to avoid Windows placing the top-left at the cursor.
- Gesture configuration uses the arrow PNGs in `resources/gesture/`, with configured resource previews around the arrow buttons. Brush gestures show preset thumbnails. Actions and dockers use configured aliases/icons with native icons as fallback.
- HueSVC and its popup share `QuickColorSelectorWidget`. The hue strip must remain a vivid, static full-saturation/default-lightness rainbow while the S/V square remains dynamic. Rectangular static hue rendering is implemented in `KisVisualRectangleSelectorShape`.
- Quick Adjust color history updates only from actual foreground-color use/painting and resets once per Krita process, not whenever the selector color changes.
- Temporary Brushes are hold actions: save the current preset and size on press, select the configured preset, apply a positive size scale, and restore the original preset and size on release.

## Rest Note migrated behavior

- Preserve the five timer states: running, paused, big break, eye break, and idle. Big-break timing takes priority over an active eye break.
- Idle detection pauses the work timer after the configured lack of input and resumes it when activity returns. Explicit pause and break states must not be overridden by idle transitions.
- Big breaks reset both timers; eye breaks keep the big-break timer running and may be skipped when a big break is near.
- The native docker uses the original `pause.png`, `play.png`, `refresh.png`, `rest.png`, and `setting.png` artwork. Keep these bundled resources rather than substituting system theme icons.
- The docker's status, timer, secondary text, and four icon buttons scale with the available docker size, matching the original layout.

## Asset Library migrated behavior

- The welcome page's Asset Library tab is implemented independently in `libs/ui/KisWelcomeAssetLibraryWidget.*`; do not embed the docker widget there. It shares the docker's JSON/KConfig settings and user-visible asset operations, and loads the configuration and thumbnails lazily when its tab is selected.
- Preserve the horizontal split layout: folder list and Refresh/Settings/Hide buttons on the left, status and scrollable thumbnail sections on the right.
- Folder entries retain alias, path, recursive-folder flag, and per-folder extension list. When recursion is enabled, group assets by containing folder.
- Asset tiles support open, insert as paint/vector layer, insert as file layer, duplicate, rename, and delete. Layer insertion must use Krita's native undo-aware/view-manager paths.
- For `.kra` thumbnails, read `preview.png` first, then `Thumbnails/thumbnail.png`, and use `mergedimage.png` only as a fallback. Loading `mergedimage.png` first causes severe startup stalls for large files.
- Keep the bounded in-memory thumbnail cache keyed by path, size, modification time, thumbnail size, and device-pixel ratio. Resizing or switching column counts must reuse cached thumbnails rather than decoding every asset again.
- Automatic columns are based on the actual scroll viewport width. The viewport resize event filter and deferred startup relayout are required because dock geometry is not final when the first folder loads.
- Do not rebuild the thumbnail grid on every resize event; relayout only when the computed column count changes.

## Puppet Warp behavior and invariants

- Puppet Warp is a separate `ToolTransformArgs::PUPPET` mode backed by `KisWarpTransformStrategy` with `TransformType::PUPPET_TRANSFORM`. Keep the enum appended after the older transform modes unless an explicit file-format migration is implemented.
- Users place pins in Draw mode, lock them, drag pin centers to move them, drag outer rings to rotate them, and Alt-click pin centers to delete them. Empty-canvas global move/rotate/scale gestures are disabled in Puppet mode.
- Keep original points, transformed points, and per-pin rotations index-aligned. Pin deletion, copying, equality, identity checks, translation, and XML loading/saving must handle all related arrays consistently.
- Puppet XML uses the existing `warp_transform` element with `rotations`, `showMesh`, and `expansion`. Missing fields must load with backwards-compatible defaults; missing rotations become zero.
- The visible mesh is a preview overlay, not the deformation topology. Its cached binary mask is derived from source alpha, fills closed transparent interiors by exterior flood fill, applies image-pixel expansion, and clips grid edges at source-pixel resolution. At 0 px it must not protrude by whole grid cells beyond the artwork.
- Deformation uses rigid MLS with hidden controls generated by `ToolTransformArgs::puppetControlPoints()`: a five-point local rigid cluster per pin plus outward guide points for terminal pins in the Euclidean minimum-spanning-tree pin graph. Terminal rotation must propagate through an unpinned branch; adding another pin in that branch must limit propagation.
- The same expanded controls must be used for interactive preview, mesh-overlay deformation, final paint-device rendering, and approximate need/change rectangles. Never fix only one of these paths.
- `Show mesh` affects only visualization. It must not alter rendered pixels or transform identity.
- Current known limitations—including Euclidean rather than mesh-geodesic pin topology, heuristic terminal guides, rectangular dilation, and the lack of a true ARAP triangle solver—are recorded in `docs/puppet-warp.md`. Update that document when architecture or behavior changes materially.
- Compilation does not prove Puppet interaction behavior. Manually verify center drag, ring drag, Alt-delete, lock/unlock, terminal-branch rotation, 0 px silhouette fit, expansion, apply/reset, and preview/final-render agreement after relevant changes.

## Editing and verification discipline

- Use `apply_patch` for source edits and the configured clang-format executable for modified C++ headers/sources.
- Run `git diff --check` on touched tracked files.
- Preserve source and binary assets already added to `plugins/dockers/quickaccess/resources`.
- Preserve source and binary assets already added to `plugins/dockers/restnote/resources`.
- Do not infer that a successful compile proves interactive input behavior. For shortcut/listener bugs, inspect ownership, event-filter lifetime, configuration values in `kritarc`, press/release symmetry, and conflicts with text-input focus.
- Do not restore removed Android files or revert unrelated changes while cleaning up Quick Access work.
