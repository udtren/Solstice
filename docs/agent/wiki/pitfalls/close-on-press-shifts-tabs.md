---
type: pitfall
updated: 2026-10-28
sources:
  - libs/ui/KisMainWindow.cpp (MdiTabMiddleClickCloser, customizeTabBar)
  - "qtbase v6.8.0 src/widgets/widgets/qmdiarea.cpp (QMdiAreaTabBar::mousePressEvent)"
  - docs/agent/feature-inventory.md ("Undocumented custom changes")
related:
  - synthetic-canvas-input.md
---

# Closing on press moves the next tab under the cursor

`QMdiArea` in tabbed view closes a sub-window when the middle button is
*pressed* over its tab (`QMdiAreaTabBar::mousePressEvent()`). The tab is
removed while the button is still down, so the next tab slides under the
cursor. Any repeated press from the same physical click then closes that
tab too. A user reported it as "middle-clicking the leftmost tab closes the
other documents" (2026-10-28); the extra press was not reproduced on the
development machine, so its source (mouse or pen driver) is unverified.

A standalone program against Qt 6.8 showed the mechanism: with the stock
tab bar, a press delivered twice, or a whole click delivered twice, closed
two tabs; with Solstice's `MdiTabMiddleClickCloser` one. A single press and
release closes one tab either way, so ordinary synthetic tests miss it.

Rules for click-to-remove in lists and tab bars:

- Act on release, and only if it lands on the item that was pressed
  (browsers do the same; dragging off cancels).
- Treat a press that is not newer than the release that removed an item
  as a repeat of that click (`QInputEvent::timestamp()`).
- `QMdiArea` deletes and recreates its tab bar when the view mode changes,
  so filters on it must be reinstalled; `customizeTabBar()` runs after
  every `setViewMode()` and checks the filter's object name first.
- To test, deliver the same press or click twice, not only one clean click.
