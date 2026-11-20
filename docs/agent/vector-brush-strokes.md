# Brush strokes on vector layers - development notes

Added 2026-11-18. The brush tools paint on a `KisShapeLayer`; each stroke
becomes a `KisBrushStrokeShape` that holds the recorded stroke and draws it
again with the brush when its transform changes. User guide:
[Brush strokes on vector layers](../vector-brush-strokes.md). Recording and
replay are those of [Brush Stroke Layer](brush-stroke-layer.md); read that
document first.

## Source ownership

| Source | Responsibility |
| --- | --- |
| [KisBrushStrokeShape.h](../../libs/ui/flake/KisBrushStrokeShape.h), [KisBrushStrokeShape.cpp](../../libs/ui/flake/KisBrushStrokeShape.cpp) | The shape, its pictures, hit outline, SVG form, factory and the saving/loading scopes |
| [kis_shape_layer.h](../../libs/ui/flake/kis_shape_layer.h), [kis_shape_layer.cc](../../libs/ui/flake/kis_shape_layer.cc) | Stroke preview (temporary target), `createAddShapeCommand()`, `finishBrushStrokePreview()`, the `brushstrokes` file |
| [kis_painter_based_stroke_strategy.cpp](../../libs/ui/tool/strokes/kis_painter_based_stroke_strategy.cpp) | Temporary target for a vector layer, forced Build-up handling, the finishing barrier job |
| [freehand_stroke.cpp](../../libs/ui/tool/strokes/freehand_stroke.cpp) | Recording on a vector layer, `createVectorShapeCommand()`, no Instant Preview clone |
| [kis_tool_freehand.cc](../../libs/ui/tool/kis_tool_freehand.cc) | The tool gate (`setPaintsOnVectorLayers()`), eraser refusal, the vector eraser's drag (`eraseVector()`, virtual `vectorEraserTransformations()`); [kis_tool_multihand.cpp](../../plugins/tools/basictools/kis_tool_multihand.cpp) overrides the latter |
| [KisBrushStrokeLayerReplay.cpp](../../libs/ui/brushstrokelayer/KisBrushStrokeLayerReplay.cpp) | `renderStrokes()`, which the shape calls |
| [kis_shape_layer_canvas.cpp](../../libs/ui/flake/kis_shape_layer_canvas.cpp), [inplace_transform_stroke_strategy.cpp](../../plugins/tools/tool_transform2/strokes/inplace_transform_stroke_strategy.cpp) | Transform previews: the preview paint scope in `repaint()`, `setBrushStrokePreview()`, and its use by the Transform Tool's in-place stroke |
| [KisBrushStrokeShapeEraser.h](../../libs/ui/flake/KisBrushStrokeShapeEraser.h), [KisBrushStrokeShapeEraser.cpp](../../libs/ui/flake/KisBrushStrokeShapeEraser.cpp) | The vector eraser: cutting a recorded stroke, replacing shapes during a drag, its undo command, the mode setting |
| [kis_tool_brush.cc](../../plugins/tools/basictools/kis_tool_brush.cc) | The **Vector Eraser** combo box (`cmbVectorEraserMode`); object names for the assistant check boxes (`chkBrushSnapToAssistants`, `chkBrushSnapSingleAssistant`) so that test control can set them |
| [KisBrushStrokePathEdit.h](../../libs/ui/flake/KisBrushStrokePathEdit.h), [KisBrushStrokePathEdit.cpp](../../libs/ui/flake/KisBrushStrokePathEdit.cpp) | Path editing functions (nearest place, move, width, split) and `KisBrushStrokeShapeEdit`, the live edit of one shape with its undo command |
| [KisToolEditBrushStrokes.h](../../plugins/tools/basictools/KisToolEditBrushStrokes.h), [KisToolEditBrushStrokes.cpp](../../plugins/tools/basictools/KisToolEditBrushStrokes.cpp) | The Edit Brush Strokes Tool: picking, the path overlay, the three modes, Tool Options |

The tools that turn it on are `KisToolBrush` (with the Multibrush, which
derives from it) and `KisToolDyna`. `KisToolThroughLayerEraser` turns it
off. Other `KisToolFreehand` subclasses keep refusing vector layers.

## Painting a stroke

1. `KisToolFreehand::beginPrimaryAction()` lets a stroke start on a
   `KisShapeLayer` when the tool paints on vector layers. With the Erase
   blend mode in effect it starts the [vector eraser](#vector-eraser)
   instead of a stroke.
2. `KisShapeLayer` is a `KisIndirectPaintingSupport`. The stroke always
   paints on a temporary target (`original()->createCompositionSourceDevice()`),
   and `copyOriginalToProjection()` composites it over the shapes, as a paint
   layer does for a Wash stroke. A Build-up preset uses the Through-Layer
   Eraser's path: paint ops are created first, the target is attached
   afterwards and merged opaque
   ([pitfall](wiki/pitfalls/paint-ops-read-temporary-target.md)). This keeps
   the live stroke equal to the replay, which paints on a paint layer.
3. No selection, interstroke data or Instant Preview clone is used.
   `KisAutoKey` is skipped for a node without a paint device.
4. `FreehandStrokeStrategy` records the stroke as on a Brush Stroke Layer
   (sequential dabs, seed, starts; device offset 0). At the end
   `createVectorShapeCommand()` merges the target with the stroke's opacity
   and blend mode into an RGBA8 picture and builds the shape with it.
5. `KisPainterBasedStrokeStrategy::finishStrokeCallback()` runs one barrier
   job: redo the command (adds the shape in the GUI thread), then
   `finishBrushStrokePreview(true)`, which paints the target into the
   layer's own device and drops it. The layer repaints its shapes later
   (spontaneous jobs wait for open strokes); until then the device already
   holds the stroke's pixels, so nothing flickers. Without a shape (nothing
   painted, a job type that is not recorded) the preview is dropped.

Shapes change only in the GUI thread. `createAddShapeCommand()` wraps
`KoShapeCreateCommand` in a command that runs `redo()`/`undo()` through
`KisSafeBlockingQueueConnectionProxy`, as `TransformShapeLayerDeferred`
does.

## The shape

- `recordRect()` is the stroke's pixel bounds when painted. The shape's
  rectangle maps to it (`recordToShape()` uses the current `size()`), and
  its transformation places it in the document.
- `paint()` takes the painter's transform, so layer transforms, image
  resolution and the 256-pixel patches are all covered. A picture is reused
  when its transform differs from the painter's by a move only; the move is
  rounded to whole pixels, so moving never redraws. Otherwise the stroke is
  drawn again with `KisBrushStrokeLayer::renderStrokes()` in RGBA8 for the
  full transform (positions mapped, brush size by `sqrt(|det|)`).
- Only a painter on a `QImage` (the layer canvas) triggers a redraw; other
  painters get the latest picture scaled.
- `Data` (stroke, outline, up to four pictures, latest first) is shared by
  a shape and its clones and guarded by a mutex. The layer paints clones in
  a worker thread; copies of a shape at other sizes find their own picture.
- A saved picture is placed against the layer's transform with the same
  rounding as `paint()` uses, for the drawn picture and for the saved one
  separately. Rounding their difference instead moved a picture whose
  rectangle starts on a half pixel by one pixel after loading; `createPiece()`
  also puts a piece's rectangle on whole pixels.
- `outline()` is the path of the recorded jobs, as wide as the brush (at
  least 6 pixels); the rectangle for canvas-mirrored strokes.
  `boundingRect()` and the paint clip add a margin of half a brush.
- Rotation maps positions only. Do not promise tip-angle fidelity.

`renderStrokes()` hands its temporary `KisImage` to the GUI thread for
destruction when called from another thread, giving up its own references
first
([pitfall](wiki/pitfalls/temporary-image-destroyed-off-gui-thread.md)).

## Vector eraser

Added 2026-11-18. In eraser mode on a vector layer the brush tools do not
start a stroke. `KisToolFreehand` keeps a `KisBrushStrokeShapeEraser` for
the drag (`m_vectorEraser`) and calls `erase(from, to, radius)` for every
move, in image pixels, with half the preset's size as the radius. All of it
runs in the GUI thread, as the vector tools do; the layer repaints the new
shapes in its worker as usual. Because no image stroke is open, those
repaints are not held back and the cut shows while dragging.

**Cutting (`cutStroke()`).** The eraser's sweep is the capsule around the
segment between two moves (Grease Pencil's eraser tests a circle per input
sample; the capsule leaves no gaps on fast moves). A point of the path is
erased when its distance to the segment is at most the radius plus half the
brush size times the pen pressure there, so that no ink is left inside the
sweep. Each job is sampled along its parameter (step: half the reach, at
most 512 samples) and every inside/outside change is refined by bisection.
The kept ranges become jobs of the same type: a line keeps its ends from
`KisPaintInformation::mix()`, a curve is split with de Casteljau. Jobs that
follow each other without a gap form a part; the jobs of each
`strokeInfoId` (Multibrush copies) form lines of their own. A part shorter
than half a pixel is dropped. Each part is a copy of the record with its
jobs; a part that does not start where the line started resets
`Start::hasLastDab` and `dabSeqNo`, so its dab spacing starts at the cut.
That is why the part after a cut can differ slightly for brushes with wide
spacing, scatter or random tips; the part before it is unchanged. Recording
the spacing state per job would remove most of it (not done).

