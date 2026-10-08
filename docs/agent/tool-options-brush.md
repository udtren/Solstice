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
  Precision; Texture enable and Scale). Phase 3b, below.
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

## Phase 3b: page parameters (implemented and manually checked 2026-10-08)

An eye button in front of a page control's label (or the control itself)
shows the control in the Brush section, as a copy kept in sync with it.

| Option (id) | Parameters (id, control) |
| --- | --- |
| Brush Tip (`BrushTip`, Pixel Brush) | Auto tip: `Diameter` (`inputRadius`), `Ratio` (`inputRatio`), `Angle` (`inputAngle`), `Density` (`density`), `Spacing` (`spacingWidget`, with Auto); Predefined tip: `PredefinedSize` (`brushSizeSpinBox`), `PredefinedAngle` (`brushRotationAngleSelector`), `PredefinedSpacing` (`brushSpacingSelectionWidget`); with SupportsPrecision: `Precision` (`sliderPrecision`), `AutoPrecision` (`autoPrecisionCheckBox`) |
| Blending Mode (`CompositeOp`) | `BlendingMode` (the list, as a blending mode combo box) |
| Curve options without a checkbox in the list (Pixel Brush Opacity, Flow, Masked Brush Opacity and Flow; Deform Opacity) | `Strength` (the strength bar at the top of the page, labeled with the option's name), `PenSettings` (Enable Pen Settings); registered by `KisCurveOptionWidget` when `isCheckable()` is false. A checkable curve option is switched by its row's checkbox instead (phase 3a) |
| Painting Mode (`PaintingMode`) | `PaintingMode` (the group box's radio buttons) |
| Texture (`Texture`) | `Scale` (`scaleSlider`) |

The kritarc list stores a parameter as `<option id>/<parameter id>`, e.g.
`BrushTip/Diameter` (`KisPaintOpSettingsWidget::toolOptionsParameterId()`).

| Part | Location |
| --- | --- |
| Registration | `KisPaintOpOption::addToolOptionsParameter()`, `toolOptionsParameters()`; called by `KisBrushOptionWidget`, `KisCompositeOpOptionWidget`, `KisPaintingModeOptionWidget`, `KisTextureOptionWidget` (`plugins/paintops/libpaintop/`) |
| Eye button and its place | `KisToolOptionsParameters::createEyeButton()`, `placeEyeButton()` (`libs/ui/KisToolOptionsParameters.*`): the label (or the control) is wrapped with the eye in a row of the same grid, box or form layout |
| Wiring | `KisPaintOpSettingsWidget::setPaintOpId()` shows the eyes and connects them to `KisToolOptionsBrushItems` |
| Copy in Tool Options | `KisToolOptionsParameterMirror` (same file), created by `KisToolOptionsBrushSection::rebuild()` as label + control rows |
| Slider copies | `KisSliderSpinBox::exponentRatio()`, `KisDoubleSliderSpinBox::exponentRatio()` (`libs/widgetutils/kis_slider_spin_box.*`, new getters) |
| Shape tools | `KisToolShape::createOptionWidgets()` appends the section as its own option widget; `moveBrushSectionLast()` keeps it after the Rectangle and Ellipse constraint widget (`KisToolRectangleBase::createOptionWidgets()`); Rectangle, Ellipse, Polygon and Polyline return true from `showsBrushOptions()` |
| Test | `KisToolOptionsBrushTest` (`testParametersRegistered`, `testDiameterMirror`, `testTipTypeVisibility`, `testOtherMirrors`) |

### Preset preview

At the top of the section (between the header and the options, so
`rebuild()` keeps it) `KisToolOptionsBrushPreview` shows the current preset's
stroke preview and name, as Clip Studio Paint's tool property palette does.

- The image comes from `KisBrushStrokePreviewCache` with the request of the
  preset's row in the paint op preset resource model
  (`indexForResourceId()`), like the Brush Presets docker: it shows the
  saved preset. A preset that is not in the resource database has only its
  name. The widget is a cache consumer while shown.
- The name is drawn over the top left of the image; a modified preset gets a
  "*" (repainted on `KisPaintOpPresetUpdateProxy::sigSettingsChanged()`).
- It follows `KisCanvasResourceProvider::sigPaintOpPresetChanged()`
  (`KisToolPaint::createBrushOptionsSection()` passes the provider) and
  collapses with the section. Height: a third of the width, 36-110 px.
- Test: `KisToolOptionsBrushTest::testPresetPreview`.

### Mirror rules

- The copy writes through the editor control, in the way the control's
  connection to its option listens: `setValue()` for sliders, `click()` for
  check boxes and radio buttons, `setSpacing()` plus
  `sigSpacingChanged()` (setSpacing blocks it), `setCompositeOp()` plus
  the list's `clicked()`. A slider copy also copies the range, soft range,
  decimals, exponent, prefix and suffix on every read.
- It reads the control again after each `sigSettingChanged()`,
  `sigCheckedChanged()` and `sigEnabledChanged()` of the option, and after
  show, hide and enable changes of the control, its mode widget and their
  parents, through a queued call: the option model updates some controls
  with blocked signals, and possibly after the option's signal.
- Enabled state: as in the editor, including the page (a Texture page is
  disabled while Pattern is unchecked); radio buttons one by one (only Wash
  while a masked brush is enabled).
- Visibility: only a parameter registered with a mode widget is hidden when
  that widget is not shown in its page (the Auto tip parameters while the
  tip is predefined, and the reverse). Tabs of a page (Texture's Options and
  Pattern tabs) do not hide parameters.

### Pitfall found while testing

The id assignment of 3a matched the first known variable name in each
`addPaintOpOption()` line; the Painting Mode line also mentions
`maskingOption` and got the masked brush's id. `testShowableOptions` now
checks that the ids are unique.

### Not covered yet

- The Path and Pencil (bezier/freehand path) tools have no Brush section.
- Masked brush page parameters, and parameters of other pages.

## Manual checks (3b)

1. Pixel Brush F5: eyes in front of Diameter, Ratio, Angle, Density and
   Spacing (Auto tip), Size, Angle and Spacing (Predefined tip),
   Precision and Auto, Blending Mode ("Selected:"), Painting Mode and
   Texture Scale. Color Smudge and other engines show none.
2. With eyes on, the Brush section shows those controls; dragging a slider
   there changes the brush and F5 at once, and changing them in F5, the
   toolbar or with Shift + drag updates Tool Options.
3. Switching between an auto and a predefined tip shows that tip's
   parameters only.
4. Painting Mode is disabled (only Wash) while a masked brush is enabled;
   Texture Scale is disabled while Pattern is unchecked.
5. Rectangle, Ellipse, Polygon and Polyline tools show the Brush section at
   the end of their options.
6. Eyes survive a restart.
7. Opacity and Flow have eyes on their strength bar and on Enable Pen
   Settings; both work from Tool Options (2026-10-08, user request).
8. The section starts with the current brush's stroke preview and name;
   switching brushes updates it, a modified brush shows "*", and it
   collapses with the section.

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
