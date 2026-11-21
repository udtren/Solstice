---
type: pitfall
updated: 2026-11-21
sources:
  - plugins/dockers/quickaccess/QuickColorPopup.cpp (QuickColorPopup::leaveEvent, pollPointer, eventFilter)
  - plugins/dockers/quickaccess/QuickAccessDockPopups.cpp (QuickAccessDock::showColorPopup)
  - plugins/dockers/quickaccess/tests/QuickAccessInteractionTest.cpp (colorPopup* cases)
  - docs/agent/quick-access.md (HueSVC popup lifecycle)
  - "user report 2026-11-21: the HueSVC popup closed at once when opened by shortcut with a pen (XP-Pen, OpenTabletDriver Windows Ink output); with a mouse it stayed open"
related:
  - synthetic-canvas-input.md
---

# A hovering pen sends false Leave events on Windows

## What happened

The Quick Access HueSVC popup is a frameless always-on-top `Qt::Tool`
window that closed on its first `QEvent::Leave`. Opened by its shortcut
under a mouse, it stayed open; under a hovering pen it closed at once,
although the popup was placed so that the pointer was inside it.

## Why (reasoned from the code, not traced in Qt)

Qt 6 on Windows receives a pen through `WM_POINTER` messages. Windows' own
mouse tracking (`TrackMouseEvent`/`WM_MOUSELEAVE`) does not see the pointer
over a window that appeared under a hovering pen, so Qt can deliver a
Leave right after the window is shown, or in the wrong order with the
Enter, while the pointer is still over the window. Drivers differ in
whether they move the system cursor with the pen at all. Which of these
produced the Leave for the reporting user was not traced; the fix does not
depend on it.

## How to avoid it

- Do not treat a `QEvent::Leave` as proof that the pointer left. Check the
  position against `frameGeometry()` first.
- Take the position from the last tablet event when one is recent (an
  application event filter for `TabletMove`/`TabletPress`/`TabletRelease`,
  reset on `TabletLeaveProximity`), and from `QCursor::pos()` otherwise.
- If a Leave is ignored because the pointer is still inside, poll (about
  100 ms) until it is outside, and stop the poll on Enter and on close.
  Otherwise the window may never close, because the next real Leave may
  never arrive.
- `Qt::Popup` windows are not affected the same way: they close on clicks
  outside, not on Leave.
- Tests should replace the cursor source instead of calling
  `QCursor::setPos()`, which moves the user's real cursor
  (`QuickColorPopup::setCursorPositionSource`).
- A driver-side workaround (OpenTabletDriver "Forced Sync") was tried by
  another session before the fix; whether it changes the Leave behavior
  was not established.
