---
type: pitfall
updated: 2026-10-26
sources:
  - libs/ui/input/kis_input_manager.cpp (KisInputManager::eventFilter, eventFilterImpl, TabletPress/TabletRelease cases)
  - libs/ui/input/kis_input_manager_p.cpp (EventEater::eventFilter, ProximityNotifier::eventFilter, CanvasSwitcher::addCanvas, allowMouseEvents, fixShortcutMatcherModifiersState)
  - libs/ui/input/kis_extended_modifiers_mapper.cpp (queryStandardModifiers)
  - plugins/extensions/testcontrol/TestControlInputInjector.cpp
  - docs/agent/test-control.md
related:
  - transform-tool-input.md
  - idle-tasks-wait-for-open-strokes.md
---

# Synthetic canvas input

Found by reading the input manager while writing the
[test control](../../test-control.md) input injector (2026-10-26). These
are code-reading conclusions; the phase 0 checks in that document confirm
them in a running Solstice.

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

## Accepted flags differ by event type

For mouse presses the input manager sets the event's accepted flag to the
matcher's result; for tablet presses and releases it always accepts. A
synthetic tablet press that reports "accepted" proves only that the input
manager saw it; check the undo stack or pixels for the stroke itself.
