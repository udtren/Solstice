# Clipping Mask — implementation plan

Status: **option A approved by the user (2026-10-27); the rendering spike
passes its tests (see [Spike](#spike-clip-stack-in-the-projection-leaf-tree-2026-10-27)).
There is no UI or file format support yet.** The decisions recorded below and
the remaining [open questions](#open-questions) bound the work. Unverified
statements are marked as such.

## User decisions (2026-10-27)

- Implement [option A](#a-native-clipping-flag-with-a-clip-stack-in-the-projection-leaf-tree)
  (native clipping flag).
- A clipped layer clips to the **layer or group layer directly below** it.
  Groups may be bases. Assumed, pending confirmation: consecutive clipped
  layers all clip to the same base (the nearest non-clipped layer below),
  as in Photoshop; they do not form a chain where each layer clips to the
  clipped layer under it.
- The Layers docker icon is discussed in [Layers docker presentation](#layers-docker-presentation).

## Request

Photoshop/CSP/SAI-style clipping: a layer marked "clipped" shows only where
the layer directly below it (its *base*) has pixels, without wrapping the
layers in a group. Today Solstice, like Krita, offers inherit alpha and the
Quick Clipping Group (`Ctrl+Shift+G`).

The screenshot the user supplied (a reply in the
[Krita with clipping test build thread](https://krita-artists.org/t/krita-with-clipping-test-build/68119/9))
compares the steps to paint inside a shape called "dress" and bake the result:

| Inherit alpha (today) | Clipping Mask (requested) |
| --- | --- |
| 1. Paint "dress". | 1. Paint "dress". |
| 2. Quick Clipping Group: group + inherit-alpha layer. | 2. Shortcut: new clipped layer. |
| 3. Paint the pattern. | 3. Paint the pattern. |
| 4. Select the parent group by hand. | 4. `Ctrl+E` (Merge Down) bakes the pattern into "dress". |
| 5. Convert the group to a paint layer. | — |
| 6. Rename "Clipping Group N Merged" back to "dress". | — |

The requested version keeps the layer stack shorter, and the original layer
name survives the merge.

### Forum and upstream findings (read 2026-10-27)

- The thread started in 2023-06 with Radian-art's test build. That build
  replaced inherit alpha with clipping; its version 2 (2025-06, based on
  Krita 5.2) kept both. uvhat has continued the work since 2025-08, on Krita
  5.3.x / Qt5 (builds c11-c27; c27 is from 2026-10-23), with
  [diffs on GitHub](https://github.com/uvhat/krita_clipping_layers). Both
  authors call the approach hacky. It composites clipped layers inside
  `KisLayerProjectionPlane::applyImpl()` and adds a cache of the clipping
  stack's projection. The bug reports in the thread come from that design:
  stale caches after reordering or hiding layers, dirty-rectangle artifacts
  with masked brushes and parallel jobs, a race condition on large canvases,
  and broken Merge Down, group merge and Ctrl+click selection on clipped
  stacks. Layer styles, filter layers and fill layers needed separate
  workarounds. In 2026-07 uvhat wrote that the work should be redone
  "through native async merger and walker approach".
- Users want three things: a shorter layer stack, the base name kept on
  merge, and **PSD interoperability**. PSD interoperability is the reason
  given most often. Several users want inherit alpha kept alongside clipping.
  The UI requests are an indent and a marker for clipped layers, a
  red-stripe-style cue, and the ability to clip groups (CSP can; Photoshop
  ignores a clipped group).
- tenzindraws (2026-04) built a visual-only variant. Inherit-alpha groups stay
  in the document; the Layers docker hides the group, and a macro creates it.
  `.kra` files stay fully compatible.
- Upstream: [Merge Request !1813](https://invent.kde.org/graphics/krita/-/merge_requests/1813)
  (Radian-art, a draft, dormant since 2025-06) and
  [issue #104 "Clipping Masks"](https://invent.kde.org/graphics/krita/-/issues/104)
  (opened 2026-04-20). The issue proposes "a system based on a 'hidden
  group', using projection leafs". It links
  [Deif Lou's notes on Photoshop clipping and blending](https://invent.kde.org/deiflou/ps-blending-options-layer-effects/-/wikis/An-exploration-of-Photoshop's-blending-options,-layer-styles-and-clipping-layers#painting-the-layers-image-the-clipped-layers-and-the-inner-effects),
  which should be read before the semantics below are finalized. There was no
  upstream implementation to cherry-pick when this was written. If one
  appears, it goes through [upstream intake](upstream-intake.md).

## Current implementation (code reading)

- **Inherit alpha is a channel flag.** `KisLayer::disableAlphaChannel()` and
  `alphaChannelDisabled()` in [kis_layer.cc](../../libs/image/kis_layer.cc)
  clear the alpha bit of `channelFlags`.
  `KisLayerProjectionPlane::applyImpl()` in
  [kis_layer_projection_plane.cpp](../../libs/image/kis_layer_projection_plane.cpp)
  passes `projectionLeaf()->channelFlags()` to `KisPainter`, so the layer
  composites with alpha locked onto the group projection built so far. That
  projection holds the combined alpha of every lower sibling, not only the
  layer directly below.
- **The property and UI**: `KisLayerPropertiesIcons::inheritAlpha`
  ([kis_layer_properties_icons.h](../../libs/image/kis_layer_properties_icons.h)),
  `KisNodeManager::toggleInheritAlpha()` (action `toggle_layer_inherit_alpha`),
  and the Layers docker icon in
  [NodeDelegate.cpp](../../plugins/dockers/layerdocker/NodeDelegate.cpp).
- **Quick Clipping Group**: `KisNodeManager::createQuickClippingGroup()` in
  [kis_node_manager.cpp](../../libs/ui/kis_node_manager.cpp) (action
  `create_quick_clipping_group` in `krita/krita.action`) uses
  `KisNodeJugglerCompressed` to add a group named "Clipping Group N" and a
  "Mask Layer" with inherit alpha.
- **Merge Down already clips.** `KisLayerManager::mergeLayer()` calls
  `KisLayerUtils::mergeDown()` in
  [kis_layer_utils.cpp](../../libs/image/kis_layer_utils.cpp).
  `KisLayer::createMergedLayerTemplate()` names the result after the lower
  layer. When the upper layer has inherit alpha and the lower one does not,
  `fillMergedLayerTemplate()` paints the upper layer with alpha still
  disabled onto a copy of the lower layer, which bakes it as a clip to the
  lower layer only. `canMergeAndKeepBlendOptions()` compares channel flags,
  so the merged result does not inherit the upper layer's flags. (Code
  reading; not executed.)
- **The projection leaf tree** ([kis_projection_leaf.cpp](../../libs/image/kis_projection_leaf.cpp))
  already differs from the node tree: pass-through groups are flattened out
  (`checkPassThrough`), and selection masks are skipped.
  `KisAsyncMerger::startMerge()` ([kis_async_merger.cpp](../../libs/image/kis_async_merger.cpp))
  walks leaves, composites each one with `compositeWithProjection()` and
  writes the projection on `N_TOPMOST`. The walkers are
  [kis_merge_walker.cc](../../libs/image/kis_merge_walker.cc),
  [kis_base_rects_walker.h](../../libs/image/kis_base_rects_walker.h),
  `kis_refresh_subtree_walker.h` and `kis_full_refresh_walker.h`.
- **.kra** stores channel flags as the `channelflags` layer attribute
  (`CHANNEL_FLAGS` in `plugins/impex/libkra/kis_kra_tags.h`). It is written by
  `KisSaveXmlVisitor::saveLayer()` and read in `kis_kra_loader.cpp`. Krita has
  no clipping attribute.
- **PSD** reads the layer record's `clipping` byte
  ([psd_layer_record.cpp](../../libs/psd/psd_layer_record.cpp)) and ignores
  it on import; export always writes 0 (`nodeClipping` in
  [psd_layer_section.cpp](../../libs/psd/psd_layer_section.cpp)). Clipped
  PSD layers therefore open unclipped today.
- **GPU projection**: `KisGpuMergeBatch::tryAdd()`
  ([KisGpuMergeBatch.cpp](../../libs/image/gpu/KisGpuMergeBatch.cpp)) maps a
  missing alpha channel flag to `alphaLocked`, which `composite_layers.comp`
  honors. Every layer it batches composites onto the one projection
  currently open. See [GPU projection](gpu-engine.md#projection-implemented-in-phase-2).
- Related consumers that read `alphaChannelDisabled()`: ORA import/export,
  `KisConvertColorSpaceProcessingVisitor`, `RemoveNodeHelper::isNodeWeird`
  in `kis_layer_utils.cpp`, and `libkis` `Node::inheritAlpha()`.

## Proposed semantics (for the native option)

These follow Photoshop's default ("Blend Clipped Layers as Group"). Confirm
the details against Deif Lou's notes before implementation.

- A **clipping run** is a maximal sequence of consecutive clipped siblings.
  Its **base** is the nearest non-clipped layer below the run in the same
  parent.
- The run renders as an isolated stack. The base is drawn with Normal blend
  and full opacity, and each clipped layer is composited onto it with its own
  blend mode and opacity, with alpha locked. The result is composited onto
  the parent with the base's blend mode and opacity. The base's transparency
  and filter masks are already part of its projection, so they also clip the
  run.
- Inherit alpha and clipping exclude each other on one layer. Inherit alpha
  keeps its meaning everywhere else. (In Radian-art's MR, turning one on
  turned the other off.)
- A clipped layer with no base (the lowest layer of its parent) renders as an
  ordinary layer. The docker marks it as invalid. This is an open decision.
- A hidden clipped layer is skipped. If the base is hidden, the whole run is
  hidden, as in Photoshop.
- A group layer can be a base (user decision). Its projection is the base
  pixels, as with any other layer.
- Out of scope for the first phase: clipped groups, a pass-through group as
  the base (it has no projection of its own), the base's layer style applied
  to the whole run, and the opposite case of clipped layers carrying their
  own styles. These need decisions (see
  [Open questions](#open-questions)). Until they are decided, the first phase
  must render them in a way that is defined and identical in preview, final
  render and export.

## Layers docker presentation

Proposed in reply to the user's question (2026-10-27) of whether the
inherit-alpha icon should change color when a layer uses clipping instead.
Awaiting confirmation.

- **Share the inherit-alpha slot.** `NodeDelegate::Private::rightmostProperties()`
  gives inherit alpha fixed slot 1, between the lock and alpha-lock icons.
  The two properties exclude each other, so the slot shows one of three
  states: off, inherit alpha, or clipping. Clipping is its own
  `KisLayerPropertiesIcons` property, and the delegate draws whichever of the
  two is on in that slot.
- **Distinguish by shape as well as color.** Clipping gets its own glyph (for
  example the alpha symbol with a down-pointing hook, like the clipping
  arrow in Photoshop and CSP) and an accent color. Color alone is not
  enough. On a selected row, the highlight already tints the row and an
  accent can lose contrast. It also fails for color-blind users. And the
  docker's icons are monochrome theme pairs (`dark_*` / `light_*` SVGs), so a
  color difference alone would be the only thing that tells the two states
  apart.
- **Mark the row, not only the icon.** Indent a clipped row and draw a short
  vertical bar or hook toward its base. Forum users found a red stripe
  quicker to spot than a red arrow icon (posts #40-#41), and it shows which
  layers belong to a run at a glance.
- **Input.** A plain click on the slot keeps toggling inherit alpha, so
  existing habits do not change. Alt+click toggles clipping (Photoshop clips
  with Alt+click between layers). The action, the shortcut and the context
  menu do the same. Shift+click is already the docker's "stasis" modifier;
  check that Alt+click on property icons is otherwise unused.

## Options

### A. Native clipping flag with a clip stack in the projection leaf tree

Add a per-layer `clipped` property next to inherit alpha. In the projection
leaf tree, the base of a run acts like a group whose children are the run.
`KisProjectionLeaf` already makes pass-through groups disappear from that
tree; here the walkers see `base -> [clipped layers]` instead of flat
siblings. The base layer gets a lazily created stack device, analogous to
`KisGroupLayer::lazyDestinationForSubtreeComposition()`. The device holds the
base's projection with the run composited onto it, and the base leaf then
composites the device onto its parent. A clipped leaf reports alpha-disabled
`channelFlags()` inside the stack, so the existing composite path, including
the GPU alpha lock, is reused.

- Layers changed: mostly `libs/image`. That covers `KisLayer` (the property,
  the stack device, copying in the copy constructor), `KisProjectionLeaf`
  (parent/child/sibling mapping, `canHaveChildLayers()`,
  `lazyDestinationForSubtreeComposition()`), the merge, refresh and full-refresh
  walkers (need/change rects of runs), `KisAsyncMerger`, `KisLayerUtils`
  (merge down, merge selected, flatten, removal), `KisLayerPropertiesIcons`
  and `kis_layer_properties_icons`. In `libs/ui`: actions, `KisNodeManager`
  and `KisLayerManager`. The Layers docker (`NodeDelegate`) gets an indent
  and a clipped marker. Saving and loading touch `libkra` and `libs/psd`.
- Rendering follows the Krita design that issue #104 proposes, without
  patching `applyImpl()` the way the test builds do. It is still unverified
  that a layer leaf can expose children without a real group node. Do a
  spike on `kis_projection_leaf_test`, `kis_walkers_test` and
  `kis_async_merger_test` first.
- .kra: a new layer attribute (for example `clipped="1"`), which is a file
  format compatibility contract ([coding rules](coding-rules.md#configuration-and-persistence))
  and needs user approval. Stock Krita ignores the attribute and shows the
  clipped layers unclipped, with no warning. A mitigation is option
  A-compat below.
- PSD: the flag maps one-to-one to the PSD `clipping` byte, for both import
  and export. Among the three options, A gives the most direct and reliable
  PSD mapping (B can only reconstruct groups that keep the exact shape it
  created).
- Inherit alpha: unchanged. The two properties exclude each other on one
  layer.
- Undo/redo: toggling the flag is a node property change and uses
  `KisLayerPropertiesIcons::setNodePropertyAutoUndo()`, like inherit alpha.
  Merges keep using `KisProcessingApplicator` commands. When the flag changes
  or a node moves, the base below must be updated as well as the layer
  itself, because the run's extent changes.
- GPU engine: when the run's leaves composite into the base's stack device,
  they batch exactly like a group's children, as long as the device lies on
  the 64 px grid and uses RGBA F32/F16. Any other case uses the CPU. The
  merger must flush before every reader of the stack device (an invariant in
  [gpu-engine.md](gpu-engine.md#invariants)). Add CPU/GPU parity cases to
  `KisGpuProjectionTest`.
- Effort: **large**. Core walker and merger changes, many layer operations
  that must understand runs (merge down, merge selected, flatten, duplicate,
  delete, move up/down, drag and drop, convert, Ctrl+click alpha selection,
  isolate mode, clone layers, animation), docker UI, .kra and PSD.

**A-compat (persistence variant of A).** Save each run to `.kra` as the
inherit-alpha group that renders the same in stock Krita. The group gets
the base's name, blend mode and opacity; inside it are the base (Normal,
100%) and the clipped layers with inherit alpha. A marker attribute on the
group records the base's original values. On load, Solstice turns a marked
group back into a flat run, and only when its shape is still valid. Files
then open correctly in Krita, but saving and loading become more complex,
and a file edited in Krita may lose the run. The user decides between
A-compat and the plain attribute.

### B. Managed inherit-alpha groups presented as clipping

Keep the image model as it is. "New Clipping Layer" creates or extends a
managed group, marked by a node property, whose first child is the base. The
group carries the base's name, blend mode and opacity. The Layers docker
draws the group as the base row and the clipped layers indented under it,
and hides the group row (tenzindraws' approach). Merge Down on the last
clipped layer bakes it into the base (the existing `fillMergedLayerTemplate()`
path) and dissolves the group, keeping the base's name.

- Layers changed: `libs/ui` (node manager, juggler commands, and
  `KisNodeModel`/dummies filtering for the docker) and
  `plugins/dockers/layerdocker`. Rendering is unchanged.
- .kra: fully Krita-compatible. It is a group with inherit alpha, plus an
  optional marker that Krita ignores.
- PSD: export can turn marked groups into clipped records; import can
  rebuild groups from clipped records. Possible, but it is an extra
  conversion layer.
- Inherit alpha: B is built on it. Inherit alpha inside a managed group
  cannot be told apart from clipping.
- Undo/redo: juggler commands, like the Quick Clipping Group. Every
  operation that can break the group's shape (dragging a layer out, ungroup,
  Python edits, another tool) needs a repair or validation step. This is
  where the risk lies.
- GPU engine: no change. Ordinary groups and inherit alpha are already
  supported.
- Effort: **medium to large**. Most of it is docker and model mapping
  between rows and nodes, and keeping hidden structure consistent. The
  stack is short only on screen; scripts, Layer Comps, isolate mode and
  exports still see the group.

### C. Workflow commands on top of the existing Quick Clipping Group

No new rendering and no docker changes. Two commands:

- **New Clipping Layer**: when the active layer is not yet in a clipping
  group, create the group with the active layer's name (not "Clipping Group
  N") and add an inherit-alpha paint layer above it. When it already is,
  add another inherit-alpha layer to the group.
- **Bake Clipping Group** (or `Ctrl+E` on the group's last inherit-alpha layer,
  if the user agrees to change Merge Down there): merge the group's content
  into the base, remove the group and keep the base's name, blend mode,
  opacity and properties.

- Layers changed: `libs/ui` only (`KisNodeManager` / `KisLayerManager`, new
  actions in `krita/krita.action`, optionally menu entries in
  `plugins/dockers/layerdocker`).
- .kra and Krita compatibility: unchanged. PSD: no improvement.
- Inherit alpha: built on it.
- Undo/redo: one juggler or `KisProcessingApplicator` command per action.
- GPU engine: no change.
- Effort: **small**. This removes steps 4-6 of the comparison but not the
  group row, and does not give PSD interoperability.

### Rejected: compositing clipped layers inside `applyImpl()`

This is the Radian-art/uvhat approach: gather clipped layers, skip them in
the normal walk, and blend them onto the base inside
`KisLayerProjectionPlane::applyImpl()`, with a cache. The forum history
shows its cost: dirty-rectangle and cache bugs, races, and special cases for
styles and filter layers. Its own maintainer wants to move away from it, and
it would conflict with the GPU batch's assumption that every layer composites
onto the open projection. The diffs are useful as test cases and UI
references only. They are GPL code by other authors, so do not copy them; any
upstream code follows the [intake rules](upstream-intake.md).

## Comparison

| | A. Native flag + clip stack | B. Managed groups | C. Commands only |
| --- | --- | --- | --- |
| Changed layers | `libs/image` (leaf, walkers, merger, layer utils), `libs/ui`, layer docker, `libkra`, `libs/psd` | `libs/ui` (model filtering, juggler), layer docker | `libs/ui` actions |
| Layer stack | Flat | Flat on screen; groups in the document | Groups remain |
| Base name kept on bake | Yes (Merge Down) | Yes | Yes |
| .kra / Krita | New attribute (Krita shows unclipped), or the A-compat group form | Identical to Krita | Identical to Krita |
| PSD clipping | Direct import/export | Conversion layer | None |
| Inherit alpha | Coexists, mutually exclusive per layer | Implementation basis | Implementation basis |
| Undo/redo | Property command and merge commands | Juggler commands plus structure repair | Juggler/applicator commands |
| GPU engine | Stack composites batch like group children; parity tests required | None | None |
| Risk | Walker/merger correctness | Hidden structure out of sync | Low |
| Effort | Large | Medium-large | Small |

## Recommendation

Choose **A** as the real feature. Only A delivers everything the thread asks
for: a flat stack, Merge Down baking with the base name kept, and PSD
clipping. It also follows the Krita design proposed in issue #104, which
avoids the failures of the existing test builds.

**C** is small and independent and works today, so it can ship first. It
would remain useful for documents that use groups even after A lands. Do
not choose B: it costs nearly as much as A, cannot give reliable PSD
interoperability, and adds a hidden structure that every layer operation has
to keep valid.

### Next steps if A is chosen

1. User decisions: the remaining open questions below, especially .kra
   persistence (plain attribute or A-compat), which must be settled before
   step 5. The work changes `libs/image`, so it has to be built from the
   main checkout's `_build`: a worktree means a full rebuild
   ([pitfall](wiki/pitfalls/build-format-test.md#worktrees)).
2. Spike in `libs/image`: a base leaf that exposes its run as children, with
   `kis_projection_leaf_test`, `kis_walkers_test` (need/change rects across
   the run) and `kis_async_merger_test` cases. Include opacity, blend modes,
   hidden layers and masks on the base and on clipped layers. Stop and report
   if the leaf model cannot express it without a real group node.
3. Rendering and the property (`KisLayer`, property icons, undo), then layer
   operations (`KisLayerUtils` merge down, merge selected, flatten, removal,
   duplicate, convert), each with tests.
4. UI: the toggle action, New Clipping Layer, docker indent and marker.
   Changes stay out of `krita5.xmlgui` ([decision](wiki/decisions/xmlgui-unchanged.md)).
5. .kra save/load, then PSD import/export with `kis_psd_test` data. The
   thread's sample PSDs from CSP and Photoshop are candidates.
6. GPU parity tests and the manual checklist. Then write the maintained
   `docs/agent/clipping-mask.md`, the user guide, the inventory rows and the
   AGENTS index entry, and archive this plan.

## Spike: clip stack in the projection leaf tree (2026-10-27)

Branch `claude/clipping-mask` (from `main` at `d1c1295675`). This is the
rendering core only: there is no UI, no undo command, no file format support
and no merge handling yet.

- `KisLayer::isClipped()` / `setClipped()` ([kis_layer.h](../../libs/image/kis_layer.h))
  holds the flag. It is copied with the layer and is not yet saved.
- [KisClipStackLayer](../../libs/image/KisClipStackLayer.h) is an internal
  `KisLayer` that a base creates on first use
  (`KisLayer::clipStackLayer()`, under a mutex, because walkers navigate on
  worker threads). It is never added to the node graph. Its original is the
  stack device. It holds the base weakly, the base owns it, and the base's
  `getLodCapableDevices()` and `setImage()` include it.
- [kis_projection_leaf.cpp](../../libs/image/kis_projection_leaf.cpp) maps
  the tree. Wherever the parent's leaves list the base or a clipped layer,
  they list the stack instead. The stack's children are the base followed by
  the run. Inside the stack, the base reports Normal, full opacity and all
  channels. Clipped layers report their own channel flags minus alpha.
  The stack reports the base's blend mode, opacity, channel flags and
  visibility. `KisProjectionLeaf::compositeOpId()` is new;
  `KisLayerProjectionPlane::applyImpl()` and `KisGpuMergeBatch::tryAdd()`
  use it instead of the layer's own blend mode. `isRoot()` is false for a
  stack, even though it has no node parent.
- Runs are adjacent siblings, skipping selection masks only; a mask or a
  fake node between layers ends a run. A clipped layer without a base
  renders unclipped. Pass-through groups take no part (they are never
  clipped and are never bases, and their children are not clipped).
- The walkers and `KisAsyncMerger` are unchanged. Their child-before-parent
  order already composites the stack like a group.

Results (2026-10-27, Windows, `_build`, RGBA8 test images, uncommitted on
the branch):

- The new [KisClippingTest](../../libs/image/tests/KisClippingTest.cpp)
  passes all 11 cases. They cover the leaf tree shape and the stack's
  overrides. The rendering is pixel-identical to the equivalent
  inherit-alpha group, for a full refresh and for incremental updates of a
  clipped layer and of the base. Unclipping restores plain compositing (and
  differs from the clipped result). A hidden base hides the run, a hidden
  clipped layer is skipped, an orphan clipped layer is not clipped, a group
  works as a base, a transparency mask on the base clips the run, and a run
  inside a group works.
- These existing tests pass against the changed library: `kis_node_test`,
  `kis_layer_test`, `kis_group_layer_test`, `kis_paint_layer_test`,
  `kis_image_test`, `kis_layer_style_projection_plane_test`,
  `kis_lod_capable_layer_offset_test`, `kis_projection_leaf_test`,
  `kis_walkers_test`, `kis_async_merger_test`, `kis_update_scheduler_test`,
  `KisGpuProjectionTest`, `kis_clone_layer_test` and
  `kis_transform_mask_test`.
- Not verified: GPU compositing of a clip stack (the test images are
  RGBA8, so they use the CPU), Instant Preview (LoD), layer styles,
  animation, and anything interactive. No UI exists yet. The rebuilt DLL
  was not installed.

Conclusion: the leaf model can express a clip stack without a real group
node in the graph, and without changes to the walkers or the merger. The
spike's stop condition did not trigger.

Known limitations of the spike, to resolve in later phases:

- Structure changes need an explicit `setDirty()` (toggling the flag, or
  moving a layer into or out of a run). The property command will do this
  in the UI phase.
- `KisLayer` rect helpers (`changeRect()`, `layerExtentImpl()`) read the
  node's own blend mode. A base or stack with Copy, Destination In or
  Destination Atop is therefore not handled.
- The stack device is created lazily. If a base gets its first run while an
  Instant Preview (LoD) stroke is running, its device may miss that
  stroke's LoD sync. Unverified.
- A layer style on the base applies only to the base inside the stack.
  The transform tool's temporary hiding of the base also hides its run.
- The image's color space conversion does not visit the stack.
  `syncColorSpace()` runs on `setImage()` only.

## Phase 3: property, Merge Down, `.kra` and docker (2026-10-27)

User decision (2026-10-27): `.kra` stores clipping as a plain layer
attribute, `clipped="1"`, and writes it only on clipped layers. Stock Krita
ignores it and shows those layers unclipped.

- **Property and undo.** `KisLayerPropertiesIcons::clipping` (`"clipping"`) is
  part of `KisLayer::sectionModelProperties()`. `setSectionModelProperties()`
  keeps clipping and inherit alpha exclusive: the one being switched on
  wins, so undo restores the other. `KisNodePropertyListCommand` adds
  `KisLayer::clipRunBounds()` (the layer, the base below it and the adjacent
  clipped layers) to its refresh rect whenever clipping or inherit alpha
  changes, because a clipped layer above can move to another base.
- **Structure changes** need no extra code. Adding, removing and moving
  layers (`KisImageLayerAddCommand`, `KisImageLayerRemoveCommand`,
  `KisImageLayerMoveCommand`, the node juggler) already refresh the
  parent over the image bounds.
- **Merge Down** (`kis_layer.cc`):
  - A clipped layer merged onto its base (`bakesIntoClipBase()`) becomes a
    paint layer with the base's name, blend mode, opacity and inherit
    alpha. Its pixels are painted exactly as the stack renders them, so
    the result looks the same. Clipped layers above it stay clipped and
    clip to the result.
  - Otherwise the merged layer takes `prevLayer->isClipped()`.
    `canMergeAndKeepBlendOptions()` also requires equal clipping. When the
    blend options differ, the members of a stack are painted with their own
    blend options (`applyForMerge()`), not with the stack's overrides.
- **`.kra`**: `CLIPPED` in `kis_kra_tags.h`, written by
  `KisSaveXmlVisitor::saveLayer()` and read by `kis_kra_loader.cpp` and
  `KisSaveXmlVisitor::loadLayerAttributes()`.
- **Layers docker** (`NodeDelegate.cpp`):
  - Clipping takes inherit alpha's slot while it is on, with the
    `clipping-enabled` icon (an orange hook arrow over a base line,
    `krita/pics/svg/{dark,light}_clipping-enabled.svg`).
  - Alt+click on the slot toggles clipping; a plain click on the clipping
    icon turns clipping off.
  - A 2 px orange bar beside the thumbnail marks a clipped row. There is no
    indent yet.
- **Actions** (`krita/krita.action`, no default shortcut):
  `toggle_layer_clipping` (`KisNodeManager::toggleClipping()`, in the
  docker's Toggle Locks & Visibility menu) and `new_clipping_layer`
  (`KisNodeManager::createClippingLayer()` →
  `KisLayerManager::addClippingLayer()`, in the Group menu).
- User guide: [Clipping Mask](../clipping-mask.md).

Results (2026-10-27, Windows, `_build`):

- `KisClippingTest` passes all 14 cases. Beyond the spike's 11, they check
  that the property excludes inherit alpha (both directions, with undo), that
  a clipping change refreshes the run outside the toggled layer's own
  bounds, that Merge Down onto the base keeps the image pixel-identical
  (the base's name and blend options survive, and the next clipped layer
  moves to the result), and that two clipped layers merge into one clipped
  layer. The test now has its own `kis_add_test()` entry, because
  `KisSurrogateUndoStore` is in `kritacommand`.
- `KisClippingKraTest` (new, `plugins/impex/libkra/tests`) passes: the
  `clipped` attribute round-trips through `.kra`, and the loaded image
  renders identically.
- `KisGpuProjectionTest::testClipStackMatchesCpu` (new) passes. An RGBA F32
  clip stack composited on the GPU matches the CPU within 2e-5, and the GPU
  path was used.
- The 13 other `libs/image` tests listed under the spike results, and
  `KisGpuProjectionTest` as a whole, still pass.
- Not run: `libs/ui` tests and `kis_kra_saver_test`. On this desktop they
  open blocking dialogs.

- Crash fix (2026-10-27): saving a `.kra` with a clipped layer crashed in
  `~KisImage`, because the clip stack took a strong reference to an image
  whose copy constructor was still running. It now reads the color space
  through the weak pointer; see the
  [pitfall](wiki/pitfalls/strong-image-ref-during-copy.md).
  `testImageCopyKeepsClipping` covers the copy.
- User check (2026-10-27): after the fix the user tested Clipping Mask in
  the installed build, including saving, and reported it OK. Which of the
  manual checks that covered is not recorded.

Not done in this phase: PSD import/export, Merge Selected / Flatten with
clipped layers, the indent of clipped rows, the `zh_CN` catalog entries for
the new strings, `libkis`, and an interactive check in Solstice.

## Open questions

- .kra: decided, a plain attribute `clipped` (2026-10-27). Open: what
  happens if upstream later defines its own attribute?
- Shortcut and action names: `Ctrl+Alt+G` is Photoshop's clipping binding,
  but Krita already uses it for `quick_ungroup` (`krita/krita.action`).
  Leave the new action unbound, or take another key? Confirm the
  [docker presentation](#layers-docker-presentation).
- Consecutive clipped layers: confirm that they share one base (Photoshop),
  rather than each clipping to the clipped layer below it.
- Clipped groups (CSP allows them; Photoshop ignores them in PSD), and a
  pass-through group as the base.
- Layer styles: should the base's style apply to the whole run
  (uvhat c17) or only to the base, and what about styles on clipped layers?
- A clipped layer without a base: render it unclipped, hidden, or forbid it?
- Merge Down on the **base** while clips sit above it: keep the clips on the
  merged result (Photoshop) or bake them too? Merge Selected that includes
  only part of a run.
- Should opening a PSD convert clipped records into clipped layers silently
  (A), and should PSD export convert existing inherit-alpha groups into
  clipping where they are equivalent?
- `libkis` `Node.clipped()` / `setClipped()`: scripting additions are avoided
  unless requested ([codebase map](codebase-map.md)).
- Layer Comps, Brush Stroke Layer and Clone Layer interactions need a
  read-through of their documents ([layer-comps.md](layer-comps.md),
  [brush-stroke-layer.md](brush-stroke-layer.md), [clone-layer.md](clone-layer.md))
  before phase 3.
