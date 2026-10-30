# UI test automation through MCP - plan

Status (2026-10-27): phase 0 is done. The test control plugin was built and
its twelve manual checks were run against a live Solstice on Windows and on
Linux; the results, the input fixes they led to and the remaining limits are
in [Test control](test-control.md), which owns the current protocol,
lifecycle and verification status. The user authorized phases 1 and 2 on
2026-10-27 ([decisions](#user-decisions-2026-10-26)); both are implemented on
branch `claude/test-control-phase1` and checked on Linux, with the Windows
check pending. The user authorized phase 3 the same day, first the
selection tools and the Transform Tool / Puppet Warp items, then Lazy Tools
and Quick Access; both parts are implemented on `claude/test-control-phase3`
and checked on Linux, Windows pending. Phase 4 (paint trace automation) was
authorized and run on Linux the same day, on the same branch, and its
tracing-overhead runs followed on `claude/test-control-phase4`; phase 5 remains
a proposal that needs the user's go-ahead.

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
| Scope | Phase 0 only. 2026-10-27: phase 1 authorized after the phase 0 checks; after phase 1 the user said to keep going ("I will test on windows later so keep going"), taken as the go-ahead for phase 2. Phase 3, first part: "Go ahead with phase 3: start with the selection tools (ellipse, polygon, freehand, contiguous) and selection modifiers, then the state-checkable Transform Tool / Puppet Warp checklist items." Then: "Go ahead with the rest of phase 3: Lazy Tools and Quick Access through the ui tools." Phase 4: "go phase 4". |

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
| Focus-dependent behavior | Phase 0 check 10: strokes paint without window focus on Windows and Linux; on Linux only after each stroke began with an `Enter` (a covered pointer had suppressed all actions) |

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

Implemented 2026-10-26: plugin skeleton, environment gate, MCP endpoint,
`session_info`, `document_new`, `tool_activate`, mouse and tablet
`input_stroke`, `wait_idle`, `query_state`, `action_trigger`,
`canvas_capture`.

Done 2026-10-27. The [phase 0 checks](test-control.md#manual-regression-checklist)
pass on Windows and Linux. Running them found and fixed: non-spontaneous
releases cancelling shape tools, the first pen stroke on a canvas lost to the
stylus device switch, `wait_idle` returning before vector selections had
their pixels (and its settle time stretching under GUI load), and strokes
ignored after a `Leave`. A Solstice core fix came out of it too: covered
views no longer repaint for the selection outline animation (branch
`claude/selection-ants-covered-views`). The user decided on phase 1 the
same day.

### Phase 1: minimum protocol

Authorized 2026-10-27; implemented on `claude/test-control-phase1` the same
day and checked on Linux (tools live, the three scenarios four times each,
phase 0 checks unchanged); Windows pending.

- The planned additions for documents, view reset, brush presets and
  modifiers; document paths restricted to a test root.
- Three scenarios: brush stroke then undo; rectangular selection then fill
  inside it; eraser stroke over a painted area.

Scope as implemented (details, scenario format and verification in [Test control](test-control.md)):

| Tool | Purpose |
| --- | --- |
| `document_close` | Close the active document, or all, discarding changes; also keeps long sessions from slowing down |
| `document_open`, `document_export_png` | Files only inside the test root given by `SOLSTICE_TEST_CONTROL_ROOT` |
| `view_reset` | Window size, zoom 100 %, no rotation or mirror, image centered |
| `brush_set` | Paintop preset by name, size, opacity, eraser mode |
| `input_click`, `input_drag`; `modifiers` on input tools | Conveniences; Shift, Ctrl and Alt as key events with canvas focus |
| `query_pixels` | Hash, non-transparent count and average color of a rectangle of the active layer or the projection |

Scenario files (format version 1) state their steps and expectations as
tool calls and compare state, preferring values a step saves and a later step
compares (before/after hashes) over absolute hashes, which can differ between
platforms. They are replayed by hand or by an LLM through MCP until the phase 2
runner exists. `query_layers`, `log_drain` and `ui_click` stay in phases 2-3.

### Phase 2: scenario runner and expectations

- Runner, report format, expectation storage in the repository, tolerance
  compare.

Authorized 2026-10-27 (see [decisions](#user-decisions-2026-10-26)) and
implemented the same day on `claude/test-control-phase1`; checked on Linux,
Windows pending. Details in [Test control](test-control.md#scenario-runner):

| Part | As implemented |
| --- | --- |
| Runner | `dev-tools/test-control/run_scenarios.py`, Python 3 standard library only: replays `*/tests/scenarios/*.json` against a running Solstice or one it launches with a scratch profile, closes documents between scenarios |
| Report | JSON (`solstice-test-control-report`, version 1) and a console summary; exit status 0, 1 (failure) or 2 (usage, file or connection) |
| Expectation storage | `<scenario>.expected.json` (recorded values) and `<scenario>.refs/<platform key>/*.png` (reference images) next to the scenario, written by `--record`, keyed by `<os>-<cpu|gpu>` |
| Tolerance compare | In the plugin (`query_pixels` with a reference): largest channel difference and a mismatch-pixel limit, against an 8-bit sRGB reference |

The runner is Python, unlike Solstice's features: it is a developer tool
outside the application (as the scripts under `build-tools/` and
`dev-tools/translations/`), it needs nothing but a Python 3 interpreter on
Windows and Linux, and it talks to the plugin only through the endpoint.
The project rule against new Python features concerns the application
([Codebase map](codebase-map.md)). The user can still ask for a native runner.

### Phase 3: tool and feature coverage

- Selection tools (rectangle, ellipse, polygon, freehand, contiguous) and
  selection modifiers; Transform Tool including Puppet Warp checklist items
  that are state-checkable (pin count, undo steps, applied pixels); Lazy Tools
  and Quick Access interactions through `ui_click`.
- Convert each feature's checklist items that state and pixels can decide; mark
  the rest as manual in the feature document.

Selection tools, selection modifiers and the Transform Tool / Puppet Warp
items: authorized 2026-10-27 ([decisions](#user-decisions-2026-10-26)) and
implemented the same day on `claude/test-control-phase3`; checked on Linux,
Windows pending. Details in [Test control](test-control.md):

| Part | As implemented |
| --- | --- |
| Selection scenarios | `plugins/tools/selectiontools/tests/scenarios/`: ellipse, polygon (closed on the first point, finished by a double click), freehand outline, contiguous, and the five modifier operations (Shift add, Alt subtract, Shift+Alt intersect, Ctrl+Alt symmetric difference, Ctrl replace) |
| Transform scenarios | `plugins/tools/tool_transform2/tests/scenarios/`: free move with Apply, undo and Return; Puppet Warp pin drag with anchoring, ring rotation then unlock (identity), Reset, Show mesh (no effect on output), pin deletion by Ctrl+Alt-click and by "Click pin: Delete pin" |
| New tools | `input_key`; `ui_query`, `ui_click`, `ui_set` and `docker_show` (widgets by object name, as planned for `ui_click`); `doubleClick` on `input_click` |
| Changed tools | Mouse strokes start with a hover move; `tool_activate` restarts an already active tool; `session_info` reports the UI languages |
| Runner | `--language` (English by default for `--launch`, since scenario texts are English) |
| Solstice change | Object names for the Puppet Warp click-action combo and the four order buttons, so tests can address them; no behavior change |

Lazy Tools and Quick Access: authorized and implemented the same day on the
same branch; checked on Linux, Windows pending:

| Part | As implemented |
| --- | --- |
| Lazy Tools scenarios | Color label and label visibility controls (`plugins/dockers/layerdocker/tests/scenarios/`), saved selections, foreground slots, Rename Alternative, Fast Image Export (`libs/ui/tests/scenarios/lazy-*.json`) |
| Quick Access scenarios | Palette brush item, Quick Brush Adjustments, HueSVC rows (`plugins/dockers/quickaccess/tests/scenarios/`) |
| New tools | `query_layers` (planned for phase 1), `layer_activate`, `ui_close` |
| Changed tools | Widget selectors by tool tip, class and window title; `async` on `action_trigger` and `ui_click` for modal dialogs; `ui_set` emits `activated`; `brush_set` reports flow, blending mode, preserve alpha and color channels; documents report their file name and modified state |
| Solstice change | A crash fix in `KisNodeModel` that the rename scenario found (queued updates after the document closed); see [Test control](test-control.md#known-limitations-and-pending-work) |

Left for later: `log_drain`; the Lazy Tools and Quick Access checks listed as
manual in [Lazy Tools](lazy-tools.md#manual-checks) and
[Quick Access](quick-access.md#automated-scenarios-2026-10-27); and the Puppet
Warp checklist items that a running transform's busy image keeps
from being read or that need more setup (preview against applied output,
undo of a pin deletion inside the transform, order buttons, rubber band,
mesh fill and expansion, multiple layers and groups), listed in
[Puppet Warp](puppet-warp.md#manual-regression-checklist).

### Phase 4: paint trace automation

- Replay the [baseline run](paint-trace-baseline-runs.md) stroke set with
  `KRITA_PAINT_TRACE` for CPU, projection and brush modes; record results as a
  new series, not pooled with hand-drawn runs.

Authorized and done 2026-10-27 on `claude/test-control-phase3`, Linux only:

| Part | As implemented |
| --- | --- |
| Series tool | `dev-tools/test-control/paint_trace_series.py`: a fresh process per run with the mode's `KRITA_GPU_*` switches and `KRITA_PAINT_TRACE`, the 16-stroke protocol as identical tablet strokes, normal exit, `build-tools/paint-trace/summarize.py`, checks, and the baseline's aggregation; [Test control](test-control.md#paint-trace-series) |
| Tool changes | `brush_set` `paintMode` (Build-up/Wash); `session_info` `gpuEngine` (the device the engine started on); the runner's launcher takes extra environment variables |
| First series | Nine processes, all accepted; results, hashes and interpretation in the [run sheet](paint-trace-baseline-runs.md#synthetic-series-linux-2026-10-27-test-control). GPU engine on the RTX 5090, canvas on Xvfb's software OpenGL |

Tracing overhead, the same day on `claude/test-control-phase4` (user:
"create new branch from main and start phase 4", after phase 4's series had
merged): test control gained trace-independent timing (`canvas_frames`, the
input results' press and release times, process CPU time in
`session_info`), and `paint_trace_series.py --overhead` ran 18 processes on
Linux. CPU time and frame cadence do not change with the trace; the stroke
tail grows by a few milliseconds only for projection-requested Build-up
([run sheet](paint-trace-baseline-runs.md#trace-overhead-linux-2026-10-27-test-control)).

Left: both series on Windows (with a hardware-GL canvas and the
shared-buffer path), where the README's numbers come from, and the README
operation workloads the protocol mentions (engine benchmarks on the
validation machine).

### Phase 5: optional CI

- Hosted Windows runners have no GPU and CI builds without tests. Running the
  harness there (software OpenGL, CPU paths only) is a separate decision after
  local use proves stable.
