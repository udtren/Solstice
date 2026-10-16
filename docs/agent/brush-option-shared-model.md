# Brush option shared model: maintenance reference

This is the current technical owner for brush option state, preset
synchronization and serialization compatibility. The user workflow is in
[Brush Editor](../brush-editor.md); control registration, mirrors and visibility
belong to [Brush options in Tool Options](tool-options-brush.md).

All 16 brush engines use the shared model. This is maintenance work, not an
unfinished migration. The [development history](wiki/history/brush-option-shared-model-development.md)
and [phase 0 investigation](wiki/history/brush-option-shared-model-phase0.md)
preserve the original proposals and dated results. Their provisional APIs,
old next steps and removed UI checks do not define current behavior.

## Source map and ownership

| Responsibility | Source |
| --- | --- |
| State ownership, attachment, changed-key routing and dependencies | [KisPaintOpOptionsModel.h](../../libs/ui/KisPaintOpOptionsModel.h), [implementation](../../libs/ui/KisPaintOpOptionsModel.cpp) |
| Preset key tracking and notification ordering | [kis_paintop_settings.cpp](../../libs/image/brushengine/kis_paintop_settings.cpp), [KisPaintOpPresetUpdateProxy.cpp](../../libs/image/brushengine/KisPaintOpPresetUpdateProxy.cpp), [kis_paintop_preset.cpp](../../libs/image/brushengine/kis_paintop_preset.cpp) |
| Editor integration, configuration and locks | [kis_paintop_settings_widget.cpp](../../libs/ui/kis_paintop_settings_widget.cpp), [kis_paintop_box.cc](../../libs/ui/kis_paintop_box.cc) |
| Generic curve states and baking helpers | [KisPaintOpOptionStateUtils.h](../../plugins/paintops/libpaintop/KisPaintOpOptionStateUtils.h), [KisStandardOptionData.cpp](../../plugins/paintops/libpaintop/KisStandardOptionData.cpp) |
| Brush tip, precision and masking data | [KisBrushTipOptionData.h](../../plugins/paintops/libpaintop/KisBrushTipOptionData.h), [implementation](../../plugins/paintops/libpaintop/KisBrushTipOptionData.cpp) |
| Resource-aware brush/masking states and texture/filter/painting-mode baking | [KisBrushBasedOptionStates.cpp](../../plugins/paintops/libpaintop/KisBrushBasedOptionStates.cpp) |
| Shared JSON preservation | [MyPaintPaintOpSettingsWidget.cpp](../../plugins/paintops/mypaint/MyPaintPaintOpSettingsWidget.cpp), [MyPaintPaintOpSettings.cpp](../../plugins/paintops/mypaint/MyPaintPaintOpSettings.cpp) |
| Test resource setup and parity utilities | [KisBrushTestMain.h](../../plugins/paintops/defaultpaintops/brush/tests/KisBrushTestMain.h), [KisPaintOpParityTestUtils.h](../../plugins/paintops/defaultpaintops/brush/tests/KisPaintOpParityTestUtils.h) |

An engine settings widget holds a model whose option states outlive individual
page interactions. The model owns registered states; IDs must be unique.
Typed states use Lager cursors/readers with `automatic_tag`. They hold raw
option values; their write functions bake derived values for serialization.
Do not introduce a second editable copy in Tool Options.

`KisPaintopBox` caches settings widgets by engine. When switching engines it
detaches the old model, supplies the current resources/canvas/image/node and
attaches the selected preset to the new model. Attachment disconnects the
previous preset, reads all options and marks the next option write for a full
rewrite. Model lifetime and preset connections must follow the settings widget,
not a transient Tool Options mirror.

The original transactional-state/explicit-commit proposal was not adopted.
Reads and writes are guarded by `isReading` and `isWriting`; an option watch
must not recursively write during model reads or the model's own notifications.
Reading through a locked-properties proxy can itself change settings, so
"read without model write-back" does not mean the proxy is side-effect free.

