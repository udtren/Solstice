# Test control

Test control lets an AI assistant or a test script operate a running
Solstice for testing. It can create a document, switch tools, draw mouse and
pen strokes on the canvas, and read back what happened: the undo history, the
selection and a fingerprint of the active layer's pixels. It uses the
[Model Context Protocol](https://modelcontextprotocol.io/) (MCP), so MCP
clients such as Claude Code can connect to it directly.

It is meant for developers and testers. It is off unless you start Solstice
with the settings below, and it then accepts connections only from your own
computer and only with a password (token) you choose.

> **Status: early test version.** The tools for drawing, selecting,
> measuring results, opening and saving test images and choosing brushes are
> in place and have been checked in a running Solstice on Linux; the second
> set (from opening files to modifier keys) and the scenario runner have not
> been checked on Windows yet.

## Starting Solstice with test control

Close every running Solstice first. If Solstice is already running, a second
start passes its request to the running copy and exits, without test
control.

Use a separate settings folder for testing, so tests do not change your
own settings (see [Settings folder](settings-folder.md)).

In PowerShell:

```powershell
$env:SOLSTICE_PROFILE_ROOT = "$env:TEMP\solstice-test-profile"
$env:SOLSTICE_TEST_CONTROL_PORT = "8765"
$env:SOLSTICE_TEST_CONTROL_TOKEN = [guid]::NewGuid().ToString("N")
$env:SOLSTICE_TEST_CONTROL_ROOT = "$env:TEMP\solstice-test-files"   # optional, see Test folder
New-Item -ItemType Directory -Force $env:SOLSTICE_TEST_CONTROL_ROOT | Out-Null
$env:SOLSTICE_TEST_CONTROL_TOKEN   # note the token for the MCP client
& "C:\path\to\Solstice\bin\solstice.exe"
```

On Linux, in a shell:

```bash
export SOLSTICE_PROFILE_ROOT="$HOME/solstice-test-profile"
export SOLSTICE_TEST_CONTROL_PORT=8765
export SOLSTICE_TEST_CONTROL_TOKEN="$(openssl rand -hex 16)"
export SOLSTICE_TEST_CONTROL_ROOT="$HOME/solstice-test-files"   # optional, see Test folder
mkdir -p "$SOLSTICE_TEST_CONTROL_ROOT"
echo "$SOLSTICE_TEST_CONTROL_TOKEN"   # note the token for the MCP client
/path/to/solstice/bin/krita
```

| Setting | Meaning |
| --- | --- |
| `SOLSTICE_TEST_CONTROL_PORT` | Port number from 1024 to 65535. Without it, test control stays off. |
| `SOLSTICE_TEST_CONTROL_TOKEN` | The token clients must send; at least 16 characters. Without it, test control stays off. |
| `SOLSTICE_TEST_CONTROL_ROOT` | Optional. The folder where documents may be opened and saved (see [Test folder](#test-folder)). |

The address is `http://127.0.0.1:<port>/mcp`.

## Connecting Claude Code

Start Claude Code from the same PowerShell window, so it sees the token
variable, and add the server:

```powershell
claude mcp add --transport http solstice http://127.0.0.1:8765/mcp --header "Authorization: Bearer $env:SOLSTICE_TEST_CONTROL_TOKEN"
```

Use `127.0.0.1`, not `localhost`. Then ask, for example: "Create an
800x600 document, draw a diagonal line with the brush tool and tell me how
many undo steps there are."

## What it can do

| Tool | What it does |
| --- | --- |
| `session_info` | Versions, the open document, the canvas and the active tool |
| `document_new` | Creates an RGBA document with one empty layer |
| `tool_activate` | Selects a tool, for example the brush or the rectangular selection |
| `input_stroke` | Draws a stroke with the mouse or a simulated pen with pressure and tilt |
| `wait_idle` | Waits until Solstice has finished processing the stroke |
| `query_state` | Reports undo history, selection bounds and size, and layer bounds with a pixel fingerprint |
| `action_trigger` | Runs a menu action such as Undo or Deselect |
| `canvas_capture` | Returns a picture of the canvas |
| `canvas_frames` | Records when the canvas shows a new frame, for timing strokes |
| `input_click`, `input_drag` | Clicks at a point, or drags in a straight line, with the mouse or the simulated pen |
| `query_pixels` | Measures a rectangle of the layer or of the whole image: a pixel fingerprint, how many pixels are painted, and their average color |
| `document_open`, `document_export_png` | Opens an image from the test folder, or saves the image there as PNG (see below) |
| `document_close` | Closes the current document or all of them without saving |
| `view_reset` | Sets the window size, 100 % zoom, no rotation or mirroring, and centers the image |
| `brush_set` | Chooses a brush preset by name and sets size, opacity, eraser mode and the foreground color |
| `input_key` | Presses a key on the canvas, for example Return to apply a transform |
| `ui_query`, `ui_click`, `ui_set` | Lists the buttons, boxes and fields of the window, dockers and dialogs, clicks a button, or sets a value, for example in Tool Options or Quick Access |
| `ui_close` | Closes a dialog or popup window by its title |
| `query_layers`, `layer_activate` | Lists the layers with their names, visibility, opacity and color labels, or selects layers by name |
| `docker_show` | Shows or hides a docker, for example Tool Options |

Input tools accept modifier keys (Shift, Ctrl, Alt), and `input_click` can
double-click. For keys, test control brings Solstice to the front and gives
the canvas the keyboard focus. Selecting a tool that is already selected
starts it afresh, for example for a new transform. An action or button that
opens a dialog which waits for an answer (such as Rename Alternative) is
started with `async`, so the dialog can then be filled in and closed.

### Test folder

`document_open` and `document_export_png` only work inside a folder you
choose when you start Solstice, with the setting `SOLSTICE_TEST_CONTROL_ROOT`
(for example `$env:SOLSTICE_TEST_CONTROL_ROOT = "$env:TEMP\solstice-test-files"`
in PowerShell). Paths are given relative to it, and they cannot leave it.
Without the setting, those two tools refuse to run.

### Scenarios

A scenario is a file that lists tool calls and the results to expect, kept
next to the tests of the feature it covers, for example
`libs/ui/tests/scenarios/rect-select-fill.json`. Scenarios cover the brush,
the selection tools with their modifier keys, the Transform Tool with
Puppet Warp, Lazy Tools (color labels, saved selections, foreground colors,
Rename Alternative, Fast Image Export) and Quick Access (palette, Quick Brush
Adjustments, HueSVC).

### Running scenarios

The scenario runner replays every scenario and reports what passed. It needs
Python 3 and nothing else. With Solstice already started as above, in the same
PowerShell window:

```powershell
python dev-tools\test-control\run_scenarios.py --all --report report.json
```

Or let the runner start a separate test copy of Solstice and close it again
(close your own Solstice first):

```powershell
python dev-tools\test-control\run_scenarios.py --all --launch "C:\path\to\Solstice\bin\solstice.exe"
```

Each scenario is listed as passed or failed, with the step and the reason for
a failure; `report.json` holds the details. Some scenarios also compare with
results recorded on a particular system (for example `linux-cpu`). On a
system with nothing recorded yet, those steps show as "unrecorded" and do not
fail. To record them for your system, run once with `--record` and keep the
files it writes next to the scenario.

Scenarios expect Solstice's English texts, such as the names in the undo
history. The test copy started with `--launch` therefore uses English
whatever your system language; `--language system` keeps your language. If
you run scenarios against your own Solstice in another language, the runner
warns you, and steps that check texts fail.

### Paint timing series

`dev-tools/test-control/paint_trace_series.py` measures painting the way the
hand-drawn paint trace runs do, but with identical synthetic strokes: it
starts a separate test copy of Solstice for each of the CPU, GPU projection
and GPU brush modes, draws the same 16 pen strokes (64 px and 256 px brushes,
Build-up and Wash), closes Solstice so the timing record is written, and
prints a summary table:

```powershell
python dev-tools\test-control\paint_trace_series.py --launch "C:\path\to\Solstice\bin\solstice.exe" --out "$env:TEMP\paint-series-1"
```

A full series starts nine copies one after another and took under ten
minutes on the Linux test machine; `--modes cpu` runs one.

To find out how much the timing record itself slows Solstice down, add
`--overhead`: each mode then also runs without the record, and the tool
compares the two by measures that need no record: how long after the pen
is lifted the canvas shows the last frame of the stroke, and how much CPU
time Solstice used. This starts 18 copies. Close your own Solstice first, and do not
use the computer for heavy work meanwhile. The results are a separate series
from hand-drawn measurements and must not be mixed with them.

## Limitations

- The pen is simulated inside Solstice. It does not test your tablet driver
  or how pressure feels.
- Keep the keyboard and the real pen away from Solstice while a test runs;
  they can change the result.
- With modifier keys, Solstice comes to the front and takes the keyboard
  focus; if the system does not allow that, the call fails.
- Strokes go to the active document's canvas only.
- A selection limits where strokes paint. Clear it (the `deselect` action)
  before painting outside it.
- While a selection made with the selection tools exists, waiting takes
  about half a second longer: Solstice finishes drawing the selection after
  the stroke itself.
- Judge a stroke by the undo history and the layer it changed. The
  "accepted" values in a stroke's result do not show success: on Linux they
  are always false for pen strokes.
- When you end a test, close Solstice and answer the save question, so no
  unsaved test document is reopened next time.
- While a transform is in progress, the image cannot be measured; scenarios
  check the result after Apply. Whether the preview matches the result is
  still a manual check.
- The Transform Tool remembers its mode and the Puppet Warp lock between
  transforms, so scenarios set them explicitly.
- The Lazy Tools and Quick Access scenarios change the settings they run
  with: they add a brush to Quick Access, a rename preset, export settings,
  and exported images in the test folder. Run them with `--launch`, which
  uses a temporary profile, not with your everyday Solstice.

## Security

Anyone who can connect can draw in your documents and trigger actions.
Test control therefore listens only on `127.0.0.1`, requires the token on
every request, and rejects requests from web pages. Do not reuse the token
elsewhere, and start Solstice normally, without the two settings, when you
are not testing.
