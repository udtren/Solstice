# Puppet Warp — agent development notes

Read the user-facing guide at [`../puppet-warp.md`](../puppet-warp.md) before
changing user-visible behavior.

## Status and architecture

Puppet Warp is a native mode of Krita's Transform Tool under
`plugins/tools/tool_transform2/`. Since 2026-10-06 (GPU engine phase 4.95,
user request) it deforms a triangle mesh of the artwork with an
as-rigid-as-possible (ARAP) solver, `libs/image/KisPuppetTransformWorker.*`,
modelled on Clip Studio Paint. Transforms saved before the change carry no
mesh and keep the legacy rigid Moving Least Squares (MLS) path described under
"Legacy MLS model".

| Area | Primary files | Responsibility |
| --- | --- | --- |
| Mode ownership | `kis_tool_transform.cc`, `kis_tool_transform.h` | Creates and selects the Puppet strategy and supplies its preview image. |
| Options UI | `kis_tool_transform_config_widget.cpp`, `kis_tool_transform_config_widget.h`, `wdg_tool_transform.ui` | Mode button, mesh visibility, expansion, and reset. |
| Interaction and preview | `kis_warp_transform_strategy.cpp`, `kis_warp_transform_strategy.h` | Hit-testing, gestures, cursors, mask generation, overlay, and preview deformation. |
| Persistent state | `tool_transform_args.cc`, `tool_transform_args.h` | Mode, pins, rotations, settings, identity/equality, XML, and effective MLS controls. |
| Final rendering | `kis_transform_utils.cpp` | Final device transforms and approximate need/change rectangles. |
| Mesh and solver | `libs/image/KisPuppetTransformWorker.*` | Mesh build/serialization, ARAP solve, point mapping, device and QImage rendering. |
| Regression coverage | `tests/test_animated_transform_parameters.cpp`, `tests/test_animated_transform_parameters.h` | Serialization, rotation identity, and generated constraints. |

`wdg_tool_transform_ui.py` is generated output. Keep it synchronized with
`wdg_tool_transform.ui`; never treat it as the UI design source.

## Data model and persistence

`ToolTransformArgs::PUPPET` is appended after the older transform modes so their
numeric values remain unchanged.

State consists of:

- `m_origPoints`: original pin positions;
- `m_transfPoints`: transformed pin positions;
- `m_puppetRotations`: radians per pin;
- `m_puppetShowMesh`: overlay visibility;
- `m_puppetExpansion`: expansion in image pixels;
- `m_puppetMesh`: the solver mesh (`KisPuppetTransformWorker::Mesh`), empty for
  legacy transforms;
- `m_puppetOrders`: stacking order per pin (higher on top), index-aligned like
  the rotations;
- `m_puppetClickAction`: what a click on a pin does (Select or Delete). This is
  a tool preference stored in the `KisToolTransform` KConfig group key
  `puppetClickAction`. It is copied with the arguments but not compared,
  serialized or part of identity.

`setPoints()` keeps the rotation and order vectors aligned with the pin
vectors. `removePuppetPoint()` removes all values at the same index.
`changePuppetOrder(indexes, change)` implements Order:
- To front: just above every other pin.
- Forward: +1.
- Backward: -1.
- To back: just below every other pin. A Puppet transform
is non-identity when any pin moved or any rotation is nonzero.

The existing `warp_transform` XML element adds `rotations`, `showMesh`,
`expansion` and `mesh`. Missing rotations load as zero after resizing to the
pin count. `mesh` is `Mesh::toString()`: origin, column step, row step (17
significant digits), columns, rows, the expansion the mesh was built with, and
the solid-triangle bits in hex. `orders` is saved for Puppet transforms;
missing orders load as 0. A missing or invalid `mesh` leaves the
transform on the legacy MLS path, so older documents render as before.
Mesh visibility and expansion default from the `KisToolTransform` KConfig group
keys `puppetShowMesh` and `puppetExpansion`.

## Mesh mask and overlay

