# UI test automation through MCP - plan

Status (2026-10-26): the user answered the plan's open decisions and
authorized phase 0 only ([decisions](#user-decisions-2026-10-26)). Phase 0
is implemented as the test control plugin; its maintained reference is
[Test control](test-control.md), which owns the current protocol, lifecycle,
verification status and the phase 0 manual checks. Phase 0 is not yet built
or run in Solstice. Phases 1-5 below are proposals and need the user's
go-ahead after the phase 0 checks.

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

## User decisions (2026-10-26)

| Question | Decision |
| --- | --- |
| Distribution | Ship the plugin in release packages; it stays inert unless enabled by environment variables. |
| MCP server | Inside Solstice (option D below), not an external bridge. |
| Expectations | Hashes and state in the repository; reference images only where tolerance comparison needs them, kept small. |
| Scenario files | Next to each feature's tests. |
| Second running instance | Handled by operation: no other Solstice may run during a test session; no `krita/main.cc` change. |
| Scope | Phase 0 only. |

Consequences for the design: with MCP in-process, the transport is MCP
Streamable HTTP on `127.0.0.1` instead of a named pipe to a bridge. Because
the plugin ships and a loopback TCP port is reachable by every local
process, a bearer token is mandatory, and Host/Origin checks block browser
pages (DNS rebinding). Launching Solstice with a scratch profile is a manual
step described in the [user guide](../test-control.md).

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
| C. Native test control plugin plus an external MCP bridge | Originally proposed; superseded by D (user decision). |
| **D. Native test control plugin serving MCP itself (chosen)** | Synthetic Qt input events enter the canvas through `KisInputManager::eventFilter()` (`libs/ui/input/kis_input_manager.cpp`), so the shortcut matcher, the active tool, stroke scheduling and rendering are all exercised. State and pixels are read from the image directly. No second process to install. |

## Architecture

```text
LLM client (Claude Code)          scenario runner (phase 2, no LLM)
            \                        /
             v                      v
   MCP Streamable HTTP, http://127.0.0.1:<port>/mcp, bearer token
             |
   Solstice process: plugins/extensions/testcontrol/ (Krita/ApplicationPlugin)
     - inert unless SOLSTICE_TEST_CONTROL_PORT and _TOKEN are set
     - HTTP endpoint and MCP handling on the GUI thread, calls serialized
     - input injector -> canvas widget -> KisInputManager -> tool
     - state/pixel queries through KisImage, KisSelection, undo stack
```

The phase 0 implementation, tool list, lifecycle, security checks and input
event sequences are documented in [Test control](test-control.md); that page
wins where this plan's earlier text differs.

### Planned additions (phases 1-3)

| Group | Additions |
| --- | --- |
| Session | `view_reset` (window geometry, zoom 100 %, rotation 0, no mirror) |
| Document | `document_open`, `document_export_png`, `document_close` (discard), paths restricted to a test root given at launch |
| Tools | `brush_set` (preset by name, size, opacity), named tool options for covered tools |
| Input | modifier keys with key events and canvas focus; `input_click`, `input_drag` conveniences |
| Queries | `query_pixels` (rect hash, optional small PNG), `query_layers` |
| Diagnostics | `log_drain` (warnings and safe-assert messages since the last call) |
| Widgets | `ui_click` by object name, for dockers such as Quick Access |

### Scenarios and expectations

A scenario is a JSON file next to the feature's tests (user decision):
setup (document, tool, preset), steps (tool calls) and expectations. The
runner (phase 2) replays it through the same endpoint without an LLM and
writes a report. Expectations prefer state over pixels:

1. State: active tool, undo count and top command, layer list, selection bounds
   and pixel count.
2. Pixels: a hash of a rectangle for exact (CPU) paths, stored in the
   repository; for GPU paths and filters with known tolerance, a maximum
   per-channel difference and a mismatch-pixel limit against a small
   reference image ([CPU/GPU parity](wiki/concepts/cpu-gpu-bit-parity.md)
   says which rule applies).

## Determinism risks

| Risk | Mitigation |
| --- | --- |
| Asynchronous strokes and idle tasks | Every step that changes the image is followed by `wait_idle`; idle tasks can lag by seconds after node-juggler strokes ([pitfall](wiki/pitfalls/idle-tasks-wait-for-open-strokes.md)) |
| View transform and window geometry | `view_reset` before each scenario (phase 1); image coordinates in scenarios |
| Smoothing, stabilizer and unsaved preset edits | Scratch profile with fixed settings; presets set by name at scenario start |
| Brush randomness | Check per-stroke seeding (`KisPerStrokeRandomSource`); scenarios avoid random-dependent assertions until verified |
| Input profile bindings | Use the default profile in the scratch root; modifiers follow `kritadefault.profile` ([Transform Tool input pitfall](wiki/pitfalls/transform-tool-input.md)) |
| Real keyboard and pen state | The input manager resyncs modifiers from the real keyboard ([synthetic input pitfall](wiki/pitfalls/synthetic-canvas-input.md)); keep hands off during runs |
| GPU differences | Separate expectations per GPU mode; tolerance comparison, never inferring GPU composition from the requested mode |
| Focus-dependent behavior | Phase 0 check 10 records whether strokes need window focus |

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

Implemented 2026-10-26 (not yet built or run in Solstice): plugin skeleton,
environment gate, MCP endpoint, `session_info`, `document_new`,
`tool_activate`, mouse and tablet `input_stroke`, `wait_idle`,
`query_state`, `action_trigger`, `canvas_capture`. Exit: the user runs the
[phase 0 checks](test-control.md#manual-regression-checklist), the results
are recorded in the reference, and the user decides on phase 1.

### Phase 1: minimum protocol

- The planned additions for documents, view reset, brush presets and
  modifiers; document paths restricted to a test root.
- Three scenarios: brush stroke then undo; rectangular selection then fill
  inside it; eraser stroke over a painted area.

### Phase 2: scenario runner and expectations

- Runner, report format, expectation storage in the repository, tolerance
  compare.

### Phase 3: tool and feature coverage

- Selection tools (rectangle, ellipse, polygon, freehand, contiguous) and
  selection modifiers; Transform Tool including Puppet Warp checklist items
  that are state-checkable (pin count, undo steps, applied pixels); Lazy Tools
  and Quick Access interactions through `ui_click`.
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
