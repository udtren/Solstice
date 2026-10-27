# Duplicate selection in the same layer - maintenance reference

Ctrl-drag in the Move Tool with an active selection on a paint layer copies
the selected pixels on that layer and drags the copy, instead of moving
them. User workflow: [Move Tool](../move-tool.md). The original plan, with
the forum findings and the rejected options, is archived in
[the development history](wiki/history/duplicate-selection-in-layer-plan.md).

## User decisions (2026-10-27)

- Option A of the plan: a duplicate mode of the Move Tool's selection
  stroke, not a Transform Tool action or a floating selection.
- Trigger: **Ctrl-drag** (the Move Tool's `SampleFgImage` alternate action,
  which did the same as a plain drag when a selection was active). Alt
  stays the precision mode.
- On a [Brush Stroke Layer](brush-stroke-layer.md), the recorded strokes
  inside the selection are copied with the pixels; the originals stay.

## Source map

| Source | Responsibility |
| --- | --- |
| [move_selection_stroke_strategy.h](../../plugins/tools/basictools/strokes/move_selection_stroke_strategy.h), [.cpp](../../plugins/tools/basictools/strokes/move_selection_stroke_strategy.cpp) | `duplicate` constructor flag (`m_duplicate`), copied by the copy constructor that `createLodClone()` uses; skips the cut; undo name "Duplicate Selection"; Brush Stroke Layer record command |
| [kis_tool_move.cc](../../plugins/tools/basictools/kis_tool_move.cc), [kis_tool_move.h](../../plugins/tools/basictools/kis_tool_move.h) | Ctrl-drag trigger in `beginAlternateAction()`, `startStrokeImpl(..., duplicate)`, `m_currentlyDuplicating`, cancelling a duplicate that ends where it started |
| [KisBrushStrokeLayer.h](../../libs/ui/KisBrushStrokeLayer.h), [KisBrushStrokeLayerTransform.cpp](../../libs/ui/brushstrokelayer/KisBrushStrokeLayerTransform.cpp) | `createSelectionDuplicateCommand()`; shares the stroke classification (`splitBySelection()`) with `createSelectionMoveCommand()` |

## Stroke contract

`MoveSelectionStrokeStrategy(layer, selection, updatesFacade, undoFacade,
duplicate)` with `duplicate = true` runs the same stroke as a move except:

- `initStrokeCallback()` copies the selected pixels into the temporary
  target but does not run the `clearSelection()` cut transaction. The
  auto-keyframe command, the temporary target
  (`KisIndirectPaintingSupport`, `COMPOSITE_OVER`, full opacity), the merge
  in `finishStrokeCallback()` and `KisSelectionMoveCommand2` (the selection
  moves to the copy) are unchanged.
- The whole duplicate is one `KisStrokeStrategyUndoCommandBased` stroke and
  one undo entry, "Duplicate Selection", holding the auto keyframe, the
  merge transaction, the record command and the selection move.
- `createLodClone()` copies the flag, so Instant Preview shows the copy
  without cutting. Vector (shape) selections have no LoD clone, as for
  moves.
- The copy is pasted over the layer with `COMPOSITE_OVER`. Where it overlaps
  its source or other pixels, semi-transparent pixels add up, as in
  Photoshop.