With a mesh, the overlay draws the solver mesh: its grid lines and alternating
diagonals, sampled at source-pixel resolution and kept only where the mask
covers them, then mapped through the solved deformation. Legacy transforms
draw the old display grid, which is not solver topology.

**Mask source.** The mask is made from a thumbnail of the original pixels
(at most 2000 px):

- with the Fast (overlay) preview, the strategy's thumbnail from
  `TransformStrokeStrategy::sigPreviewDeviceReady`;
- with the Accurate previews (in-place stroke, `InplaceTransformStrokeStrategy`)
  there is no thumbnail. Since 2026-10-07 the in-place stroke emits
  `sigPreviewDeviceReady` with a copy-on-write copy of its node caches (merged
  for several nodes) after creating them. `KisToolTransform` keeps it and, in
  Puppet mode, makes a thumbnail with `createThumbnail()` and passes it to
  `KisWarpTransformStrategy::setPuppetMaskSource()`. It is used only for the
  mask, never drawn (the strategies would draw a thumbnail as an overlay at
  0.9 opacity over the in-place result). When the device arrives in Puppet
  mode, the tool rebuilds the mesh and commits the arguments.
- Before this, Accurate previews never had a mesh: Puppet Warp fell back to
  the legacy MLS model without a mesh overlay, and with no pins the layer
  disappeared (next point).
- `KisTransformUtils::transformDevice()`: a warp or legacy Puppet transform
  without control points now copies the source (as
  `KisWarpTransformWorker::transformQImage()` does for the preview). The
  in-place stroke clears the layer first, and the warp worker writes nothing
  without points.

`KisWarpTransformStrategy::Private::updatePuppetMask()`:

1. Treats source pixels with alpha greater than 8 as artwork.
2. Flood-fills four-connected exterior transparency from the image boundary.
3. Treats unreachable transparent areas as enclosed artwork, filling closed
   line-art interiors.
4. Converts expansion from image to source coordinates and performs horizontal
   and vertical linear dilation passes.
5. Caches by source `QImage::cacheKey()`, original bounds, and expansion.

At 0 px, clip the overlay at source-pixel resolution. Never return to the old
behavior of including an entire coarse cell when it touches artwork.

The grid targets roughly 32 image pixels per cell, clamped to 4–48 rows and
columns. Horizontal, vertical, and alternating diagonal edges are sampled at
source-pixel resolution. Only masked samples enter the painter path, and every
retained point uses the same effective Puppet constraints as the image preview.

Expected edge cases:

- Open line art does not enclose transparent interiors.
- A fully opaque layer covers its whole bounds.
- Disconnected opaque components share one deformation field.
- Expansion is currently rectangular/separable, not a Euclidean-radius
  dilation.

## Deformation model

### Mesh (phase 4.95)

`KisPuppetTransformWorker::Mesh::build(mask, bounds, expansion)`:
- covers the transaction's original rect with a regular grid of about 24
  image pixels per cell, clamped to 4-64 columns and rows;
- splits each cell into two triangles along alternating diagonals, matching
  the overlay;
- marks a triangle solid when a mask pixel center lies inside it.

The grid is stored as origin plus column and row steps, so
`transformSrcAndDst()` and `scale3dSrcAndDst()` map it exactly (translations,
level-of-detail scaling, any affine map).

`KisWarpTransformStrategy::Private::ensurePuppetMesh()` builds the mesh from
the preview mask at the start of `recalculateTransformations()`. It runs only
when the arguments have no mesh or the expansion changed, and stores the mesh
in the arguments. The preview thumbnail is limited to 2000 px, so rebuilding
the mask from the full-resolution device on apply could differ; sharing the
stored mesh keeps preview, overlay, final rendering and bounds identical.

### ARAP solver

`KisPuppetTransformWorker` solves all grid vertices.

- **Energy:** per-triangle ARAP with cotangent edge weights. Triangles without
  artwork get stiffness 1e-3, so limbs separated by empty space are
  effectively independent while every point keeps a continuous mapping.
