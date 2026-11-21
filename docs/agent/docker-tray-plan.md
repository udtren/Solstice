# Docker Tray — implementation plan

Status: **design approved by the user on 2026-11-21. Phase 1 (audit and
shared borrowing registry) implemented on branch `claude/docker-tray`,
automated tests pass and the user checked it in the application. Phase 2
(the tray, its action and the right-click hook) implemented and checked by
the user. Phase 3 (configuration dialog) implemented and checked by the
user. Phase 4 (dockers that depended on their own visibility) implemented
and checked by the user. Phases 5-6 not started.** See
[Phase 1 results](#phase-1-results-2026-11-21),
[Phase 2 results](#phase-2-results-2026-11-21),
[Phase 3 results](#phase-3-results-2026-11-21) and
[Phase 4 results](#phase-4-results-2026-11-21). This plan
does not authorize removing anything that
[the Popup Palette removal](popup-palette-removal.md) kept.

## Request (2026-11-21)

Krita's Popup Palette (removed in Solstice, see
[popup-palette-removal.md](popup-palette-removal.md)) had a docker area opened
with its `>` button: one docker at a time, chosen from a configurable list
(`KisDockerHud`). The user asked for a new popup, the **Docker Tray**, that
extends this idea:

1. The tray's size follows its content.
2. Any number of dockers can be shown on the tray at once.
3. The size of each registered docker is defined individually.
4. Dockers that are currently hidden in the main window are usable on the tray.

The user's sketch shows two dockers stacked in a left column and one tall
docker in a right column.

## User decisions (2026-11-21)

The user accepted every recommended option:

- **Opening:** right-click with a brush tool opens the tray (the slot the Popup
  Palette used), and a new shortcut action opens it as well.
- **Position:** the tray opens at the cursor, clamped to the screen.
- **Number of trays:** one tray. Several named trays (presets) are out of
  scope; the stored format keeps room for them (see [Persistence](#persistence)).
- **Sizing:** sizes are entered as numbers in the configuration dialog first;
  dragging cell edges on the tray comes in a later phase.

## Existing mechanisms

| Mechanism | Location | Use for the tray |
| --- | --- | --- |
| `KisDockerHud` borrows `docker->widget()` into its own layout, puts an "open in %1" label into the `QDockWidget`, and returns the widget when hidden. A static `borrowedWidgetOwners` queue hands a docker between several borrowers. It also borrows the title-bar widget area of `TimelineDocker` and `AnimationCurvesDocker`. | `libs/ui/widgets/KisDockerHud.{h,cpp}`; used by the toolbar docker box (`libs/ui/kis_control_frame.cpp`, config id `toolbar`) | The borrowing model, the queue and the animation title-bar handling. The queue only knows `KisDockerHud` instances. |
| Quick Adjust's Tool Options pad borrows `sharedtooldocker`'s widget into a frameless `Qt::Tool` window, hides the docker and disables its toggle action until it returns. | `plugins/dockers/quickaccess/QuickAdjustToolOptionsPad.cpp`, [quick-access.md](quick-access.md#quick-adjust-ownership-and-history-2026-10-16) | A second borrower the tray must coexist with; its scroll-area `sizeHint` calculation and screen clamping. |
| `KisPopupWidgetInterface` (`popup()`, `onScreen()`, `dismiss()`, `finished()`), `KisPopupWidgetAction` (right-click "Show Popup Widget") and `KisInputManager::registerPopupWidget()` (dismiss on canvas press, `popupWasActive`). | `libs/ui/KisPopupWidgetInterface.h`, `libs/ui/input/KisPopupWidgetAction.cpp`, `libs/ui/input/kis_input_manager.cpp` | Kept without an implementer when the Popup Palette was removed. The tray implements the interface; `KisToolPaint::popupWidget()` returns it again. |
| `KisDlgListPicker` | `libs/ui/widgets/KisDlgListPicker.*` | Available/current list for choosing dockers. |

Facts that make requirement 4 possible:

- `KisMainWindow` creates a dock widget for every docker factory at
  construction (`createDockWidget()` loop), whether or not it is shown.
- `KisMainWindow::canvasObservers()` includes every docker, so hidden dockers
  receive `setCanvas()`/`unsetCanvas()` like visible ones.

## Design

### Borrow content, not the dock widget

The tray borrows each docker's content widget (`QDockWidget::widget()`), never
the `QDockWidget` itself. Re-parenting a dock widget out of the main window
breaks `QMainWindow::saveState()`/`restoreState()` and workspaces.

- Borrow when the tray is shown, return when it is hidden (the `KisDockerHud`
  rule). A hidden tray holds nothing, so window close, workspace loading and
  docker deletion never find a widget outside its docker.
- While borrowed, the docker shows a placeholder label naming the tray. A
  hidden docker stays hidden; its visibility and toggle action are not
  changed (unlike the Tool Options pad, the tray is transient).
- Every widget is returned before the tray's main window is destroyed
  (explicit return in the owner's teardown, before `delete d`), guarded by
  `QPointer` as in the Tool Options pad.

### Shared borrowing registry

Three borrowers can ask for the same docker: the tray, the toolbar docker box
and the Tool Options pad. Extract the borrow/return/queue logic of
`KisDockerHud` into a per-main-window registry in `libs/ui`
(`KisDockerBorrowRegistry`, implemented in phase 1) that all three use. It owns the placeholder, the
title-bar area handling and the queue; the last requester holds the widget,
and the previous holder shows the "open in %1" label and gets it back when
the holder returns it. `KisDockerHud`'s observable behavior stays as it is.

### Layout model: columns of stacked dockers

The tray is a row of columns; each column is a vertical stack of docker cells.
This covers the sketch without a 2D editor.

- Each cell has a stored width and height. The column width is the widest
  cell; the stored size is clamped to the content's `minimumSizeHint()`.
- Tray size: the sum of column widths by the tallest column, plus margins and
  a small header (configure button). It is recomputed when cells change and
  clamped to the available geometry of the cursor's screen; a cell that
  cannot fit is shrunk, never clipped off-screen.
- A docker id whose factory is missing (plugin not loaded) is skipped and
  kept in the configuration.
- A docker appears at most once in the tray.

### Window and interaction

- A frameless, always-on-top `Qt::Tool` child of the main window (the HueSVC
  popup's lifecycle), not a `Qt::Popup`: dockers open combo-box lists and
  modal dialogs (layer properties) that a `Qt::Popup` would close or block.
- Opens at the cursor, clamped to the screen. Activate the layout before
  computing the position and reapply it after `show()` (the Quick Access
  gesture-preview pitfall on Windows).
- Closes on: the opening shortcut again, right-click again, Esc, a press on
  the canvas (through `registerPopupWidget()`), and application deactivation.
  It stays open while a child `Qt::Popup` or a modal dialog it opened is
  active.
- Keyboard: text fields inside dockers (layer rename) take focus; canvas
  shortcuts do not run while they do. To be confirmed interactively.

### Ownership and entry points

- One tray per `KisMainWindow`, created lazily on first open (not in a plugin
  constructor; see the `KisActionPlugin` assertion rule in AGENTS.md), owned
  by the main window or its view manager.
- `KisToolPaint::popupWidget()` returns the tray of its canvas's main window.
  Other tools keep returning their context menus; the right-click entries in
  `krita/data/input/*.profile` are unchanged.
- A new action `show_docker_tray` ("Show Docker Tray") in
  `krita/krita.action`, no default shortcut unless the user picks one.
  `krita5.xmlgui` stays unchanged ([decision](wiki/decisions/xmlgui-unchanged.md)).

### Persistence

`kritarc`, group `DockerTray`, application-wide (every window reads the same
layout and borrows its own dockers):

```json
{"version": 1,
 "trays": [{"name": "Default",
            "columns": [{"items": [{"id": "KisLayerBox", "w": 320, "h": 260},
                                   {"id": "sharedtooldocker", "w": 320, "h": 240}]},
                        {"items": [{"id": "PresetDocker", "w": 280, "h": 520}]}]}]}
```

Ids are dock-widget object names, as in `KisDockerHud`'s `*/dockerList`.
Only the first tray is used; the list keeps presets possible without a format
change. Default: one column with the Layers docker.

### Configuration dialog

Opened from the tray's header button. Lists available dockers (including
hidden ones) and the tray's columns; add/remove/reorder dockers, add/remove
columns, move a docker between columns, and edit width/height as numbers.
Apply updates an open tray at once.

## Risks found in the investigation

- **Dockers that rely on their `QDockWidget`'s own events.** Borrowed content
  does not receive the dock widget's show, hide or resize events.
  - `LayerBox` enables thumbnail idle tasks in `showEvent()` and clears them
    in `hideEvent()` (`plugins/dockers/layerdocker/LayerBox.cpp`), and checks
    `isVisible()` in `setCanvas()`. A hidden Layers docker in the tray would
    not update thumbnails.
  - `AssetLibraryDock`, `RestNoteDock` and `KoToolBoxDocker` re-lay out in the
    dock widget's `resizeEvent()`.
  - `QuickAdjustDock` reacts to its own show/hide.
  Plan: the registry tells the docker it is presented elsewhere (a small
  optional interface, e.g. `setPresentedOutside(bool, QSize)`), and each
  affected docker adapts. Phase 1 starts with an audit of every docker in
  `plugins/dockers` and `libs/ui` for `showEvent`, `hideEvent`,
  `resizeEvent`, `isVisible()` and `visibilityChanged`.
- **Animation dockers:** their controls live in the title bar's widget area
  and must be borrowed with the content (existing `KisDockerHud` code).
- **Tool Options in toolbar mode:** `sharedtooldocker` does not exist then; it
  is skipped like a missing docker.
- **Large minimum sizes** of some dockers can make the tray larger than the
  screen; covered by the clamping rule.
- **Multiple windows:** a tray borrows only from its own window; the registry
  is per window.

## Phases

1. Audit docker visibility dependencies. Extract the borrowing registry and
   move `KisDockerHud` and the Tool Options pad onto it without behavior
   change (tests for borrow/return/queue order).
2. Tray widget with the column layout, auto size, at-cursor opening,
   dismissal, the `show_docker_tray` action and `KisToolPaint::popupWidget()`.
   Read the configuration from `kritarc` (hand-edited for testing).
3. Configuration dialog with numeric sizes.
4. Per-docker adaptations from the audit (Layers thumbnails first).
5. Drag-resizing cells on the tray, writing sizes back.
6. User guide `docs/docker-tray.md`, maintained reference
   `docs/agent/docker-tray.md` (this plan moves to the wiki history), README
   feature link, AGENTS index and feature-inventory touch points.

## Phase 1 results (2026-11-21)

### Docker audit

Every docker factory in `plugins/dockers` and `libs/ui/toolbox` was searched
for `showEvent`, `hideEvent`, `resizeEvent`, `changeEvent`, `isVisible()`,
`isHidden()`, `visibilityChanged`, `topLevelChanged`, `dockLocationChanged`,
`titleBarWidget()` and calls to the dock's own `widget()`. Overrides in
child widgets of the content move with the content and need nothing. What
depends on the `QDockWidget` itself:

| Docker | Dependency | Effect on the tray | Plan |
| --- | --- | --- | --- |
| Layers (`LayerBox`) | `showEvent()`/`hideEvent()` set and clear the node model's idle task manager; `setCanvas()` sets it only if `isVisible()` | A hidden Layers docker's thumbnails do not update on the tray | Fixed in phase 4 |
| Quick Brush Adjustments (`QuickAdjustDock`) | `showEvent()`/`hideEvent()` start/stop the 200 ms canvas sync timer and show its Tool Options pad; `setControlsEnabled()` uses `widget()` | Hidden: controls stop following the canvas. Borrowed: enabling reaches the placeholder, not the controls | Fixed in phase 4 (the Tool Options pad still follows the dock) |
| Quick Access (`QuickAccessDock`) | `applyAppearanceSettings()` finds header buttons through `widget()` | Settings changed while borrowed do not restyle the tray copy until it returns | Fixed in phase 4 |
| Asset Library (`AssetLibraryDock`) | `resizeEvent()` saves the size and re-flows thumbnail columns; a viewport event filter also re-flows | Column count follows the viewport filter on the tray; size saving does not run (wanted) | No change needed (phase 4) |
| Rest Note (`RestNoteDock`) | `resizeEvent()` scales the time font; settings dialog parented to `widget()` | Font not rescaled on the tray; dialog parented to the placeholder | Fixed in phase 4 |
| Toolbox (`KoToolBoxDocker`) | Title bar and `resizeEvent()`/`dockLocationChanged` choose orientation | Orientation stays as in the docker | Accepted, no change (phase 4) |
| Timeline, Animation Curves | Playback controls in `KisUtilityTitleBar::widgetArea()` | Borrowed with the content (registry) | Done |
| Python dockers (`libs/libkis/DockWidget`) | Scripts may call `setWidget()` at any time | A `setWidget()` while borrowed replaces the placeholder | Not covered; document as a limitation |

Every other docker sets its content once in the constructor and has no
dock-level visibility logic. All dockers stay canvas observers while hidden
(`KisMainWindow::canvasObservers()`).

The audit also found a defect in the existing code: the toolbar docker box
(`KisDockerHud`) borrowed `docker->widget()` without knowing about Quick
Adjust's Tool Options pad, so choosing Tool Options in the docker box while
the pad held it would have borrowed the pad's placeholder label. The shared
registry fixes this.

### Shared borrowing registry

- `libs/ui/widgets/KisDockerBorrowRegistry.{h,cpp}`: `KisDockerBorrower`
  (interface: name, placeholder text, `dockerContentGranted()`,
  `dockerContentHeldElsewhere()`) and `KisDockerBorrowRegistry` (one per
  main window, a child found with `forWindow()`; `request()`, `release()`,
  `releaseAll()`, `holder()`, `borrowers()`).
- Rules: the last requester holds the content; earlier requesters wait and
  are told who holds it; releasing passes it to the newest waiting borrower
  or back to the docker. A waiting borrower that requests again moves to the
  end. The docker's visibility is never changed by the registry. The
  placeholder is a `QLabel` owned by the registry, deleted later on return.
- Lifetime: `~KisDockerBorrower` releases everything (borrowers inherit
  `QObject`/`QWidget` first, so their children are still alive); the
  registry's destructor returns content to live dockers; dockers and
  content are held by `QPointer`. Callbacks must not call back into the
  registry.
- `KisDockerHud` (toolbar docker box) and `QuickAdjustToolOptionsPad` now
  borrow through the registry. Their visible behavior is unchanged; the pad
  additionally shows "Docker is open in %1" when another borrower takes
  Tool Options, and gets it back afterwards. The pad still hides the
  Tool Options docker and disables its toggle action while it exists.
- Strings: no new user-visible strings; both labels reuse existing
  `i18nc` messages.

### Verification

- Built `kritaui`, `kritaquickaccessdocker`, `KisDockerBorrowRegistryTest`
  and `QuickAdjustDockTest`; installed `libs/ui` and
  `plugins/dockers/quickaccess` into `_install`.
- `KisDockerBorrowRegistryTest`: 11 passed (per-window registry, borrow and
  return with the docker visible and hidden, unknown and empty dockers, the
  waiting order, re-request, borrower and registry destroyed while holding,
  animation title-bar controls).
- `QuickAdjustDockTest`: 15 passed, including the new
  `anotherBorrowerTakesAndReturns`.
- Checked by the user in the application (2026-11-21, all passed): the
  toolbar docker box (choose a docker, open and close the box, switch
  dockers while open, Timeline with its playback controls), the Quick
  Adjust Tool Options pad (toggle, close and reopen Quick Brush
  Adjustments, Tool Options docker restored), and both together (the pad
  shows "Docker is open in Toolbar Docker Box" and gets Tool Options back).

## Phase 2 results (2026-11-21)

### Source

| Part | Location |
| --- | --- |
| Layout model and size fitting | `libs/ui/widgets/KisDockerTrayLayout.{h,cpp}`: `KisDockerTrayLayout` (JSON read/write, defaults, `load()`/`save()` with an injectable `KSharedConfigPtr`), `KisDockerTrayGeometry::fit()` |
| Tray widget | `libs/ui/widgets/KisDockerTray.{h,cpp}`: `KisDockerTray` (`QWidget`, `KisDockerBorrower`, `KisPopupWidgetInterface`), one per main window, found with `forWindow()` |
| Shortcut action | `show_docker_tray` in `krita/krita.action` (General, no default shortcut), created in `KisControlFrame::createDockerBox()` (`libs/ui/kis_control_frame.cpp`) |
| Right-click | `KisToolPaint::popupWidget()` (`libs/ui/tool/kis_tool_paint.{h,cc}`) returns the tray of the canvas's main window |
| Tests | `libs/ui/tests/KisDockerTrayTest.cpp` |

### Behavior as implemented

- Opening reads the layout from `kritarc` each time (`DockerTray/Layout`),
  borrows every docker of the layout, sizes the cells, then shows the tray
  centered at the cursor and clamped 8 px inside the screen's available
  geometry. Dockers the window does not have are skipped. (Phase 2 did not
  open a tray with no docker; since phase 3 it opens with a hint, see below.)
- Each cell shows the docker's title above its content. A column is as wide
  as its widest cell; a cell is never smaller than its content's minimum.
  When the tray would not fit on the screen, columns and cells shrink in
  proportion (`KisDockerTrayGeometry::fit()`).
- The window is a frameless `Qt::Tool` child of the main window with a fixed
  size constraint, so it follows its content. It is activated when shown, so
  Esc works at once; the main window gets the focus back when it closes.
- Closing: Esc, the shortcut again (the action is also added to the tray, so
  it works while the tray has the focus), a press on the canvas (the tray is
  registered with `KisInputManager`), right-click on the canvas (the same
  press), and the application becoming inactive. Every non-spontaneous hide
  returns all content to the dockers; a spontaneous hide (minimizing the main
  window) keeps it.
- `dismiss()` only hides; `closeTray()` hides and emits `finished()`. The
  input manager calls `dismiss()` from `deregisterPopupWidget()`, which then
  reads its popup pointer again, so emitting `finished()` there would
  deregister twice and hit its `KIS_ASSERT`. The destructor emits
  `finished()` to detach from an input manager that still holds the tray.
- The right-click popup returns the tray for every tool derived from
  `KisToolPaint` (brushes, shapes, fill, gradient...), as the Popup Palette
  did; tools with their own right-click menu (selection, transform, crop,
  path, reference images) keep it. The change to `KisToolPaint`'s virtual
  table needs the tool plugins rebuilt and installed.
- Strings: the action ("Show Docker Tray") and the borrower name ("Docker
  Tray", used in "Docker is open in Docker Tray"); `zh_CN` translated, the
  feature name kept in English.

### Verification

- Full build of `_build` (all targets, so every tool plugin picks up the
  `KisToolPaint` change) and `cmake --install` of the whole build tree.
- `KisDockerTrayTest`: 18 passed (JSON round trip, cleanup of bad cells,
  invalid JSON, load/save, `fit()` keep/shrink/minimum, tray per window and
  window flags, open and close with a hidden docker, cell sizes, centering,
  `closeTray()` vs `dismiss()`, Esc, missing dockers, nothing to show,
  screen clamping at both corners, another borrower taking a cell, layout
  change rebuilds).
- `KisDockerBorrowRegistryTest` 11, `QuickAdjustDockTest` 15 and
  `KisInputManagerTest` 8 passed.
- `po/zh_CN/krita.po` updated with `update_translations.py`; the two new
  entries translated; `msgfmt --check --statistics`: 10,698 translated, no
  fuzzy or untranslated entries.
- Checked by the user in the application (2026-11-21, all passed):
  right-click with a brush and closing by canvas click, second right-click
  and Esc, painting right after closing; opening and closing with the
  shortcut, also after clicking inside the tray; a hidden docker used on the
  tray and still hidden and working afterwards; drop-down lists and dialogs
  (Layer Properties) from tray dockers; opening near screen edges and on a
  second monitor; Timeline with its playback controls. Layers thumbnails of
  a hidden Layers docker remain a known phase 4 item.

## Phase 3 results (2026-11-21)

### Source

| Part | Location |
| --- | --- |
| Dialog | `libs/ui/dialogs/KisDlgDockerTray.{h,cpp}` (`KisDlgDockerTray`, built in code, no `.ui`) |
| Editing operations | `KisDockerTrayLayout::find()`, `addDocker()`, `removeDocker()`, `moveDocker()`, `moveColumn()`, `addColumn()`, `removeColumn()`, `setSize()` |
| Tray header and reload | `KisDockerTray::configure()`, `reload()`; header with the configure button (`dockerTrayConfigure`), empty-tray hint |
| Tests | `libs/ui/tests/KisDockerTrayTest.cpp` (`layoutEditing`, `dialogEditsACopy`, `reloadKeepsTheTrayOpen`) |

### Behavior as implemented

- The tray has a header row with a configure button (top right). It opens
  **Configure Docker Tray**, a modal dialog over the tray.
- Left: **Available dockers**, every dock widget of the window not on the
  tray (hidden ones included), sorted by title. Right: **Dockers on the
  tray**, one bold row per column ("Column 1", ...) with its dockers and
  their width and height. A docker of the layout that the window does not
  have is listed as "<id> (not available)" and kept.
- **Add** (or double-click) appends the chosen docker to the selected column
  (or the column of the selected docker; the last column if nothing is
  selected). **Remove** returns a docker to the available list. **Move Up /
  Move Down** move a docker within its column and, past either end, to the
  end of the previous or the start of the next column; with a column
  selected they move the whole column left or right. **Add Column**, **Remove
  Column** (with its dockers). **Width / Height** spin boxes (50-4000 px) edit
  the selected docker. **Restore Defaults** sets the Layers-only layout.
- The dialog edits a copy. OK saves it (empty columns are dropped) and
  reloads an open tray around the same center; Cancel changes nothing.
  Reloading hides the tray without `finished()`, so the input manager keeps
  it registered, and opens it again.
- A tray whose layout has no existing docker now opens with a hint ("No
  dockers are on the Docker Tray. Use the configure button to add some.")
  instead of staying closed, so the dialog can always be reached.
- Stored sizes are those of the whole cell, including the docker title. A
  column shows at the width of its widest cell.

### Verification

- Built `kritaui` and `KisDockerTrayTest`; installed `libs/ui`.
- `KisDockerTrayTest`: 21 passed (the 18 of phase 2, with the empty-layout
  case now expecting the hint, plus layout editing, the dialog's buttons and
  spin boxes on a copy, and reload keeping the tray open).
- `zh_CN`: 16 new entries translated (feature name kept in English);
  `msgfmt --check --statistics`: 10,714 translated, no fuzzy or
  untranslated entries.
- Checked by the user in the application (2026-11-21, all passed): adding
  dockers in two columns with new sizes updates the open tray in place;
  Cancel changes nothing; removing every docker shows the hint and the
  configure button, then Restore Defaults; the layout survives closing and
  reopening the tray.

## Phase 4 results (2026-11-21)

### Approach

No new interface was needed. A docker that cares whether its content is
seen watches its **content widget's** `QEvent::Show` and `QEvent::Hide` with
an event filter, instead of its own `QDockWidget::showEvent()` and
`hideEvent()`. The content is visible wherever it is shown: in the docker,
on the tray or in the toolbar docker box. Moving it between them re-parents
it, which hides it (`Hide`), and the new host shows it (`Show`). Code that
reached the content through `widget()` keeps its own pointer to the root,
because `widget()` is the registry's placeholder while the content is
borrowed.

### Changes

| Docker | Change |
| --- | --- |
| Layers (`plugins/dockers/layerdocker/LayerBox.{h,cpp}`, upstream file) | `showEvent()`/`hideEvent()` replaced by `eventFilter()` on the content (`m_mainWidget`): `Show` sets the node model's idle task manager (thumbnails), `Hide` clears it; `setCanvas()` checks `m_mainWidget->isVisible()` |
| Quick Brush Adjustments (`QuickAdjustDock.{h,cpp}`) | The 200 ms sync timer starts (after one `syncFromCanvas()`) on the content's `Show` and stops on `Hide`; `setControlsEnabled()` uses `m_root`. The Tool Options pad stays tied to the dock's own show/hide and position, so it does not appear while only the tray shows Quick Adjust |
| Quick Access (`QuickAccessDock.{h,cpp}`) | `applyAppearanceSettings()` uses `m_root` |
| Rest Note (`RestNoteDock.{h,cpp}`) | The dock's `resizeEvent()` override is removed; `updateSizing()` runs on the content's `Resize` (an event filter on `m_root`); the settings dialog's parent is `m_root` |

Asset Library and the toolbox need nothing. Python dockers that call
`setWidget()` while their content is borrowed remain unsupported (the
placeholder is replaced); this is a limitation.

### Verification

- Incremental build of `_build`; installed `plugins/dockers/layerdocker`,
  `quickaccess` and `restnote`.
- Passed: `QuickAdjustDockTest` 16 (new `syncTimerFollowsContentShownElsewhere`:
  the timer runs while the content is shown in another visible widget with
  the dock hidden, and stops when that widget hides; the existing
  `syncTimerRunsOnlyWhileShown` still passes), the other Quick Access suites
  (Config 11, Core 9, GridEdit 11, Interaction 18, ItemProperties 24,
  KeyController 4), `RestNoteCoreTest` 16, `RestNoteWidgetsTest` 9,
  `TestNodeView` 4, `KisDockerTrayTest` 21, `KisDockerBorrowRegistryTest` 11.
- No new user-visible strings.
- Checked by the user in the application (2026-11-21, all passed): Layers
  thumbnails update on the tray with the Layers docker hidden; Quick Brush
  Adjustments on the tray follows the canvas and is disabled without a
  document; Rest Note's time text scales with the cell and its settings open
  from the tray; all four dockers still work as ordinary dockers.

## Manual checks (to run once implemented)

1. Right-click with a brush tool opens the tray at the cursor; again closes it.
   Transform, selection and other tools still show their context menus.
2. The shortcut action opens and closes the tray.
3. A hidden docker (e.g. Layers) works on the tray, including thumbnails;
   after closing, it is still hidden and works when shown normally.
4. A visible docker shows the placeholder while borrowed and its content
   returns on close.
5. Combo-box lists and dialogs opened from tray dockers work without closing
   the tray.
6. The tray resizes when dockers are added, removed or resized, and stays on
   screen near every edge and on a second monitor.
7. Toolbar docker box and Quick Adjust Tool Options pad borrowing the same
   docker hand it over and get it back.
8. Timeline and Animation Curves controls work on the tray.
9. Closing a window or loading a workspace with the tray open leaves every
   docker intact.
