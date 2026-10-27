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
  - docs/agent/test-control.md
related:
  - transform-tool-input.md
  - idle-tasks-wait-for-open-strokes.md
---

# Synthetic canvas input

Found by reading the input manager while writing the
[test control](../../test-control.md) input injector (2026-10-26), and by
running its phase 0 checks on Linux the same day and on Windows on
2026-10-27 (the last four sections).

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
therefore needs 600 ms of idle polls while the current selection
`hasShapeSelection()` (75 of 75 reads correct on Windows), and keeps three
polls otherwise.
