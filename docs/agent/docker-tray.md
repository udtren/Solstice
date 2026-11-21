# Docker Tray — maintenance reference

User guide: [`../docker-tray.md`](../docker-tray.md).
Development history (request, decisions, docker audit, phase results):
[`wiki/history/docker-tray-development.md`](wiki/history/docker-tray-development.md).

Status: implemented 2026-11-21 on branch `claude/docker-tray` (phases 1-5),
each phase checked by the user in the application. Not yet in a release.

The Docker Tray is a popup that shows several dockers of its main window at
once, in columns, with a size per docker. Dockers that are hidden in the main
window work on the tray. It opens at the cursor from a right-click with a
painting tool or the **Show Docker Tray** shortcut. It extends the idea of
the removed Popup Palette's docker area
([removal notes](popup-palette-removal.md)).

## Locations

| Part | Location |
| --- | --- |
| Borrowing registry | `libs/ui/widgets/KisDockerBorrowRegistry.{h,cpp}`: `KisDockerBorrower` (interface), `KisDockerBorrowRegistry` (one per main window) |
| Tray widget | `libs/ui/widgets/KisDockerTray.{h,cpp}`: `KisDockerTray`, file-local `ResizeGrip` |
| Layout model, screen fit | `libs/ui/widgets/KisDockerTrayLayout.{h,cpp}`: `KisDockerTrayLayout`, `KisDockerTrayGeometry::fit()` |
| Configuration dialog | `libs/ui/dialogs/KisDlgDockerTray.{h,cpp}` |
| Shortcut action | `show_docker_tray` in `krita/krita.action` (General, no default shortcut); created in `KisControlFrame::createDockerBox()` (`libs/ui/kis_control_frame.cpp`) |
| Right-click | `KisToolPaint::popupWidget()` (`libs/ui/tool/kis_tool_paint.{h,cc}`) |
| Other borrowers | `KisDockerHud` (toolbar docker box, `libs/ui/widgets/KisDockerHud.{h,cpp}`), `QuickAdjustToolOptionsPad` (`plugins/dockers/quickaccess/`) |
| Dockers adapted to borrowing | `LayerBox` (`plugins/dockers/layerdocker/`), `QuickAdjustDock`, `QuickAccessDock` (`plugins/dockers/quickaccess/`), `RestNoteDock` (`plugins/dockers/restnote/`) |
| Tests | `libs/ui/tests/KisDockerBorrowRegistryTest.cpp`, `libs/ui/tests/KisDockerTrayTest.cpp`, `plugins/dockers/quickaccess/tests/QuickAdjustDockTest.cpp` (pad hand-over, sync timer) |
| Strings | `po/zh_CN/krita.po` (action, borrower name, dialog, hint); the feature name stays "Docker Tray" in translations |

## Borrowing

Only a docker's content widget (`QDockWidget::widget()`) moves, never the
`QDockWidget`: moving the dock widget out of the main window breaks
`saveState()`/`restoreState()` and workspaces.

`KisDockerBorrowRegistry::forWindow(window)` finds or creates the registry, a
direct child of the main window.

- `request(id, borrower)` gives the content of the dock widget named `id` to
  the borrower: `dockerContentGranted(id, content, titleArea)` puts it into the
  borrower's layout, then the docker gets a placeholder `QLabel` with the
  borrower's `dockerPlaceholderText()`. For `TimelineDocker` and
  `AnimationCurvesDocker` the title bar's `KisUtilityTitleBar::widgetArea()`
  (playback controls) moves too.
- The last requester holds the content. Earlier borrowers wait and get
  `dockerContentHeldElsewhere(id, holderName)`, the previous holder just before
  its widgets move. `release()` passes the content to the newest waiting
  borrower or back to the docker; a waiting borrower that requests again
  moves to the end of the queue.
- The registry never changes a docker's visibility. A hidden docker stays
  hidden while its content is borrowed.
- Lifetime: `~KisDockerBorrower` releases everything. Borrowers must inherit
  `QObject`/`QWidget` first, so their children are still alive when the base
  destructor runs. The registry's destructor returns content to live dockers;
  dockers and content are held by `QPointer`. Callbacks must not call back into
  the registry.

Borrowers: the toolbar docker box borrows while it is shown; the tray borrows
while it is open; the Quick Adjust Tool Options pad holds `sharedtooldocker`
for as long as it exists (it also hides that docker and disables its toggle
action), and shows "Docker is open in ..." while another borrower holds it.

## Tray