- **Pins:** each pin adds its center and four points at radius 1.0 x cell
  size as constraints (barycentric in their triangles, weight 1e4), rotated by
  the pin rotation. The radius scales with the mesh, so the reduced-detail
  preview and the full-resolution result agree; the legacy 8-64 px clamp did
  not scale.
- **Hinge rule** (user report 2026-10-07: rotating a joint pin twisted the
  neighbour's side and left a gap):
  - Neighbouring pins are pins whose owned parts share a solid triangle.
  - For a rotated pin, the four rigid points that point toward a neighbour
    (cosine > 0.5) are left unconstrained, and initial-guess offsets in that
    cone are translated, not rotated.
  - The pin's rotation therefore turns only the side beyond it. In the unit
    test, a 90-degree elbow rotation now gives a 4.5 px forearm deviation
    (7.2 px before) and turns the forearm 90.6 degrees.
  - Constraining those points unrotated instead tore the joint (worse).
- **Soft joints were tried and dropped.** Lower stiffness within 1.5-3 cells of
  a pin left the fold gap at 430-1166 px.
- **Remaining limitation:** folding a joint far still pushes the artwork
  around the joint aside and leaves a gap (558 px in the upper arm at 137
  degrees in `testFoldedElbowGap`). See "Open issue: folded joints" below.
- **Initial guess:** every vertex starts with the rigid motion of the pin
  nearest to it along the mesh. That is a multi-source Dijkstra over triangle
  edges, with empty edges costing 50x. A rotated pin therefore already turns
  the free part beyond it.
  - Starting from the smooth MLS field needed hundreds of iterations to carry
    a 90-degree rotation through a narrow joint: forearm deviations of 126 px
    after 40 iterations, 12 px after 400.
- **Iterations:** 40 local/global iterations. The local step fits each
  triangle's rotation; the global step is one `SimplicialLDLT`
  factorization reused for x and y.
- The result is deterministic for the same mesh and pins.
- `map()` evaluates the piecewise-affine mesh: O(1) cell lookup, barycentric
  weights, and extrapolation from the border cells outside the grid.
- `run()` renders like `KisWarpTransformWorker::run()`: it clears the
  destination, then `processGrid` at 8 px precision.
- **GPU rendering (phase 4.96).** With the GPU engine and RGBA float devices,
  `run()` records the same cells with `KisGpuGridWarpWorker::Recorder` (the
  Liquify recorder) and paints them on the GPU, bit-identical per group.
  Several groups are recorded in one grid pass (`GroupRecordersOp`) and still
  composited with "over" on the CPU. Group layers take the destination's
  offset. `KRITA_GPU_PUPPET=0` keeps it on the CPU. Details and the parity
  rule: `docs/agent/wiki/history/gpu-phases-4.93-.md`, phase 4.96.
- `runOnQImage()` renders the preview in thumbnail space through the
  strategy's image/thumbnail maps. `approxChangeRect()` maps the rect.
- `KisTransformUtils::needRect()` returns the source bounds; `changeRect()`
  returns the mapped rect united with the source rect.

**Rendering groups and pin order.**
- Each part of the artwork belongs to the pin nearest to it along the artwork
  (the Dijkstra owner kept as `m_owner`). `ownerAt()` and `orderAt()` use the
  mesh vertex with the largest barycentric weight.
- **Only cells that touch artwork are rendered** (`touchesArtwork()`: a corner
  or the center lies in a solid triangle). Empty space squeezed by a bend
  rendered transparent cells over folded artwork, because the grid polygon ops
  overwrite pixels. In the reported case (a forearm bent onto the hip), these
  cells cut jagged white holes.
- **Each owning pin renders into its own layer** (`stackingGroups()`). The
  layers composite bottom to top with `COMPOSITE_OVER` (QPainter source-over
  for the preview).
  - Group order: unowned parts, then by order, then by pin index; with equal
    orders, later pins are on top.
  - The first fix rendered per order level, but parts of different pins in
    one level still erased each other with transparent edge pixels: 410
    transparent pixels inside the torso in the test, 583 without the
    artwork-cell filter. Per-pin groups give 0.
- With a single group, rendering is one pass.
- Orders do not affect the solve.

Timing: a 17x14 grid solves in about 1 ms, the largest 64x64 grid in about
16 ms. The strategy caches the solved worker between repaints, keyed by mesh,
pins and rotations.

### Legacy MLS model

Transforms without a mesh convert visible pins into hidden controls passed to
`KisWarpTransformWorker` in rigid mode.

### Local rigid constraints

Each pin produces five controls: its center and four axial auxiliary points.
The auxiliary radius is 28% of nearest-pin distance, clamped to 8–64 image
pixels. Moving translates all five; rotating rotates the four offsets around
the transformed center. Untouched pins therefore hold rigid neighborhoods
instead of acting as dimensionless points.

### Terminal-branch propagation

`puppetControlPoints()` builds a Euclidean minimum spanning tree over original
pin positions. A degree-one pin is terminal. Three hidden guides extend away
from its only neighbor at 0.75, 1.5, and 3.0 times the neighbor distance.
Moving or rotating the terminal pin transforms these guides, causing the
unpinned region beyond it to follow. Adding a pin changes the graph and limits
or splits propagation.

Use identical expanded controls (legacy) or the identical stored mesh and
pins (mesh model) in:

- interactive image preview;
- mesh-overlay deformation;
- final paint-device rendering;
- approximate need/change rectangles.

Never fix only one path; terminal rotations make preview/final disagreement
especially obvious.

## Multi-layer and group processing

Puppet Warp inherits Transform Tool multi-node processing. Multiple selected
layers or a selected group are combined for preview and mesh calculation. On
apply, the same transform is sent to each eligible processed node, preserving
layer and group structure rather than flattening it.

Keep preview bounds and final per-node application aligned. Locked, hidden,
non-editable, and unsupported nodes may be filtered by shared transform logic.
Add Puppet-specific multi-layer regression coverage before changing this path.

## Interaction invariants

- Place pins in Draw mode, then lock before manipulation.
- Drag centers to move pins.
- Hit-test the locked pin annulus for `ROTATE_PIN`; accumulate the incremental
  angle between previous and current mouse vectors about the pin center.
- **Deleting pins.** Alt-click pin centers for `DELETE_POINT`. The "Click
  pin" option set to Delete pin makes a plain click delete as well, in Draw
  and locked modes.
- **Hit priority.** Center hits take priority over ring hits.
- **Selection** (locked mode, `pointsInAction`):
  - Shift- or Ctrl-click toggles a pin (`MULTIPLE_POINT_SELECTION`).
  - Dragging on empty canvas selects the pins inside a rectangle
    (`RUBBER_BAND`); with Shift held at press, it adds to the selection.
  - A plain click on empty canvas clears the selection.
  - The rubber band only repaints; it does not recalculate.
- **Moving and rotating a selection.** Dragging a pin of a multiple selection
  moves all selected pins by the same delta. Rotating the ring of a selected
  pin rotates every selected pin's rotation by the angle and orbits the other
  selected pins about that pin.
- **Order buttons.** They act on the selection
  (`KisWarpTransformStrategy::changePuppetOrder()` via
  `KisToolTransformConfigWidget::sigPuppetOrderChange` and
  `KisToolTransform::slotPuppetOrderChange`, which commits an undo step).
- **Options row.** The "Click pin" combo and the Order buttons are built in
  code under the Puppet options row; `wdg_tool_transform.ui` and its
  generated `.py` are unchanged.
- **Disabled gestures.** Disable inherited empty-canvas global move, rotate,
  and scale gestures in Puppet mode (empty-canvas drags select pins instead).
- `Show mesh` affects visualization only, never pixels or identity.

## Open issue: folded joints

Status (2026-10-07): unsolved; the user decided to leave Puppet Warp as
committed in `749ec6cbbd` for now.

**Symptom.** Rotating a joint pin far (about 90 degrees or more) pushes the
artwork around the joint outward and opens a gap on the neighbour's side
(user screenshots of an elbow folded onto the hip). The turned part and its
neighbour share the joint's mesh vertices, so the connected mesh must stretch
or squeeze instead of letting the parts overlap.

**Attempt: hinge with a mesh cut (implemented, then reverted at the user's
request).**
- Split a rotated pin's part along a line through the pin: the side toward
  neighbouring pins was reassigned to the neighbour, the rest turned.
  Vertices on the split got one solver variable per side, so the sides
  overlapped and Order decided the top.
- Each side's pin constraint had to use a triangle of its own side
  (barycentric extrapolation). Using the containing triangle mixed in the
  other side's vertices and bent the neighbour by 8-12 px.
- The outer side of the bend opens like a wedge. A turned disc around the pin
  (0.5-1.5 cells) did not reach the edge of a wide (100 px) arm. Sweeping the
  joint's cross section along the outer split ray through the rotation
  angle filled it with a rounded joint in synthetic tests.
- The split followed mesh triangles (24 px), leaving steps and rotated teeth
  along the cut. An exact split line near the pin, plus 2 px rendering cells
  around the hinge, fixed that in tests (coarse cells had to overlap the fine
  region by one cell to avoid one-pixel T-junction cracks).
- A 60-degree cone toward the neighbour cut an L-shaped elbow badly; a
  bisector between the neighbour's direction and the turning part's
  direction (a mitre) was better.
- The mesh overlay had to break its lines where the side changes.
- Synthetic tests passed (no gap, exact rotation, no holes in a wide joint),
  but on the user's artwork (pins at the shoulder and the elbow, forearm
  folded across the body) the joint still showed a ragged notch and stray
  pieces on the outer side, and the overlay scattered. Likely causes not
  covered by the tests: rounded or irregular joint silhouettes, the hand
  touching the torso (the hip pin then counts as a neighbour), and one
  fixed split line for a joint that should bend over an area.

