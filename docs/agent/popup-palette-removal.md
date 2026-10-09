# Removal of the Popup Palette and the On-Canvas Brush Editor

Solstice removed Krita's right-click Popup Palette and its On-Canvas Brush
Editor (brush HUD) on 2026-10-09 at the user's request. Quick Access
(`docs/agent/quick-access.md`) provides the color selector, brush grid and
adjustments instead. This document records what was removed, what had to be
kept and why, so that later work does not restore or break either side.

## Removed

| Part | Location |
| --- | --- |
| On-Canvas Brush Editor docker plugin (`BrushHudDocker`, `KisBrushHud`, `KisDlgConfigureBrushHud`, `KisUniformPaintOpPropertyWidget`, its test) | `plugins/dockers/brushhud/` and `add_subdirectory(brushhud)` in `plugins/dockers/CMakeLists.txt` |
| Popup Palette widget | `libs/ui/kis_popup_palette.{h,cpp}`, `libs/ui/CMakeLists.txt` |
| Its creation and ownership | `KisCanvas2::setFavoriteResourceManager()`, `popupPalette()`, `slotPopupPaletteRequestedZoomChange()` and the re-parenting in `setCanvasWidget()` and the display-config update (`libs/ui/canvas/kis_canvas2.{h,cpp}`); the call in `KisMainWindow::showView()` |
| Brush tools returning it on right-click | `KisToolPaint::popupWidget()` (`libs/ui/tool/kis_tool_paint.{h,cc}`) |
| Pop-up Palette page of the Preferences | `PopupPaletteTab` in `libs/ui/dialogs/kis_dlg_preferences.{h,cc}`, `libs/ui/forms/WdgPopupPaletteSettings.ui` |
| Settings used only by the two features | `KisConfig::showBrushHud()`, `showPaletteBottomBar()`, `brushHudSetting()` and their setters |
| Unneeded includes of `kis_popup_palette.h` | `input/kis_input_manager_p.cpp`, `input/KisPopupWidgetAction.cpp`, `kis_favorite_resource_manager.cpp` |

The installed `lib/kritaplugins/kritabrushhud.dll` of an existing test
installation must be deleted by hand; `cmake --install` does not remove it.

## Kept, and why

- **The "Show Popup Widget" input action** (`libs/ui/input/KisPopupWidgetAction.*`)
  and its right-click entries in `krita/data/input/*.profile`. The same
  action shows the right-click context menus of the Transform, Crop,
  selection, Path, Shape Select and Reference Images tools and the Enclose
  and Fill subtools (`KoToolBase::popupActionsMenu()`). It opens a popup
  widget only when the tool returns one (`KoToolBase::popupWidget()`, now
  `nullptr` for every tool). Removing the action or the profile entries
  removes those menus, and saved user profiles name it. This is the most
  likely cause of the problem in an earlier removal attempt.
- **`KisPopupWidgetInterface` and the popup handling of `KisInputManager`**
  (`registerPopupWidget()`, click-to-dismiss, `popupWasActive`): part of the
  same mechanism; now without an implementer.
- **The tools' `popupWidget()` overrides and right-click event filters**
  (`kis_tool_select_base.h`, `kis_tool_polyline_base.cpp`,
  `KisToolOutlineBase.cpp`, `kis_tool_path.cc`, `KisPathEnclosingProducer.cpp`,
  `KisDynamicDelegatedTool.h`): the overrides now return `nullptr` through
  `KoToolBase`; the filters still use right-click to undo the last point
  while drawing. Left as upstream code.
- **`KisFavoriteResourceManager`**: used by the next/previous favorite preset
  actions, the Brush Editor and the save preset dialog. Its color history
  signals (`hidePalettes`, `updatePalettes`, `setSelectedColor`,
  `sigChangeFGColorSelector`) have no receiver now. The number of favorite
  presets (`numFavoritePresets`, default 10) was set on the removed
  Preferences page; it keeps its saved value.
- **`KisDockerHud`**: also used by the toolbar's docker box
  (`kis_control_frame.cpp`). Its default docker list and current docker
  changed from `BrushHudDocker` to `KisLayerBox`.
- **Uniform properties** (`KisPaintOpSettings::uniformProperties()`,
  `libs/image/brushengine/kis_*_paintop_property*`, the engines' overrides,
  `KisCurveOptionDataUniformProperty`): no UI uses them any more, but the
  engines still define them and `KisBrushTipOptionParityTest` and
  `KisPaintOpOptionsModelTest` exercise them. Removing them is a separate,
  larger change.
- `config-popup-palette` icons in `krita/pics/svg/svg-icons.qrc`, the
  `popuppalette/*` keys in existing kritarc files (only
  `popuppalette/colorHistorySorting` is still read, by the favorite manager)
  and translations in `po/`.

## Behavior

- Right-click on a brush tool does nothing. Right-click menus of the tools
  listed above work as before.
- The Quick Access popups and gestures do not use any of the removed parts.

## Tests and manual checks

Individual tests after the removal: `KisInputManagerTest`,
`QuickAccessCoreTest`, the brush option parity tests and
`KisToolOptionsBrushTest` pass.

Manual checks (all passed on 2026-10-09):

1. Right-click on the canvas with a brush tool: nothing opens, no warning.
2. Right-click with the Transform, Crop, a selection, Path, Shape Select and
   Reference Images tools: their context menus open.
3. Polyline, polygon, path and outline selection: right-click while drawing
   removes the last point.
4. Settings > Configure Solstice: no Pop-up Palette page; the other pages
   save and reset normally.
5. Settings > Dockers: no On-Canvas Brush Editor. The toolbar's docker box
   offers the remaining dockers.
6. Next/previous favorite preset (`,` and `.`) still cycle the favorites.
7. Quick Access palette, HueSVC popup and gestures work as before.
