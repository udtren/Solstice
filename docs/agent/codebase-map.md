# Codebase map — agent reference

Use this document to decide *where* a change belongs before editing. It covers
the repository layout, the library layering, and the most important entry-point
classes. Extension recipes are in `docs/agent/extension-points.md`; build and
verification rules are in `docs/agent/development-workflow.md`.

## Top-level layout

| Path | Contents | Touch when |
| --- | --- | --- |
| `libs/` | Shared libraries (`kritaglobal` … `kritaui`, `kritalibkis`). Every plugin links against these. | A change must be visible to several plugins, or it modifies core application/window/document behavior. |
| `plugins/` | Loadable modules (`MODULE` targets): dockers, tools, filters, paintops, file import/export, extensions, generators, color, Vision ML. | Almost all new features. Prefer a plugin over editing `libs/`. |
| `krita/` | Application executable (`main.cc`), global action definitions (`krita.action`, `kritamenu.action`), main menu layout (`krita5.xmlgui`), bundled data (`data/`), icons and branding (`pics/`). | New main-menu actions, default resources, icons, branding. |
| `docs/` | User documents (`docs/<feature>.md`) and agent documents (`docs/agent/`). | Every feature change (see `AGENTS.md`). |
| `sdk/tests/` | Shared test utilities (`kritatestsdk`, `testutil.h`, stubs). | Writing unit tests. |
| `benchmarks/` | Upstream performance benchmarks. | Rarely. |
| `cmake/modules/` | CMake helpers (`KritaAddBrokenUnitTest.cmake` defines `kis_add_test(s)`). | Rarely. |
| `3rdparty*`, `plugins/visionml/thirdparty/` | Vendored or dependency sources. | Only for deliberate dependency updates. Never reformat. |
| `packaging/` | Platform packaging (desktop only; Android packaging removed). | Packaging/branding work only. |
| `po/` | Translations (generated upstream). | Never edit by hand. |
| `pch/` | Precompiled headers selected by `kis_add_library`. | Never for feature work. |
| `winquirks/` | MSVC compatibility headers. | Never for feature work. |

## Library layering (`libs/`)

Libraries are built in the order listed in `libs/CMakeLists.txt`. A library may
depend only on libraries built before it. Do not introduce an upward dependency
(for example, `kritaimage` must never include `kritaui` headers).

| Directory | CMake target | Responsibility | Key headers |
| --- | --- | --- | --- |
| `libs/version` | `kritaversion` | Version strings | `KritaVersionWrapper.h` |
| `libs/global` | `kritaglobal` | Assertions, debug categories, shared pointers, math/util helpers | `kis_assert.h`, `kis_debug.h`, `kis_shared_ptr.h` |
| `libs/koplugin` | `kritaplugin` | Plugin loading from JSON metadata | `KoPluginLoader.h` |
| `libs/widgetutils` | `kritawidgetutils` | Icons, action registry, config helpers, small widget utilities | `kis_icon_utils.h`, `kis_action_registry.h`, `KoIcon.h`, `kis_slider_spin_box.h` |
| `libs/widgets` | `kritawidgets` | Generic reusable widgets (color selectors, dock title bars, choosers) | `KoDockWidgetTitleBar.h` |
| `libs/store` | `kritastore` | Zip/directory storage for `.kra` | `KoStore.h` |
| `libs/flake` | `kritaflake` | Vector shapes, canvas abstraction, tool/dock registries | `KoToolRegistry.h`, `KoToolFactoryBase.h`, `KoDockRegistry.h`, `KoDockFactoryBase.h`, `KoCanvasObserverBase.h`, `KoCanvasBase.h` |
| `libs/pigment` | `kritapigment` | Color spaces, color conversion, composite ops | `KoColor.h`, `KoColorSpace.h`, `KoColorSpaceRegistry.h` |
| `libs/command` | `kritacommand` | Undo command base (`KUndo2Command`) | `kundo2command.h` |
| `libs/brush` | `kritalibbrush` | Brush tips | `kis_brush.h` |
| `libs/image` | `kritaimage` | Image model: nodes, layers, masks, paint devices, strokes, filters, generators, processing, undo adapters, tiles | `kis_image.h`, `kis_node.h`, `kis_paint_device.h`, `kis_processing_applicator.h`, `kis_stroke_strategy.h`, `filter/kis_filter.h`, `filter/kis_filter_registry.h`, `brushengine/` |
| `libs/ui` | `kritaui` | Application, main window, documents, views, canvas, tools base, actions, dialogs, preferences, input handling | see below |
| `libs/impex` | `kritaimpex` | Import/export helpers | |
| `libs/libkis` | `kritalibkis` | Scripting API used by Python plugins | `Krita.h`, `Document.h`, `Node.h` |
| `libs/resources`, `libs/resourcewidgets` | `kritaresources`, `kritaresourcewidgets` | Resource storage, bundles, tagging, resource choosers | `KisResourceModel.h`, `KisResourceLocator.h` |
| `libs/metadata`, `libs/psd`, `libs/psdutils`, `libs/multiarch`, `libs/surfacecolormanagementapi` | | Specialized support libraries | |

### Important `libs/ui` entry points

