# Extension points — agent reference

Recipes for adding or updating native Krita features. Each recipe lists the
files to create or update. Read `docs/agent/codebase-map.md` first to choose
the extension point, and `docs/agent/coding-rules.md` for conventions.

## Common plugin skeleton

Every loadable plugin in `plugins/` has the same shape:

```text
plugins/<category>/<feature>/
  CMakeLists.txt            # builds a MODULE target and installs it
  krita_<feature>.json      # plugin metadata (service type, library name)
  <Feature>Plugin.cpp       # K_PLUGIN_FACTORY_WITH_JSON + registry registration
  <Feature>*.h / *.cpp      # implementation
  <feature>.action          # optional: action definitions for shortcuts/menus
  resources/                # optional: icons embedded with qt_add_resources()
  tests/                    # optional: unit tests (kis_add_tests)
```

Required steps for a new plugin:

1. Add `add_subdirectory(<feature>)` to `plugins/<category>/CMakeLists.txt`.
2. `CMakeLists.txt`:
   ```cmake
   set(KRITA_<FEATURE>_SOURCES <Feature>Plugin.cpp <Feature>Dock.cpp)
   kis_add_library(krita<feature><suffix> MODULE ${KRITA_<FEATURE>_SOURCES})
   target_link_libraries(krita<feature><suffix> kritaui)
   install(TARGETS krita<feature><suffix> DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
   ```
   Use `kis_add_library`, not `add_library` (it wires precompiled headers).
   Compile `.ui` forms with `ki18n_wrap_ui(<SOURCES_VAR> form.ui)` before the
   `kis_add_library` call.
3. JSON metadata — `X-KDE-Library` must equal the CMake target name, and the
   JSON filename must match the one in `K_PLUGIN_FACTORY_WITH_JSON`:
   ```json
   {
       "Id": "<UniqueId>",
       "Type": "Service",
       "X-KDE-Library": "krita<feature><suffix>",
       "X-KDE-ServiceTypes": ["Krita/Dock"],
       "X-Krita-Version": "28"
   }
   ```
4. Plugin entry point:
   ```cpp
   class FooPlugin : public QObject {
       Q_OBJECT
   public:
       FooPlugin(QObject *parent, const QVariantList &) : QObject(parent) {
           KoDockRegistry::instance()->add(new FooDockFactory());
       }
   };
   K_PLUGIN_FACTORY_WITH_JSON(FooPluginFactory, "krita_foodocker.json", registerPlugin<FooPlugin>();)
   #include "FooPlugin.moc"
   ```
   The constructor only registers factories. Never access `KisPart`, a main
   window, or a view manager here.
5. Create `docs/<feature>.md`, `docs/agent/<feature>.md`, a README link, and an
   `AGENTS.md` index entry (see `docs/agent/feature-inventory.md`).

## Docker (`plugins/dockers/`, `Krita/Dock`)

Reference implementations: `plugins/dockers/restnote/` (minimal),
`plugins/dockers/assetlibrary/` (view-manager aware),
`plugins/dockers/quickaccess/` (actions, event filters, core static library,
tests).

- Factory subclasses `KoDockFactoryBase`: implement `id()`,
  `defaultDockPosition()`, and `createDockWidget()`. The `id()` string is
  persisted in window layouts and workspaces — never rename it after release.
- The dock subclasses `QDockWidget` and either `KoCanvasObserverBase`
  (`setCanvas()` / `unsetCanvas()`) or `KisMainwindowObserver` (adds
  `setViewManager()`).
- `setCanvas()` is called repeatedly with different canvases and with
  `nullptr`. Disconnect from the previous canvas before connecting to the new
  one; `unsetCanvas()` must drop every canvas pointer.
- Register actions in `setViewManager()`, not in the constructor.
- Global event filters, timers, and native hooks must be owned by a persistent
  dock instance (not by the plugin object or popup copies). See the Quick
  Access ownership notes.
- Put pure logic (models, parsers, layout) in a separate `STATIC` library so it
  can be unit tested without `kritaui` (pattern: `kritaquickaccesscore`).

## View plugin / menu command (`plugins/extensions/`, `Krita/ViewPlugin`)

Reference: `plugins/extensions/buginfo/`.

- Subclass `KisActionPlugin`. In the constructor create actions with
  `createAction("<action_name>")` and connect them; use `viewManager()` only
  inside slots.
