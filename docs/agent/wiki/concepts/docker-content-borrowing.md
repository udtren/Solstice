---
type: concept
updated: 2026-11-21
sources:
  - libs/ui/widgets/KisDockerBorrowRegistry.h
  - libs/ui/widgets/KisDockerHud.cpp
  - plugins/dockers/quickaccess/QuickAdjustToolOptionsPad.cpp
  - docs/agent/docker-tray-plan.md
related:
  - native-refactor-boundaries.md
---

# Showing a docker's content elsewhere

Solstice shows docker content outside its docker in three places: the
toolbar docker box (`KisDockerHud`), Quick Adjust's Tool Options pad and the
planned Docker Tray. All three move only `QDockWidget::widget()` (and, for
Timeline and Animation Curves, the title bar's `widgetArea()`); the
`QDockWidget` stays in the main window so `saveState()`/`restoreState()` and
workspaces keep working. They borrow through `KisDockerBorrowRegistry`, one
per main window, which decides who holds a docker and puts a placeholder
into the docker meanwhile. The rules are in the
[Docker Tray plan](../../docker-tray-plan.md#shared-borrowing-registry).

What moves and what does not:

- Child widgets of the content move with it, including their own
  `resizeEvent()`/`showEvent()` overrides and canvas connections.
- The `QDockWidget` subclass stays behind. Its `showEvent()`, `hideEvent()`,
  `resizeEvent()` and `isVisible()` describe the docker, not the borrowed
  content: a hidden docker shown elsewhere still believes it is hidden
  (`LayerBox` stops thumbnail idle tasks, `QuickAdjustDock` stops its sync
  timer).
- Code in the docker that reaches its content through `widget()` gets the
  placeholder while the content is borrowed (`QuickAdjustDock::setControlsEnabled()`,
  `QuickAccessDock::applyAppearanceSettings()`). Keep a member pointer to the
  content root instead.
- Dockers stay canvas observers while hidden, so borrowed content of a
  hidden docker follows the canvas.

The fix pattern (Docker Tray phase 4): watch the content widget's
`QEvent::Show`/`QEvent::Hide` (or `Resize`) with an event filter instead of
overriding the dock's `showEvent()`/`hideEvent()`/`resizeEvent()`. Children
get show and hide events when their parent window shows or hides, and a
re-parent hides the widget, so the content's events describe where it is
really seen (`LayerBox`, `QuickAdjustDock`, `RestNoteDock`).

The per-docker audit is in the
[Docker Tray plan](../../docker-tray-plan.md#docker-audit).