- **One tray per main window**: `KisDockerTray::forWindow(window, config)`
  creates it lazily as a `Qt::Tool | Qt::FramelessWindowHint` child of the
  main window. Never create it from a plugin constructor (see the
  `KisActionPlugin` rule in `AGENTS.md`).
- **Opening** (`popupAt(globalPos)`; `popup()` ignores the input manager's
  canvas position and uses `QCursor::pos()`): read the layout from `kritarc`,
  rebuild cells only if it changed, request every docker, size the cells
  (`applySizes()`), show the tray centered at the position and clamped 8 px
  inside the screen's available geometry, activate it so Esc works, and move
  it again after `show()` (Windows can place a new window first). Dockers the
  window does not have are skipped. If none exists the tray still opens with
  a hint, so the configure button stays reachable.
- **Closing**:
  - `dismiss()` (`KisPopupWidgetInterface`) only hides.
  - `closeTray()` hides and emits `finished()`; it is used for Esc, the
    shortcut again, and the application becoming inactive.
  - `KisInputManager::deregisterPopupWidget()` calls `dismiss()` and then
    reads its popup pointer again. Emitting `finished()` from `dismiss()` would
    deregister re-entrantly, null the pointer and fire its `KIS_ASSERT`.
  - The destructor emits `finished()` to detach from an input manager that
    still holds the tray.
  - A non-spontaneous hide returns all content (`releaseAll()`). A
    spontaneous hide (main window minimized) keeps it.
- **Registration**: the right-click path registers the tray with
  `KisInputManager` (`KisPopupWidgetAction`); the shortcut path registers it
  after it is shown. A press on the canvas then closes it and is consumed, so
  a second right-click closes the tray. `reload()` (after the dialog) hides
  without `finished()` and reopens, keeping the registration.
- **Shortcut**: the action is also added to the tray, so it closes the tray
  while the tray has the focus.
- **Cells**: each cell is a frame with the docker title and the content. The
  spacing between cells is made of 6 px `ResizeGrip`s: one to the right of
  every column (`dockerTrayWidthGrip_<column>`) and one under every docker
  (`dockerTrayHeightGrip_<id>`).
- **Sizing**: a column is as wide as its widest cell; a cell is never smaller
  than its content's minimum size hint. `KisDockerTrayGeometry::fit()` shrinks
  columns and cells in proportion, never below their minimum, when the tray
  would not fit the screen; the available size accounts for the panel frame,
  the header and the grips after the last column and the last cell.
- **Dragging**: a width grip sets every shown docker of the column; a height
  grip sets one docker. Drags are clamped to the content minimum (at least
  `MinimumExtent`, so shown and stored sizes agree) and `MaximumExtent`. On
  release the sizes are written to the layout and saved, and `keepOnScreen()`
  moves the tray back inside its screen.
- **Layout updates**: the window uses `QLayout::SetFixedSize`, but nested
  widgets update their layouts through posted events, one level per pass of
  the event loop. `activateLayouts()` activates them innermost first after
  sizes change, so the tray takes its size at once.

## Layout and persistence

`kritarc`, group `DockerTray`, key `Layout`, application-wide (every window
reads the same layout and borrows its own dockers):

```json
{"version": 1,
 "trays": [{"name": "Default",
            "columns": [{"items": [{"id": "KisLayerBox", "w": 320, "h": 260}]},
                        {"items": [{"id": "PresetDocker", "w": 280, "h": 520}]}]}]}
```