- Every action name must be defined in an `.action` file (see Actions below).
- Menu placement: either add `<Action name="..."/>` to `krita/krita5.xmlgui`,
  or ship a plugin `.xmlgui` installed to `${KDE_INSTALL_DATADIR}/kritaplugins`.
- Image changes must go through undoable commands or strokes (see
  `docs/agent/coding-rules.md`).

## Per-window feature inside `libs/ui`

Reference: `libs/ui/KisSolsticeLazyTools.*`.

Use this when a feature needs several view-manager subsystems and is not a
docker. The object is a member of `KisViewManager::Private`, and its
`createActions()` is called after the action manager exists
(`KisViewManager.cpp`). Add new sources to `kritaui_LIB_SRCS` in
`libs/ui/CMakeLists.txt`; export classes with `KRITAUI_EXPORT` only if a plugin
needs them. A `libs/ui` change requires rebuilding and installing `kritaui`.

## Tool (`plugins/tools/`, `Krita/Tool`)

- Register a `KoToolFactoryBase` (or `KisToolPaintFactoryBase`) subclass with
  `KoToolRegistry::instance()->add(...)`.
- Tool classes derive from `KisTool`, `KisToolPaint`, or `KisToolShape` in
  `libs/ui/tool/`.
- Tool option widgets are returned from `createOptionWidget()`; the tool and
  its option widget must survive canvas switches.
- Tool actions are defined in the tool's `.action` file or `plugins/tools/tools.action`.
- Adding a mode to an existing tool (e.g. a new transform mode) belongs in that
  tool's strategy classes. The Transform Tool additionally has serialized state
  (`tool_transform_args.*`, `kis_transform_mask_adapter.*`) — read
  `docs/agent/puppet-warp.md` before touching it.

## Filter / generator (`plugins/filters/`, `plugins/generators/`)

- Register with `KisFilterRegistry::instance()->add(new MyFilter())` (see
  `plugins/filters/blur/blur.cpp`) or `KisGeneratorRegistry`.
- Implement `processImpl()` on `KisPaintDeviceSP`; it runs on worker threads and
  must not touch widgets or global UI state.
- The filter id and configuration property names are stored in `.kra` files
  (filter layers/masks). Keep them stable.
- Configuration UI derives from `KisConfigWidget`.

## Paint operation (`plugins/paintops/`)

Highest-complexity extension. Reuse option classes from
`plugins/paintops/libpaintop/`. Preset settings are persisted in `.kpp` files;
never rename existing setting keys.

## File import/export (`plugins/impex/<format>/`)

Import and export are separate targets and JSON files with `X-KDE-Import` or
`X-KDE-Export` MIME types and `X-KDE-Extensions`. Filters derive from
`KisImportExportFilter`. Export must not change a document's path or modified
state.

## Settings

- Global Solstice settings: add controls to `libs/ui/forms/wdggeneralsettings.ui`
  and load/save them in `libs/ui/dialogs/kis_dlg_preferences.cc`.
- Plugin-owned settings: use a plugin-local dialog and `KisConfig` /
  `KConfigGroup`. Key naming and legacy-compatibility rules are in
  `docs/agent/coding-rules.md`.

## Actions and shortcuts

- Action XML format (`<ActionCollection>` → `<Actions category>` → `<Action name>`)
  with `text`, `icon`, `shortcut`, `activationFlags`, `activationConditions`,
  `isCheckable`. Examples: `plugins/dockers/quickaccess/quickaccess.action`,
  `krita/kritamenu.action`.
- Plugin action files are installed with
  `install(FILES <feature>.action DESTINATION ${KDE_INSTALL_DATADIR}/krita/actions)`.
- Application-level actions live in `krita/krita.action` / `krita/kritamenu.action`
  (installed by `krita/CMakeLists.txt`).
- Action names are persisted in user shortcut schemes. Never rename an action
  after release; choose a unique, feature-prefixed name.
- A running Krita reads installed action files only at startup.

## Icons and resources

- Plugin icons: put PNG/SVG files in `resources/` and embed them:
  ```cmake
  file(GLOB_RECURSE <VAR> CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.png")
  qt_add_resources(<target> <name>_icons PREFIX "/<feature>"
      BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources" FILES ${<VAR>})
  ```
  and load them as `:/<feature>/<file>`.
- Theme icons shared with the application use `KisIconUtils::loadIcon("name")`
  and live in `krita/pics/` (`svg/svg-icons.qrc`, with `dark_`/`light_`
  variants).
- Branding assets: `krita/pics/branding/` — see
  `docs/agent/solstice-visual-branding-todo.md`.
