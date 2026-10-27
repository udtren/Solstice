---
type: pitfall
updated: 2026-10-27
sources:
  - libs/ui/input/kis_input_manager.cpp (KisInputManager::eventFilter, eventFilterImpl, TabletPress/TabletRelease cases)
  - libs/ui/input/kis_input_manager_p.cpp (EventEater::eventFilter, ProximityNotifier::eventFilter, CanvasSwitcher::addCanvas, allowMouseEvents, fixShortcutMatcherModifiersState)
  - libs/ui/input/kis_extended_modifiers_mapper.cpp (queryStandardModifiers)
  - libs/ui/tool/kis_tool_rectangle_base.cpp (endPrimaryAction), libs/ui/tool/KisToolOutlineBase.cpp
  - libs/flake/KoToolProxy.cpp (tabletEvent), libs/flake/KoToolManager.cpp (Private::switchInputDevice)
  - QtTest/qtestmouse.h, QtTest/qtestspontaneevent.h (Qt 6.10)
  - Linux run of the phase 0 checks, 2026-10-26
  - Windows run of the phase 0 checks, 2026-10-27
  - libs/image/kis_selection.cc, KisSelectionUpdateCompressor.cpp, kis_selection_mask.cpp (update compressors)
  - libs/ui/tool/kis_selection_tool_config_widget_helper.cpp (default selection mode)
  - plugins/extensions/testcontrol/TestControlInputInjector.cpp
  - libs/ui/tool/kis_tool_polyline_base.cpp (mouseMoveEvent, m_closeSnappingActivated)
  - Linux phase 3 scenario runs, 2026-10-27 (xdotool, Xvfb)
  - plugins/dockers/layerdocker/LayerBox.cpp (QComboBox::activated connections)
  - libs/ui/KisSolsticeLazyTools.cpp (showRenameDialog, exec())
  - docs/agent/test-control.md
related:
  - transform-tool-input.md
  - idle-tasks-wait-for-open-strokes.md
---

# Synthetic canvas input