**Also tried and dropped earlier:** soft joints (lower stiffness within
1.5-3 cells of a pin; gap 430-1166 px), constraining the neighbour-facing
rigid points unrotated (tore the joint).

**If this is resumed:**
- Build the regression image from the user's real artwork (a rounded elbow,
  forearm folded across the body, hand touching the hip) before changing the
  solver; the synthetic rectangle tests were not predictive.
- Consider what Clip Studio Paint appears to do (parts slide under each other
  with a soft blend at the joint) rather than a hard cut, or a cut limited to
  the inner side of the bend with the outer side left connected and
  stretched.
- Decide how touching but separate parts (hand on hip) should count as
  neighbours.

## Known limitations

1. The mesh is a regular grid (about 24 px cells, at most 64x64), not a
   contour-following triangulation. Gaps narrower than a cell can couple
   neighbouring limbs.
2. The displayed mesh is not editable.
3. A joint bends over about one cell around the pin, so a rotated free part
   turns about the pin but may shift by a few pixels. In the unit test a
   90-degree elbow rotation turns the forearm by 90.6 degrees with a 4.5 px
   deviation.
3a. Folding a joint far pushes the surrounding artwork aside and leaves a gap
   instead of overlapping the parts (see "Open issue: folded joints").
3b. Neighbouring pins are pins whose parts touch in the artwork, so a hand
   touching the hip makes the hip pin a neighbour of the elbow and changes
   which side of the elbow turns.
