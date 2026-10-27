# Test control (MCP) — agent development notes

## Status and scope

An application plugin that, when enabled by environment variables, serves a
Model Context Protocol (MCP) endpoint inside Solstice. An MCP client such as
Claude Code can create a document, switch tools, play synthetic mouse and
tablet strokes through the canvas input manager, wait for the image to become
idle, and read state and pixel hashes. The user guide is
[Test control](../test-control.md); the plan, user decisions and later phases
are in [UI test automation plan](ui-automation-plan.md).

Scope: phase 0 of the plan (feasibility, done 2026-10-27) and phase 1
(minimum protocol, in progress on `claude/test-control-phase1`): sixteen
tools, documents in the current window, RGBA documents created by
`document_new` or opened from the test root, modifier keys with canvas
focus, and [scenario files](#scenarios-format-version-1) replayed through
the tools. No scenario runner yet (phase 2).

Verification (2026-10-26, cloud session, no Krita build available):

- `TestControlProtocolTest` (11 cases) and `TestControlInputInjectorTest`
  (7 cases) pass when built standalone against Qt 6.4.2 on Linux (offscreen
  platform), outside Krita's CMake. Not yet run through `ctest`.
- Every plugin source passed `g++ -fsyntax-only -Wall -Wextra` against the
  repository headers, with stub headers for generated export/config files
  and KDE Frameworks. This is not a compile of the real target.
- The official MCP TypeScript SDK client (1.32.2, Streamable HTTP transport)
  connected to a standalone server built from the protocol sources:
  initialize, tools/list, two concurrent calls (run in order), a failing
  tool, an unknown tool and an unauthenticated connection behaved as
  specified.
- Windows (2026-10-26, clang, Qt 6.8): built in `_build` with the full
  tree; both tests pass through `ctest` after two test-only fixes (a
  `quint64` timestamp narrowed to 32-bit `ulong`, and a read loop that
  starved the same-thread server).
- Linux (2026-10-26, Ubuntu 26.04, GCC 15, Qt 6.10, Xvfb without a window
  manager, `xcb`): full build and install; both tests pass through `ctest`.
  The [manual checks](#manual-regression-checklist) were run against a
  running Solstice through the MCP endpoint; results are recorded there.
  Three input defects they found are fixed (spontaneous events, the hover
  move before a pen press, `wait_idle` settling). Step 10 stayed a known
  limitation until 2026-10-27 (fixed, see below).
- Windows (2026-10-27, Windows 11 25H2, Qt 6.8, `_install`, scratch
  `SOLSTICE_PROFILE_ROOT`, branch `claude/test-control-linux`): both tests
  pass through `ctest`; the manual checks were run against a running
  Solstice through the MCP endpoint; results are recorded there. They found
  that `wait_idle` returned before a vector selection had its pixels;
  fixed (600 ms settle while the current selection is a vector selection).
  Step 10 works on Windows.
- Linux re-run (2026-10-27, same setup as 2026-10-26, branch
  `claude/test-control-linux` at `a83b58884c`): both tests pass through
  `ctest` (three runs); the manual checks give the same results as on
  2026-10-26, now with the 600 ms settle; results recorded there.
- Step 10 fix (2026-10-27, Linux): the cause was a `Leave` the canvas got
  when a window covered the pointer, not focus; strokes now begin with an
  `Enter`. Step 10 passes on Linux; the rest of the checklist passes again.
  Windows has not been re-run with this change.
- Phase 1 (2026-10-27, Linux, same setup, branch `claude/test-control-phase1`):
  `TestControlPathsTest` (7 cases) and the injector test with the new
  modifier case (8 cases) pass through `ctest`. Live, through the endpoint:
  the new tools behave as described below, including rejected paths
  (`..`, absolute, wrong extension, missing directory, a test root that does
  not exist); a Shift drag with the rectangle selection tool adds to the
  selection (union bounds 400x300); the [three scenarios](#scenarios-format-version-1)
  pass in four runs each, and a copy with two wrong expectations fails on
  exactly those steps. The phase 0 checklist still passes. Windows not yet
  checked.

## Source locations

| Part | Files |
| --- | --- |
| Plugin entry, environment gate | `plugins/extensions/testcontrol/TestControlPlugin.{h,cpp}`, `kritatestcontrol.json` |
| HTTP endpoint (QtNetwork only) | `TestControlHttpServer.{h,cpp}` |
| JSON-RPC/MCP handling, call queue (QtCore only) | `TestControlMcpServer.{h,cpp}` |
| Synthetic pointer strokes and modifier keys (QtWidgets only) | `TestControlInputInjector.{h,cpp}` |
| Canvas tools: session, documents from scratch, tools, input, waits, queries | `TestControlCanvasTools.{h,cpp}` |
| Document tools: open, export, close, view reset, brush | `TestControlDocumentTools.{h,cpp}` |
| Helpers shared by the tool classes, `pollUntil` | `TestControlSupport.{h,cpp}` |
| Test root path checks (QtCore only) | `TestControlPaths.{h,cpp}` |
| Tests | `tests/TestControlProtocolTest.cpp`, `tests/TestControlInputInjectorTest.cpp`, `tests/TestControlPathsTest.cpp` |
| Scenarios (next to the covered feature's tests) | `plugins/paintops/defaultpaintops/brush/tests/scenarios/brush-stroke-undo.json`, `eraser-stroke.json`; `libs/ui/tests/scenarios/rect-select-fill.json` |
| Hook in an upstream file | `plugins/extensions/CMakeLists.txt` (`add_subdirectory(testcontrol)` only when `BUILD_WITH_QT6`) |

## Build, test, and install

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritatestcontrol TestControlProtocolTest TestControlInputInjectorTest TestControlPathsTest -j 2"
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && ctest --test-dir <krita-dev-root>\_build -R TestControl --output-on-failure"
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\extensions\testcontrol\cmake_install.cmake"
```

The new directory needs a CMake reconfigure (triggered by the build). The
plugin links `kritaui` and `Qt6::Network`; it changes no shared library.
The protocol and injector sources build without Krita, which is how they were
tested in the cloud session.

## Configuration and compatibility

| Variable | Meaning |
| --- | --- |
| `SOLSTICE_TEST_CONTROL_PORT` | Port 1024-65535. Unset: the plugin does nothing. |
| `SOLSTICE_TEST_CONTROL_TOKEN` | Required bearer token, at least 16 characters; otherwise a warning is logged and nothing listens. Never logged. |
| `SOLSTICE_TEST_CONTROL_ROOT` | Optional. An existing absolute directory, the only place `document_open` and `document_export_png` read and write. Logged at start. Unset: those two tools fail; set but not an existing directory: a warning, and they fail. |

- Endpoint: `http://127.0.0.1:<port>/mcp`, MCP Streamable HTTP, JSON
  responses only (no SSE stream; `GET` returns 405). Protocol versions
  `2025-06-18`, `2025-03-26` and `2024-11-05`; an unknown requested version
  gets `2025-06-18`.
- Server name `solstice-test-control`, version
  `KritaVersionWrapper::solsticeVersionString()`.
- No `kritarc` keys, no persisted data, no actions, no menu entries and no
  new translatable strings. `document_new` passes an empty document name for
  that reason.
- The plugin ships in release packages (user decision 2026-10-26) and is
  inert there unless both variables are set.
- Built only with Qt 6 (`QPointingDevice`, `QTabletEvent` constructors).
- Use a scratch `SOLSTICE_PROFILE_ROOT` for test sessions
  ([Settings location](settings-location.md)). Start no test instance while
  another Solstice runs: the single-instance check in `krita/main.cc` is not
  keyed by the profile root, so the second launch hands its arguments to the
  running instance and exits (operational rule, user decision 2026-10-26).

### Tools (phases 0 and 1)

| Tool | Arguments | Result |
| --- | --- | --- |
| `session_info` | none | Solstice/Krita/Qt versions, platform, window count, active tool, document, canvas widget (class, size, focus), GPU/trace environment variables |
| `document_new` | `width`, `height` (1-16384, default 800x600), `colorDepth` (`U8`/`U16`/`F16`/`F32`) | Document and canvas once the new view is active (RGBA, one transparent paint layer, white canvas color, 300 ppi) |
| `tool_activate` | `id` (factory ID) | Active tool ID; an unknown ID lists all IDs |
| `input_stroke` | `points` (`x`, `y`, optional `pressure`, `xTilt`, `yTilt`, `t` from 0 to 120000 ms), `device` (`mouse`/`tablet`), `coordinates` (`image`/`widget`), `button`, `realtime`, `modifiers` (`shift`, `ctrl`, `alt`, `meta`) | Events sent, press/release accepted flags, duration, points outside the canvas widget, modifiers, active tool |
| `input_click` | `x`, `y`, `device`, `coordinates`, `button`, `pressure`, `modifiers` | As `input_stroke`; one press and release |
| `input_drag` | `from`, `to` (`x`, `y`), `steps` (default 20), `durationMs` (default 200), `device`, `coordinates`, `button`, `pressure`, `realtime`, `modifiers` | As `input_stroke`; a straight line in equal steps |
| `wait_idle` | `timeoutMs` (default 10000) | `KisImage::isIdle()` held on three polls 20 ms apart, and, while the current selection has a shape selection, for 600 ms plus twice the longest poll gap, measured with a clock; waited time (at least 40 ms, or about 640 ms), longest gap between polls (`maxPollGapMs`) |
| `query_state` | `includeLayerHash` | Active tool, document, undo count/index/last command text, selection (bounds, selected pixel count, SHA-256 of the selection projection), active layer (name, class, exact bounds, color space, SHA-256 of its pixels) |
| `action_trigger` | `name` | Triggers a main window action (`edit_undo`, `deselect`, tool actions, ...) |
| `canvas_capture` | `maxSize` | PNG of the canvas widget as an MCP image block; diagnostic only |
| `query_pixels` | `x`, `y`, `width`, `height` (image pixels, default the whole image, at most 4096x4096), `source` (`layer`/`projection`), `includePng`, `maxPngSize` | SHA-256 of the device's pixels, non-transparent and opaque counts, mean alpha, mean color of the non-transparent pixels (8-bit sRGB), color space; optional PNG |
| `document_open` | `path` (relative to the test root) | Opens without dialogs (`KisMainWindow::BatchMode`); document and canvas once its view is active |
| `document_export_png` | `path` (relative, `.png`) | Exports the active document's image without dialogs, replacing a file; size in bytes |
| `document_close` | `all` | Closes the active document or every document without asking; documents closed and left |
| `view_reset` | `width`, `height` (window, default 1280x900) | Normal window of that size, zoom 100 %, no rotation or mirroring, image centered; zoom, rotation, image rectangle in the canvas |
| `brush_set` | `preset` (name), `size` (1-1000), `opacity` (0-1), `eraser`, `color` (`#RRGGBB`) | Current preset, size, opacity, eraser mode and foreground color; an unknown preset lists up to 200 names |

Tool failures are MCP tool results with `isError: true` and an `error`
field; protocol errors (unknown tool, malformed arguments) are JSON-RPC errors.

## Architecture and lifecycle

- **Loading.** `Krita/ApplicationPlugin`, loaded once with `qApp` as parent
  while the first main window is constructed (`KisMainWindow` constructor).
  The constructor reads the environment and opens the socket only; tools
  resolve `KisPart::currentMainwindow()`, its view manager, canvas and image
  on every call and fail with a message when none exists. On
  `QCoreApplication::aboutToQuit` the server stops listening.
- **HTTP.** `TestControlHttpServer` binds `QHostAddress::LocalHost` (IPv4;
  clients should use `127.0.0.1`, not `localhost`). Checks in order: Host
  must be `127.0.0.1:<port>` or `localhost:<port>`, Origin (if present) the
  same as `http://`, path `/mcp`, `Authorization: Bearer <token>` (constant
  time), method `POST`, no `Transfer-Encoding`, `Content-Length` up to 8 MiB,
  head up to 32 KiB. `Expect: 100-continue` gets an interim response. One
  request per connection (`Connection: close`); idle connections close after
  30 s. A reply after the client disconnected is dropped.
- **MCP.** `TestControlMcpServer` answers `initialize`, `ping` and
  `tools/list` immediately and queues `tools/call`: calls run one at a time in
  arrival order, each started from the event loop, because they share one
  canvas. Notifications and client responses get 202. A handler's reply is
  accepted once.
- **Input.** `TestControlInputInjector` delivers events to the canvas
  widget, so `KisInputManager::eventFilter()` (installed on that widget)
  handles them like device input and passes them to the shortcut matcher and
  the active tool. Every event is marked spontaneous
  (`QSpontaneKeyEvent::setSpontaneous()` from the inline
  `QtTest/qtestspontaneevent.h`; the plugin does not link QtTest) and
  delivered with `QCoreApplication::notify()`, as QTest does:
  `KisToolRectangleBase` and `KisToolOutlineBase` cancel the shape on a
  non-spontaneous release, and `sendEvent()` clears the flag in Qt 6.
  Every stroke starts with an `Enter` at the first point, as when a real
  pointer reaches the canvas: after a `Leave` (for example when another
  window appeared over the pointer) `KisShortcutMatcher` suppresses all
  actions until the next `Enter`. Mouse: press, moves, release. Tablet: `TabletEnterProximity` (to the
  application object, where `ProximityNotifier` listens), one hover
  `TabletMove` (no buttons, no pressure) at the first point, `TabletPress`
  `HoverSettleMs` (50 ms) later, a `Qt::MouseEventSynthesizedBySystem` mouse
  press, `TabletMove`s, `TabletRelease`, a synthesized mouse release and
  `TabletLeaveProximity`. The hover move lets `KoToolProxy::tabletEvent()`
  switch the canvas to the stylus before the press: the first stylus event
  on a canvas creates new tool instances (`KoToolManager::switchInputDevice`),
  which ended a stroke already started, so the first pen stroke on each new
  canvas painted only a dot. The wait outlasts the input manager's move
  compression (`tabletEventsDelay`, 10 ms), which a press would discard.
  The synthesized press reproduces the one Windows generates after a pen
  press; the input manager eats exactly one mouse press after each
  `TabletPress` (`EventEater::peckish`), and without it the next mouse stroke
  would lose its press. Leaving proximity re-enables mouse events
  (`allowMouseEvents()`). Events carry timestamps on a monotonic clock; in
  real-time mode each point is sent at its `t` offset.
- **Modifiers.** With `modifiers`, the canvas tools activate the window,
  give the canvas widget keyboard focus and wait up to 2 s for it, then the
  injector sends `ShortcutOverride` and `KeyPress` for Ctrl, Shift, Alt and
  Meta after the `Enter`, sets the modifiers on every pointer event and ends
  with `KeyRelease`s. A modifier key's own event carries the state before it,
  as from the platform (`QKeyEvent::modifiers()` toggles that key's flag).
  Without focus, `KisInputManager` reads the real keyboard instead
  (`fixShortcutMatcherModifiersState()`), so the call fails if the canvas
  cannot get focus.
- **Documents.** `document_close` follows libkis `Document::close()`: clear
  the modified flag, `closePath(false)`, close and delete the document's
  views, then `KisPart::removeDocument()`. `document_open` uses
  `KisMainWindow::openDocument()` with `BatchMode`, and the document keeps
  batch mode. `document_export_png` switches the document to batch mode
  around `exportDocumentSync()` and restores it.
- **Test root.** `TestControlPaths` accepts only relative paths and resolves
  `..` and symbolic links (`QFileInfo::canonicalFilePath()`); the result must
  stay inside the canonical root (case-insensitive on Windows). An export
  target's directory must exist.
- **Coordinates.** Image coordinates are converted with
  `KisCoordinatesConverter::imageToWidget()` of the current canvas; points
  outside the widget are still sent and counted.
- **Reads.** `query_state` takes `KisImage::tryBarrierLock(true)` and fails
  instead of blocking when the image is busy. Undo commands reach the stack
  before the image reports idle (`KisDocument`'s undo stack pushes from
  stroke threads through a blocking queued call).

## Invariants

- Without `SOLSTICE_TEST_CONTROL_PORT` the plugin creates no server, timer,
  device or object besides itself.
- Never listen on a non-loopback address; never accept a request without the
  token, with a foreign Host, or with a foreign Origin.
- The token never appears in logs, results or documents.
- Tool calls stay serialized; input goes through the canvas widget's event
  filters, not directly to tools or strokes, so tests exercise the real
  input path.
- No user-visible strings, settings, actions or persisted formats.

## Manual regression checklist

Phase 0 exit criteria. Start Solstice from a shell with a scratch profile and
the two variables (see the [user guide](../test-control.md)), connect an MCP
client, and record the results with the date in this document.

1. Without the variables: no "Test control" log line, and nothing listens on
   the port (`netstat -ano | findstr :8765`).
2. With a token shorter than 16 characters: a warning, nothing listens.
3. With both variables: the log shows the endpoint; `session_info` returns
   versions and `"document": null` on the welcome screen.
4. `document_new` (800x600): the document appears and `canvas` reports the
   canvas widget class and size.
5. `tool_activate` `KritaShape/KisToolBrush`, then a mouse `input_stroke`
   from (100,100) to (400,300) with about 30 points, `wait_idle`,
   `query_state`: undo count 1, a freehand stroke command, non-empty layer
   bounds around the line, `pressAccepted` true.
6. A tablet stroke with pressure rising from 0.1 to 1.0 using a preset with
   pressure-controlled size: undo count 2; `canvas_capture` shows the width
   growing along the stroke.
7. A mouse stroke right after the tablet stroke paints (undo count 3), which
   confirms the eaten-press and proximity handling.
8. `tool_activate` `KisToolSelectRectangular`, a two-point mouse stroke from
   (100,100) to (300,200), `wait_idle`, `query_state`: selection bounds about
   (100,100) 200x100 and about 20000 selected pixels.
9. `action_trigger` `deselect`, then `edit_undo`: the selection and undo
   index change accordingly.
10. Repeat 5 with another application's window in front of Solstice; record
    whether the stroke works without focus.
11. Close Solstice during a real-time stroke of a few seconds: no crash.
12. `curl` without the token gets 401; with `-H "Origin: http://example.com"`
    gets 403.

Results, Linux (2026-10-26, Ubuntu 26.04, Qt 6.10, `xcb` on Xvfb without a
window manager, scratch `SOLSTICE_PROFILE_ROOT`, branch
`claude/test-control-linux`):

| Step | Result |
| --- | --- |
| 1 | Pass: no log line; nothing listens on 8765 (`ss -ltn`). |
| 2 | Pass: "must have at least 16 characters; not started"; nothing listens. |
| 3 | Pass: endpoint logged, listens on `127.0.0.1` only; eight tools listed; `"document": null`. |
| 4 | Pass: 800x600 RGBA; canvas `KisOpenGLCanvas2` 1266x1529. |
| 5 | Pass: undo 1 "Freehand Brush Stroke", bounds (80,80) 340x240, press accepted; the canvas had no focus. |
| 6 | Pass after the hover fix: the first pen stroke on the canvas paints along the whole line, width and opacity growing with pressure. Before it, that stroke painted a dot (later pen strokes were fine). `pressAccepted`/`releaseAccepted` are `false` on Linux by design (see limitations). |
| 7 | Pass: undo 3, bounds extended by the mouse line. |
| 8 | Pass after the spontaneous fix: "Select Rectangle", bounds (100,100) 200x100, 20000 pixels. Before it, the tool showed the 200x100 drag and cancelled it on release. |
| 9 | Pass: `deselect` adds "Deselect" and clears it; `edit_undo` restores the 200x100 selection. |
| 10 | Limitation: with another application's window holding X focus, a synthetic stroke is accepted but paints nothing (undo unchanged). With no application focused, or Solstice focused, strokes paint. Same before and after the fixes. |
| 11 | Pass: Ctrl+Q during a 4 s real-time stroke closes Solstice (exit 0); the injector reports "The target widget was destroyed during the stroke". With an unsaved change (run on the build before the fixes), the save prompt appears mid-stroke and "No" exits cleanly too. |
| 12 | Pass: 401 without or with a wrong token, 403 for a foreign Origin or Host, 405 for GET, 200 for a valid ping; the token is not in the log. |

A read right after `wait_idle` once returned an empty selection after "Select
Rectangle" (1 in 25 runs) when `wait_idle` returned on its first check; with
the three-poll settle it did not recur in 75 runs.

Results, Windows (2026-10-27, Windows 11 25H2, Qt 6.8, scratch
`SOLSTICE_PROFILE_ROOT` under `%LOCALAPPDATA%\Temp`, Japanese UI, Solstice
started from `_install`, MCP calls from a Python client):

| Step | Result |
| --- | --- |
| 1 | Pass: no log line; nothing listens on 8765 (`netstat -ano`). |
| 2 | Pass: "must have at least 16 characters; not started"; nothing listens; the token is not in the log. |
| 3 | Pass: endpoint logged, listens on `127.0.0.1` only; eight tools listed; `"document": null`. |
| 4 | Pass: 800x600 RGBA; canvas `KisOpenGLCanvas2` 1370x1260. |
| 5 | Pass: undo 1 "Freehand Brush Stroke", bounds (80,80) 340x240, press accepted. |
| 6 | Pass: the first pen stroke on the canvas paints along the whole line, width and opacity growing with pressure. `pressAccepted`/`releaseAccepted` are `true` on Windows. |
| 7 | Pass: undo 3, bounds extended by the mouse line. |
| 8 | Pass after the `wait_idle` fix: "Select Rectangle", bounds (100,100) 200x100, 20000 pixels. Before it, the check passed only with a pause before the read; back-to-back calls read an active, empty selection or a busy image (see below). |
| 9 | Pass: `deselect` adds "Deselect" and clears it; `edit_undo` restores the 200x100 selection. |
| 10 | Pass, unlike Linux: with another window in the foreground (`windowActive` and `hasFocus` false), mouse and pen strokes paint (undo, bounds and hash change). |
| 11 | Pass: closing the window 1.5 s into a 4 s real-time stroke with an unsaved change shows the save prompt while the stroke goes on (all 81 events sent); "No" exits with 0. With no unsaved change, Solstice exits 0.4 s after the close request (exit 0); the HTTP call gets no response (the connection closes) instead of the "target widget was destroyed" error seen on Linux. No crash either way. |
| 12 | Pass: 401 without or with a wrong token, 403 for a foreign Origin or Host, 405 for GET, 200 for a valid ping. |

The selection tools default to vector selections, whose pixels Krita renders
from timers that `KisImage::isIdle()` does not report
([pitfall](wiki/pitfalls/synthetic-canvas-input.md)). With the three-poll
settle, a read right after `wait_idle` was correct in 1 of 25 back-to-back
selections (2 of 15 in a second series); the selection was always right
0.5 s later. With the 600 ms settle: 75 of 75 correct, no busy reads;
`wait_idle` then takes 0.6-1.1 s while such a selection exists and still
about 40 ms without one.

Results, Linux re-run (2026-10-27, Ubuntu 26.04, Qt 6.10, `xcb` on Xvfb
without a window manager, fresh scratch `SOLSTICE_PROFILE_ROOT` and `HOME`,
branch `claude/test-control-linux` at `a83b58884c`):

| Step | Result |
| --- | --- |
| 1-2 | Pass, as on 2026-10-26. |
| 3 | Pass: endpoint logged, listens on `127.0.0.1` only; `"document": null`. |
| 4 | Pass: 800x600 RGBA; canvas `KisOpenGLCanvas2` 1266x1529. |
| 5 | Pass: undo 1 "Freehand Brush Stroke", bounds (80,80) 340x240. |
| 6 | Pass: the first pen stroke on the canvas paints the whole line (bounds to x 718); accepted flags `false` as expected on Linux. |
| 7 | Pass: undo 3. |
| 8 | Pass: "Select Rectangle", bounds (100,100) 200x100, 20000 pixels, read right after `wait_idle`. |
| 9 | Pass: `deselect` clears it; `edit_undo` restores it. |
| 10 | Limitation unchanged: with xclock holding X focus, the stroke is accepted but paints nothing (undo 0). Investigated and fixed afterwards; see below. |
| 11 | Pass: Ctrl+Q 1.5 s into a 4 s real-time stroke (no unsaved change) closes Solstice with exit 0; the injector reports "The target widget was destroyed during the stroke" after 72 events. No crash. |
| 12 | Pass: 401 without the token, 403 for a foreign Origin, 200 for a valid ping; the token is not in the log. |

Back-to-back reads (new document, brush stroke, rectangle selection,
`wait_idle`, `query_state`): 75 of 75 correct, no empty and no busy reads.
`wait_idle` after the selection grows with the number of open documents, as
measured in one session that adds a document per round:

| Open documents | `wait_idle` after the selection |
| --- | --- |
| 1-4 | 600-617 ms |
| 5-7 | 721-775 ms |
| 8-11 | 886-958 ms |
| 12 | 1180-1254 ms |
| up to 75 | median 6.1 s, maximum 10.5 s (past the 10 s default timeout) |

A second `wait_idle` right after the first, with nothing new queued, takes
as long, and `wait_idle` after a brush stroke (no selection) stays at 40 ms.
The explanation first assumed here (the image going busy and restarting the
31-poll count) was wrong; see the investigation below.

The slowdown, investigated 2026-10-27 with temporary instrumentation of
`wait_idle` (polls, busy polls, gaps between polls):

- The image never went busy (0 busy polls in every call). The 20 ms polls
  were stretched: the longest gap grew from 20 ms (20 documents without
  selections) to 60 ms (one selection), 172 ms (16 selections in 36
  documents) and about 300 ms. Documents without selections added no load.
- Stack samples of the GUI thread (gdb, 25 samples) were all in
  `QWidgetRepaintManager::paintAndFlush`, mostly `KisOpenGLCanvas2::paintGL`
  and framebuffer blits (software OpenGL under Xvfb).
- `KisSelectionDecoration` runs a 150 ms "marching ants" timer in every view
  with an active selection and repaints the canvas on each tick. Background
  documents in the tabbed document area are covered, not hidden
  (`isVisible()` stays true), so they keep repainting. A temporary check that
  skipped the tick when `visibleRegion()` was empty brought 36 documents with
  selections from 5.4 s to 0.9 s (longest gap 73 ms); not kept (a change to
  core behavior, for the user to decide).
- Fix in test control: the 600 ms are measured with a clock instead of 31
  polls, plus twice the longest poll gap (the selection's own timers run on
  the same slowed event loop). Worst case, core unchanged and documents with
  selections accumulating: 40 of 40 reads correct; `wait_idle` after the
  selection 683 ms with 5 documents (gap 41 ms), 1006 ms with 25 (179 ms),
  1246 ms with 40 (296 ms). The whole checklist passes again.

Step 10 on Linux, investigated 2026-10-27 with `DEBUG_MATCHER` traces of
`KisShortcutMatcher` (a temporary local build): the failing strokes reached
`buttonPressed()` but started no action, because `prepareReadyShortcuts()`
returns nothing while `actionsSuppressed()` is true, which it is whenever
`cursorEntered` is false. The canvas gets a `Leave` when another window
(xclock) is mapped over the pointer, and nothing sends an `Enter` again
until the pointer moves or Solstice gets focus (`CanvasSwitcher` sends a
synthetic `Enter` on `FocusIn`). Focus itself is not the cause: with xclock
focused but not covering the pointer, or covering it while Solstice had
focus, strokes painted. On Windows the foreground window was not over the
pointer, so step 10 passed there. Fix: the injector sends a spontaneous
`QEnterEvent` before each stroke. After it, with xclock covering the canvas
and holding focus, mouse and pen strokes paint in full (5 of 5 rounds), and
the whole checklist passes again on Linux, including step 11 with a save
prompt during the stroke (202 events sent, "No" exits 0).

## Scenarios (format version 1)

A scenario is a JSON file next to the covered feature's tests (user
decision): the steps are tool calls, each with optional expectations and
saved values. Until the phase 2 runner exists, an LLM or a person replays it
through the endpoint.

```json
{
  "format": "solstice-test-control-scenario",
  "version": 1,
  "name": "brush-stroke-undo",
  "description": "What the scenario shows.",
  "covers": "What it exercises.",
  "steps": [
    {"call": "query_pixels", "save": {"emptyLayer": "sha256"}},
    {"call": "input_stroke", "args": {"points": [{"x": 100, "y": 150}]}},
    {"call": "wait_idle"},
    {"call": "query_state", "expect": {"undo.count": 1, "undo.lastCommand": "Freehand Brush Stroke"}},
    {"call": "query_pixels", "expect": {"sha256": {"notEquals": "$emptyLayer"}, "nonTransparentPixels": {"min": 10000}}}
  ]
}
```

- `call` and `args`: a tool and its arguments. A step fails if the tool
  returns an error.
- `expect`: dotted paths into the tool's structured result. A plain value
  must be equal; an object with `equals`, `notEquals`, `min` and/or `max`
  compares accordingly. A string starting with `$` names a saved value.
- `save`: stores result values under names for later steps.
- Prefer state (undo, bounds, counts) and values saved earlier in the same run
  (a hash before and after) over absolute hashes, which can differ between
  platforms and builds. Every step that changes the image is followed by
  `wait_idle`; scenarios end with `document_close`.

| Scenario | Shows |
| --- | --- |
| `plugins/paintops/defaultpaintops/brush/tests/scenarios/brush-stroke-undo.json` | A Basic-5 mouse stroke paints black pixels within the expected bounds; undo restores the empty layer's hash; redo restores the painted hash |
| `libs/ui/tests/scenarios/rect-select-fill.json` | A mouse-drawn 200x100 selection (vector) is exact; Fill with Foreground Color fills exactly those 20000 pixels red; deselect |
| `plugins/paintops/defaultpaintops/brush/tests/scenarios/eraser-stroke.json` | On a layer filled blue, a 40 px eraser-mode stroke leaves no pixel under its center line and none touched far from it; undo restores the filled hash |

## Known limitations and pending work

- An active selection limits where strokes paint. A scenario that selects
  and then paints elsewhere must `deselect` first; otherwise the stroke adds
  an undo step and changes no pixels.
- `wait_idle` waits 600 ms of idle polls whenever the current selection is
  a vector selection, also for strokes that do not touch it.
- Every open document with a selection repaints its canvas every 150 ms
  (marching ants), also in background tabs, which slows the GUI thread as
  documents accumulate, and with software OpenGL a lot. `wait_idle` now
  measures its settle time with a clock (about 1.2 s with 40 such documents
  instead of over 10 s), but every call still slows down. Close documents a
  scenario no longer needs (`document_close`). The core cause is in
  `KisSelectionDecoration`; a fix is on branch
  `claude/selection-ants-covered-views`.
- `pressAccepted`/`releaseAccepted` report Qt's accepted flag. On Linux
  without Krita's Qt enter/leave patch, the input manager clears it for every
  tablet press, move and release so that Qt synthesizes mouse events, so
  tablet strokes report `false` even when they paint. Judge strokes by the
  undo stack, bounds and hashes.
- On Linux, autosaves of untitled documents go to `$HOME`
  (`krita-<pid>-document_<n>-autosave.kra`), not to `SOLSTICE_PROFILE_ROOT`.
  A test instance stopped with unsaved documents leaves them there, and the
  next start reopens them, so the active view is not the one `document_new`
  made. Quit test instances through the save prompt, or remove those files.

Findings from reading the input code (2026-10-26), to confirm in phase 0:

- Modifiers: `KisInputManager::eventFilterImpl()` resyncs the shortcut
  matcher from the real keyboard whenever the canvas widget lacks focus or an
  event's modifiers disagree with the matcher. Phase 1 therefore sends key
  events and takes canvas focus, which activates the Solstice window (on
  Windows the system may refuse to bring it to the front; the call then
  fails). See the [pitfall](wiki/pitfalls/synthetic-canvas-input.md).
- Events are sent to the current view's canvas widget. The input manager
  only processes events for its current `eventsReceiver` (set when a canvas
  is added or focused); with several views open, the target may differ.
- The real user's keyboard state, a pen in proximity, or popups over the
  canvas can interfere with a running test.
- Real tablet drivers, pressure curves as felt, latency and visual judgement
  remain manual (plan, [What stays manual](ui-automation-plan.md#what-stays-manual)).
- Not implemented: the scenario runner and tolerance comparison (phase 2),
  `query_layers`, `log_drain` and `ui_click` (phases 2-3).