**Shapes.** `KisBrushStrokeShape::createPiece()` makes the shape of a part:
a copy of the shape (stacking order and other properties) with its own
data, a rectangle around the part's points (half a brush wider, inside the
original's), and a transformation that keeps `expectedTransform()` equal to
the original's. It has no picture; the layer draws it at its next repaint.
Set the transformation before the size: `KoShape::setSize()` on a copy
writes into the data still shared with its source
([pitfall](wiki/pitfalls/koshape-copy-shares-size.md)).

`erase()` handles the layer's own visible, unprotected stroke shapes (not
those in groups). A touched shape is removed and its pieces added; a piece
made earlier in the same drag is deleted when it is cut again, a shape from
before the drag is kept for undo. `finish()` returns one command
(`EraseStrokesCommand`, "Erase Brush Strokes") whose first redo does
nothing; it owns whichever shapes are not in the layer. Dropping the eraser
without `finish()` cancels. The mode (`TouchedPart`, `WholeLine`) is read
from `VectorBrushStrokes/eraserMode` in `kritarc` when a drag starts.

**The Multibrush's eraser (2026-11-20).** `KisToolFreehand::eraseVector()`
calls `erase()` once for every map of
`vectorEraserTransformations()` (image pixels; the identity by default),
read when the drag starts. `KisToolMultihand` returns the list its
`initTransformations()` gave the helper for that drag, so the eraser
sweeps at every copy of the brush with the same radius. Before, it erased
under the pen only. In Translate mode the copies' places are random for
each drag, as for painting.

A stroke with canvas mirror flags is cut along its recorded path only; its
mirrored copies follow, since each piece keeps the flags and axes.
`KisBrushStrokeShape::createPiece()` takes the rectangle of a piece from
its jobs and from their mirror images around `mirrorAxesCenter`: the
copies are painted, though no job holds them. Until 2026-11-20 it took
the jobs only, so the shape of an edited or erased canvas-mirror line was
the drawn side's rectangle and `paint()` clipped the copy at its edge.