4. Legacy transforms (no stored mesh) keep the MLS model with its Euclidean MST
   topology and heuristic terminal guides.
5. There are no explicit fixed/movable/rotation-disabled/weighted pin types.
6. Order is per owning pin: one part cannot be split across orders, and the
   part a pixel belongs to follows the geodesic nearest pin, which can differ
   from the intended limb near a joint.
7. Opaque backgrounds mask the entire rectangle.
8. Full-resolution mask construction is synchronous, although cached.
9. Rotation rings can become visually crowded.

## Recommended improvement path

1. Done in phase 4.95 for the mesh model: triangular ARAP with geodesic
   pin ownership; visible mesh and solver topology are identical. Next: a
   contour-following triangulation and a density setting.
2. Done in GPU engine phase 4.96: the final rendering of the mesh warp on the
   GPU. Next, if needed: composite the group layers on the GPU, and a GPU
   preview.
2a. Folded joints that overlap instead of pushing artwork aside (open issue
   above).
3. Add explicit pin type, strength, rotation correction, and influence radius
   while preserving backwards-compatible serialized defaults.
4. Improve masks with Euclidean expansion, configurable alpha threshold,
   selection-aware boundaries, hole policy, and paint-device mask reuse.
5. Improve selected/hovered ring display, cursor feedback, numeric rotation,
   multi-pin transforms, snapping, and undo granularity.
