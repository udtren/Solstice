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

> **Status: early test version.** The first set of tools is in place for
> trying out the approach. It does not yet support modifier keys, opening or
> exporting files, or choosing brush presets. It has been checked in a
> running Solstice on Linux; Windows has not been checked yet.

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
$env:SOLSTICE_TEST_CONTROL_TOKEN   # note the token for the MCP client
& "C:\path\to\Solstice\bin\solstice.exe"
```

On Linux, in a shell:

```bash
export SOLSTICE_PROFILE_ROOT="$HOME/solstice-test-profile"
export SOLSTICE_TEST_CONTROL_PORT=8765
export SOLSTICE_TEST_CONTROL_TOKEN="$(openssl rand -hex 16)"
echo "$SOLSTICE_TEST_CONTROL_TOKEN"   # note the token for the MCP client
/path/to/solstice/bin/krita
```

| Setting | Meaning |
| --- | --- |
| `SOLSTICE_TEST_CONTROL_PORT` | Port number from 1024 to 65535. Without it, test control stays off. |
| `SOLSTICE_TEST_CONTROL_TOKEN` | The token clients must send; at least 16 characters. Without it, test control stays off. |

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

## Limitations

- The pen is simulated inside Solstice. It does not test your tablet driver
  or how pressure feels.
- Keep the keyboard and the real pen away from Solstice while a test runs;
  they can change the result.
- Modifier keys (Ctrl, Shift, Alt) are not supported yet.
- Strokes go to the active document's canvas only.
- While another application has the keyboard focus, strokes do not paint
  (seen on Linux). Keep Solstice in front during a test.
- Judge a stroke by the undo history and the layer it changed. The
  "accepted" values in a stroke's result do not show success: on Linux they
  are always false for pen strokes.
- When you end a test, close Solstice and answer the save question, so no
  unsaved test document is reopened next time.

## Security

Anyone who can connect can draw in your documents and trigger actions.
Test control therefore listens only on `127.0.0.1`, requires the token on
every request, and rejects requests from web pages. Do not reuse the token
elsewhere, and start Solstice normally, without the two settings, when you
are not testing.
