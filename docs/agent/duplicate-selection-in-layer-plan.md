# Duplicate selection in the same layer — implementation plan

Status: **option A approved by the user (2026-10-27), with the recommended
Ctrl-drag trigger; implementation not started.** No source has been
changed. The remaining [open questions](#open-questions) bound the work.
Unverified statements are marked as such.

User decision (2026-10-27): on a Brush Stroke Layer, a duplicate **copies
the recorded strokes inside the selection** along with the pixels. The
original records stay where they are, and the copies are offset by the drag.

## Request

The selected pixels should be duplicated **on the active layer** and the
copy dragged into place. Photoshop does this with Alt-drag in the Move Tool
while a selection is active. Today, copying a selection makes a new layer,
which then has to be moved and merged back down.

### Forum findings (read 2026-10-27)

The source is the thread
[Feature Request: Duplicate selected (in the same layer)](https://krita-artists.org/t/feature-request-duplicate-selected-in-the-same-layer/11105/41),
2020-08 to 2026-10, merged with an earlier "selection duplicate/stamp with
modifier key" request.

- The workflows are tiles and repeating patterns, concept details, mockups,
  animation frames, and layers whose opacity or blend mode a new layer would
  not inherit (#18).
- In Photoshop the copy stays on the same layer only with a selection, the
  Move Tool and Alt-drag. Otherwise it makes a new layer too (#10, #19). The
  copy can be moved or transformed while the selection is active.
- raghukamath (#19) suggests **Ctrl-drag**, because Alt is the Move Tool's
  precision mode and Ctrl-drag already duplicates layers in the docker.
  novames00 (#30) adds: hold Ctrl and drag again to stamp another copy;
  releasing the button commits each copy.
- Paste into Active Layer (`Ctrl+Shift+V` since 5.3) is the current
  workaround. Rakurri (#34) and huulong (#38) describe the cost: Copy, Paste
  Into, click, then drag, every time, and each paste returns to the original
  position, so repeated copies drift away and a found rotation has to be
  redone.
- squarepixels (#42, 2026-10-07) also wants to drag the selection's pixels
  without switching tools, and reports that on a Fill Layer, pasting gives
  about half opacity, so duplicating on fill layers fails. Both are separate
  follow-ups (see [Open questions](#open-questions)).

## Current implementation (code reading)

- **The Move Tool already floats selected pixels.** With an active selection
  on a `KisPaintLayer`, `KisToolMove::startStrokeImpl()` in
  [kis_tool_move.cc](../../plugins/tools/basictools/kis_tool_move.cc) starts
  a `MoveSelectionStrokeStrategy`
  ([move_selection_stroke_strategy.cpp](../../plugins/tools/basictools/strokes/move_selection_stroke_strategy.cpp)).
  Its `initStrokeCallback()`:
  1. copies the selected pixels into `movedDevice` (`KisPainter::bitBlt` with
     the selection);
  2. cuts them from the layer with `clearSelection()` inside a
     `KisTransaction` (an undo command);
  3. shows `movedDevice` as the layer's **temporary target** through
     `KisIndirectPaintingSupport`
     ([kis_indirect_painting_support.h](../../libs/image/kis_indirect_painting_support.h)),
     composited over the layer without changing it.

  `doStrokeCallback()` moves the device and the selection offset.
  `finishStrokeCallback()` merges it into the layer (`mergeToLayer`) and
  commits a `KisSelectionMoveCommand2`. It also moves recorded strokes of a
  [Brush Stroke Layer](brush-stroke-layer.md) (Solstice hook). The whole
  move is one undo entry ("Move Selection"). `createLodClone()` copies the
  strategy for Instant Preview. Vector selections do not support LoD.
- **The Move Tool keeps its stroke open across drags** (`if (m_strokeId)
  return true;`) until it is committed or the mode changes.
- **Modifiers in the Move Tool**: Shift snaps to an axis and Alt is precision
  mode (both in `applyModifiers()`). Alternate actions go to
  `beginAlternateAction()`, where `SampleFgNode`/`SampleBgImage` toggle
  layer-picking mode and everything else uses `MoveGroup`. The defaults in
  `libs/ui/tool/kis_tool.h` are Ctrl+Alt+Left for SampleFgNode and Ctrl+Left
  for SampleFgImage. With a selection, `startStrokeImpl()` ignores the mode
  and always moves the selection, so **Ctrl-drag currently does the same as
  a plain drag when a selection is active**. (Code reading; Solstice's input
  profile and Quick Access/Lazy Tools key handling have not been checked
  interactively.)
- **The Transform Tool** cuts too:
  `InplaceTransformStrokeStrategy::createCacheAndClearNode()`
  ([inplace_transform_stroke_strategy.cpp](../../plugins/tools/tool_transform2/strokes/inplace_transform_stroke_strategy.cpp))
  copies the selection into a cache device, then clears it from the layer,
  except when the transform has an **external source**.
- **Paste into Active Layer**: `KisPasteIntoActionFactory::run()` in
  [KisPasteActionFactories.cpp](../../libs/ui/actions/KisPasteActionFactories.cpp)
  (action `paste_into` in `krita/kritamenu.action`, `Ctrl+Shift+V`) takes the
  clipboard and deselects the active selection. It then calls
  `KisToolTransform::newActivationWithExternalSource()`, so the clip is
  transformed onto the active layer and the layer itself is not cleared.
- **Copy/Cut Selection to New Layer** are the actions
  `copy_selection_to_new_layer` (`Ctrl+Alt+J`) and
  `cut_selection_to_new_layer` in `krita/krita.action`.
- **GPU engine**: the temporary-target path has GPU-aware code
  (`KisPaintLayer` "CPU-painted temporary target",
  `kis_indirect_painting_support.cpp` syncing GPU tiles to the CPU before a
  CPU merge). Move Selection already uses it with a CPU-painted device, see
  [GPU engine](gpu-engine.md#pixel-brush-integration).

## Options

### A. Duplicate-drag in the Move Tool

Give `MoveSelectionStrokeStrategy` a `duplicate` mode that skips step 2
(the cut). Everything else stays: the copy floats as the temporary target,
follows the cursor, merges on release, and the selection moves to the copy,
as in Photoshop. Dragging again with the modifier stamps the next copy from
the current one.

- Layers changed: the tool plugin only (`plugins/tools/basictools`). That
  means `MoveSelectionStrokeStrategy` (a constructor flag, the copy
  constructor for LoD clones, the undo name "Duplicate Selection", the
  skipped cut and auto-keyframe handling as for a move) and `KisToolMove`
  (decide duplicate at the start of the drag; end an open move stroke first
  so each duplicate is its own stroke and undo entry; an optional "Duplicate"
  checkbox in the tool options for users without a keyboard).
- Trigger: Ctrl-drag with an active selection, the SampleFgImage alternate
  action, which is currently redundant there (see above). Without a
  selection, Ctrl-drag stays as it is.
- .kra / Krita compatibility: no format change. The result is ordinary
  pixels.
- Clipping and inherit alpha: unaffected; the temporary target composites
  inside the layer's own projection.
- Undo/redo: one `KisStrokeStrategyUndoCommandBased` stroke per duplicate,
  holding the merge transaction and the selection move. There is no cut
  command to undo.
- GPU engine: no new GPU code. The path is the same as today's Move
  Selection; add it to the manual check on an RGBA F32 image with the GPU
  engine on.
- Brush Stroke Layer: `finishStrokeCallback()` currently moves recorded
  strokes with the pixels. A duplicate must not do that. It must either copy
  the covered records or leave the copy unrecorded. See
  [Open questions](#open-questions).
- Effort: **small**. Extend `move_selection_stroke_test.cpp` (duplicate, undo,
  redo, LoD clone, repeated stamps) and run the manual checks.

### B. Duplicate in place with the Transform Tool

Add an action, for example "Duplicate Selection in Place", that copies the
selected pixels of the active layer into a device at their own position and
calls `newActivationWithExternalSource()` on the Transform Tool, which is
the same path Paste into Active Layer uses. The user can then move, rotate,
scale or warp the copy before applying it, and nothing is cleared from the
layer.

- Layers changed: `libs/ui` (a paste-into-like action factory next to
  `KisPasteIntoActionFactory`, action definitions in `krita/kritamenu.action`,
  selection manager wiring). Optionally, the Transform Tool, if it should
  keep the selection active (Paste Into deselects it).
- .kra / Krita compatibility: none affected.
- Clipping and inherit alpha: unaffected.
- Undo/redo: the Transform Tool's existing stroke. One entry when applied;
  cancel restores the layer.
- GPU engine: the existing external-source transform path, with GPU
  transforms where supported. Preview and final render must stay identical
  ([Puppet Warp](puppet-warp.md)). The Brush Stroke Layer already disables
  replay for external-source transforms
  ([contract](brush-stroke-layer.md#transform-and-comparison-contract)).
- Effort: **small to medium**. It needs no new tool code, but checks of the
  transform modes, cancel, and repeated use are required.

### C. A general floating selection

A Photoshop/GIMP-style floating selection that survives tool switches until
it is anchored: a new temporary node or state that the Move, Transform,
paint and selection tools all respect.

- Layers changed: `libs/image` (a new floating object, rendering, undo),
  `libs/ui` (selection manager, actions, docker presentation) and every tool
  that must anchor or respect it.
- .kra: anchor on save, or a new persistence contract.
- Effort: **large**, for little gain over A plus B. Not recommended.

## Comparison

| | A. Move Tool duplicate-drag | B. Duplicate in place + Transform | C. Floating selection |
| --- | --- | --- | --- |
| Changed layers | `plugins/tools/basictools` | `libs/ui` actions (+ optional Transform Tool tweak) | `libs/image`, `libs/ui`, many tools |
| Steps per copy | 1 (Ctrl-drag) | 2 (shortcut, drag/transform, apply) | 1-2 |
| Rotate/scale the copy | No (move only) | Yes | Yes |
| .kra / Krita | No change | No change | New contract or anchoring |
| Inherit alpha / clipping | Unaffected | Unaffected | Must be designed |
| Undo/redo | One stroke per copy | Transform stroke | New commands |
| GPU engine | Existing Move Selection path | Existing external-source transform path | New |
| Effort | Small | Small-medium | Large |

## Recommendation

Implement **A**. It matches the most requested gesture (select, then
Ctrl-drag) with one change to an existing, tested stroke strategy. It also
leaves the file format and the GPU path untouched. Add **B** afterwards if
the user wants to rotate or scale copies; it reuses the Paste-Into path.
Do not pursue C.

### Next steps if A is chosen

1. User decisions: the modifier, the Brush Stroke Layer behavior and the
   tool-options toggle (see below).
2. Implement the strategy flag and the tool trigger. Format the changed lines
   with clang-format only ([coding rules](coding-rules.md#formatting-and-diffs)).
3. Tests: `move_selection_stroke_test` cases for duplicate, undo, redo, LoD
   and stamping. Run the existing Move Tool tests.
4. Manual checks: paint layer with a rectangle and a feathered selection;
   repeated stamps; Instant Preview on and off; undo/redo after each stamp;
   an animated layer (auto-keyframe); a Brush Stroke Layer; RGBA F32 with
   the GPU engine on; Ctrl-drag without a selection (unchanged).
5. Write the maintained `docs/agent/duplicate-selection-in-layer.md` and a
   user note (Move Tool section of the user docs). Add inventory rows and the
   AGENTS index entry, and archive this plan.

## Open questions

- Modifier: Ctrl (forum proposal; currently redundant with a selection), Alt
  (Photoshop, but it is the Move Tool's precision mode), or configurable?
  Should precision mode still work while duplicating (Ctrl+Alt is the
  layer-picking toggle)?
- Should the selection move to the copy (Photoshop) or stay on the source?
- Which layers count: only paint layers, as Move Selection today, or also
  Fill/Generator layers and masks (`KisSelectionBasedLayer` and `KisMask`
  implement `KisIndirectPaintingSupport`, but the Move Tool does not use
  selection mode on them; squarepixels' fill-layer report)?
- Keyboard nudge: should the arrow-key move with the modifier also
  duplicate?
- A separate request in #42 is outside this plan: dragging the selection's
  content from painting tools without switching to the Move Tool.