| Class / file | Role |
| --- | --- |
| `KisApplication` (`KisApplication.cpp`) | Application startup, resource initialization, splash. |
| `KisPart` (`KisPart.h`) | Singleton owning documents, views, and main windows. Do not use it from plugin constructors. |
| `KisMainWindow` | Top-level window, menus, docker management, window-level actions (e.g. Export Region). |
| `KisViewManager` | Per-main-window controller: canvas resources, node/selection/filter managers, action manager. Owns `KisSolsticeLazyTools`. |
| `KisView`, `KisDocument` | One view on one document; document I/O and undo stack. |
| `KisActionPlugin` | Base for `Krita/ViewPlugin` extensions; `createAction(name)` and `viewManager()`. |
| `kis_action.h`, `kis_action_manager.h` | Krita actions and activation conditions. |
| `kis_config.h` (`KisConfig`) | Typed access to `kritarc`. `KisConfig(true)` is read-only; `KisConfig(false)` writes on destruction. |
| `kis_mainwindow_observer.h` (`KisMainwindowObserver`) | Docker interface that receives `setViewManager()`. |
| `kis_canvas_resource_provider.h` | Foreground/background color, current preset, and other canvas resources. |
| `kis_node_manager.h`, `kis_node_commands_adapter.h` | Undoable node creation, renaming, and removal. |
| `tool/` | `KisTool`, `KisToolPaint`, `KisToolShape`, tool factories, stroke helpers. |
| `dialogs/kis_dlg_preferences.cc` | Settings dialog pages; custom Solstice General settings are persisted here. |
| `forms/*.ui` | Qt Designer forms compiled with `ki18n_wrap_ui`. |
| `canvas/`, `opengl/` | Canvas rendering and decorations. Performance sensitive. |
| `input/` | Input manager, shortcut and tablet handling. Focus-sensitive; see lifecycle rules. |
| `KisWelcomePageWidget`, `KisWelcomeAssetLibraryWidget` | Welcome page, including the custom Asset Library tab. |
| `KisImportExportManager`, `KisImportExportFilter` | File import/export dispatch and filter base class. |

## Plugin directories (`plugins/`)

Each category directory has a `CMakeLists.txt` that lists subdirectories with
`add_subdirectory()`. A new plugin is not built until it is added there.

| Directory | Service type in JSON | Registry / base class |
| --- | --- | --- |
| `plugins/dockers/<name>/` | `Krita/Dock` | `KoDockRegistry` + `KoDockFactoryBase` + `QDockWidget` (+ `KisMainwindowObserver` or `KoCanvasObserverBase`) |
| `plugins/tools/<name>/` | `Krita/Tool` | `KoToolRegistry` + `KoToolFactoryBase` / `KisToolPaintFactoryBase` + `KisTool*` |
| `plugins/filters/<name>/` | `Krita/Filter` | `KisFilterRegistry` + `KisFilter` |
| `plugins/generators/<name>/` | `Krita/Generator` | `KisGeneratorRegistry` + `KisGenerator` |
| `plugins/paintops/<name>/` | `Krita/Paintop` | `KisPaintOpRegistry`; shared option widgets in `plugins/paintops/libpaintop/` |
| `plugins/impex/<format>/` | `Krita/FileFilter` | `KisImportExportFilter` (separate import and export JSON files) |
| `plugins/extensions/<name>/` | `Krita/ViewPlugin` | `KisActionPlugin` (menu actions and dialogs) |
| `plugins/color/`, `plugins/metadata/`, `plugins/flake/`, `plugins/platforms/` | `Krita/ColorSpace`, `Krita/Metadata`, `Krita/Shape`, `Krita/PlatformPlugin` | Specialized |
| `plugins/python/`, `plugins/extensions/pykrita/` | Python plugins / Python host | Upstream Python support. Custom features are native C++ — do not add new Python features. |
| `plugins/visionml/` | Custom native ML module | See `docs/agent/vision-ml.md`. |

## Where does my change go?

| Goal | Location |
| --- | --- |
| New panel / palette / floating UI | New docker under `plugins/dockers/<feature>/` |
| New canvas tool or tool mode | New tool under `plugins/tools/`, or a strategy in an existing tool (e.g. `tool_transform2` for transform modes) |
| New image-processing operation with preview | Filter under `plugins/filters/` |
| New menu command with a dialog | Extension under `plugins/extensions/` using `KisActionPlugin` |
| Command that needs per-window state or integrates with several managers | `libs/ui/` class owned by `KisViewManager` (pattern: `KisSolsticeLazyTools`) |
| New file format | `plugins/impex/<format>/` |
| New option in Settings > Configure | `libs/ui/forms/wdggeneralsettings.ui` + `libs/ui/dialogs/kis_dlg_preferences.cc` (or a plugin-local settings dialog) |
| Add controls to an existing docker | That docker's directory (e.g. `plugins/dockers/layerdocker/`) |
| Reusable widget for several plugins | `libs/widgets/` (no `kritaui` dependency) or `libs/ui/widgets/` (needs `kritaui`) |
| New main-menu entry | Action definition in `krita/kritamenu.action` (or the plugin's own `.action` file) and placement in `krita/krita5.xmlgui` |
| New icon | Plugin-local `resources/` embedded with `qt_add_resources()`, or `krita/pics/` with its `.qrc` for application-wide icons |
| Scripting API | `libs/libkis/` — avoid unless requested |

Prefer the smallest scope: plugin-local first, `libs/ui` only when the
feature must interact with window/view-manager internals, and lower libraries
only for genuinely shared functionality.
