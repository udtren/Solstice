# Brush Presets: scroll to the selected preset

User guide: the "Selected preset" section of
[`../brush-stroke-preview.md`](../brush-stroke-preview.md).

Added 2026-10-08 at the user's request. In Krita, switching brushes
(shortcut, toolbar, Quick Access, another docker) selects the new preset in
the Brush Presets docker and scrolls the list to it. Solstice adds a setting
to keep the list where it is and move only the highlight.

## Behavior

| Setting | Brush selected elsewhere | Docker resized |
| --- | --- | --- |
| **Scroll to Selected Preset** on | the list scrolls to the preset (Krita) | the selected preset is centered (Krita) |
| off (Solstice default) | the highlight moves; the scroll position stays | the scroll position stays |

- The setting is in the Brush Presets docker's display menu (the ≡ button),
  section "Selection". It is saved in `kritarc` as
  `Solstice/BrushPresetScrollToSelection` (default `false`) and shared by all
  Brush Presets dockers; each docker reads it again on every selection and
  when its menu opens.
- Clicking a preset or moving with the keyboard scrolls as usual: Qt's
  auto scroll is turned off only while the program sets the current item.
- The toolbar brush popup and the Brush Editor's preset list keep Krita's
  behavior (they do not call `enableScrollToSelectionSetting()`).

## Locations

| Part | Location |
| --- | --- |
| Flag and resize behavior | `KisResourceItemListView::setFollowCurrentItem()`, `resizeEvent()` (`libs/resourcewidgets/`) |
| Selection without auto scroll | `KisResourceItemChooser::setViewCurrentIndex()`, used by both `setCurrentResource()` overloads |
| Menu entry, setting | `KisPaintOpPresetsChooserPopup::enableScrollToSelectionSetting()`, `Private::applyScrollToSelection()` (`libs/ui/widgets/kis_paintop_presets_chooser_popup.cpp`) |
| Docker | `PresetDockerDock` constructor (`plugins/dockers/presetdocker/presetdocker_dock.cpp`) |
| Test | `KisBrushStrokePreviewTest::testDockerScrollToSelection` |

## Implementation notes

- `QAbstractItemView::setCurrentIndex()` scrolls through `currentChanged()`
  only when `autoScroll` is on; for a hidden view it remembers to scroll when
  shown (`shouldScrollToCurrentOnShow`), which also follows `autoScroll`.
  `setViewCurrentIndex()` therefore switches `autoScroll` off around the call
  and restores it, so drag auto scroll is unaffected.
- The test fails (scroll value 433 instead of 0) when the auto scroll is not
  switched off.

## Manual checks

- With the setting off (default): scroll the docker away from the current
  preset, switch brushes with a shortcut or the toolbar; the list stays and
  the highlight moves (visible after scrolling back). Resizing the docker
  keeps the position.
- With the setting on: the list scrolls to the selected preset, as in Krita.
- Clicking presets and keyboard navigation in the docker behave as before.
- The toolbar brush popup still scrolls to the selected preset.
