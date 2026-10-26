# UI test automation through MCP - plan

Status (2026-10-26): proposal only. The user asked whether interactive tests
(painting with a brush, operating selection tools) could be automated through
a new MCP server driven by an LLM, then asked for this plan. Nothing here is
implemented, built or authorized for implementation yet; the
[open decisions](#open-decisions-for-the-user) need the user's answers first.
When work starts, the maintained reference becomes `docs/agent/ui-automation.md`
(see [Documentation when implemented](#documentation-when-implemented)).

## Goal

Reduce the manual regression checks the user runs after native changes by
driving a running Solstice through its real input path, and checking the result
from the application state and pixels, not from screenshots alone.

- The repeatable part (replay a scenario, compare state and pixels) must be
  deterministic and must not depend on an LLM's judgement.
- An LLM uses the same interface through MCP to write scenarios from the
  existing manual checklists, explore new behavior, and triage failures.

Non-goals:

- Replacing checks that depend on the operating system's input stack, a real
  tablet driver, or a person's perception (see [What stays manual](#what-stays-manual)).
- A general scripting API. libkis and Python scripting already exist; this is
  a test harness with a narrow command set.
- CI execution in the first phases (see [Phase 5](#phase-5-optional-ci)).

## Current test assets

| Asset | What it covers | Limit |
| --- | --- | --- |
| `kis_add_tests` unit tests (`ctest`), see [Development workflow](development-workflow.md#test) | Parsing, persistence, strokes and filters in-process, e.g. `libs/ui/tests/freehand_stroke_test.cpp`, `kis_selection_manager_test.cpp` | No main window, canvas widget or input manager in most tests |
| QTest widget tests | Docker interaction, e.g. `plugins/dockers/quickaccess/tests/QuickAccessInteractionTest.cpp` (`QTest::mouseClick`) | One widget at a time, not the whole application |
| `KRITA_PAINT_TRACE` (`libs/image/KisPaintTrace.cpp`) | Latency and execution path of real strokes | Strokes are drawn by hand; see [Paint trace baseline runs](paint-trace-baseline-runs.md) |
| Feature manual checklists | Interaction contracts, e.g. [Puppet Warp](puppet-warp.md#manual-regression-checklist), [Lazy Tools](lazy-tools.md#manual-checks), [Quick Access](quick-access.md) | Run by the user |

GitHub Actions builds with `BUILD_TESTING=OFF` and does not launch the
application ([GitHub Actions](github-actions.md)).

## Options considered

| Option | Assessment |
| --- | --- |
| A. Screen-level control (screenshots plus OS clicks, e.g. computer use in the Claude desktop app, or pywinauto) | Available today without code. Breaks with window layout, theme, scale and timing; no pressure or tilt; results are judged from pixels on screen. Useful for occasional exploratory checks, not for regression gates. |
| B. libkis scripting (`Node::paintLine()`/`paintPath()` in `libs/libkis/Node.cpp`, `Document::setSelection()`) | Paints through the brush engine but bypasses the input manager, the tool and its options, so tool regressions are not covered. Selection tools cannot be operated. New Python features are also against the project rule in [Codebase map](codebase-map.md). |
| **C. Native test control plugin plus an external MCP bridge (proposed)** | Synthetic Qt input events enter the canvas through `KisInputManager::eventFilter()` (`libs/ui/input/kis_input_manager.cpp`), so the shortcut matcher, the active tool, stroke scheduling and rendering are all exercised. State and pixels are read from the image directly. |
| D. MCP server inside Solstice (HTTP/SSE in the plugin) | Fewer processes, but puts a JSON-RPC/MCP implementation and its protocol updates into a native module. Kept as an alternative; see decision 2. |

## Proposed architecture

```text
LLM client (Claude Code, MCP over stdio)      scenario runner (no LLM)
            \                                   /
             v                                 v
        MCP bridge  (dev-tools/test-control/, outside the application)
                    |  local socket: QLocalServer (named pipe on Windows),
                    |  newline-delimited JSON-RPC 2.0
                    v
        Solstice process
          plugins/extensions/testcontrol/  (Krita/ViewPlugin)
            - inert unless SOLSTICE_TEST_CONTROL is set
            - command dispatcher on the GUI thread
            - input injector -> canvas widget -> KisInputManager -> tool
            - state/pixel queries through KisImage, KisSelectionManager, ...
```

### Native plugin

- Location `plugins/extensions/testcontrol/`, following the view-plugin recipe
  in [Extension points](extension-points.md#view-plugin--menu-command-pluginsextensions-kritaviewplugin).
  No changes to `libs/ui` are expected for the first phases; add touch points
  to [the feature inventory](feature-inventory.md) if that changes.
- Activation: if the environment variable `SOLSTICE_TEST_CONTROL` (the pipe
  name) is absent, the plugin creates no server and no actions. This mirrors
  how `KRITA_PAINT_TRACE` is read once (`KisPaintTrace::enabled()`).
- Lifecycle: `KisActionPlugin` is created per view manager, so one window
  would otherwise start one server each. A process-wide server object is
  created by the first plugin instance once a main window exists, never in a
  plugin constructor before a view manager exists (the `KisActionPlugin.cpp`
  `m_viewManager` assertion recorded in `AGENTS.md`). It is destroyed on
  `QCoreApplication::aboutToQuit`. Commands resolve the active window and view
  at call time and fail cleanly when there is none.
- Threading: socket I/O and command execution run on the GUI thread. Long
  waits (`image.wait_idle`) are asynchronous (timer polling), never a nested
  blocking loop that could reorder input.
- Access: `QLocalServer::UserAccessOption`, one client at a time. No command
  evaluates code; file commands accept paths only inside a test root given at
  launch.

### Input injection

- Mouse: `QMouseEvent` press/move/release sent to the canvas widget with
  `QCoreApplication::sendEvent()`, with explicit buttons and modifiers.
- Tablet: `QTabletEvent` with pressure, tilt and rotation. Qt 6 requires a
  `QPointingDevice`; the plugin registers its own stylus device. Whether the
  tool and `KisPaintingInformationBuilder` accept it unchanged is the main
  [phase 0](#phase-0-feasibility-spike) question.
- Timing: each point carries a timestamp. The default mode dispatches moves on
  a timer at those times, so `compressMoveEventCommon()` and stroke scheduling
  see realistic spacing. A fast mode sends them back to back for logic-only
  scenarios.
- Coordinates: commands take image coordinates by default and convert them
  with the canvas `KisCoordinatesConverter`; widget coordinates are allowed for
  canvas decorations and handles.
- Keys: actions are triggered by name through `KisActionManager::actionByName()`.
  Raw key events are used only for modifier state and tool-local keys, since
  shortcut dispatch also depends on focus and the active window.

### Command set (protocol version 1, draft)

| Group | Commands |
| --- | --- |
| Session | `session.info` (versions, GPU modes, active window), `session.reset_view` (window geometry, zoom 100 %, rotation 0, no mirror) |
| Document | `document.new` (size, color model/depth, resolution), `document.open`, `document.export_png`, `document.close` (discard) |
| Tools | `tool.activate` (factory ID, e.g. `KritaShape/KisToolBrush`, `KisToolSelectRectangular`), `tool.set_brush` (preset name, size, opacity), `tool.set_option` (named options for the tools covered) |
| Input | `input.stroke` (points `{x, y, pressure, tilt, t}`, device mouse/tablet, button, modifiers), `input.click`, `input.drag`, `input.key` |
| Actions | `action.trigger` (action name), `edit.undo`, `edit.redo` |
| Sync | `image.wait_idle` (wraps `KisImage::isIdle()`/`waitForDone()`, with a timeout) |
| Queries | `query.layers`, `query.selection` (bounds, pixel count, hash), `query.pixels` (rect: hash, optional small PNG), `query.undo` (count, top command text), `query.tool` (active tool ID) |
| Diagnostics | `capture.canvas` (widget image, for the LLM and failure reports), `log.drain` (warnings and safe-assert messages since the last call) |

Each response carries the result, elapsed time and any warnings logged while
the command ran.

### MCP bridge and scenario runner

- `dev-tools/test-control/`: a small stdio MCP server that maps MCP tools one
  to one onto the protocol commands, plus `launch` and `shutdown`. Python
  matches the existing `dev-tools/docs` tooling; the "no new Python features"
  rule concerns application features, but the language is decision 2.
- `launch` starts `<krita-dev-root>\_install\bin\krita.exe` with
  `SOLSTICE_TEST_CONTROL`, a scratch `SOLSTICE_PROFILE_ROOT` (never the user's
  profile, see [Settings location](settings-location.md)) and the GPU mode
  variables (`KRITA_GPU_PROJECTION`, `KRITA_GPU_BRUSH`) when a scenario asks
  for them. It must refuse to start while another Solstice process is running:
  the single-instance check in `krita/main.cc` is not keyed by the profile
  root, so a second launch hands its arguments to the running instance and
  exits. It never terminates a Solstice process it did not start (rule in
  `AGENTS.md`).
- The scenario runner replays scenario files through the same protocol
  without an LLM and writes a report (pass/fail per step, diffs, captures).

### Scenarios and expectations

A scenario is a JSON file: setup (document, tool, preset), steps (protocol
commands) and expectations. Expectations prefer state over pixels:

1. State: active tool, undo count and top command, layer list, selection bounds
   and pixel count.
2. Pixels: a hash of a rectangle for exact (CPU) paths; for GPU paths and
   filters with known tolerance, a maximum per-channel difference and a
   mismatch-pixel limit against a reference image
   ([CPU/GPU parity](wiki/concepts/cpu-gpu-bit-parity.md) says which rule
   applies).

## Determinism risks

| Risk | Mitigation |
| --- | --- |
| Asynchronous strokes and idle tasks | Every step that changes the image is followed by `image.wait_idle`; idle tasks can lag by seconds after node-juggler strokes ([pitfall](wiki/pitfalls/idle-tasks-wait-for-open-strokes.md)) |
| View transform and window geometry | `session.reset_view` before each scenario; image coordinates in scenarios |
| Smoothing, stabilizer and unsaved preset edits | Scratch profile with fixed settings; presets set by name at scenario start |
| Brush randomness | Check per-stroke seeding (`KisPerStrokeRandomSource`); scenarios avoid random-dependent assertions until verified |
| Input profile bindings | Use the default profile in the scratch root; modifiers follow `kritadefault.profile` ([Transform Tool input pitfall](wiki/pitfalls/transform-tool-input.md)) |
| GPU differences | Separate expectations per GPU mode; tolerance comparison, never inferring GPU composition from the requested mode |
| Focus-dependent behavior | Phase 0 records which behaviors need real window focus; those scenarios run with the test window raised, or stay manual |

## What stays manual

- Real tablet drivers (Wintab, Windows Ink), pressure curves and hover;
  synthetic events enter above the Qt platform layer.
- Feel and latency as perceived by the user; paint-trace numbers from
  synthetic strokes form a separate series from hand-drawn ones.
- Visual judgement of themes, icons and layout, and OS-level dialogs, docking
  and multi-monitor behavior.
- Interaction contracts that `AGENTS.md` says compilation does not prove still
  need a first manual confirmation when a feature is new; automation then
  guards against regressions.

## Phases

### Phase 0: feasibility spike

- Plugin skeleton, environment gate, process-wide `QLocalServer`, `session.info`.
- One synthetic mouse stroke and one synthetic tablet stroke with pressure
  through the brush tool; verify that the stroke is created, pressure reaches
  the paint information, and undo has one step.
- One rectangular selection by drag; read its bounds.
- Record which events or checks need real focus.
- Exit: a short report in this plan (results, blockers) and the user's go/no-go.

### Phase 1: minimum protocol and bridge

- Commands: document new/export/close, tool activate, brush preset and size,
  stroke/click/drag, action trigger, undo/redo, wait idle, layer/selection/
  pixel/undo queries, canvas capture.
- MCP bridge with `launch`/`shutdown` and scratch profile handling.
- Three scenarios: brush stroke then undo; rectangular selection then fill
  inside it; eraser stroke over a painted area.

### Phase 2: scenario runner and expectations

- Runner, report format, expectation storage (decision 3), tolerance compare.
- Plugin unit tests for parsing and coordinate conversion (`kis_add_tests`).

### Phase 3: tool and feature coverage

- Selection tools (rectangle, ellipse, polygon, freehand, contiguous) and
  selection modifiers; Transform Tool including Puppet Warp checklist items
  that are state-checkable (pin count, undo steps, applied pixels); Lazy Tools
  and Quick Access interactions where widget commands are needed
  (`ui.click` by object name would be a protocol addition).
- Convert each feature's checklist items that state and pixels can decide; mark
  the rest as manual in the feature document.

### Phase 4: paint trace automation

- Replay the [baseline run](paint-trace-baseline-runs.md) stroke set with
  `KRITA_PAINT_TRACE` for CPU, projection and brush modes; record results as a
  new series, not pooled with hand-drawn runs.

### Phase 5: optional CI

- Hosted Windows runners have no GPU and CI builds without tests. Running the
  harness there (software OpenGL, CPU paths only) is a separate decision after
  local use proves stable.

## Open decisions for the user

1. **Distribution:** build and ship the plugin in release packages (inert
   without the environment variable), or build it only in development builds
   with a CMake option such as `SOLSTICE_BUILD_TEST_CONTROL` (default off in
   packaging)? Proposed: development builds only.
2. **Bridge:** external bridge in Python under `dev-tools/test-control/`
   (proposed), external bridge in another language, or MCP directly inside the
   plugin (option D)?
3. **Expectations storage:** hashes and state in the repository with reference
   PNGs kept small and only where tolerance comparison needs them (proposed),
   or reference images outside the repository?
4. **Scenarios in the repository:** keep scenario files next to each feature's
   tests (proposed), or in one `tests/ui-scenarios/` tree?
5. **Single-instance handling:** require that no other Solstice runs during a
   test session (proposed, no source change), or add a test-mode
   single-instance key in `krita/main.cc`?
6. **Start:** approve phase 0 only, or phases 0 to 2 together?

## Documentation when implemented

- `docs/agent/ui-automation.md`: maintained reference (protocol, lifecycle,
  launch rules, scenario format, determinism rules, verification status); this
  plan then points to it and keeps only history and unresolved scope.
- `AGENTS.md` technical index entry and [feature inventory](feature-inventory.md)
  touch points.
- [Development workflow](development-workflow.md): how to run scenarios before
  the hand-off, and which manual checks they replace.
- No user guide under `docs/` while the feature is development-only
  (decision 1); add one if it ships to users.
