# Puppet Warp — agent development notes

Read the user-facing guide at [`../puppet-warp.md`](../puppet-warp.md) before
changing user-visible behavior.

## Status and architecture

Puppet Warp is a native mode of Krita's Transform Tool under
`plugins/tools/tool_transform2/`. The current implementation reuses Krita's
rigid Moving Least Squares (MLS) warp worker. It is not a true
triangle-mesh/ARAP puppet solver.

| Area | Primary files | Responsibility |
| --- | --- | --- |
| Mode ownership | `kis_tool_transform.cc`, `kis_tool_transform.h` | Creates and selects the Puppet strategy and supplies its preview image. |
| Options UI | `kis_tool_transform_config_widget.cpp`, `kis_tool_transform_config_widget.h`, `wdg_tool_transform.ui` | Mode button, mesh visibility, expansion, and reset. |
| Interaction and preview | `kis_warp_transform_strategy.cpp`, `kis_warp_transform_strategy.h` | Hit-testing, gestures, cursors, mask generation, overlay, and preview deformation. |
| Persistent state | `tool_transform_args.cc`, `tool_transform_args.h` | Mode, pins, rotations, settings, identity/equality, XML, and effective MLS controls. |
| Final rendering | `kis_transform_utils.cpp` | Final device transforms and approximate need/change rectangles. |
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
- `m_puppetExpansion`: expansion in image pixels.

`setPoints()` keeps the rotation vector aligned with the pin vectors.
`removePuppetPoint()` removes all values at the same index. A Puppet transform
is non-identity when any pin moved or any rotation is nonzero.

The existing `warp_transform` XML element adds `rotations`, `showMesh`, and
`expansion`. Missing rotations load as zero after resizing to the pin count.
Mesh visibility and expansion default from the `KisToolTransform` KConfig group
keys `puppetShowMesh` and `puppetExpansion`.

## Mesh mask and overlay

The visible mesh is a preview overlay, not solver topology.

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

The implementation converts visible pins into hidden controls passed to
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

Use identical expanded controls in:

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
- Alt-click pin centers for `DELETE_POINT`.
- Center hits take priority over ring hits.
- Disable inherited empty-canvas global move, rotate, and scale gestures in
  Puppet mode.
- `Show mesh` affects visualization only, never pixels or identity.

## Known limitations

1. The solver remains global rigid MLS, without per-triangle rigidity energy or
   iterative ARAP solving.
2. The displayed grid is not editable solver topology.
3. Euclidean MST topology may connect anatomically unrelated overlapping limbs.
4. Terminal guide distances are heuristic and may over-propagate.
5. There are no explicit fixed/movable/rotation-disabled/weighted pin types.
6. There is no ordering/depth control for folded artwork.
7. Opaque backgrounds mask the entire rectangle.
8. Full-resolution mask construction is synchronous, although cached.
9. Rotation rings can become visually crowded.

## Recommended improvement path

1. Generate silhouette triangulation and infer pin connections by geodesic
   distance rather than Euclidean distance.
2. Add a constrained triangular ARAP or comparable rigidity-preserving solver,
   making visible mesh and solver topology identical.
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
- A terminal hip/shoulder rotates the unpinned branch beyond it.
- Adding another pin limits previous terminal propagation.
- Alt-click removes exactly one pin and keeps rotation indices aligned.
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
- Missing serialized fields retain safe backwards-compatible defaults.
- Do not infer interaction correctness from compilation alone; verify
  hit-testing, cursor mode, and press/move/release symmetry interactively.