6. Expand automated coverage with pin-array alignment, old XML, rotation-only
   identity, graph topology, terminal propagation, preview/final equivalence,
   mask cases, golden images, UI gestures, and multi-layer/group transforms.

## Build, test, and install

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritatooltransform test_animated_transform_parameters -j 2"
```

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && <krita-dev-root>\_build\bin\test_animated_transform_parameters.exe testPuppetTransformSerialization"
```

`KisPuppetTransformWorkerTest` (`libs/image/tests`):
- mesh marking and serialization;
- identity;
- pin order stacking of an overlapping forearm (front, back, equal), with no
  transparent pixels left inside the torso;
- a rotated elbow turning a free forearm rigidly;
- a moved pin carrying the free end;
- scale consistency for the level-of-detail preview;
- device rendering;
- (in `KisGpuPaintDeviceTest`) `testGpuPuppetMatchesCpu`: the GPU rendering
  against the CPU;
- 64x64 solve time.

`test_animated_transform_parameters` loads the installed tool plugin: install
`libs/image` and `tool_transform2` before running it after a
`ToolTransformArgs` layout change, or it crashes in
`KisSimpleModifyTransformMaskCommand`.

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\tools\tool_transform2\cmake_install.cmake
```

Krita must be closed before installation because Windows locks the plugin DLL.
Restart after installing. Format modified C++ and run `git diff --check`.

## Manual regression checklist

- Mode switching works.
- Draw/lock placement works.
- Center drag is local and untouched pins anchor their areas.
- Ring drag rotates about the correct pin.
- Rotating an elbow/shoulder/hip pin turns the unpinned part beyond it as one
  rigid piece (no twisting or smearing), while other pins hold.
- A terminal hip/shoulder rotates the unpinned branch beyond it.
- Adding another pin limits previous terminal propagation.
- Alt-click removes exactly one pin and keeps rotation and order indices aligned.
- "Click pin: Delete pin" deletes with a plain click; "Select pin" restores selection.
- Shift-click and rectangle selection select several pins; dragging one moves all; ring rotation turns them together.
- Order buttons change which overlapping part is on top, in the preview and the applied result.
- The 0 px mesh follows the silhouette without cell protrusion.
- Expansion changes the boundary correctly.
- Closed line art fills while open contours remain open.
- Show mesh does not change output.
- Apply matches preview on single layers, multiple layers, and groups.
- Reset restores source and state.
- Saved state restores positions, rotations, visibility, and expansion.

## Required invariants

- Keep the enum appended after existing modes unless migrating the file format.
- Keep original points, transformed points, and rotations index-aligned.
- Use one effective-control generator across every deformation and bounds path.
- Build the mesh once and share it through `ToolTransformArgs`; never rebuild it
  separately for the final rendering.
- Keep transforms without a mesh on the legacy MLS path.
- Missing serialized fields retain safe backwards-compatible defaults.
- Both preview styles (Fast and Accurate) must build the mesh from the same
  kind of thumbnail; the Accurate mask source is never drawn.
- Do not infer interaction correctness from compilation alone; verify
  hit-testing, cursor mode, and press/move/release symmetry interactively.
