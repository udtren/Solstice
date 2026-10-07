# Quick Access Manager

Quick Access Manager is implemented as native Krita dockers and popup tools. It
provides configurable action, docker, and brush grids; editable profiles;
gesture menus; Quick Brush Adjustments; a compact HueSVC selector; and
hold-based temporary brush shortcuts.

HueSVC places compact overlapping foreground and background color swatches at
the selector's top-left. Clicking either swatch swaps the two colors.
Its popup uses a fixed-width vertical adjustment panel so each brush and layer
slider spans the panel, matching the original plugin layout. Docker-only status
buttons and their separators are omitted from the popup. Opening either brush
or layer blending-mode list keeps the HueSVC popup open while the list is in
use.

## Arranging items

- **Ctrl + drag** an item to move it to another cell of the palette without
  opening Grid Edit. A frame shows where it will land; items in the way are
  moved aside. A plain click still uses the item, and any other mouse button
  cancels the drag. Resizing, multi-selection and moving items between tabs
  stay in Grid Edit.
- Items added from the **Resources** dialog fill the palette's last empty row
  from the left and continue on the next row when the row is full. Items
  added from the header menu start a new row each time.

## Brushes

Brushes show stroke previews, like the Brush Presets docker:

- In the **Resources** dialog, the **Brushes** tab works like the Brush
  Presets docker: stroke previews with names, tags, search, the **Engines**
  and **Bundles** filters and the grouping dropdown (shared with the
  docker). Select several presets with Ctrl or Shift and click **Add
  Selected Brushes**, or double-click a preset.
- A brush added to the palette (from the Resources dialog or with **Add
  Current Brush**) shows its stroke preview over two cells, without the name.
  The name appears in the tooltip.
- Right-click a brush and choose **Property** to choose its **Display**:
  **Stroke Preview** (two cells) or **Icon** (the preset icon, one cell).
  Changing it resizes the item and moves the following items as needed.
- **Grid Edit** shows each brush the same way as the palette.

Brushes added before stroke previews existed keep their icon until their
display is changed.

Existing profile aliases, custom labels, colors, icons, and legacy JSON fields
remain supported where practical.

Native implementation: [`../plugins/dockers/quickaccess`](../plugins/dockers/quickaccess/)