No file format, setting or action changes. The result is ordinary pixels;
clipping, inherit alpha and the GPU engine see the same temporary-target path
as Move Selection ([GPU engine](gpu-engine.md#pixel-brush-integration)).

## Move Tool trigger

`KisToolMove::beginAlternateAction()` passes `duplicate = true` for
`SampleFgImage` (default Ctrl+Left; in the Photoshop-compatible input
profile, Alt+Left). `startStrokeImpl()` duplicates only when selection mode
is usable (a paint layer and a non-empty selection). Otherwise the action is
today's `MoveGroup` move.

- A duplicate first ends any open Move Tool stroke (`endStroke()`), so each
  copy is its own stroke and undo entry, and the next Ctrl-drag copies the
  last copy (stamping).
- The duplicate uses the tool's current mode (`moveToolMode()`), not
  `MoveGroup`; selection strokes ignore the mode. A plain drag or an arrow
  key after a duplicate therefore continues the open duplicate stroke and
  moves the floating copy; the source stays.
- A duplicate whose accumulated offset is zero when the drag ends
  (`endAction()`) or when the stroke is ended (`endStroke()`) is cancelled:
  pasting the copy over its source would only darken semi-transparent
  pixels and add an undo entry.
- Shift (axis snap) and Alt (precision) are read from the real event
  modifiers during the drag (`applyModifiers()`), so both work once the drag
  has started. Ctrl+Shift+Left and Ctrl+Alt+Left start other alternate
  actions (`AlternateSecondary`, `SampleFgNode`), so the modifier has to be
  added after the drag starts.

The input profiles are unchanged. The Move Tool has no "Duplicate" tool
option; it remains a possible follow-up for users without a keyboard.

## Brush Stroke Layer

`finishStrokeCallback()` calls `createSelectionDuplicateCommand(mask,
offset)` instead of `createSelectionMoveCommand()` in duplicate mode, at
full resolution only (`currentLevelOfDetail() == 0`), with the selection
copy moved back to where it was before the drag:

- Strokes are classified with the move's coverage rules
  ([selection moves](brush-stroke-layer.md#selection-moves)): a copy holds a
  stroke whole only when every pixel it covers is fully selected.
- Inside strokes stay, and moved clones (`transformedStrokes()`, a
  translation) are appended after all strokes, matching the copy pasted
  over the layer.
- A crossing stroke (including a selected mirror copy alone) leaves the
  record unchanged and logs a warning; the background check then finds the
  mismatch. No stroke inside, a zero offset or a keyframe channel returns
  `nullptr`.
- The command is the shared `SetStrokesCommand` (undoable, `recorded =
  false`), so the merge's dirty region requests a record check.

## Tests

Build and test commands are in [Development workflow](development-workflow.md)
and the [Brush Stroke Layer reference](brush-stroke-layer.md#build-tests-and-installation).

| Test | Coverage |
| --- | --- |
| `MoveSelectionStrokeTest::testDuplicateKeepsSource` | Source kept, copy at the offset, selection moved, undo name, Instant Preview (LoD 2) device, undo and redo |
| `MoveSelectionStrokeTest::testDuplicateStamps` | Two duplicates in a row copy the last copy; each is its own undo step |
| `KisBrushStrokeLayerTest::testSelectionDuplicateCopiesStrokes` | Crossing selection and zero offset give no command; copies appended after the originals; record matches the pixels; undo |
| `KisBrushStrokeLayerTransformTest::testSelectionDuplicateWithInstantPreview` | Duplicate stroke with LoD 2 copies the record once (full resolution only) and matches the pixels; undo through the stroke's undo entry |

The Move Tool trigger (`KisToolMove`) has no automated test: it needs canvas
input. It is covered by the manual checks below.

## Manual regression checks

1. Paint layer, rectangle selection: Ctrl-drag copies, the source stays, the
   selection follows the copy. Undo removes the copy and returns the
   selection; redo restores both.
2. Feathered selection: the copy's soft edge composites over what it lands
   on; no cut is left at the source.
3. Repeated stamps: Ctrl-drag, release, Ctrl-drag again; each copy is one
   undo step. A plain drag after a Ctrl-drag moves the floating copy without
   a second copy. A Ctrl-click without moving changes nothing.
4. Shift and Alt pressed after the drag starts: axis snap and precision.
5. Instant Preview on and off, zoomed out: the preview and the final result
   match.
6. Animated layer: a duplicate on a frame without a keyframe creates one
   (auto keyframe), as a move does.
7. Brush Stroke Layer: duplicate strokes wholly inside the selection, then
   scale the image; the copies redraw sharp. A selection cutting a stroke
   leaves the record unchanged and the warning appears after idle.
8. RGBA F32 image with the GPU engine on: duplicate and undo/redo; no stale
   tiles.
9. Ctrl-drag without a selection: still moves the layer group (unchanged).

## Verification status

2026-10-27, branch `claude/mystifying-almeida-29a7c3`, Windows: built out of
tree (see [build pitfalls](wiki/pitfalls/build-format-test.md#worktrees)),
with `libkritaimage`, `libkritaui` and `libkritalibkra` compiled from this
branch, and the test executables run directly (not through CTest):
`MoveSelectionStrokeTest` 5 of 5, `KisBrushStrokeLayerTransformTest` 7 of 7
and `KisBrushStrokeLayerTest` 43 of 43 passed, including the four new cases.
The Move Tool plugin (`kritadefaulttools.dll`) was built but not installed,
and no manual check has been run.
