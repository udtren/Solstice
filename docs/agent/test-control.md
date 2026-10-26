# Test control (MCP) — agent development notes

## Status and scope

An application plugin that, when enabled by environment variables, serves a
Model Context Protocol (MCP) endpoint inside Solstice. An MCP client such as
Claude Code can create a document, switch tools, play synthetic mouse and
tablet strokes through the canvas input manager, wait for the image to become
idle, and read state and pixel hashes. The user guide is
[Test control](../test-control.md); the plan, user decisions and later phases
are in [UI test automation plan](ui-automation-plan.md).

Scope is phase 0 of the plan (feasibility): eight tools, one document at a
time in the current window, RGBA documents only, no modifiers, no file
commands.

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
- Not built in `_build`, not installed, not run in Solstice. No interactive
  confirmation. The [manual checks](#manual-regression-checklist) are the
  phase 0 exit criteria.

## Source locations

| Part | Files |
| --- | --- |
| Plugin entry, environment gate | `plugins/extensions/testcontrol/TestControlPlugin.{h,cpp}`, `kritatestcontrol.json` |
| HTTP endpoint (QtNetwork only) | `TestControlHttpServer.{h,cpp}` |
| JSON-RPC/MCP handling, call queue (QtCore only) | `TestControlMcpServer.{h,cpp}` |
| Synthetic pointer strokes (QtWidgets only) | `TestControlInputInjector.{h,cpp}` |
| Tools acting on Solstice | `TestControlCanvasTools.{h,cpp}` |
| Tests | `tests/TestControlProtocolTest.cpp`, `tests/TestControlInputInjectorTest.cpp` |
| Hook in an upstream file | `plugins/extensions/CMakeLists.txt` (`add_subdirectory(testcontrol)` only when `BUILD_WITH_QT6`) |

## Build, test, and install

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritatestcontrol TestControlProtocolTest TestControlInputInjectorTest -j 2"
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

### Tools (phase 0)

| Tool | Arguments | Result |
| --- | --- | --- |
| `session_info` | none | Solstice/Krita/Qt versions, platform, window count, active tool, document, canvas widget (class, size, focus), GPU/trace environment variables |
| `document_new` | `width`, `height` (1-16384, default 800x600), `colorDepth` (`U8`/`U16`/`F16`/`F32`) | Document and canvas once the new view is active (RGBA, one transparent paint layer, white canvas color, 300 ppi) |
| `tool_activate` | `id` (factory ID) | Active tool ID; an unknown ID lists all IDs |
| `input_stroke` | `points` (`x`, `y`, optional `pressure`, `xTilt`, `yTilt`, `t` from 0 to 120000 ms), `device` (`mouse`/`tablet`), `coordinates` (`image`/`widget`), `button`, `realtime` | Events sent, press/release accepted flags, duration, points outside the canvas widget, active tool |
| `wait_idle` | `timeoutMs` (default 10000) | `KisImage::isIdle()` reached, waited time |
| `query_state` | `includeLayerHash` | Active tool, document, undo count/index/last command text, selection (bounds, selected pixel count, SHA-256 of the selection projection), active layer (name, class, exact bounds, color space, SHA-256 of its pixels) |
| `action_trigger` | `name` | Triggers a main window action (`edit_undo`, `deselect`, tool actions, ...) |
| `canvas_capture` | `maxSize` | PNG of the canvas widget as an MCP image block; diagnostic only |

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
- **Input.** `TestControlInputInjector` sends events with
  `QCoreApplication::sendEvent()` to the canvas widget, so
  `KisInputManager::eventFilter()` (installed on that widget) handles them
  like device input and passes them to the shortcut matcher and the active
  tool. Mouse: press, moves, release. Tablet: `TabletEnterProximity` (to the
  application object, where `ProximityNotifier` listens), `TabletPress`, a
  `Qt::MouseEventSynthesizedBySystem` mouse press, `TabletMove`s,
  `TabletRelease`, a synthesized mouse release and `TabletLeaveProximity`.
  The synthesized press reproduces the one Windows generates after a pen
  press; the input manager eats exactly one mouse press after each
  `TabletPress` (`EventEater::peckish`), and without it the next mouse stroke
  would lose its press. Leaving proximity re-enables mouse events
  (`allowMouseEvents()`). Events carry timestamps on a monotonic clock; in
  real-time mode each point is sent at its `t` offset.
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

## Known limitations and pending work

Findings from reading the input code (2026-10-26), to confirm in phase 0:

- Modifiers are not supported. `KisInputManager::eventFilterImpl()` resyncs
  the shortcut matcher from `QApplication::queryKeyboardModifiers()` (the
  real keyboard) whenever the canvas widget lacks focus or an event's
  modifiers disagree with the matcher. Synthetic Ctrl/Shift/Alt therefore
  need key events and canvas focus; see the
  [pitfall](wiki/pitfalls/synthetic-canvas-input.md).
- Events are sent to the current view's canvas widget. The input manager
  only processes events for its current `eventsReceiver` (set when a canvas
  is added or focused); with several views open, the target may differ.
- The real user's keyboard state, a pen in proximity, or popups over the
  canvas can interfere with a running test.
- Real tablet drivers, pressure curves as felt, latency and visual judgement
  remain manual (plan, [What stays manual](ui-automation-plan.md#what-stays-manual)).
- Not implemented: modifier keys, document open/export/close, view reset,
  brush preset and size selection, scenario files and runner, tolerance
  comparison. These are phases 1-3 of the plan.
