# At least 1 pixel: development notes

User guide: [At least 1 pixel](../brush-minimum-one-pixel.md).

The Freehand Brush tool option **At least 1 pixel** keeps every dab of a
brush stroke at least 1 px large. Pen pressure, other sensors and tapered
stroke starts and ends can make the computed dab smaller than a pixel, so a
thin line fades out or breaks up; with the option on, those dabs are drawn at
1 px instead. The idea comes from Clip Studio Paint's brush size setting of
the same name.

## User decisions (2026-11-11)

- A check box in the Freehand Brush tool's Tool Options, near **Snap to
  Assistants**, on by default for new and existing profiles.
- Off keeps the previous behavior exactly.
- Japanese label: 最低限1pxを描画する.

## Design

### Where the setting lives

It is a tool setting kept in kritarc, like the smoothing options, not a
preset setting: it applies to whatever brush is selected and does not mark
presets as modified or change `.kpp` files.

| Item | Value |
| --- | --- |
| kritarc key | `Solstice/BrushMinimumOnePixel` (bool, default `true`; `KisMinimumDabSize::configKey()`, `defaultEnabled()`) |
| Paint op settings property | `Solstice/MinimumOnePixelDab` (bool; `KisMinimumDabSize::settingsKey()`); absent means off |

The value reaches the paint ops through the stroke's preset:
`KisResourcesSnapshot` clones the current preset for every stroke, and when
it clones the canvas's current preset (no `presetOverride`) it writes the
kritarc value into the clone. Paint ops read it in their constructors. So:

- the option is fixed for a whole stroke, and the stroke's LOD (Instant
  Preview) clone and its final pass see the same value;
- worker threads never read kritarc for it;
- the user's preset object is not touched (no dirty mark, nothing saved);
- every tool that paints through `KisResourcesSnapshot` follows it: Freehand
  Brush, Dynamic Brush, Multibrush, Line, the shape tools, the
  Through-Layer Eraser and so on. The check box is shown by `KisToolBrush`
  (Freehand Brush and Multibrush); the other tools have no control for it.

The snapshot writes the property with the base-class setter
`KisPropertiesConfiguration::setProperty()`. `KisPaintOpSettings::setProperty()`
would call `onPropertyChanged()`, which clears the clone's cached brush
(`KisBrushBasedPaintOpSettings::m_savedBrush`) and its resource cache
interface, so every stroke would rebuild its brush tip (expensive for text,
pipe and large predefined tips).

### Presets passed in (`presetOverride`)

A snapshot created with `presetOverride` keeps whatever the preset carries:

- Brush Stroke Layer records clone the stroke's preset
  (`FreehandStrokeStrategy`, `freehand_stroke.cpp`), so the property is
  recorded with each stroke and serialized with the record's settings.
  Replay and scaling redraw the stroke with the value it was drawn with.
  Records made before this feature have no property and replay unchanged.
- `KisBrushStrokePreviewRenderer` (Brush Presets, Tool Options preview,
  Quick Access) renders saved presets without the property: stroke previews
  do not depend on the tool option, and the preview cache needs no new key.

### The clamp

`KisMinimumDabSize::apply(scale, tipSize)` returns
`max(scale, 1 / tipSize)`, where `scale` is the size option's result and
`tipSize` the tip's size in pixels at scale 1:

- brush-based engines use `KisBrush::userEffectiveSize()` (the auto tip's
  diameter; the larger side of an image tip times its scale), through
  `KisBrushBasedPaintOp::applyMinimumDabSize()`;
- Quick Brush uses its diameter (`KisRoundMarkerOp::lod0Scale()`).

Rules:

- The clamp is applied to the level-of-detail 0 scale, before
  `KisLodTransform::lodToScale()`. An Instant Preview stroke at LOD 1 draws
  0.5 px dabs, the final 1 px line scaled down, not a 1 px line that would
  be 2 px wide at full size (`testMatchesOnePixelBrush`).
- It is applied in `paintAt()` and in `updateSpacingImpl()` alike, so the
  spacing of the raised dabs is that of a 1 px dab.
- A dab of size 0 (pressure 0 with a size curve through 0) is drawn at 1 px
  too, as in Clip Studio Paint. With the option off, such dabs are still
  skipped by `checkSizeTooSmall()`.
- The size is the tip's larger dimension. The Ratio option and the tip's
  own ratio are not raised: a flat tip's thin side can stay below 1 px.
- A tip whose own size is below 1 px is drawn at 1 px.
- Only the main dab is raised. A masking brush's preset is created by
  `KisPaintOpPreset::createMaskingPreset()` without the property.

### Engines

| Engine | Covered | Where |
| --- | --- | --- |
| Pixel Brush (CPU and GPU dabs) | yes | `KisBrushOp::paintAt()`, `updateSpacingImpl()`. The GPU brush renders the dab requests that `paintAt()` creates, so it needs no separate code |
| Color Smudge | yes | `KisColorSmudgeOp::paintAt()`, `updateSpacingImpl()` |
| Filter | yes | `KisFilterOp::paintAt()`, `updateSpacingImpl()` |
| Tangent Normal | yes | `KisTangentNormalPaintOp::paintAt()`, `updateSpacingImpl()` |
| Hatching | yes | `KisHatchingPaintOp::paintAt()`, `updateSpacingImpl()` |
| Clone | yes | `KisDuplicateOp::paintAt()`, `updateSpacingImpl()` (no LOD support) |
| Quick Brush | yes | `KisRoundMarkerOp::lod0Scale()` |
| Sketch, Bristle, Deform, Spray, Curve, Grid, Particle, Shape, MyPaint | no | They compute sizes their own way (lines, bristles, particles, libmypaint radius); not requested |

