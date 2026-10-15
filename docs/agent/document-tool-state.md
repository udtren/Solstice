# Document tool state — maintenance notes

User behavior: [Tools when switching documents](../document-tool-state.md).

## Locations and scope

- `libs/flake/KoToolManager.cpp`: `Private::movedFocus()` and `toolById()`.
- `libs/flake/tests/TestKoToolManager.cpp`, registered in the adjacent
  `CMakeLists.txt`: manager-level regression tests with inert tools/canvases.
- Call path: `KisViewManager::setCurrentView()` focuses the canvas;
  `QApplication::focusChanged` reaches `KoToolManager::Private::movedFocus()`.
- Library: `kritaflake`; no public ABI, configuration, or document format change.

## State and lifetime invariants

`canvasses` contains a list of `CanvasData` per controller. Its first entry is
the mouse state. Input-device entries on the same canvas share tool instances
through `createTool()`, but keep separate active tool IDs and histories.

On a focus change, reuse the entry for the current input device. If none exists,
create it for the destination canvas, append it before activation, and seed its
tool from the destination's mouse state. Do not activate that mouse state first:
`switchCanvasData()` also changes the manager's input device, which used to
restore stale mouse tools on a later return to the source canvas. It can also
start an unwanted transform stroke if an intermediate tool is activated.

Keep the existing rule in `switchInputDevice()` that mouse events do not switch
away from a tablet device. Pen and eraser states remain independent. Repeated
focus on the current canvas must not reactivate the tool.

`toolById(canvas, id)` must use the matching controller's tool map, even when
another canvas is active. Reading the first entry is sufficient because all
device entries share the canvas's tool instances. Lookup must not activate a
canvas or tool. Unknown IDs and unregistered canvases return null.

The regression fixture unregisters controllers before their derived destructor
finishes; the base controller destructor no longer has the derived `canvas()`
implementation. Tools must be destroyed before canvases and action collections.

The change is limited to focus switching and lookup. Creation/removal of views
continues to use the existing attach/detach lifecycle. Transform rendering and
Puppet Warp state are untouched.

## Build, test, and install

Use the environment and paths in `development-workflow.md`:

```bat
cmake --build <krita-dev-root>\_build --target kritaflake TestKoToolManager TestInputDevice -j 2
set QT_QPA_PLATFORM=offscreen
set QT_QPA_FONTDIR=C:\Windows\Fonts
set KRITA_NO_ASSERT_MSG=1
<krita-dev-root>\_build\bin\TestKoToolManager.exe
<krita-dev-root>\_build\bin\TestInputDevice.exe
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\libs\flake\cmake_install.cmake
```

Restart Solstice after installing `libkritaflake.dll`. Never terminate an open
application to unlock the DLL without the user's approval.

On 2026-10-15, the new regression suite failed five rows against the old code.
After the fix: `TestKoToolManager` 8 passed, `TestInputDevice` 11 passed (QtTest
counts include setup/cleanup). Coverage includes inactive-canvas lookup, pen
and eraser round trips, a device new to the destination, no transient transform
activation, independent device states, and mouse-only restoration.

## Manual regression checks

1. Open A and B. Before using the pen on either canvas, select Transform in B
   with the mouse. Use the pen in B and select Brush. Use the mouse to visit A
   and then B without pen input on A. B should remain Brush, with no transform
   frame appearing even briefly.
2. Repeat for the eraser end and with each document in a separate main window.
3. Use different tools for pen and eraser in B; confirm that changing ends
   restores their respective tools after switching documents.
4. With mouse input only, leave Transform selected in B, visit A, and return:
   B should still restore Transform. This existing behavior is intentional.

The automated test drives the same focus-change slot with real manager and
proxy classes, but uses inert tools. Actual tab input, tablet hardware, layer
selection, and transform previews still need the manual checks above.