Found by reading the input manager while writing the
[test control](../../test-control.md) input injector (2026-10-26), and by
running its phase 0 checks on Linux the same day and on Windows on
2026-10-27 (the sections from "A Leave suppresses" to "Count settle
time"), and by the phase 3 scenario runs on Linux (the last two).

## Only the current receiver is filtered

`KisInputManager::eventFilter()` returns at once unless the object is its
`eventsReceiver`. That is the canvas widget set by
`CanvasSwitcher::addCanvas()` for the newest canvas, or by a `FocusIn` on a
canvas. Events sent to another view's canvas widget bypass the input
manager. Send to `KisViewManager::canvasBase()->canvasWidget()` right after
the view became current.

## A pen press eats the next mouse press

On Windows (and Linux without Krita's Qt enter/leave patch) a `TabletPress`
calls `eatOneMousePress()`: `EventEater` drops the next left mouse press,
because the operating system synthesizes one after a pen press. It also calls
`blockMouseEvents()`, which drops every mouse event until
`allowMouseEvents()` (a `TabletLeaveProximity`, `Enter`, `Leave` or
`FocusIn`). A synthetic pen stroke that does not imitate the OS leaves both
states set, and the next synthetic mouse stroke loses its press or all its
events. Send a `Qt::MouseEventSynthesizedBySystem` press after the
`TabletPress`, and a `TabletLeaveProximity` to the application object
(where `ProximityNotifier` listens) after the release.

## Modifiers come from the real keyboard

For pointer events, `eventFilterImpl()` calls
`fixShortcutMatcherModifiersState()` when the canvas widget has no focus, or
when the event's modifiers disagree with the shortcut matcher. That function
reads `QApplication::queryKeyboardModifiers()`, the physical keyboard. A
synthetic Ctrl-click without focus therefore runs as a plain click, and a key
the tester holds changes a synthetic stroke. Synthetic modifiers need key
press/release events through the matcher while the canvas has focus.

Two details found when implementing them (2026-10-27): send
`ShortcutOverride` and then `KeyPress` for each modifier, as Qt does (Krita
handles keys in `ShortcutOverride`), and build a modifier key's own event
with the state *before* it. `QKeyEvent::modifiers()` toggles the flag of the
key itself, because platforms report the earlier state, so a Ctrl `KeyPress`
built with `ControlModifier` reads as no modifier. Taking focus needs
`activateWindow()` plus `setFocus()`, then a wait until `hasFocus()`: on
Linux/X11 without a window manager it works at once; on Windows the system
may refuse to bring another application's window forward.

## Accepted flags differ by event type and platform

For mouse presses the input manager sets the event's accepted flag to the
matcher's result. Tablet presses and releases are accepted on Windows, but
on Linux without Krita's Qt enter/leave patch every tablet press, move and
release is set unaccepted on purpose (so Qt synthesizes mouse events). The
flag says nothing about the stroke; check the undo stack or pixels.

## Releases must be spontaneous, and sendEvent() clears the flag

`KisToolRectangleBase::endPrimaryAction()` (rectangle and ellipse tools) and
`KisToolOutlineBase` end the shape only when the release is
`spontaneous()`; otherwise they cancel it, because a non-spontaneous release
is what the canvas sends when it loses focus. Events from
`QCoreApplication::sendEvent()` are never spontaneous: in Qt 6 it resets the
flag even when the event had it. The drag shows its size overlay and then
vanishes. Mark the event with `QSpontaneKeyEvent::setSpontaneous()` (inline
in `QtTest/qtestspontaneevent.h`) and deliver it with
`QCoreApplication::instance()->notify()`, as `QTest::mouseEvent()` does.

## The first stylus event on a canvas switches tool instances

`KoToolProxy::tabletEvent()` calls `KoToolManager::Private::switchInputDevice()`
for every tablet event. The first time a stylus appears on a canvas, that
creates new tool instances for it and calls `switchTool()`, which ends a
stroke already begun: a pen stroke that starts with `TabletPress` paints a
dot on a new canvas and works afterwards (switching back to the mouse never
happens). A real pen hovers first. Send a hover `TabletMove` (no buttons, no
pressure) and wait longer than the move compression (`tabletEventsDelay`,
10 ms by default) before the press; a press discards a compressed move.

## A Leave suppresses every action until the next Enter

`KisShortcutMatcher::actionsSuppressed()` is true while `cursorEntered` is
false, and then `prepareReadyShortcuts()` offers no shortcut: a press
reaches `buttonPressed()` and starts nothing, while the event still counts
as accepted. The canvas gets a `Leave` whenever another window appears over
the pointer, and only a later `Enter` (pointer movement, or the synthetic
one `CanvasSwitcher` sends on `FocusIn`) clears it. Synthetic strokes looked
focus-dependent on Linux for this reason (2026-10-27): they failed only when
Solstice had lost focus and another window covered the pointer. Start every
synthetic stroke with an `Enter`, as a real pointer reaching the canvas does.

## Idle once is not settled

`KisImage::isIdle()` can be true while follow-up work is still queued to the
GUI thread. Require idle on several polls with event-loop passes in between
(test control: three polls 20 ms apart).

## A vector selection renders its pixels later

The selection tools default to vector mode (`SHAPE_PROTECTION` in
`KisSelectionToolConfigWidgetHelper`). A vector selection's pixels are not
written by the tool's stroke: `KisSelection` renders its shape selection
into the pixel selection from `KisSelectionUpdateCompressor` (100 ms,
postponed by every change), and the selection mask's own update compressor
follows. Neither timer makes `KisImage::isIdle()` false. After a rectangle
selection, "Select Rectangle" is on the undo stack at once, but the selection
reads as active and empty (0x0 bounds) for 70-220 ms; a short job runs at
about 60-100 ms and another about 430 ms after the stroke, and a read during
either finds the image busy. Three polls 20 ms apart missed it in about 1 of
25 runs on Linux and in 13 of 15 on Windows. Test control's `wait_idle`
therefore needs 600 ms of idle time while the current selection
`hasShapeSelection()` (75 of 75 reads correct on Windows), and keeps three
polls otherwise.

## Count settle time with a clock, not in polls

A poll timer runs late when the GUI thread is busy. Every view with an active
selection repaints its canvas every 150 ms for the marching ants, background
tabs included, since they are covered rather than hidden. With 36 such
documents under Xvfb's software OpenGL, 20 ms polls came 170-300 ms apart, so
a settle of 31 polls took 3-6 s and passed the 10 s timeout with about 75
documents, while the image itself never went busy. Measure the settle time
with `QElapsedTimer`, and add a margin of the longest poll gap seen, because
the timers being waited for are delayed by the same load (test control: 600 ms
plus twice that gap; 40 of 40 reads correct at a 296 ms gap).

## Clicks need a hover move first

A real pointer always moves over the canvas before it clicks, and some tools
depend on it. `KisToolPolylineBase` (polygonal selection, polyline) decides
in `mouseMoveEvent()` whether the pointer is close enough to the first point
to close the shape (`m_closeSnappingActivated`); a press without a preceding
move at that spot adds a point instead. Send a button-less move at the press
position before each press (test control: 50 ms before, for mouse and pen).
A test widget only receives such moves with `setMouseTracking(true)`.

## A key sent to a window can stay held

On Linux, `xdotool key --window <id> Return` (used to dismiss a dialog at
startup) delivers the key with `XSendEvent`. Afterwards Solstice treated
Return as still pressed and received auto-repeated Return presses, so every
polygonal selection finished after its first click (Return ends the shape),
while the other tools looked fine (2026-10-27). Dismiss dialogs on a test
display with a mouse click, keep `xset r off`, and suspect a stuck key when
a tool finishes or cancels on its own.

## A modal dialog blocks a serialized call queue

Test control runs one tool call at a time. An action or button that opens a
modal dialog (`QDialog::exec()`, `QMessageBox::warning()`) runs the dialog's
event loop inside that call, so the call never replies and every later call
waits behind it: nothing can close the dialog. Trigger such actions after
replying (test control: `async` on `action_trigger` and `ui_click`); the
dialog's nested event loop then serves the next calls. Rename Alternative and
Fast Image Export's warnings work this way.

## setCurrentIndex() is not a user's choice

A user picking a combo box item makes Qt emit `activated` (also for the item
already shown), while `setCurrentIndex()` emits only `currentIndexChanged`,
and nothing when the index does not change. The Layers docker's color label
and label visibility combos are connected to `activated`, so setting their
index did nothing. Emit `activated` and `textActivated` after setting a
combo box when imitating a choice (test control's `ui_set` does).
