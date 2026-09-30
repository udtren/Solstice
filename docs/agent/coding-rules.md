# Coding rules and pitfalls — agent reference

Conventions and known failure modes for native C++ work in this repository.
Global policy (branch, documentation, Android removal) is in `AGENTS.md`.

## Source file conventions

- New files start with an SPDX header:
  ```cpp
  /*
   * SPDX-FileCopyrightText: 2026 Krita contributors
   * SPDX-License-Identifier: GPL-3.0-or-later
   */
  ```
  Keep existing headers of upstream files unchanged.
- Naming: new classes use `Kis<Feature>` in `libs/`, or a feature prefix in a
  plugin (`QuickAccess*`, `RestNote*`, `AssetLibrary*`). File names match class
  names (`FooDock.h` / `FooDock.cpp`). Do not rename existing upstream files.
- Qt signal/slot: follow the surrounding file. New code may use pointer-to-member
  `connect`.
- Include `<Feature>.moc` at the end of a `.cpp` that declares a `Q_OBJECT`
  class inside the `.cpp` (plugin entry points).
- Export macros (`KRITAUI_EXPORT`, `KRITAIMAGE_EXPORT`, …) are required only for
  symbols used across shared-library boundaries.

## Formatting and diffs

- Format with the configured `clang-format` (repository `.clang-format`).
- For **new** files, format the whole file.
- For **existing upstream files**, format only the lines you changed
  (`clang-format --lines=<start>:<end>` or `git clang-format`). Whole-file
  formatting of upstream files reorders includes and rewraps unrelated code,
  producing large diffs that make upstream synchronization with `krita/6.0`
  harder (this has already happened in `krita/main.cc` and
  `libs/ui/KisApplication.cpp`).
- Never format vendored code (`3rdparty*`, `plugins/visionml/thirdparty/`).
- Run `git diff --check` on touched files.

## User-visible strings

- Wrap every user-visible string with `i18n()` / `i18nc()` (from
  `<klocalizedstring.h>`); `.ui` files are compiled with `ki18n_wrap_ui`.
  Do not use `tr()` in new native code.
- The product name visible to users is **Solstice**. Internal identifiers
  (target names, `kritarc`, resource paths, MIME types, plugin JSON ids,
  `Krita/*` service types) stay `krita` for compatibility.

## Configuration and persistence

- `kritarc` location: `%LOCALAPPDATA%\kritarc`. Access through
  `KisConfig(true)` (read) / `KisConfig(false)` (write), or `KConfigGroup`.
- New custom global keys use the `Solstice/` prefix
  (e.g. `Solstice/ColorPickFromAnywhere`). Feature-specific groups documented in
  each feature's agent document take precedence.
- Features migrated from Python plugins must keep reading legacy keys, JSON
  fields, and file paths. Add aliases instead of renaming.
- Anything written to `.kra`, `.kpp`, workspaces, window layouts, or shortcut
  schemes (filter ids, transform args XML, dock ids, action names) is a
  compatibility contract. Changing it requires explicit user approval and
  backward-compatible reading.
- Per-user data files belong under `%APPDATA%\krita\<feature>\`
  (`KoResourcePaths` / `QStandardPaths`), not next to the executable.

## Image model, undo, and threading

- Never modify `KisImage` / nodes / paint devices directly from UI code without
  undo. Use:
  - `KisNodeCommandsAdapter` / `KisNodeManager` for node add/remove/rename;
  - `KisProcessingApplicator` or a `KisStrokeStrategy` for pixel/processing work;
  - `KUndo2Command` subclasses for other document state.
- Image data is processed on worker threads by the stroke system. Filters,
  processing visitors, and stroke jobs must not touch widgets. Communicate back
  to the GUI with queued signals.
- Wait for or lock the image (`KisImage::barrierLock()` / applicators) before
  reading pixel data that may be under modification.
- Preview and final render paths must produce identical results (critical for
  the Transform Tool / Puppet Warp).

## Lifecycle and ownership

- Plugin constructors only register factories/actions. `KisPart::instance()`,
  main windows, and view managers are not ready there
  (`KisActionPlugin.cpp` `m_viewManager` assertion).
- Multiple main windows and multiple views may exist. Per-window state belongs
  to `KisViewManager` or to a dock instance, not to static globals. Process-wide
  resources (global hotkeys, native event filters) must be registered once and
  reference-counted/idempotent.
- Use `QPointer` for pointers to canvases, views, and view managers held beyond
  a call. Handle `setCanvas(nullptr)`.
- Application event filters: exactly one owner, installed once, removed in the
  destructor. Key press/release handling must be symmetric and must restore
  state on focus loss.
- Do not create extra top-level windows parented to nothing; parent dialogs to
  `viewManager()->mainWindowAsQWidget()`.

## Assertions and logging

- Use `KIS_SAFE_ASSERT_RECOVER_RETURN(cond)` / `KIS_SAFE_ASSERT_RECOVER(cond) {...}`
  for recoverable invariants and `KIS_ASSERT_RECOVER*` / `KIS_ASSERT` only for
  fatal logic errors (`libs/global/kis_assert.h`).
- Logging: `dbgUI`, `dbgPlugins`, `dbgImage`, `dbgTools`, `warnUI`, … from
  `kis_debug.h`. Do not leave `qDebug()` spam in committed code.

## Platform scope

- Desktop only. Do not add `Q_OS_ANDROID` / `ANDROID` branches, Android
  sources, or packaging. When editing a block that already contains Android
  conditionals, it is acceptable to leave them unless the task is Android
  removal.
- Windows is the primary development platform. Windows-only code
  (`RegisterHotKey`, native event filters) must be guarded with `Q_OS_WIN` /
  `WIN32` and must not break Linux/macOS builds.

## Files to leave alone

- `*_ui.py` files beside `.ui` forms (e.g. `libs/ui/forms/*_ui.py`) are generated
  inspection artifacts and are not built. Edit the `.ui` file, not the `.py`.
  Do not delete them unless the user asks. (Upstream `pykrita` `_ui.py` files are
  real Python sources.)
- `po/`, `pch/`, `winquirks/`, vendored third-party code.
- Unrelated user changes in the working tree.
