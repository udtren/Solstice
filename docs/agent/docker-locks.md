# Docker locks: technical notes

User guide: [`../docker-locks.md`](../docker-locks.md).

Added 2026-10-07 at the user's request, modelled on Clip Studio Paint's
"fix palette dock width / height / arrangement" options.
The user checked all three options in the application (2026-10-07). The
user then had the third, floating prevention, removed the same day as not
needed (see "Removed: floating prevention").

## Locations

| Part | Location |
| --- | --- |
| Menu actions | `KisMainWindow` constructor, appended to `d->dockWidgetMenu` (Settings → Dockers) after the docker list and a separator. Plain `QAction`s, not registered in `kritamenu.action`. |
| Settings | `kritarc` keys `Solstice/LockDockedDockerWidths`, `Solstice/LockDockedDockerHeights` (default `false`), read and written through `KisConfig`. |
| Apply | `KisMainWindow::updateSolsticeDockLocks()`, `slotSolsticeDockLocksToggled()` |
| Separator lock | `KisMainWindow::event()`, `solsticeSeparatorLocked()`, `resetSolsticeSeparatorCursor()` |

`krita5.xmlgui` is deliberately unchanged: a change requires bumping
`KRITA5_XMLGUI_VERSION`, which can discard users' local toolbar
customizations (the user has a local `krita5.xmlgui`).

## Separator lock

Qt moves the separators of dock areas and docked dockers inside
`QMainWindow::event()` (`QMainWindowLayoutSeparatorHelper::windowEvent()` in
`qmainwindowlayout_p.h`): a left press on a separator starts the move, and
hover or mouse moves set a split cursor on the main window. Qt's own
`separatorCursor()` uses `Qt::SplitHCursor` for separators that change
widths (left/right dock areas, dockers side by side) and `Qt::SplitVCursor`
for those that change heights (top/bottom dock areas, stacked dockers).

`KisMainWindow::event()` only acts while a size lock is on:

- `HoverMove` and button-less `MouseMove`: the base class handles the event;
  if the cursor is then the split cursor of a locked direction, a synthetic
  move far outside every separator makes Qt restore its cursor.
- Left `MouseButtonPress`: a synthetic button-less move at the press position
  lets Qt pick the separator under it; if its cursor is a locked direction,
  the cursor is reset and the press is consumed, so no move starts.

No global or application-wide event filter is installed; the lock lives in
the main window's own event handler. Separators inside floating
`QDockWidgetGroupWindow`s are not covered.

Lifetime rules (a first version crashed at startup):

- `KisMainWindow::event()` runs while `Private`'s constructor is still
  creating `d` (child events from `new KActionMenu(..., parent)`), so it
  reads `d` only for hover, mouse-move and press events, which cannot arrive
  before the window is shown. Reading `d` first crashed with an access
  violation at startup.
- The synthetic cursor reset in `updateSolsticeDockLocks()` runs only for a
  visible window.
- The destructor disconnects the dockers' signals from the window before
  `delete d`: the dockers are destroyed later by the QWidget base. It was
  added for the floating prevention's lambdas, which read `d`, and is kept
  for the remaining docker connections.

## Removed: floating prevention

The third option, **Prevent Docked Dockers from Floating**, was removed on
2026-10-07 at the user's request. Its implementation:

- `applySolsticeNoFloat()` removed `QDockWidget::DockWidgetFloatable` from
  docked dockers, marked by the property `solsticeNoFloat`.
- `watchSolsticeDockLocks()` re-applied the rule on `topLevelChanged()` and
  `featuresChanged()`.

It is in Git history before the removal commit. Pitfall recorded there: the
feature must not be restored on `topLevelChanged(true)`. Dragging unplugs a
docker into a temporary floating window, and restoring the feature let the
drop leave it floating.

Dock widget features are not saved in the window state, so dockers start
floatable again after a restart. The `kritarc` key
`Solstice/PreventDockedDockerFloating` is no longer read; stale values are
harmless.

The options are application-wide: toggling one in a window writes the
settings and calls `updateSolsticeDockLocks()` on every main window from
`KisPart::mainWindows()`.

## Manual regression checks

- Each option toggles at once and survives a restart; a second window follows.
- Width lock: dock area edges and side-by-side separators cannot be dragged,
  and show no resize cursor; stacked separators still can (and vice versa
  for the height lock); both off restores dragging and cursors.
- Floating: the Dockers submenu shows only the two lock options; dockers
  float by dragging, double-clicking or the Float button, with the locks on
  or off.
- Title bar lock: lock and unlock a docker with the locks on and off; the
  docker's lock state and Float button stay correct.
- Workspaces: loading a workspace with floating dockers still floats them.

## Limitations

- Only dragging is prevented; window resizing, showing or hiding dockers and
  workspaces still change sizes.
- Floating tabbed groups keep their internal separators.
- The actions sit in the Dockers submenu, which `KisMainWindow` disables on
  the welcome page together with the docker toggles.