**Up to Intersection (`cutStrokeUpToIntersections()`).** The touched
stretches are found as above, then expressed as places along each line (the
job's number within its `strokeInfoId` plus the parameter). The line is
flattened (`flattenLine()`: line jobs as they are, curves in steps of about
two pixels, at most 64) and crossed with (a) the lines of the layer's other
strokes, (b) the stroke's other copies and (c) itself, where segments up to
two apart do not count. Other lines have their two ends moved outwards by
half of both widths plus a pixel, so a line that stops at the stroke counts
as crossing it. Each stretch grows to the nearest crossing on each side, or
to the line's end; crossings inside the stretch are ignored, so touching a
crossing removes the line on both sides of it. The cut is exactly at the
crossing, where the other line's ink covers the new end. The search is brute
force with bounding-box rejection, as in Grease Pencil's trim tool.

For the layer, `otherLines()` gives the eraser the paths of the other
visible strokes near the touched one: flattened in image pixels once per
shape and drag (`m_lines`, dropped when the shape is replaced) and mapped
into the touched stroke's recorded pixels. `erase()` runs the cheap
`cutStroke()` test first and searches crossings only for touched strokes.
Only brush stroke shapes directly on the same layer are considered.

Not built: control point editing, per-point width, changing a stroke's
color or brush.

## Editing the path

Added 2026-11-18. A recorded stroke already is a path with control points:
the end points of its jobs, each a `KisPaintInformation`. Editing changes a
copy of the record and replaces the shape; nothing new is stored.

**Functions (`KisBrushStrokePathEdit`, recorded pixels).** `nearestPoint()`
gives the nearest place on the path as a job and a parameter (curves in 16
steps). `movePoints()` and `scaleWidth()` change the control points of the
hit job's line (`strokeInfoId`) with a weight that is 1 at the anchor and
falls as a cosine to 0 at `reach`, measured along the line; with a reach of
0 only the control point nearest to the anchor changes. The end of one job
and the start of the next get the same weight (they lie at the same
distance), so the path stays connected; a curve's handles move with the end
point they belong to. `scaleWidth()` multiplies the pen pressure and keeps
it between 0.01 and 1. `splitAt()` cuts without removing
(`KisBrushStrokeShapeEraser::splitStroke()`).

**Live edit (`KisBrushStrokeShapeEdit`).** Holds the original shape and
shows shapes of edited strokes in its place: every `setStrokes()` makes new
shapes with `createPiece(stroke, false)` (the rectangle follows the edited
path instead of staying inside the original's), removes the previous ones
and deletes them unless they are the original. `finish()` returns the
replace command (`KisBrushStrokeShapeEraser::createReplaceCommand()`, first
redo does nothing); dropping the object cancels. GUI thread, like the
eraser.

**Tool (`KisToolEditBrushStrokes`, id `KritaShape/KisToolEditBrushStrokes`,
toolbox section Shape, priority 13).** Hovering picks the nearest stroke
shape directly on the active vector layer, within 10 view pixels or within
the line's own width, and shows its path, control points (thinned to one
per 7 view pixels), the picked place and the reach. A drag always edits the
stroke as it was at the press (`originalStroke()`), so the result does not
depend on the mouse path. Change Width doubles the pressure every 80 view
pixels to the right. Cut acts at the press. Mode and Reach are stored under
`KisToolEditBrushStrokes` in `kritarc` (`mode`, `reach`); the widgets are
`cmbEditBrushStrokesMode` and `sliderEditBrushStrokesReach`. Undo texts:
"Move Brush Stroke Points", "Change Brush Stroke Width", "Cut Brush
Stroke". The action is in `plugins/tools/tools.action`, the icon
`krita_tool_edit_brush_strokes` in `krita/pics/tools/SVG/16/`.

Everything after the edited place is redrawn with a different path length
before it, so its dabs can shift (the same effect as after an eraser cut).

**Selection and stroke properties (2026-11-18).** A click without a drag
toggles the line in the layer's shape selection
(`canvas()->selectedShapesProxy()->selection()`, the one the Select Shapes
Tool uses); a click beside the lines clears it. Selected lines are drawn
with an orange path, flattened again only when the list of selected shapes
changes. A line that was selected stays selected after a drag edit (its
replacement shapes are selected).

The buttons `btnEditBrushStrokesApplyColor`, `...ApplySize` and
`...ApplyBrush` call `KisBrushStrokePathEdit::withColor()` (foreground
color), `withBrushSize()` (the current brush size divided by the scale of
the shape's `expectedTransform()`, since the record is in recorded pixels)
and `withBrush()` (a clone of the preset at the stroke's present size) for
every selected line through `KisBrushStrokeShapeEdit::replaceStrokes()`,
which returns one replace command. Undo texts: "Change Brush Stroke Color",
"... Size", "... Brush". The preset for Apply Brush comes from a
`KisResourcesSnapshot` made in the GUI thread, as a stroke's does: it
carries its resources, so the stroke can be drawn in a worker and saved.

**Simplifying (2026-11-18).** `KisBrushStrokePathEdit::simplified(stroke,
tolerance)` returns the stroke with fewer jobs, or the stroke itself if no
job can go. Per line (`strokeInfoId`), connected LINE and CURVE jobs form a
run; POINT jobs and gaps of more than 0.001 px end a run and stay as they
are. A run is sampled every `qBound(0.25, tolerance, 2)` recorded pixels
(at most 256 samples per job; job ends are always samples) and fitted
again with cubic curves after Schneider's algorithm (Graphics Gems, 1990):
least squares with fixed end tangents, up to four Newton
reparameterizations while the error is below twice the tolerance, split at
the worst sample otherwise. The error of a sample is the larger of its
distance to the curve at its parameter and its pressure difference times
half the brush size (of a brush at least 20 px wide), because a CURVE job
changes the pen values evenly from `pi1` to `pi2`
(`KisPaintOpUtils::paintCurve()` mixes at 0.5 while it subdivides). A
control point where the path turns by more than 60 degrees, measured over
`max(2, 2 * tolerance)` pixels on each side, is a corner: the fit never
crosses it (of candidates closer than that stretch, the sharpest). A run
keeps its jobs when the fit has no fewer.

The new jobs are all CURVE jobs. Their end infos are the recorded ones
where a fitted point is a job end (`pi2` of the job ending there, `pi1` of
the next for the outgoing side) and `KisPaintInformation::mix()` inside a
job, so time, tilt and rotation are kept at the remaining points only. The
jobs of mirrored copies are sorted by their place in their own line (job
number plus parameter), which keeps the copies drawn in turns.
`pointCount()` counts the control points of all lines.

In the tool: the slider `sliderEditBrushStrokesSimplify` (0.1 to 20 image
pixels, default 1, `simplifyTolerance` in the tool's config group) and the
button `btnEditBrushStrokesSimplify` call it for every selected line with
the tolerance divided by the scale of the shape's `expectedTransform()`,
through `changeSelectedStrokes()`; undo text "Simplify Brush Strokes". A
floating message gives the control points left, or says that none can be
removed.

Measured in `testSimplify` (the test stroke: 60 lines with a pressure wave
and a curve, 62 jobs): 10 jobs at 1 px (path within 0.28 px), 24 at
0.1 px, 6 at 4 px (within 1.6 px); the drawn ink changed from 1451 to
1440 pixels with 5 pixels differing by more than half.

**Adding and deleting points (2026-11-18).**
`KisBrushStrokePathEdit::insertPoint(stroke, place, &added)` splits the job
at the place in two that follow the same path (de Casteljau for a curve),
with `KisPaintInformation::mix()` for the point's pen values; `added` is
the start of the second job, the anchor for `movePoints()` with a reach of
0. Within 0.001 of a job's end, or on a POINT job, it returns the stroke
itself and the control point that is there. `nearestControlPoint()` gives
the end of the hit job that is nearer along the job (`t` below 0.5: its
start), as the start of the following job or `t` = 1 of the line's last; a
hit on a POINT job (the dab at a line's start, which `nearestPoint()`
returns there because it comes first) means the control point where the
path goes on. `deletePoint()` removes that control point: between two jobs
they become one CURVE job from the first one's `pi1` to the second one's
`pi2`, its handles the least squares fit (`CurveFit` without a tolerance)
to both jobs sampled every pixel; at a line's start the first job goes and
a POINT job lying on the old start takes the new start's values; at its
end the last job goes. A line of one LINE or CURVE job is returned
unchanged. Only the hit line of a mirrored stroke changes.

In the tool, the modes Add Point and Delete Point (combo indices 3 and 4;
`mode` is now read with an upper bound of 4) act at the press, like Cut.
Add Point keeps the stroke with the new point (`m_strokeWithPoint`) and a
drag moves that point alone from it; Delete Point shows "A line keeps at
least two control points." when nothing can go. The mode is read once at
the press (`m_pressMode`) and used until the release. A click that changes
nothing toggles the selection only in Move Points and Change Width. While
hovering in Delete Point the marker sits on the control point a click
would remove; the reach circle shows only in the two modes that use it.
Undo texts: "Add Brush Stroke Point", "Delete Brush Stroke Point".

**Switching corners (2026-11-18).** Nothing is stored for a control point:
whether it is a corner follows from the two jobs that meet in it.
`KisBrushStrokePathEdit::isCorner(stroke, place)` takes the control point
nearest to the place (as Delete Point does) and calls it a corner if
neither job has a handle there (two LINE jobs, or CURVE jobs whose handle
at that end lies within 0.001 px of the point), or if the path turns there
by more than 10 degrees (`SmoothCosine`; the direction of a job at an end
comes from its handle there, else from its other handle, else from its
chord). A line's ends are neither. `switchCorner()` returns the changed
stroke:

- corner to curve: both jobs get a handle at the point along the
  direction from the control point before to the one after (across the
  way there, if the line comes back to where it was), each a third of the
  distance to that neighbour. A LINE job becomes a CURVE job whose other
  handle lies a third along its chord, so it leaves its other end as the
  line did.
- curve to corner: the handles at the point are set onto it. A job whose
  two handles then lie on its chord becomes a LINE job, so a line that was
  made a curve comes back exactly.

The pen's values and the number of jobs stay; only the hit line of a
mirrored stroke changes. The stroke itself is returned for a line's ends,
for unconnected jobs and when the three points coincide. Switching twice
is not the identity for a curve point that had other handles (a fitted
curve of Simplify Lines): it gets the standard handles.
`controlPoints(stroke, jobIndex, &corners)` fills a flag for each point.

In the tool, the mode Switch Corner (combo index 5; `mode` is read with
the upper bound `SwitchCorner`) acts at the press, like Delete Point, with
the marker on the control point a click would switch while hovering.
`showPath()` asks for the corner flags only in this mode (`m_pointCorners`)
and `paint()` draws those points as squares; in the other modes all points
stay circles, since on a freshly drawn line every inner point is a corner.
A click on a line's end shows "Only control points between a line's ends
can be switched." Undo text: "Switch Brush Stroke Corner".

**Density (2026-11-18).** `KisPaintInformation::densityFactor()` (new, in
`libs/image/brushengine/kis_paint_information.{h,cc}`; 1 by default, kept
between 0 and 1, copied and mixed like the pressure) is how dense the line
is at a control point. No paint op reads it. `KisBrushStrokePathEdit::
scaleDensity(stroke, anchor, factor, reach)` multiplies it around the
anchor with the falloff of `movePoints()` and keeps it between 0.01 and 1.
Because it travels in the pen's values, the eraser's cuts, added points,
Simplify Lines and Join Lines carry it through `KisPaintInformation::mix()`
without code of their own.

`KisBrushStrokeShape::Data::render()` applies it to the finished picture
(`applyDensity()`), not to the dabs: a factor for each dab's opacity adds
up again where dabs overlap, so a Build-up ink pen with close spacing
would hardly change (that was tried first and dropped). The path is
flattened in the picture's pixels (LINE jobs in pieces of at most the
radius, CURVE jobs in at least four; the density changes evenly with the
job's parameter, as `paintCurve()` mixes it; canvas-mirror copies are
added by mirroring the points). Every pixel within one brush size plus 2
px of the path takes the density of the nearest place on it, and the
picture's premultiplied channels are multiplied by it. Exception: where
two places more than `4 * halfBrush + 4` px apart along the path (a line
crossing itself; another copy's line counts as far) both lie within half
a brush of the pixel, the denser one wins, so a light part crossing a
dense part takes no ink out of it. Only the area around segments with a
density below 1 is worked on; a stroke without one costs one loop over
its jobs. The first picture of a stroke comes from the live drawing and
never has a density.

In the tool, the mode Change Density (combo index 6; `mode` is read with
the upper bound `ChangeDensity`) is a drag like Change Width: the factor
doubles every 80 view pixels to the right, with Reach; a click toggles
the selection. Undo text: "Change Brush Stroke Density".

File format: `writePaintInformation()` adds the density from version 4 of
the stroke data ([Brush Stroke Layer](brush-stroke-layer.md#persistence-and-compatibility)).
`saveStrokes()` writes version 4 only when a stroke has a density below
1, and version 3 otherwise, so files that do not use it stay readable by
Solstice builds that know version 3. A non-finite density reads as 1.

**Width by a fixed amount (2026-11-18).** `KisBrushStrokePathEdit::
addWidth(stroke, anchor, amount, reach)` adds `amount` recorded pixels to
the line's width around the anchor, with the falloff of `movePoints()`.
A recorded stroke has no width of its own: it has the brush size and the
pen pressure, and the brush's Size option turns the pressure into a share
of the size. That share is far from the pressure itself (the default
brush `b) Basic-5 Size` has 0.05, 0.23, 0.58 and 1 at the pressures 0.25,
0.5, 0.75 and 1), so `sizeResponse()` reads the option from the preset's
settings as `KisKritaSensorPack` writes them (`PressureSize`,
`SizeUseCurve`, `SizeValue`, `SizeSensor` with its `<curve>`,
`SizeUseSameCurve` and `SizecommonCurve`, the older `CustomSize` and
`CurveSize`); the option's classes are in `plugins/paintops/libpaintop`,
which `libs/ui` cannot link. `widthShare(stroke, pressure)` is the
strength times the pressure curve (`KisCubicCurve`), and
`sizeFollowsPressure()` says whether the option is on with a pressure
sensor.

- Size follows pressure: each control point within the reach gets the
  share `widthShare(pressure) + amount * weight / size`, and the pressure
  that gives it (bisection; the curve is taken as rising; at least 0.01).
  If a share exceeds the one at full pressure, the stroke's preset is
  cloned with a size larger by that ratio and every point of the stroke
  gets the pressure that keeps its width.
- Otherwise: `withBrushSize(size + amount)` for the whole stroke, at
  least 1 px (or the size it has, if smaller). The anchor and the reach
  do not matter.

In the tool, the mode Add Width (combo index 7; `mode` is read with the
upper bound `AddWidth`) is a drag like Change Width: the amount is the
drag to the right in image pixels over `AddWidthDragRatio` (4), times the
layer's record scale. Undo text: "Add Brush Stroke Width".

**Free Transform of selected lines (2026-11-18).** The Transform Tool
works on a whole vector layer (`KisShapeLayer::transform()` sets the
layer's transformation, Free Transform only) and the Select Shapes Tool
changes the shapes' transformations (affine). Pulling one corner of
selected lines needs a projective map, which a shape's transformation
cannot hold for a stroke that is drawn again with its brush, so the path
itself is changed: `KisBrushStrokePathEdit::mapped(stroke, map)` puts
every control point and handle where `map` says (pen values kept; a curve
bends only through its four points). `canMapPoints()` is false for
strokes with the canvas mirror: their copies are painted by mirroring
each dab around the axes and cannot follow.

In the tool, the mode Free Transform (combo index 8; `mode` is read with
the upper bound `FreeTransform`):

- `frame()` is the rectangle around the selected lines' paths in image
  pixels, grown by half the widest brush (at least 2 px), as four corners
  (`m_frame`, for the shapes in `m_frameShapes`). It is made again when
  the selection is another list of shapes, when the mode changes and when
  the tool is deactivated. After a transformation it is the dragged
  frame, around the new shapes, so the next drag starts from it.
- `frameHandleAt()`: corners 0 to 3, side handles 4 to 7 (top, right,
  bottom, left; within `PickDistance`), 8 for the inside. A press on the
  frame comes before the lines; a press elsewhere on a line goes the
  usual way and a click toggles its selection. A click inside the frame
  (less than 2 view pixels of movement) toggles the line under it.
- During the drag only `m_dragFrame` changes (`movedFrame()`), and
  `paint()` draws the frame, its handles and the selected lines' paths
  through `QTransform::quadToQuad(m_frame, m_dragFrame)`. The strokes are
  not redrawn until the release: one `replaceStrokes()` for all selected
  lines (`transformSelected()`), undo text "Transform Brush Strokes".
- Each stroke is mapped through image pixels (`expectedTransform()`, the
  frame's transformation, back). Its brush size is multiplied by the
  square root of the map's area scale at the middle of the stroke's
  rectangle.
- `isUsableFrame()` refuses a frame that is not convex or has a flat
  corner ("The frame cannot be folded or flattened.").

**Redrawing a part (2026-11-19).** `KisBrushStrokePathEdit::redrawn(stroke,
from, to, path, spacing)` replaces a part of one line (the jobs of one
`strokeInfoId`) by straight jobs along `path`:

- The part runs from `from` to `to` in distances along the line. Without
  a valid `to` on the same line it runs to the line's end, or to its
  start if the line's place nearest to the path's last point lies before
  `from`. If the part runs backwards, the path is reversed.
- The path is shifted so that it meets the line where the part is cut
  out: by the offset of each end, blended linearly along the path's
  length. An end that is the line's own end is not shifted.
- Points nearer than `spacing` (3 recorded pixels) to the last kept one
  are dropped; the ends stay.
- Each new control point gets the pen's values (`KisPaintInformation::
  mix()`) the old part had at the same share of its length, so pressure,
  time and density run through as before, over a path of another length.
- Jobs before and after are kept; a job the part starts or ends in is
  split there (`splitJob()`, de Casteljau for curves). A POINT job inside
  the part, or at a line end that moves, goes to the nearest new control
  point. The new jobs take the place of the first job they replace in
  the job list, so the order among Multibrush copies stays nearly as
  drawn.

In the tool, the mode Redraw (combo index 9; `mode` is read with the
upper bound `Redraw`): the press has to be on a line (as in the other
modes), the drag is collected in `m_redrawPath` (image pixels, drawn in
red by `paint()`), and the release calls `redrawn()` once. The path ends
on the line if its last point is within the pick distance or half the
brush of it. A drag shorter than 3 view pixels is a click and toggles the
selection. Undo text: "Redraw Brush Stroke".

**Redrawing the width (2026-11-19).** `KisBrushStrokePathEdit::
redrawnWidth(stroke, places, widths, spacing)` gives a part of one line
the widths of a trace:

- `places` are places of the line (`nearestPoint()` of the trace's
  points), `widths` the width wanted at each, in recorded pixels. Places
  on another line than the first valid one are left out. They are sorted
  by their distance along the line; the part runs from the first to the
  last, and the width between two places changes evenly.
- The jobs of the part are divided (`splitJob()`, the path stays) so that
  control points are at most `spacing` (4 recorded pixels) apart, with
  points at the part's two ends and at least one between them. The
  points inside take the widths; the two at the ends keep theirs, so the
  change ends within the spacing. An end of the line itself takes its
  width, and so does the first dab (the POINT job) there.
- The widths become pressures through `withShares()`, which `addWidth()`
  now uses too: a share of the brush size per control point, the Size
  option's curve inverted, and a brush that grows when a share exceeds
  what it has at full pressure (then every point is solved again).
- A brush whose size does not follow the pressure gets the mean of the
  widths as the size of the whole stroke, with no points added.
- The jobs' distances are taken from the line as it was, during the
  division. Measuring the divided line again would move a curve's
  boundaries by the rounding of its length and put a point on the wrong
  side of the part's end.

In the tool, the mode Redraw Width (combo index 10; `mode` is read with
the upper bound `RedrawWidth`): the drag is collected as in Redraw, with
the pen pressure of each point in `m_redrawPressures` (the release's own
point is not added: a tablet reports no pressure there). At the release,
each point within reach of the line (the pick distance, or half the
current brush plus half the line's brush) gives a place and the width
`current brush size x widthShare(current preset, pressure)`, scaled to
recorded pixels. `widthShare()` has an overload for a preset. `paint()`
draws the trace as a translucent band of the brush's size. Undo text:
"Redraw Brush Stroke Width".

**Assistants along lines (2026-11-19).** `KisBrushStrokePathEdit::
pathPieces(stroke, tolerance)` gives the paths of a stroke's lines as few
pieces (`PathPiece`: two ends and two handles, `curve` or not), in
recorded pixels: a copy of the stroke with a pressure of 1 everywhere is
simplified (`simplified()` keeps points for a width that changes, which a
path does not need), and each LINE or CURVE job with a length is a piece.
A fitted curve whose handles lie on its chord counts as straight.

The button **Create Assistants** (`btnEditBrushStrokesCreateAssistants`,
`createAssistants()`) takes the pieces of the selected lines at the
Simplify tolerance, maps them through the shape's `expectedTransform()`
and `KisImage::pixelToDocument()` (assistants are in document
coordinates), and makes a `spline` assistant for a curved piece (handles
in the order start, end, control 1, control 2) and a `ruler` assistant
for a straight one, from `KisPaintingAssistantFactoryRegistry`. One
`AssistantsCommand` (in the tool's source; the Assistant Tool's own
`EditAssistantsCommand` is inside its plugin) sets the decoration's list
with and without them: "Create Assistants from Brush Strokes". The layer
is not changed and need not be editable. Snapping across the pieces
needs "Snap to Single Line" off: with it on,
`KisPaintingAssistantsDecoration::adjustPosition()` keeps the assistant
the stroke started on.

**A selection from lines (2026-11-20).** `KisBrushStrokePathEdit::
closedPath(stroke)` is a `QPainterPath` of the stroke's lines
(`KisBrushStrokeShapeEraser::strokeLines()`), each closed from its end to
its start, with `Qt::WindingFill`. The button **Select Inside**
(`btnEditBrushStrokesSelectInside`, `selectInside()`) adds the paths of
the selected lines, mapped by `expectedTransform()`, paints them
antialiased into a `KisPixelSelection` and hands it to
`KisSelectionToolHelper::selectPixelSelection()` with `SELECTION_REPLACE`
("Select Inside Brush Strokes"), as the polygonal selection tool does.
Nothing selected after painting: "The selected lines enclose no area.",
no command. The outline cache is left to be computed (the path has
inner edges where lines overlap).

**Joining (2026-11-18).** `KisBrushStrokePathEdit::joined(parts)` takes
strokes with the transformation from their recorded pixels to the first
one's (`JoinPart`). `canJoin()` accepts a stroke whose jobs all have one
`strokeInfoId` and include a LINE or CURVE job; others are left out. Each
stroke's path jobs form a chain, mapped into the first stroke's pixels
(positions and handles; tilt and rotation are not turned). While more than
one chain is left, the two nearest ends of different chains are connected:
of a pair, the chain with the lower number keeps its direction, the other
is put before or after it and turned around if needed (jobs reversed,
`pi1`/`pi2` and the handles swapped), so the first stroke's line is never
turned. A gap above 0.001 px gets a LINE job from one end's info to the
other's. Afterwards the time is rewritten to run on job by job with each
job's own duration (a connection takes none), since a turned or appended
line would run back. The result is a copy of the first stroke (preset,
colors, seed, mirror flags, `starts`) with the new jobs; POINT jobs stay
only if they are the first stroke's leading ones and that stroke still
starts the line, and if it does not, the line's `starts` entry is reset as
after a cut.

`KisBrushStrokeShapeEdit::joinStrokes(layer, shapes, text, &joined)` builds
the parts from each shape's `expectedTransform()` (through image pixels
into the first shape's record), makes the shape with
`createPiece(stroke, false)` of the first shape (its place and stacking
order), removes the joined shapes and returns the replace command. In the
tool, `btnEditBrushStrokesJoin` ("Join Lines") calls it for the selected
lines in selection order (`KoSelection::selectedShapes()` keeps the order
of selecting), so the line selected first gives brush and color; the new
shape is selected. Undo text "Join Brush Strokes". Messages when fewer
than two lines are selected or fewer than two can be joined.

Not built: dragging the handles of control points, a density above the
drawn one, smoothing a joint, joining lines of mirrored strokes, a fixed
amount of width for all selected lines at once (Add Width works on the
dragged line), rotation and numeric input in Free Transform, a redraw of
the strokes while the frame is dragged, a width that follows the
perspective along a line, a smooth joint and fitted curves for a redrawn
part, the tablet's pressure curve and a preview of the result for Redraw
Width.
After undo of an Apply button or Simplify Lines the lines are not selected
(the shapes that were are gone).

## Transform previews

Added 2026-11-18. Without Instant Preview the Transform Tool's in-place
stroke applies every step of a drag to a vector layer for real
(`extLayer->transform()` then `forceUpdateTimedNode()`), and its last step
is the result: `finishAction()` does not apply the transform again. Each
step used to draw every brush stroke of the layer again.

`KisShapeLayer::setBrushStrokePreview(true)` makes the layer's repaints
open a `KisBrushStrokeShape::PreviewPaintScope`; inside it a shape that has
no picture for the painter's transform draws its latest picture scaled and
marks the scope instead of calling `render()`. The canvas remembers the
rectangle painted that way. `setBrushStrokePreview(false)` puts that
rectangle back into the dirty region, so the next repaint draws the strokes
exactly. No picture is added during a preview, so the pictures of the
earlier transform stay and undo or cancel shows them at once.

`InplaceTransformStrokeStrategy::transformNode()` enables the preview for
every vector layer it transforms and keeps the layer in
`previewedShapeLayers`; `finalizeStrokeImpl()` (reached when the transform
is applied and when it is cancelled) starts with a barrier job that calls
`endBrushStrokePreview()`: preview off, then `forceUpdateTimedNode()`. The
strategy's destructor switches the preview off as a last resort. The exact
repaint is a spontaneous job, so it runs right after the stroke.

The preview never outlives the transform stroke, and the image is busy
while that stroke is open, so saving, autosave and export do not see a
previewed picture. Do not enable it from anything that can leave it on.
`forceUpdateTimedNode()` keeps the preview while it is enabled.

Measured in `testTransformPreview` (30 short strokes, image scaled by 2,
including the scaling stroke): 15 ms with the preview, 107 to 109 ms with
the exact drawing. In the application a 30-step corner drag on a layer with
24 strokes took 0.71 s for a requested 0.6 s.

Not covered: the Select Shapes Tool's resize and rotate handles change the
shapes outside any image stroke (flake interaction strategies), with no
place to end a preview, so selected strokes are still drawn again at every
step there.

**Resizing and the painting copies (2026-11-20).** The layer paints
shallow copies of its shapes in a worker thread
(`KoShapeManager::preparePaintJobs()`). `KoShape::setSizeImpl()` is const
and wrote the size into the data a copy still shares, so every step of a
resize drag (`KoShapeResizeCommand::replaceResizeAction()` first sets the
size the drag started with) changed the size of a copy that was being
painted. A brush stroke is slow to draw again, so the copy was often
caught: ink at the starting size was painted into the update's patches
and stayed where later updates did not reach, outside the shape and not
selectable. `KoShape::setSize()` now detaches its data first. Sizes set
through `setSizeImpl()` alone (`KoShapeGroup`) still write through.

## Persistence and compatibility

The shape is an SVG `<image>` with the picture as an inline PNG (covering
exactly the shape's rectangle) and `data-solstice-brush-stroke-*`
attributes:

| Attribute | Meaning |
| --- | --- |
| `-rect` | `x y width height` of `recordRect()`; marks the element as a brush stroke |
| `-index` | The stroke's index in the layer's `brushstrokes` file |
| (no suffix) | The stroke inline: `saveStrokes()` data, `qCompress`, Base64 |
| `-exact` | `1` when the PNG is the brush's drawing pixel for pixel |

`KisShapeLayer::saveLayer()` opens a `SavingScope`; the shapes then write
indexes, and the layer writes all strokes with `saveStrokes()` (format in
[Brush Stroke Layer](brush-stroke-layer.md#persistence-and-compatibility))
to `brushstrokes` beside `content.svg`, with each brush stored once. A write
failure fails the save. Without a scope (clipboard, SVG export) each shape
embeds its stroke. `loadLayer()` reads the file into a `LoadingScope` before
parsing.

The factory is registered for `svg:image` with loading priority 2, above the
image shape. `loadSvg()` returns false without a valid `-rect`, so other
images still load as image shapes. A shape whose stroke is missing or
unreadable keeps its picture and scales it; the layer adds a load warning.
A picture not marked exact is drawn again at first paint. Upstream Krita
loads the element as an image shape and drops the attributes on save.

## Copying a document with a vector layer

Found while testing this feature, but independent of it: saving and
autosave copy the document, and a copied vector layer at a resolution other
than 72 ppi threw its copied picture away and started a repaint inside the
image's copy constructor, which deleted the image under construction
(freeze or crash). Three changes, all in the vector layer
([pitfall](wiki/pitfalls/strong-image-ref-during-copy.md)):

- the copy constructor carries the source's resolution over
  (`ShapeLayerContainerModel::copyResolutionFrom()`), so the copy does not
  take its image's unchanged resolution for a change and reset its cache;
- `KisShapeLayerCanvas::updateCanvas()` ignores updates of empty
  rectangles while the canvas has no image (the shape manager forwards
  one to every copy), so they no longer mark the layer for a full repaint;
- `KisShapeLayerCanvas::setImage()` starts a repaint that is still needed
  from the event loop, not at once.

A copy therefore has its picture and no pending update, which
`KisDocument::initiateSavingInBackground()` asserts
(`!hasDelayedNodeWithUpdates(newRoot)`): with only the third change that
assert showed its dialog at every autosave. Keep
`testDocumentCopyWithVectorLayer` passing for both a path shape and a brush
stroke; it checks the picture and the assert's condition. Create test
documents with their resolution before the layer exists
(`Document(resolution)`): `KisImage::setResolution()` afterwards does not
reach the layer. `KisDocument::exportDocumentSync()` does not copy the document, so a
test or scenario that only exports does not cover this; `KisDocument::clone()`
does. Checked in the application on 2026-11-18: a scratch profile with
`AutoSaveInterval=10`, a stroke on a vector layer in a 300 ppi document, 40 s
of waiting; the autosave file was written and the application kept painting.

## Build, tests and installation

`KisShapeLayer` and `KisToolFreehand` changed layout: build the whole tree
and install it before running tests
([pitfall](wiki/pitfalls/tests-load-installed-plugins.md)).

| Test | Coverage |
| --- | --- |
| `KisVectorBrushStrokeTest` | Stroke to shape for Build-up, Wash and half opacity against a paint layer; undo/redo; image scale redraw and sharpness; move keeps pixels; rotation; `.kra` round trip at 300 ppi and scale after load; document copy at 300 ppi (path shape and brush stroke); inline SVG; plain images stay image shapes; redraw time (reported) |
| `KisVectorBrushStrokeTest` (eraser) | `testCutStroke`: untouched, a cut across lines and across a curve, cut points on the eraser's reach, everything erased. `testEraserCutsShapes`: two shapes, empty where erased, pixels beside the cut, undo/redo, `.kra` round trip. `testEraserWholeLineAndCancel`, `testEraserOnScaledLayer` |
| `KisVectorBrushStrokeTest` (intersections) | `testCutUpToIntersections`: between two crossings, before the first, after the last (on a curve), no crossing, a line ending two pixels short of the stroke, one ending far short. `testEraserUpToIntersection`: two crossing strokes on a layer, each trimmed to the other, two undo steps, the setting |
| `KisVectorBrushStrokeTest::testPathPieces` (also) | `pathPieces(QPainterPath)`: a line, a curve, a straight curve as a line, a piece without length dropped, the four sides of a closed rectangle; `ellipseAxes()`: as drawn, taller than wide, and under rotation, shear and scale (the three points on the mapped ellipse, axes at a right angle, the first the longer), refused for an empty rectangle and a flattening transform |
| `vector-shape-assistants.json` scenario | In the application: an ellipse from the Ellipse Tool, selected with the Select Shapes Tool, Create Assistants (one undo step; none with nothing selected); a brush stroke drawn 12 px inside the ellipse's top on a paint layer with Snap to Assistants lies on the ellipse |
| `vector-eraser-multibrush.json` scenario | In the application: a Multibrush stroke with the fresh profile's copies, then the Multibrush in eraser mode, Whole Line, across the drawn line: the layer is empty (every copy removed), one "Erase Brush Strokes" step, undo returns the picture |
| `vector-eraser-intersection.json` scenario | In the application: two crossing strokes, the mode chosen in the combo box, one stroke touched near its start and removed up to the crossing, undo |
| `KisVectorBrushStrokeTest::testMovedShapeKeepsPicture` | Also: a copy of the shape keeps its size and bounding rectangle when the shape is resized |
| `KisVectorBrushStrokeTest` (path editing) | `testShapeEdit` also: a line with the canvas mirror's flags, a point moved: the new shape's rectangle holds both sides and the copy is painted to its far end. `testPathEditFunctions`: nearest place on lines and on a curve, control points, a move with and without reach (falloff, connected path, original untouched), width, split. `testShapeEdit`: a move in two steps on the layer, undo/redo, cancel, no undo step without a change, width, cut |
| `KisVectorBrushStrokeTest` (properties) | `testStrokeColorSizeBrush`: the three functions leave the stroke alone; two lines recolored in one undo step with unchanged coverage; a larger brush for one line only; another brush at the same width, saved and loaded; undo of each |
| `KisVectorBrushStrokeTest::testSimplify` | Fewer jobs within the tolerance both ways at 0.1, 1 and 4 px; end points, pressure peak and running time kept; a straight line becomes one curve and then stays; a corner stays a point of the path; mirrored copies simplified alike and drawn in turns; on the layer one undo step with nearly the same ink |
| `KisVectorBrushStrokeTest::testAddDeletePoint` | A point added on a curve and on a line: same path, the place's pen values, the new point alone moves; nothing added on an existing point; the nearest control point from both halves of a job and from the starting dab; a point deleted between two lines (one curve through the place), at the line's start (the dab moves along) and end; a line of one job unchanged; a mirrored copy untouched; on the layer no visible change from an added point, undo of both |
| `KisVectorBrushStrokeTest::testPathPieces` | `pathPieces()` on a wave of 62 jobs with a pressure that rises and falls: at 1 px no more pieces than `simplified()` gives jobs with the pressure counted (9 against 10), connected from the line's start to its end, within 1.5 px of the path (0.49), with curved pieces; `closedPath()` of the wave contains points between it and the piece from its end to its start and none beyond, and has no height for a straight line; the stroke unchanged; at 0 a piece for each of the 61 path jobs; a straight line is one straight piece; a null stroke has none |
| `KisVectorBrushStrokeTest::testJoin` | The four ways two ends can meet (direction, pen values with their points, time never running back, the first line's start kept or reset), touching ends without a connection, three lines out of order, a part in other pixels with a curve, starting dabs, what cannot be joined; on the layer two lines of different color become one with ink in the gap, undo and redo, and a moved line joined where it lies |
| `vector-join-lines.json` scenario | In the application: a black and a red stroke selected by clicks and joined with the button (one black line over the gap), a second press does nothing, undo and redo |
| `KisVectorBrushStrokeTest::testSwitchCorner` | A point between two lines made a curve (handles along the neighbours' direction, a third of the way; points and pen values kept) and a corner again (the two lines as they were); a right angle rounded at one and at both turns, one of them made a corner again while the other keeps its handle; the corner flags of a line's points; a line coming back on itself; the line's ends, the starting dab and no place unchanged; the drawn line's last joint a curve point by its handle; a simplified line's inner points all curve points, one made a corner and a curve again; a mirrored copy untouched; on the layer a corner changes pixels only near it, undo of both steps |
| `KisVectorBrushStrokeTest::testRedrawWidth` | `redrawnWidth()` on a straight line of 20 jobs at a pressure of 0.7: one width from x = 60 to 140 gives control points every 4 px with that width (by the brush's curve), the part's ends and the rest keep theirs, the jobs meet with the same pen values on the path as drawn, 10 jobs more; widths that change along the part, given in any order; a part inside one job (two ends and a point between); a part up to a line end, where the end and the first dab take the width; a width beyond the brush makes it grow and the rest keeps its width; invalid input returns the stroke; a brush that does not follow the pressure gets the mean as its size; `widthShare()` for a preset; on the layer a line 12 px wide is 5 px wide in the part and as drawn outside; undo and redo |
| `KisVectorBrushStrokeTest::testRedraw` | `redrawn()` on a straight line of 20 jobs: an arc between two places replaces that part (the path passes the arc's top and no longer the old middle), the rest is unchanged point for point, the jobs meet and the pen's time does not run back, the last point keeps its time; the same with the path reversed and with a path 3 px beside the line; a path that leaves the line takes the line's end with it, towards the end or towards the start (the first dab goes along); invalid input returns the stroke; on the layer ink is on the arc and gone from the old middle; undo and redo |
| `KisVectorBrushStrokeTest::testStrokeBackgroundColor` (also) | `usesBackgroundColor()`: false for the Mix brush at full pressure, for the brush without Mix and for no stroke; true for the Mix brush at half pressure |
| `KisVectorBrushStrokeTest::testStrokeBackgroundColor` | `withBackgroundColor()` changes `bgColor` only and returns the stroke itself for the color it has; on the layer, a line of a brush with the Mix option at half pressure loses its green when the background goes from white to red (mean green 125 to 0) and keeps its alpha; undo and redo |
| `KisVectorBrushStrokeTest::testMapPoints` | `mapped()` with a projective map on a stroke with a point, lines and a curve: every point and handle is where the map puts it, the pen's values and the preset stay, jobs still meet; a stroke with the canvas mirror and a map that gives no finite place return the stroke itself; on the layer a line moved 20 px down and halved towards its start has its ink there (top and bottom within 1 px, 80 px shorter within 3 px); undo and redo |
| `KisVectorBrushStrokeTest::testAddWidth` | `widthShare()` of `b) Basic-5 Size` rises with the pressure and is 1 at full pressure; `addWidth()` on a line at the pressure 0.7: the anchor's width grows by the amount and the neighbours' by the cosine falloff, the preset stays, nothing changes beyond the reach; a thick and a thin part get the same amount with a long reach; thinner by half the width, and a pressure of at least 0.01; wider than the brush: the brush grows to that width and the other places keep theirs; without the Size option the brush size of the stroke changes (at least 1 px) and the pressures stay; on the layer the line is 6 px wider and 6 px thinner at the place (within 2 px) and as drawn at the ends; undo and redo |
| `KisVectorBrushStrokeTest::testDensity` (Build-up ink and a Wash brush) | `scaleDensity()`: the anchor's value, the cosine falloff, both jobs of a point agree, positions and pressure kept, the bounds 0.01 and 1; the pen's values copy and mix it, a new point has the mean; the stroke data is version 3 without and 4 with a density and reads back the same; on the layer the ink at the anchor is half of the drawn one, grows towards the reach, and nothing changes beyond it; undo and redo; `.kra` saved and loaded with the density, and the line drawn again at twice the size has half the ink at the same place |
| `edit-brush-strokes-tool.json` scenario | In the application: the tool activated, Mode and Reach set in Tool Options, a drag moves the middle of a line, a drag changes its width, a click cuts it; a click selects the line and Apply Color, Apply Size and Apply Brush change it; Simplify Lines at 1 px makes one undo step and leaves the line in place, a second press makes none; Add Point with a drag, Delete Point with a click and Switch Corner with a click make one undo step each; Create Assistants on the selected line is one undo step that leaves the picture; Select Inside on that straight line makes no selection and no undo step; in Redraw Width a trace from x = 370 to 430 with a brush of 60 px puts ink 23 px above the path, leaves the place 60 px further white, one undo step; in Redraw an arc drawn from x = 360 to 440 puts ink 45 px above the line, takes it from the old middle and leaves the ends, one undo step; in Free Transform a click selects the line, a drag inside the frame moves it 50 px down and a drag of the frame's bottom side stretches it downwards, one undo step each; a drag of 100 px to the right in Add Width puts ink 14 px above the line's path where it was white and leaves the place 60 px along the line white, one undo step; a drag to the left in Change Density turns the black middle gray over the white background and leaves the pixels 60 px away as they were; everything undone to the drawn line |
| `KisVectorBrushStrokeTest::testTransformPreview`, `KisBrushStrokeLayerTransformTest::testVectorLayerPreviewsDuringTransform` | The preview is softer than the exact drawing and keeps it while forced; ending it gives the picture of a transform without preview; the in-place stroke previews, draws exactly when applied (equal to undo then redo) and when cancelled |
| `vector-brush-stroke.json` scenario | In the application: two strokes, undo/redo, an eraser drag across a stroke (empty at the crossing, one undo step, redo), drawing again, Whole Line chosen in the combo box removing every touched line |
| `KisBrushStrokeLayerTest`, `KisBrushStrokeLayerTransformTest`, `KisBrushStrokeReplayTest`, `KisThroughLayerEraserTest` | Shared recording and stroke paths |

The test waits for the image with the event loop running
(`waitForImage()`). `KisImage::waitForDone()` from the GUI thread can
deadlock with `TransformShapeLayerDeferred`, which existed before this
feature.

## Manual regression checks

1. Draw on a vector layer with a Wash and a Build-up preset, a textured
   tip, pressure, the Multibrush and the canvas mirror. No flicker when the
   pen lifts; quick successive strokes stay visible.
2. Select, move, resize, rotate, duplicate, reorder and delete strokes with
   the Select Shapes Tool; undo and redo each.
3. Scale the image and the layer; use the Transform Tool with Instant
   Preview on and off: the drag is smooth and soft, the strokes sharpen
   when the transform is applied, and Esc returns the sharp drawing. Save
   right after applying and reopen.
4. Save, reopen and scale. Copy a stroke to another document. Open the file
   in Krita and confirm pictures appear.
5. Eraser mode cuts lines: Touched Part and Whole Line, a small and a
   large eraser, fast drags, a drag across many lines, erasing the same
   place twice, lines on a scaled or moved layer, Multibrush lines. Undo
   and redo each drag; switch tools mid-drag. Other paint tools still
   refuse the layer.
6. Up to Intersection: overshooting corners, a line ending at another
   (T junction), hatching over an outline, a loop, a line with no crossing,
   a touch right on a crossing, thick against thin lines, a layer with many
   strokes (speed).
7. Edit Brush Strokes Tool: the path, control points and reach circle
   appear on hover and follow a drag (not covered by any automated check:
   canvas captures leave out decorations); Move Points with a large, small
   and zero Reach; Change Width both ways; Cut; undo and redo each; zoomed
   and rotated canvas; a scaled layer; switching tools mid-drag. Select
   lines by clicking (orange paths), also with the Select Shapes Tool;
   Apply Color, Size and Brush on one and several lines, with an image-tip
   and a textured brush; save, reopen and check the changed lines.
   Simplify Lines at 0.5, 1 and 5 px on pen-drawn lines with pressure, on
   lines with corners, on Multibrush lines and on a scaled layer: the
   message's counts, the look before and after, Move Points afterwards,
   undo, save and reopen. Add Point (click only, click and drag, on an
   existing point) and Delete Point (a middle point, the first, the last,
   a line of two points) on a simplified and on a freshly drawn line; the
   marker in Delete Point sits on the point that goes. Switch Corner on a
   simplified line: a curve point, a corner, the same point twice, a
   line's first and last point (refused with a message); while hovering in
   that mode corners show as squares and curve points as circles.
   Select Inside: a closed loop, a loop with a gap, a line that crosses
   itself, two lines, a straight line (message); undo; fill the
   selection on a paint layer; on a scaled layer.
   Create Assistants: on a curved line, a straight line and several
   selected lines; draw along them with Snap to Assistants on and Snap
   to Single Line off, on a paint layer and on the vector layer; undo and
   redo; edit them with the Assistant Tool; save and load; on a scaled
   layer; with a small and a large Tolerance.
   Redraw Width: trace a line with a pen, lightly and firmly (the width
   follows); with brushes of other sizes; a line drawn with a brush of
   one size (the whole line changes); a trace that wanders off the line;
   the band while tracing; on Multibrush and canvas-mirror lines; on a
   scaled layer.
   Redraw: draw from a line back onto it, in both directions; from a line
   away from it towards each end; on a line with changing width (the
   width keeps its course); on a line that crosses itself; on Multibrush
   and canvas-mirror lines; on a scaled layer; with a pen (many points).
   Free
   Transform: select two of three lines; drag a corner, a side and the
   inside of the frame and look at the preview paths, the frame after
   the release, and the line that is not selected; try to fold the frame
   (refused); change the selection (the frame is made again); a line
   drawn with the canvas mirror (left, with a message); a Multibrush
   line; a scaled layer; undo and redo. Add
   Width: drag right and left on a line of a pressure-size brush (thin
   and thick places), past the brush size, with a long Reach, on a
   fixed-size pen (the whole stroke changes), on a brush with opacity by
   pressure, on Multibrush and canvas-mirror lines and on a scaled layer.
   Change
   Density: drag left and right on an ink line (Build-up), a Wash line,
   a soft brush and a scattering brush, with Reach 0 and a long Reach, on
   a line that crosses itself, on Multibrush and canvas-mirror lines and
   on a scaled layer; then erase through the lighter part, simplify, save
   and reopen. Join
   Lines on two
   and on several lines, ends near and far, lines drawn in opposite
   directions, different brushes and colors (the first selected wins), a
   moved or scaled line, Multibrush lines (refused); then Move Points and
   Change Width across the joint, undo, save and reopen.
8. Draw with Instant Preview on and with the GPU engine on a float
   document.

## Comparison table in the user guide

`docs/vector-brush-strokes.md` has "Comparison with Clip Studio Paint"
(added 2026-11-18): one row for each function on the official page
`https://tips.clip-studio.com/en-us/articles/600` as it read that day (a
2017 article for Ver. 1.10.0), with Yes, Partly or No and the Solstice
tool. Rules: only functions the page names; "Yes" only for what a test or
scenario in this document covers; differences go in the third column.
When a function of this feature is added or changed, update its row; when
the page is read again, change the date. The guide also says the
functions were checked by hand with a pen tablet: the user's manual test
of 2026-11-20 (the list under "Manual regression checks"), which
replaced the earlier statement that no real pen had been used. The "Not
verified: real pen" remarks in the verification notes below are dated
records from before that test. The SVG row rests on
`saveSvg()` and its test, not on an export run in the application.

## Known gaps

- The Select Shapes Tool's handles redraw the selected strokes exactly at
  every step (a few milliseconds per short stroke); only the Transform
  Tool previews.
- Control points have no handles to drag. Switch Corner sets standard
  handles (a third of the way to each neighbour) or pulls them in; a
  corner is geometry, not a stored flag, so Simplify Lines decides again
  with its own 60 degree rule and may round a flatter corner. A joint made
  by Join Lines is a straight job with the two ends' pen values: thin
  between tapered ends, and not smoothed.
  Simplifying measures position and pressure only; tilt, rotation and
  speed between the remaining points are interpolated.
- Select Inside: no add, subtract or intersect with the selection there
  is, no grow or feather, and no area from several lines that only
  together form an outline (join them first). The border is the path,
  not the ink's edge.
- Create Assistants for vector shapes (2026-11-20): every selected
  `KoPathShape` (`KoSelection::selectedVisibleShapes()`, any layer the
  selection holds) besides the brush strokes. `pathPieces(QPainterPath)`
  turns the shape's `outline()` into pieces (a curve with its handles on
  its chord is a line; pieces without length are dropped), mapped by
  `absoluteTransformation()`: shapes are in document coordinates, as
  assistants are. A shape whose `pathShapeId()` is "EllipseShape" (the
  class is in the path shapes plugin, so it is told by name), closed and
  all curves, becomes one `ellipse` assistant with the handles of
  `ellipseAxes()`: both ends of the longer axis and one of the shorter,
  the ellipse's own axes under the shape's transformation (shear moves
  them). With neither strokes nor path shapes selected: "Nothing is
  selected. Click lines with this tool, or select vector shapes with the
  Select Shapes Tool." The tool itself selects brush strokes only; the
  shapes are selected in the Select Shapes Tool and stay selected when
  the tool changes. Not covered: text shapes, a shape's inner subpaths
  told apart, arcs as Ellipse assistants.
- Create Assistants: one assistant per piece, so a long line gives many;
  they are not linked to the line (an edit of the line leaves them), and
  `AssistantsCommand` keeps the assistant objects themselves, not clones
  as `EditAssistantsCommand` does: after its undo and redo, edits the
  Assistant Tool made to them in between are still in them. No selection
  from a line (CSP's "Selection from ruler"). No test control tool reads
  the assistants; the scratch run checks them by painting.
- Redraw Width reads the pressure from the event (`KoPointerEvent::
  pressure()`), not through the pressure curve of the tablet settings
  that the freehand tools apply. Points of the trace that are away from
  the line are dropped, and the width between the remaining ones changes
  evenly, so a trace that leaves the line and comes back still covers
  what lies between. On a self-crossing line the nearest place can be on
  the other branch, which makes the part longer than what was traced.
  Other sensors of the current brush's Size option (speed, fade) are not
  in the width. The band drawn while tracing has the brush's full size,
  whatever the pressure.
- Redraw makes LINE jobs only and does not smooth the joints with the
  old line. Where the drawing ends is decided by the nearest place of the
  whole stroke: on a self-crossing line or a Multibrush stroke that can
  be another branch or another copy (then the part runs to the line's
  end instead). The synthesized pen values are the old part's, stretched:
  the dab spacing state (`starts`) is untouched, so after a redraw the
  dabs fall at other places along the line, as after any path edit.
- Free Transform changes control points, not the picture: a projective
  map of a Bezier curve is not the curve through the mapped points, so
  long jobs (after Simplify Lines with a large tolerance) bend slightly
  wrong in a strong perspective. One brush size per stroke: the width
  does not narrow towards the far end. The frame is not saved and is
  made again from the paths when the selection changes, so a frame that
  was pulled into a trapezoid does not come back after undo.
- Add Width reads the Size option from the preset's settings by key. It
  knows the pressure sensor's curve and the strength only: other sensors
  of the option count as 1, a falling curve is not handled, and a brush
  engine without that option (MyPaint) counts as having a fixed size.
  When the brush grows, the lowered pressures also change every other
  option that follows the pressure. The dabs do not fall on control
  points and the pressure is mixed linearly between them, so with a
  curved Size option the width between two points is not exactly the
  mean of theirs (measured: within 2 px of the amount).
- The density is applied to the finished picture by the nearest place of
  the path: ink further than one brush size from the path (scatter) keeps
  its density, and the rule for crossings is a heuristic (parts further
  than four half brushes apart along the path, both within half a brush).
  It cannot exceed 1. Simplify Lines does not measure it. Brush Stroke
  Layers ignore it (`renderStrokes()` does not apply it; nothing there
  sets one).
- Apply Background Color (`withBackgroundColor()`, `applyBackgroundColor()`,
  button `btnEditBrushStrokesApplyBackgroundColor`, undo text "Change
  Brush Stroke Background Color", added 2026-11-18) sets `bgColor`, which
  the replay gives to the painter. Most presets ignore it, so the button
  often changes nothing visible but still makes an undo step. Since
  2026-11-20 a message says so ("The background color does not show in
  the selected lines: their brushes do not use it."):
  `KisBrushStrokePathEdit::usesBackgroundColor(stroke, recordRect)` draws
  the stroke with a black and a white background color
  (`KisBrushStrokeLayer::renderStrokes()`, in the GUI thread) and compares
  the pictures, so it holds for every engine and option. The tool tries
  one line of each preset object among the selected lines, at most 8;
  with more it shows no message. A Mix brush at full pressure counts as
  not showing it. No test control tool reads floating messages: the
  message itself is not covered by a scenario.
- Apply Color sets the foreground color only; a stroke's background color,
  gradient and pattern cannot be changed.
- Up to Intersection sees brush strokes on the same layer only, and its
  speed on a layer with very many strokes is not measured.
- The part after a cut restarts its dab spacing (see Vector eraser).
- Verified on 2026-11-18: the tests above and the scenario in a scratch
  instance (CPU engine). Not verified: pen input, the Multibrush, the
  Dynamic Brush, canvas mirror, the Select Shapes Tool interaction, the
  Transform Tool, the GPU engine and loading in upstream Krita. For the
  eraser: the tests and the two scenarios (mouse drags in all three modes,
  the mode set through the combo box); not pen input, erasing many lines at
  once, T junctions and loops in the application, or the look of soft and
  textured brushes after a cut in real drawings. For the Edit Brush Strokes
  Tool: the tests and the scenario (mouse drags and a click in the three
  modes); not the overlay's look, pen input, or zoomed and rotated views.
  For transform previews: both tests, and in a scratch instance a corner
  drag with Apply, undo and redo; cancelling with Esc could not be driven
  there (the synthetic key does not cancel a transform on a paint layer
  either), so cancel is covered by the test only. For Simplify Lines:
  `testSimplify`, the scenario, and in a scratch instance two tablet
  strokes (a pressure wave and a zigzag) simplified at 1 px and 5 px with
  pictures compared by eye (1 px: no visible change; 5 px: the wave
  slightly changed and the flattest of three corners rounded), undo of
  each, no assert lines in the logs. Not verified: real pen-drawn lines,
  soft and textured brushes, the Multibrush in the application. For Add
  Point and Delete Point: `testAddDeletePoint`, the scenario, and scratch
  runs on a simplified wave and zigzag (a point deleted on each, a point
  added with a drag, three undos back to the simplified picture, no assert
  lines). In one of six scratch runs the first Delete Point click after Simplify Lines did nothing (no undo step, same picture) while the identical sequence worked in the other five; not explained. The tool's path is deterministic, and a window appearing over the pointer suppresses a synthetic press (wiki: synthetic canvas input), which the floating message shown by Simplify Lines may have done. Not verified: the marker's look, pen input.
  For Join Lines: `testJoin`, `vector-join-lines.json`, and a scratch run
  joining two pairs of pressure-tapered tablet strokes (one pair with a
  gap and opposite directions, one touching), pictures compared by eye
  (the connection between tapered ends is hairline thin), two undos back
  to the drawn picture, no assert lines. Not verified: more than two lines
  in the application, soft and textured brushes, saving a joined line in
  the application (the record is an ordinary stroke; saving strokes is
  covered by the round-trip tests).
  For Switch Corner: `testSwitchCorner`, the scenario's step, and a
  scratch run on a wave and a zigzag drawn as tablet strokes and
  simplified at 2 px: the zigzag's middle corner made a curve (rounded,
  pictures viewed) and a corner again (the simplified picture exactly), a
  point at the wave's crest made a corner (pointed) and a curve again
  (smooth, a little narrower than before: the standard handles), a click
  on the zigzag's start makes no undo step, four undos give the simplified
  picture and one more the drawn one, no assert lines. Not verified: the
  squares and the marker on the canvas (`canvas_capture` does not show
  tool overlays), pen input, soft and textured brushes, a scaled layer in
  the application.
  For Redraw: `testRedraw` and the related suites. In the application:
  the scenario's step and a scratch run with three lines (a wave between
  two places of a line of rising pressure, a path leaving a line towards
  its end, one towards its start): three undo steps, the rising line's
  widths outside the part unchanged (2.7 px at x = 300), three undos
  give back the drawn picture, no assert lines; picture looked at. Not
  checked: pen input, the red path while drawing, self-crossing,
  Multibrush and canvas-mirror lines, a scaled layer.
  For Select Inside: `closedPath()` in `testPathPieces`. In the
  application: the scenario's step (a straight line: nothing) and a
  scratch run: an ellipse of 100 by 80 px drawn with a gap, selected,
  the button: one undo step, a selection of 25438 pixels (the ellipse
  has 25133) with bounds 199 x 160; filled on a new paint layer: the
  centre, the inside near the edge and the gap are filled, outside is
  not; a straight line: no command, no selection; three undos leave no
  selection; no assert lines; picture looked at. Not checked: a
  self-crossing line, several lines, a scaled layer in the application.
  For Create Assistants: `testPathPieces` and the suite (36). In the
  application: the scenario's step and a scratch run: a wave drawn on a
  vector layer, selected, the button (one undo step); on a new paint
  layer a red stroke drawn 15 px below the wave with Snap to Assistants
  on and Snap to Single Line off lies on the wave at x = 325, 400 and
  475 and not below it; after undo of stroke, layer and assistants the
  same stroke stays where it is drawn; no assert lines; picture looked
  at. Not checked: Snap to Single Line on, the Assistant Tool on the
  created assistants, save and load, several lines, a scaled layer.
  Once, four runs of the scenario in a row failed at different steps
  (a picture after undo, two busy reads, an empty undo stack) and the
  next four passed on the same build; the canvas size was the same
  (228 x 744). Cause not found; input from outside the test (the desktop
  in use) would fit. Later, the read of the state right after the
  button failed as busy in 3 of 11 runs (the known busy read after
  `wait_idle`, `test-control.md`); the scenario has a `view_reset` and a
  second `wait_idle` there now and passed 6 runs in a row.
  For Redraw Width: `testRedrawWidth` and the suite (35). In the
  application: the scenario's step and a scratch run with three lines:
  a line of rising pressure traced with the mouse and a brush of 12 px
  (11.0 px over the part, 2.7 and 18.0 px outside as drawn), an ink line
  of 9.4 px traced with a pen at pressures from 0.4 to 1 and a brush of
  30 px (8.6, 14.1, 22.0 px along the part: the brush grew, the ends
  stayed 9.4 px), an ink line traced with a brush of 4 px by a trace
  that leaves the line in the middle (3.5 px over the whole part); three
  undo steps, three undos give back the drawn picture, no assert lines;
  picture looked at. Not checked: a real pen, the band while tracing,
  self-crossing, Multibrush and canvas-mirror lines, a scaled layer.
  Open: the scenario's second frame drag made no undo step in 2 of about
  50 runs and could not be reproduced with logging (wiki log,
  2026-11-18); other scenarios lose a synthesized drag at a similar rate.
  For Free Transform: `testMapPoints` and the related suites. In the
  application: the scenario's steps (the four vector scenarios passed
  three times in a row with assert dialogs on) and a scratch run with a
  wave of a pressure-size brush and two ink lines, two of them selected:
  a corner pulled out, the left side moved and a drag inside made three
  undo steps, the third line kept its width (9.4 px), a folded frame
  made no step, three undos gave back the drawn picture, no assert
  lines; pictures looked at. Not checked: pen input, the look of the
  frame and the preview paths on the canvas (`canvas_capture` has no
  tool overlays), canvas-mirror and Multibrush lines, a scaled layer.
  For Add Width: `testAddWidth` and the related suites. In the application:
  the scenario's step (the four vector scenarios passed three times in a
  row with assert dialogs on) and a scratch run with `b) Basic-5 Size`
  (20 px, pressure 0.8), `d) Ink-3 Gpen` (14 px) and a line of rising
  pressure: a drag for +10 px changed the width at the place from 11.8 to
  22.0, 13.3 to 23.1 and 9.4 to 19.2, with the places 100 px along the
  line unchanged; a drag for -6 px gave 16.1, 17.3 and 13.3; with Reach
  2000 a drag for +8 px widened the whole rising line by 7.9 to 8.3 px;
  seven undos gave back the drawn picture; no assert lines. Not checked:
  pen input, a fixed-size pen and a brush with opacity by pressure in the
  application, Multibrush and canvas-mirror lines, a scaled layer.
  For Change Density: `testDensity` with a Build-up ink pen and a Wash brush (alpha at the anchor 128 of 254 and 129 of 255; 128 of 255 at twice the size after saving and loading a `.kra`; at a crossing the dense part keeps 255 while the end of the light part falls from 254 to 66) and the related suites. In the application: the scenario's step (three runs of the four vector scenarios with assert dialogs enabled, all passing) and a scratch run with tablet strokes of an ink pen (Build-up), `b) Basic-5 Size Opacity` (Wash) and an ink line with a loop: a drag of 160 view pixels to the left with Reach 60 took the ink at the place from 255 to 60 (ink) and 65 (Wash), to 161 and 162 at 30 px along the line and left 255 at 70 px; the loop's top became light while its crossing stayed dark (picture viewed); a drag to the right brought 255 back; four undos gave the drawn picture; no assert lines. Read these values from the color over the white background: `query_pixels` cannot read a vector layer alone (`source: layer` fails with "The active node has no pixels") and the projection's alpha is 255 there. Not verified: pen input, soft and scattering brushes, Multibrush and canvas-mirror lines, the time on very large strokes.
