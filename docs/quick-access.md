# Quick Access Manager

Quick Access Manager is implemented as native Krita dockers and popup tools. It
provides configurable action, docker, and brush grids; editable profiles;
gesture menus; Quick Brush Adjustments; a compact HueSVC selector; and
hold-based temporary brush shortcuts.

HueSVC places compact overlapping foreground and background color swatches at
the selector's top-left. Clicking either swatch swaps the two colors.
Its popup uses a fixed-width vertical adjustment panel so each brush and layer
slider spans the panel, matching the original plugin layout. Docker-only status
buttons and their separators are omitted from the popup.

Existing profile aliases, custom labels, colors, icons, and legacy JSON fields
remain supported where practical.

Native implementation: [`../plugins/dockers/quickaccess`](../plugins/dockers/quickaccess/)
