# Brush preset grouping: technical notes

User guide: the "Grouping" section of
[`../brush-stroke-preview.md`](../brush-stroke-preview.md).

Added 2026-10-07 at the user's request: a dropdown in the Brush Presets
docker's filter bar shows the presets in groups. Read
[`brush-stroke-preview.md`](brush-stroke-preview.md) first; the grouping
builds on the docker's filter bar and stroke preview grid.

## Behavior

| Option | Groups | Header | Order |
| --- | --- | --- | --- |
| No Grouping (default) | none | - | plain grid, as before |
| Group by Engine | brush engine (`paintopid` metadata) | engine display name from `KisPaintOpRegistry` | by name; "Unknown engine" last |
| Group by Bundle | the bundle containing the preset | bundle label as in the Bundles filter | by name; "Not in a bundle" last |

- A preset stored in several active bundles is shown once, under the bundle
  whose label sorts first. Presets only in folders, memory or other
  non-bundle storage go to "Not in a bundle".
- Within a group, presets keep the model order (the same order as without
  grouping). Tag, search, engine and bundle filters apply as before; empty
  groups are not shown.
- The choice is saved in `kritarc` as `Solstice/BrushPresetGrouping`
  (0 none, 1 engine, 2 bundle) and shared by all Brush Presets dockers.

## Locations

| Part | Location |
| --- | --- |
| Grouped layout and headers | `KisResourceItemListView::setGrouping()`, `doItemsLayout()`, `paintEvent()` (`libs/resourcewidgets/`) |
| Dropdown, group keys, cache | `KisPresetDockerFilters` (`m_grouping`, `groupOf()`, `applyGrouping()`, `m_groupCache`) |
| Wiring | `KisPresetChooser::enableDockerFilters()` passes the item view to the filter bar |
| Test | `KisBrushStrokePreviewTest::testDockerGrouping` |

## Layout

The preset grid is a `QListView` in icon mode with a fixed grid size. Icon
mode uses free movement, so items can be placed with
`setPositionForIndex()`; Qt has no grouped icon grid.

`KisResourceItemListView::doItemsLayout()` runs Qt's layout first, then, with
a group function and in `IconGrid` mode:

1. buckets the visible rows by group in model order and sorts the groups by
   sort key (`localeAwareCompare`);
2. takes the first visible row's position from Qt's layout as the origin, so
   the item offset inside a grid cell stays Qt's;
3. stacks the groups: a header row (font height + 10 px), then the group's
   items in `viewport width / grid width` columns, then 4 px;
4. calls `updateGeometries()` for the scroll range.

`paintEvent()` draws the headers after the items: bold text in the palette's
text color and a line at 25% opacity. Header rects are kept in contents
coordinates and offset by the scroll position.

Resizing relayouts through `QListView::Adjust`, and model changes (filters,
tags, search, resource changes) through Qt's delayed layout, so the groups
follow automatically. Stroke preview requests use `visualRect()`, which
reflects the moved positions.

## Group keys and cost

`KisPresetDockerFilters::groupOf()` returns (sort key, label): sort keys start
with "1" for named groups and "2" for the catch-all group. Bundle membership
comes from `KisTagFilterResourceProxyModel::activeStorageIdsForIndex()`,
which runs a database query, so results are cached per resource id.
`refresh()` (resource or storage changes) and every change of the dropdown
clear the cache. The view holds a function that calls back into the filter
bar; the filter bar's destructor clears it (`QPointer` to the view).

## Manual checks

- Default is No Grouping; the grid looks as before.
- Engine and bundle grouping show headers in name order, catch-all last;
  groups follow narrow and wide docker sizes and scrolling.
- Tag, search, engine and bundle filters combined with grouping hide empty
  groups.
- Selecting, double-clicking and keyboard navigation work across groups;
  the current preset stays selected when switching grouping.
- The setting survives a restart.

## Limitations

- A preset in several bundles appears only in one bundle group.
- Group headers cannot be collapsed.
- Grouping is available in the Brush Presets docker, the toolbar brush popup
  and the Quick Access Resources dialog (all with the docker filter bar), not
  in the Brush Editor's preset list.

## Brush tips (2026-10-10)

`KisPresetDockerFilters` takes a resource type (default
`ResourceType::PaintOpPresets`). For another type it hides the engine facet,
offers only No Grouping and Group by Bundle (the combo items carry the
`Grouping` value as data; the stored value is the enum, not the index) and
stores the grouping as `Solstice/<type>Grouping` (`Solstice/brushesGrouping`
for brush tips). `KisPredefinedBrushChooser`
(`plugins/paintops/libpaintop/kis_predefined_brush_chooser.cpp`) puts one for
`ResourceType::Brushes` under its tip list. Storages of type Adobe Brush
Library (`.abr`) count as bundles for both presets and tips
(`docs/agent/abr-import-plan.md`). Test: `testBrushTipFilters` in
`libs/ui/tests/KisBrushStrokePreviewTest.cpp` (the test resources hold no
tips, so the filtering itself is checked only when tips exist).

The tip list also gets more of the editor's width: the form's top
`horizontalLayout_2` had no stretch factors, so the settings column (spin
boxes with expanding policies) took all extra width and the list stayed at
four columns; `KisPredefinedBrushChooser` now sets stretch 3 (list) to 2
(settings).

Manual checks: in the Brush Editor's Predefined tips, the Bundles dropdown
lists the bundles and ABR libraries that hold tips, unchecking one hides its
tips, and Group by Bundle shows the tips under bundle headers; the presets'
grouping is unaffected.

## Scroll position across relayouts (2026-10-10)

Clicking a preset at the bottom of a grouped list threw the list upwards.
`KisResourceItemListView::doItemsLayout()` first runs `QListView`'s plain
layout, which sets the scroll range without the group headers and clips the
scroll value; the grouped layout then extends the range but the value stayed
clipped (459 became 433 in the test). The selection triggers such a relayout.
`doItemsLayout()` now remembers the vertical scroll value before the plain
layout and sets it again after the grouped layout's `updateGeometries()`.
Test: `testClickAtBottomKeepsScroll` (no grouping, by bundle, by engine) in
`libs/ui/tests/KisBrushStrokePreviewTest.cpp`.

Manual check: with Group by Bundle or Group by Engine, scroll the Brush
Presets docker to the bottom and click a preset there; the list stays.