## Serialization and notification contract

Presets keep their existing keys, values and file format. Brush engines still
consume `KisPaintOpSettings`; this is not a new preset format.

1. **First option edit after attachment or full settings replacement:** reset
   settings and write every option. This retains legacy normalization behavior:
   read-only legacy `Custom<id>`/`Curve<id>` keys and obsolete engine keys
   disappear. Attachment alone does not perform this rewrite.
2. **Subsequent option edits:** write the changed option and its transitive
   dependents. Determine owned keys from serialization, remove previously owned
   keys that the option no longer writes, and copy its new values through
   `KisLockedPropertiesProxy`. Do not clear unrelated keys or sweep key prefixes.
3. **External changes:** receive `sigSettingsKeysChanged(keys, allKeys)`;
   read owners of known keys and their transitive dependents. Unknown keys
   require a full read; `allKeys` also schedules a full rewrite on the next edit.
   Refresh key ownership after reading.

Dependencies are required in **both** directions of synchronization: an F5
edit must write dependent options, and a toolbar/canvas change must refresh
dependent raw state before the next F5 edit. The latter prevents a brush size
from reverting after a canvas resize followed by editing Rotation.

`resetSettings()` preserves `paintop` and the supplied preserved-key list.
`KisPaintopBox` supplies `lodUserAllowed` and `lodSizeThreshold`.
MyPaint additionally preserves its JSON document in its settings override.
Do not assume arbitrary runtime keys survive a full reset.

`setProperty()` records changed keys only for an actual value change.
Setting an equal value retains the ordinary property-change notification path
without adding a changed key or newly marking the preset dirty.
`KisPaintOpSettings::removeProperty()` records deletion but does not by itself
notify or dirty the preset; it is not a virtual override of the base method.
Removal through a base configuration pointer can bypass this tracking, as
used for transient lock bookkeeping. Keep these distinctions when changing
mutation paths.

The synchronous delivery order is early warning, changed keys, then
uncompressed change notification; the ordinary settings-changed notification
is compressed (100 ms). `UpdatedPostponer` batches changes and delivers their
accumulated key set on release. The model keeps its write guard active through
postponer destruction, then emits `sigPresetSettingsWritten()`.

## Editor, lock and UI boundaries

For an attached model, `slotGuiChangedCurrentPreset()` handles the editor's
own LOD settings without clearing and rewriting all option settings.
The warning/fallback path remains for a missing or unattached model.
Likewise, `writeConfiguration()` leaves the attached settings alone and uses
`writeAll()` for a different target; `setConfiguration()` does not reread the
attached settings unnecessarily. Legacy widget loops remain as safety paths
and for comparison fixtures, not as evidence of an unfinished engine migration.

Reads and writes retain locked-properties proxy behavior. Partial option
writes must not overwrite unrelated `*_previous` or `Saved*` bookkeeping.
Those names are not a blanket preservation list in the model: safety depends
on key ownership and the lock proxy, with the explicit full rewrite described
above. `lockProperties()` serializes the model's baked option selected by its
Tool Options ID, with a widget fallback when the option cannot be found.

Two deliberate user-visible differences from the old editor are preserved:

- Discarding a lock restores the preset's value from before the lock, rather
  than a locked value overwritten by an unrelated editor edit.
- LOD-only changes do not mark the brush preset modified.

Tool Options mirrors write through the existing F5 controls, which update the
shared model. There is no independent per-option widget factory. Stable,
untranslated option IDs also identify per-engine visibility preferences;
renaming IDs loses those selections. Mirror ownership, eye placement, parameter
availability and supported tools are defined in
[the Tool Options reference](tool-options-brush.md).

Uniform Properties still use direct preset setters and the changed-key path.
The [On-Canvas Brush Editor was removed](popup-palette-removal.md); references
to its HUD in the archived plan are not present-day test requirements.

## Derived values and engine-specific invariants

### Pixel Brush

