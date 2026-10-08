# Brush options in Tool Options: technical notes

User guide: the "Brush options in Tool Options" section of
[`../brush-editor.md`](../brush-editor.md). Plan and phase history:
[`brush-option-shared-model-plan.md`](brush-option-shared-model-plan.md),
phase 3.

Phase 3 of the brush option shared model. Like Clip Studio Paint's
sub tool detail (eye marks) and tool property palette, an eye in the Brush
Editor (F5) chooses which brush items appear in the Tool Options docker.

## Decisions (user, 2026-10-08)

- Curve options (Size, Ratio, Spacing, ...): only the enable checkbox goes to
  Tool Options; curves, sensors and the strength slider stay in F5.
- Page parameters: start with the main ones (Brush Tip Diameter, Ratio,
  Angle, Spacing, Density, auto spacing; Blending Mode; Painting Mode;
  Precision; Texture enable and Scale). Phase 3b; not implemented yet.
- The shown items are kept per brush engine in kritarc, not in presets.
- The Brush section is below the tool's own options.

## Phase 3a (implemented and manually checked 2026-10-08)

| Part | Location |
| --- | --- |
| Item id and shown flag of an option | `KisPaintOpOption::setToolOptionsId()`, `setShownInToolOptions()`, `sigShownInToolOptionsChanged()` (`libs/ui/kis_paintop_option.*`) |
| Eye roles | `__CategorizedListModelBase::isShowableInToolOptionsRole`, `isShownInToolOptionsRole` (`libs/ui/kis_categorized_list_model.h`); `KisPaintOpOptionListModel::data()`/`setData()`, `slotShownInToolOptionsChanged()` |
| Eye column | `KisCategorizedItemDelegate::setToolOptionsColumnVisible()`, `paint()`, `sizeHint()`, `editorEvent()` |
| Per-engine storage | `KisToolOptionsBrushItems` (`libs/ui/KisToolOptionsBrushItems.*`): `Solstice/ToolOptionsBrushItems/<paintop id>` (comma separated ids), `Solstice/ToolOptionsBrushCollapsed` |
| Engine id, sync with the storage | `KisPaintOpSettingsWidget::setPaintOpId()`, `toolOptionsOptions()`; called by `KisPaintopBox::setCurrentPaintop()` after creating the widget |
| Engine switch notification | `KisPaintopBox::sigCurrentSettingsWidgetChanged()`, `currentSettingsWidget()` |
| Brush section | `KisToolOptionsBrushSection` (`libs/ui/tool/KisToolOptionsBrushSection.*`), added by `KisToolPaint::createOptionWidget()` when `showsBrushOptions()` |
| Tools | `KisToolFreehand` (Freehand Brush, Dynamic Brush, Multibrush, Colorize Mask brush, ...) and `KisToolLine` return true |
| Ids | Pixel Brush and Deform pass every option through `withToolOptionsId()` with its options-model id (`kis_brushop_settings_widget.cpp`, `kis_deform_paintop_settings_widget.cpp`) |
| Test | `plugins/paintops/defaultpaintops/brush/tests/KisToolOptionsBrushTest.cpp` (shares `KisBrushTestMain.h` with the parity test) |

### Rules

- An option has an eye only when it is checkable and has a Tool Options id.
  Engines without ids (not migrated to the shared model) show no eye column
  and their Brush section shows a hint.
- The ids are the options-model ids, stable and untranslated. Renaming one
  drops it from users' kritarc lists.
- The Brush section binds each checkbox to the editor's `KisPaintOpOption`
  (`setChecked()`, `sigCheckedChanged`, `sigEnabledChanged`). For migrated
  engines the checked state is a cursor into the options model, so the model
  writes the preset; the editor's list follows through its existing signals.
- Options are grouped under their editor category when more than one
  category is shown (the Pixel Brush has "Size" in General and in Masked
  Brush).
- A click in the eye column never toggles the checkbox: `editorEvent()`
  consumes it and passes other events with the row rectangle shifted, as
  painted.

### Not covered yet

- Shape tools (Rectangle, Ellipse, Polygon, ...) build their options from
  `WdgGeometryOptions` and have no Brush section.
- Page parameters (phase 3b).

## Manual checks (3a)

1. Pixel Brush: F5 shows an eye column; checkable rows have a box, others
   none. Clicking the box shows the eye without toggling the checkbox.
2. With eyes on (e.g. Size, Spacing, Masked Brush Size), the Freehand Brush
   and Line tools show them under "Brush" below their options, grouped by
   category; toggling a checkbox there changes F5, the stroke and the
   preset's modified mark, and the reverse.
3. Switching presets updates the checkboxes; switching to an engine
   without support shows the hint; Deform shows its own items.
4. The choice survives a restart and applies to every Pixel Brush preset.
5. The section's header collapses and expands, remembered.
6. Lightness Strength is disabled in Tool Options unless the tip is in
   Lightness map mode.