- Ids are dock widget object names (each docker factory's `id()`), as in
  `KisDockerHud`'s `*/dockerList`.
- Only the first tray is used; the list leaves room for named trays without a
  format change.
- `fromJson()` drops cells without an id, repeated dockers and empty columns,
  clamps sizes to [50, 4000] (`MinimumExtent`, `MaximumExtent`) and uses
  300 × 400 for a missing size. Invalid JSON gives the default layout (one
  column with `KisLayerBox`).
- A stored size is the whole cell, including the docker title.
- The tray re-reads the layout every time it opens; the dialog and the grips
  save it. Ids the window does not have are kept.

## Configuration dialog

`KisDlgDockerTray` works on a copy of the layout, passed the window's dock
widgets (id and title, hidden ones included). Available dockers are sorted by
title. The tray tree shows one bold row per column with its dockers and their
sizes; a docker the window does not have is listed as "<id> (not available)".
Buttons: Add (or double-click), Remove, Move Up/Down (a docker moves within
its column and, past either end, into the previous or next column; a selected
column moves left or right), Add Column, Remove Column (with its dockers),
Width/Height spin boxes, Restore Defaults. OK returns the layout with empty
columns dropped; the tray saves it and reloads.

## Dockers on the tray

A docker whose code depends on being seen must follow its **content
widget's** visibility, not the `QDockWidget`'s: install an event filter on
the content and react to `QEvent::Show`, `QEvent::Hide` and `QEvent::Resize`.
The content is shown wherever it is, and a re-parent hides it before the new
host shows it. Code must also keep its own pointer to the content root:
`widget()` is the registry's placeholder while the content is borrowed.
Background: [showing a docker's content elsewhere](wiki/concepts/docker-content-borrowing.md).

| Docker | Rule |
| --- | --- |
| Layers (`LayerBox`) | Thumbnail idle tasks follow the content's Show/Hide; `setCanvas()` checks `m_mainWidget->isVisible()` |
| Quick Brush Adjustments (`QuickAdjustDock`) | The 200 ms sync timer follows the content's Show/Hide; `setControlsEnabled()` uses `m_root`. The Tool Options pad follows the dock itself, so it does not open from the tray |
| Quick Access (`QuickAccessDock`) | `applyAppearanceSettings()` uses `m_root` |
| Rest Note (`RestNoteDock`) | Font scaling follows the content's Resize; the settings dialog is parented to `m_root` |
| Timeline, Animation Curves | Playback controls borrowed with the content |

The audit of every docker (2026-11-21) found nothing else that depends on the
dock widget itself; Asset Library and the toolbox need no change. When adding
a docker, check it for `showEvent`, `hideEvent`, `resizeEvent`, `isVisible()`,
`visibilityChanged` and calls to its own `widget()`.

## Build, test and install

| Change in | Build | Install |
| --- | --- | --- |
| `libs/ui` (registry, tray, layout, dialog, control frame) | `kritaui` | `libs\ui` |
| `KisToolPaint::popupWidget()` | all targets: overriding a virtual that `KoToolBase` defines inline changes the tool plugins' own vtables | the whole build tree |
| Adapted dockers | `kritalayerdocker`, `kritaquickaccessdocker`, `kritarestnotedocker` | their `plugins\dockers\<name>` directories |

Tests: `KisDockerBorrowRegistryTest` (11), `KisDockerTrayTest` (22),
`QuickAdjustDockTest` (16), and the other Quick Access, Rest Note and
`TestNodeView` suites for the adapted dockers. Run test executables with
`KRITA_NO_ASSERT_MSG=1`.

## Manual regression checks

1. Right-click with a brush opens the tray at the cursor; a canvas click, a
   second right-click and Esc close it; painting works right after.
   Transform, selection, crop, path and reference image tools keep their
   right-click menus.
2. The Show Docker Tray shortcut opens and closes it, also after clicking
   inside the tray.
3. A hidden docker works on the tray and is hidden and working afterwards.
   Layers thumbnails update on the tray while the Layers docker is hidden.
4. Drop-down lists and dialogs (Layer Properties) opened from tray dockers
   work without closing the tray.
5. Opening near screen edges and on a second monitor keeps the tray on screen.
6. Timeline on the tray shows its playback controls.
7. The configure button: add and arrange dockers in columns, change sizes,
   OK updates the open tray; Cancel changes nothing; an empty layout shows
   the hint; Restore Defaults; the layout survives closing and reopening.
8. Dragging a column edge resizes all dockers of the column, a docker's
   bottom edge only that docker; minimums hold; sizes are saved, shown in the
   dialog and reused; the tray moves back onto the screen after growing past
   an edge.
9. The toolbar docker box and the Quick Adjust Tool Options pad hand a
   shared docker over and get it back.
10. Quick Brush Adjustments, Quick Access and Rest Note work on the tray
    (controls follow the canvas, settings restyle it, the time text scales)
    and as ordinary dockers.

## Limitations

- One tray per window; named trays (presets) are not implemented.
- A docker appears at most once on the tray, and its content can be in only
  one place at a time; the other borrower shows "Docker is open in ...".
- Python dockers that call `setWidget()` while their content is borrowed
  replace the placeholder; they are not supported.
- The Quick Adjust Tool Options pad does not open from the tray.
- The tray closes when the application becomes inactive, and on a canvas
  click; clicks elsewhere in the main window (dockers, menus) leave it open.
- Moving heavy widgets between windows (for example dockers built on Qt
  Quick) costs a re-parent each time the tray opens.