Register/read Brush Tip before Masking Brush. The declared dependency edges
are source to dependent:

- Brush Tip → Masking Brush.
- Brush Tip → Lightness Strength.
- Masking Brush → Painting Mode.

Painting Mode serializes Wash when masking is enabled. Lightness Strength
serializes as enabled only for a lightness tip. Brush tip data combine brush,
precision and common size; resource resolution and effective size must use the
same helpers as the editor.

Masking data retain the loaded master-size coefficient until relevant
masking/master/common-size values change. Start preservation after the UI has
settled rounded values (for example 5.96591 to 5.97). Ending preservation alone
must not emit a user edit and re-enter brush-size updates during Shift-drag.
External size changes must also reread masking state; cover this with
`KisBrushTipOptionParityTest::testEditAfterResizeKeepsSize`.

Texture serialization includes resource data through the shared baking helper.
Curve states retain linked enable/range behavior. Keep model serialization
equal to the widget's effective output, not just its raw stored fields.

### Color Smudge

The four dependencies are:

- Brush Tip → Smudge Length.
- Smudge Length → Smudge Radius.
- Brush Tip → Paint Thickness.
- Brush Tip → Overlay Mode.

A non-alpha tip forces the new smudge engine when baking Smudge Length.
Smudge Radius uses the appropriate range (0–1 for the new engine, 0–3 for
the old one). Paint Thickness is enabled only with lightness tips; Overlay
Mode is disabled with them. Raw state retains the user's values while the
baked output reflects these conditions. Do not flatten those conditions into
permanent edits to the raw state.

### Spray, MyPaint and Filter

Spray's shape controls use shared cursors for Spray Area diameter/scale.
This is a shared-state relationship, not a missing serialized dependency edge.

MyPaint registers `MyPaint/json` with `addSharedKey()`. Each partial write
starts from the preset's current JSON and patches its own setting, preserving
other settings in that document. Full rewrites rely on MyPaint's
`resetSettings()` override preserving the JSON too. The Basic controls alias
the corresponding setting cursors (radius, hardness and opacity); they must
not become independent states.

Filter serialization uses the shared baking function, including the default
configuration when a filter has no configuration yet. Resource-dependent
baking must work in test/export paths as well as an open editor.

## Engine coverage and regression tests

Each engine settings widget registers its model states and Tool Options IDs.
These test targets own serialization parity; paths are relative to
`plugins/paintops/`.

| Engine | Test directory | Test target |
| --- | --- | --- |
| Deform | `deform/tests/` | `KisPaintOpOptionsModelTest` |
| Pixel Brush | `defaultpaintops/brush/tests/` | `KisBrushTipOptionParityTest` |
| Color Smudge | `colorsmudge/tests/` | `KisColorSmudgeParityTest` |
| Sketch | `sketch/tests/` | `KisSketchParityTest` |
| Bristle | `hairy/tests/` | `KisHairyParityTest` |
| Tangent Normal | `tangentnormal/tests/` | `KisTangentNormalParityTest` |
| Hatching | `hatching/tests/` | `KisHatchingParityTest` |
| Filter | `filterop/tests/` | `KisFilterOpParityTest` |
| Quick Brush | `roundmarker/tests/` | `KisRoundMarkerParityTest` |
| Curve | `curvebrush/tests/` | `KisCurveParityTest` |
| Grid | `gridbrush/tests/` | `KisGridParityTest` |
| Particle | `particle/tests/` | `KisParticleParityTest` |
| Shape | `experiment/tests/` | `KisExperimentParityTest` |
| Spray | `spray/tests/` | `KisSprayParityTest` |
| MyPaint | `mypaint/tests/` | `KisMyPaintParityTest` |
| Clone | `defaultpaintops/brush/tests/` | `KisCloneParityTest` |

