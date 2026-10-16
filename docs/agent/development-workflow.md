# Development workflow — agent reference

Build, test, install, and hand-off procedure shared by all features. Paths use
the placeholders defined in `AGENTS.md` (`<repository-root>`,
`<krita-dev-root>`, `<toolchain-root>`).

## Before editing

1. Create or use a feature branch based on `main` (`codex/<topic>` for Codex).
   Push the feature branch and merge through a pull request. Never push directly
   to `main`; this also applies to release and documentation changes (user
   decision, 2026-10-16). Never commit custom work to historical branches.
2. Read `AGENTS.md`, `docs/agent/codebase-map.md`, and the complete agent
   document of every feature you will touch.
3. Check `git status`. The tree may be dirty; existing changes belong to the
   user.
4. Identify the CMake target(s) that own each file you will modify
   (`kis_add_library(<target> ...)` in the nearest `CMakeLists.txt`).

## Shell notes (Windows)

Agents may run commands from PowerShell, `cmd.exe`, or Git Bash (MSYS). The
command examples in these documents are written for `cmd.exe`/PowerShell.
When the agent's shell is Git Bash:

- Prefix `cmd.exe` invocations with `MSYS_NO_PATHCONV=1`. With MSYS path
  conversion enabled, `/d /s /c` are rewritten as paths and `cmd.exe` opens an
  interactive prompt instead of running the command. The prefix is harmless
  when conversion is already disabled.
  ```bash
  MSYS_NO_PATHCONV=1 cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target <target> -j 2"
  ```
- Pass Windows-style paths (`C:/Users/...` or `C:\Users\...`) to native Windows
  programs (`git -C`, `cmake`, `ctest`, `clang-format`). MSYS paths such as
  `/c/Users/...` work only for bash builtins and MSYS tools, and fail for native
  programs when path conversion is disabled (as in Hermes Agent).
- Put scratch files that native tools must read under the agent's scratch
  directory (`$TMPDIR`), not bare `/tmp`.
- Check for a running Krita before installing with
  `MSYS_NO_PATHCONV=1 tasklist /FI "IMAGENAME eq solstice.exe"` (and `krita.exe` for older launchers) (Git Bash) or
  `Get-Process -Name solstice,krita -ErrorAction SilentlyContinue` (PowerShell).

## Build

All commands run inside the environment script:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target <target> -j 2"
```

Common targets:

| Source area | Target |
| --- | --- |
| `libs/ui` | `kritaui` |
| `libs/widgets` / `libs/widgetutils` | `kritawidgets` / `kritawidgetutils` |
| `libs/image` | `kritaimage` |
| `krita/` (executable, actions, data) | `krita` |
| `plugins/dockers/<x>` | `krita<x>docker` (check its `CMakeLists.txt`) |
| `plugins/tools/tool_transform2` | `kritatooltransform` |
| `plugins/dockers/layerdocker` | `kritalayerdocker` |
| `plugins/visionml` | `kritavisionml` |

A new subdirectory or new source file requires CMake to reconfigure; building a
target normally triggers that automatically. Building a low library (e.g.
`kritaimage`) rebuilds many dependents — expect long builds.

## Test

- Unit tests are declared with `kis_add_tests(Foo.cpp LINK_LIBRARIES <libs> kritatestsdk)`
  inside a `tests/` directory added via `if(BUILD_TESTING) add_subdirectory(tests) endif()`.
- Test names follow the source path, e.g.
  `plugins-dockers-quickaccess-QuickAccessCoreTest`.
- Run:
  ```bat
  cmd.exe /d /s /c "call <krita-dev-root>\env.bat && ctest --test-dir <krita-dev-root>\_build -R <test-name-regex> --output-on-failure"
  ```
  or run the test executable in `<krita-dev-root>\_build\bin\` with a single
  test function name.
- Add tests for parsing, persistence, serialization, legacy import, and pure
  layout logic. Interactive behavior (focus, shortcuts, tablet input, canvas
  rendering) still requires manual checks.

## Install

Install every target you rebuilt, from its build directory:

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\<path-to-source-dir>\cmake_install.cmake
```

Examples: `libs\ui`, `libs\widgets`, `krita`, `plugins\dockers\restnote`.

- `.action`, `.xmlgui`, and data files are installed by the directory that
  declares them (`krita/` for `krita.action`, `kritamenu.action`,
  `krita5.xmlgui`).
- Windows locks loaded DLLs. If installation fails because Krita is running,
  ask the user to close Krita. Never terminate Krita yourself.
- After installing, Krita must be fully restarted.

## Hand-off checklist

1. Formatted with `<toolchain-root>\bin\clang-format.exe` (changed lines only
   for upstream files — see `docs/agent/coding-rules.md`).
2. `git diff --check` is clean for touched files.
3. All affected targets built; relevant tests pass (report failures verbatim).
4. All affected targets installed.
5. User document `docs/<feature>.md` and agent document
   `docs/agent/<feature>.md` updated in the same change; README link valid;
   `AGENTS.md` index updated for new features.
6. Listed the feature's manual regression checks for the user to run, and
   stated which ones could not be verified by an agent.
7. If user-visible strings were added or changed: ran
   `dev-tools/translations/update_translations.py`, translated the new and
   fuzzy `po/zh_CN/krita.po` entries (rules in `docs/agent/translations.md`),
   checked that `msgfmt --check` reports no fuzzy or untranslated entries,
   and committed the catalog with the change.
8. Followed [Documentation maintenance](documentation.md): updated the owning
   reference, checked links and indexes, and recorded a concise wiki entry.
   For documentation-only work, native format/build/test/install steps do not
   apply unless source behavior is also changed; do not claim new runtime
   verification from a prose edit.

## Upstream synchronization

Upstream merges are discontinued. `krita/6.0`, `krita-sol` and
`krita-sol-gpu` are historical references, not active integration branches.
Approved selective changes follow [Upstream intake](upstream-intake.md);
do not add a persistent upstream remote.

Keep edits to inherited files small so reviews and selective patches remain
tractable. Use [Feature inventory](feature-inventory.md) to find shared hooks
and its comparison commands to inspect the current Solstice divergence.