## Source map

| Source | Responsibility |
| --- | --- |
| `libs/image/brushengine/KisMinimumDabSize.h` | Keys, default, `apply()` (header only) |
| `libs/ui/tool/kis_resources_snapshot.cpp` | Writes the property into the stroke's preset clone |
| `plugins/paintops/libpaintop/kis_brush_based_paintop.{h,cpp}` | Reads the property (`m_minimumOnePixelDab`); `applyMinimumDabSize()` |
| `plugins/paintops/defaultpaintops/brush/kis_brushop.cpp`, `duplicate/kis_duplicateop.cpp`, `plugins/paintops/colorsmudge/kis_colorsmudgeop.cpp`, `filterop/kis_filterop.cpp`, `tangentnormal/kis_tangent_normal_paintop.cpp`, `hatching/kis_hatching_paintop.cpp` | Clamp calls |
| `plugins/paintops/roundmarker/kis_roundmarkerop.{h,cpp}` | Quick Brush clamp |
| `plugins/tools/basictools/kis_tool_brush.{h,cc}` | Check box `chkMinimumOnePixel` (after the assistant options), `slotSetMinimumOnePixel()` writes kritarc; `activate()` re-reads it |
| `plugins/paintops/defaultpaintops/brush/tests/KisBrushMinimumOnePixelTest.cpp` | Tests |

`KisBrushBasedPaintOp` gained a data member, so every module that derives
from it (default paint ops, Color Smudge, Filter, Tangent Normal, Hatching)
must be rebuilt and installed together with `kritalibpaintop`.

## Build, test and install

```bat
cmake --build <krita-dev-root>\_build --target kritaui kritalibpaintop kritadefaultpaintops kritacolorsmudgepaintop kritafilterop kritatangentnormalpaintop kritaroundmarkerpaintop kritahatchingpaintop kritadefaulttools KisBrushMinimumOnePixelTest
ctest --test-dir <krita-dev-root>\_build -R KisBrushMinimumOnePixelTest --output-on-failure
```

Install from `libs\ui`, `plugins\paintops\libpaintop`,
`plugins\paintops\defaultpaintops`, `colorsmudge`, `filterop`,
`tangentnormal`, `roundmarker`, `hatching` and `plugins\tools\basictools`
(`cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <dir>\cmake_install.cmake`).

`KisBrushMinimumOnePixelTest`:

- `testApply`: the clamp arithmetic.
- `testLowPressureStroke`: a 10 px Pixel Brush line at pressure 0 and 0.02
  (0 and 0.2 px dabs). On, every column holds as much ink as the line of a
  1 px brush at full pressure; off, pressure 0 paints nothing and 0.02 paints
  at most 0.055 per column (about a tenth); without the property the result
  equals off byte for byte.
- `testMatchesOnePixelBrush`: at LOD 0, 1 and 2, a tip raised from pressure
  0 draws exactly the same pixels as a 1 px tip at full pressure (also a 4 px
  tip against pressure 0.25).
- `testSnapshotCarriesToolOption`: the snapshot copies the kritarc value
  into the stroke's clone only, leaves the user's preset clean, leaves a
  `presetOverride` preset alone, and the default is on.

## Manual checks

1. Freehand Brush Tool Options: **At least 1 pixel** appears below **Snap to
   Assistants** (and its options when shown), checked in a new profile.
2. With a pressure-size brush (for example an inking preset), quick strokes
   with light starts and ends: on, the ends taper to a 1 px line instead of
   fading or breaking into dots; off, they look as before.
3. The same with Instant Preview on and a large zoom-out: the preview while
   drawing and the final line match.
4. Toggle the box, restart: the state is kept. Toggling in one window is
   picked up by another window's Freehand Brush when the tool is activated
   again.
5. Line Tool and the shape tools follow the option set in the Freehand
   Brush.
6. A Brush Stroke Layer stroke drawn with the option on keeps its 1 px ends
   after the layer is scaled, also after the option is turned off.
7. GPU brush (RGBA float document): light-pressure ends match the CPU result.

## Verification status

- 2026-11-11 (Windows, `_build`/`_install`): built and installed;
  `KisBrushMinimumOnePixelTest` passes, and so do `KisDabRenderingQueueTest`,
  `KisToolOptionsBrushTest`, `KisBrushTipOptionParityTest`,
  `KisImageTipDescriptionTest`, `KisCloneParityTest`,
  `KisBrushStrokeReplayTest`, `KisBrushStrokeLayerTest`,
  `KisBrushStrokeLayerTransformTest`, `KisThroughLayerEraserTest`,
  `KisGpuBrushJobsTest`, `KisAbrPresetTest`, the Filter, Hatching,
  Color Smudge, Quick Brush and Tangent Normal parity tests,
  `KisColorsmudgeOpTest`, `KisBrushStrokePreviewTest`,
  `MoveSelectionStrokeTest` and `kis_paintop_test`.
- Before installation, the tests that load installed paint op plugins
  (Brush Stroke Layer, ABR presets) crashed in `KisDabCacheBase`: old plugins
  built against the previous `KisBrushBasedPaintOp` layout ran with the new
  `kritalibpaintop`. Install every engine module listed above together.
- Not checked interactively with a tablet yet (manual checks 1-7).