The parity fixtures compare complete key/value output with pre-migration
references and incremental edits with a full model write. Keep dependency
cases that fail if a required edge is removed. Tests also cover attachment,
read/write feedback, external changes, locking and obsolete key removal.
`KisToolOptionsBrushTest` covers the UI bridge; its
`testLockWritesModelOption` is a contract check, not proof that only the model
path can produce that output.

Reference `.properties` files were captured before each engine's migration.
`SOLSTICE_WRITE_REFERENCES=1` regenerates them; never use it on the current
implementation simply to make a failed parity test pass. Review an intentional
baseline change against the compatibility contract. Test setup must load the
required bundled brushes/resources in an isolated database; an empty test
resource database cannot establish preset parity.

### Build and automated checks

Use [Development workflow](development-workflow.md) for the configured
environment, build tree and test DLL/resource paths. Build the affected engine
and its parity test; shared model changes require `kritaui` and all affected
engine tests. Changes to shared option data/baking also require
`kritalibpaintop` and its consumers. For example, from the configured shell:

```bat
cmake --build <krita-dev-root>\_build --target KisPaintOpOptionsModelTest KisBrushTipOptionParityTest KisToolOptionsBrushTest
ctest --test-dir <krita-dev-root>\_build -R "Kis(PaintOpOptionsModel|BrushTipOptionParity|ToolOptionsBrush)Test" --output-on-failure
```

Run the other parity targets from the table for a shared-model change.
When rendering or serialized brush data changes, include the applicable brush,
Color Smudge, MyPaint, dab-queue, preset and GPU stroke tests documented by
their owners; inspect CMake registration for disabled/broken tests rather than
assuming every named test participates in a default CTest run.

Install changed shared libraries and affected plugins before interactive
checks. Restart Solstice fully; do not terminate a running instance without
the user's approval. A documentation extraction does not require those native
build/install steps.

### Manual regression checks

1. Switch among all engines and between presets; use Q and pen/eraser switches.
   Confirm F5 values, name and thumbnail follow the selected preset.
2. Edit in F5 and Tool Options, then with toolbar size/opacity and Shift-drag.
   Confirm both UIs and the painted stroke follow, without feedback loops.
3. Pixel Brush: exercise auto/predefined/lightness tips, masking size,
   Lightness Strength and forced Wash. Resize on canvas, then change Rotation
   in F5; the new size and masking relationship must remain.
4. Color Smudge: switch alpha/lightness/color tips, old/new engine and radius;
   check derived thickness/overlay behavior without losing raw option values.
5. MyPaint: edit one curve setting and Basic controls, save/reload and verify
   unrelated JSON settings remain. Spray: change shared diameter/scale controls.
   Filter: exercise filter selection and configuration.
6. Lock options, switch presets, edit an unrelated option, then discard or save
   the lock. Discard restores the pre-lock value. Save, reload and clone presets
   and compare effective settings.
7. Change only LOD settings; no modified mark should appear. Switch engines and
   return; preserved settings and Tool Options selections must remain.
8. Follow [Tool Options manual checks](tool-options-brush.md#manual-checks-3b)
   for visibility and parameter mirrors. For floating Tool Options, also use
   the ownership/lifecycle checks in [Quick Access](quick-access.md).

## Verification record and maintenance scope

The archived development record reports manual acceptance of Deform, Pixel
Brush, Color Smudge, Sketch, Bristle, Tangent Normal, Hatching, Filter and Quick
Brush on 2026-10-08; Curve, Grid, Particle, Shape, Spray, MyPaint, Clone and
the normal-path cleanup on 2026-10-09. The 2026-10-11 external-resize
correction has a regression test; the dated entry in
[the wiki log](wiki/log.md#2026-10-11-ingest-masking-coefficient-after-a-canvas-resize)
also records the user's manual acceptance.

This reference was extracted against commit
`8ccba6bf312807510fe4596d42a334f2fdf52545` on 2026-10-16. Source inspection and
documentation checks establish this extraction, not a new native test run or
interactive acceptance. Keep future behavior changes and their checks in this
reference and the user guide; preserve dated development evidence in history.
