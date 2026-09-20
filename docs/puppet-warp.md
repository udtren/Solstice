# Puppet Warp development notes

## Status and intent

Puppet Warp is a native mode of Krita's Transform Tool in
`plugins/tools/tool_transform2`. It is intended for posing a raster drawing by
placing pins, then moving or rotating those pins while the remaining pins hold
other parts of the drawing in place.

The current implementation is usable and has been tested interactively with a
full-body character. It deliberately reuses Krita's existing rigid Moving Least
Squares (MLS) warp worker. It is not yet a true triangle-mesh/ARAP puppet solver.
That distinction is important when evaluating deformation quality or planning
future work.

The original behavior reference is Clip Studio Paint's Puppet Warp feature:
<https://tips.clip-studio.com/en-us/articles/10496>.

## Current user workflow

1. Select the layer or selection to transform.
2. Activate the Transform Tool and choose **Puppet**.
3. While **Draw** is active, click the artwork to place pins.
4. Click **Lock Points** to leave pin-placement mode.
5. Drag the center of a pin to move that pin.
6. Drag the outer circular handle to rotate around that pin.
7. Alt-click a pin center to delete it.
8. Use **Show mesh** to toggle the overlay and **Expansion** to extend the
   generated artwork mask from 0 to 64 pixels.
9. Apply or reset the transform using the normal Transform Tool controls.

An untouched pin is an anchor; it is not necessary to select or explicitly lock
each anchor. Moving a pin translates its local rigid neighborhood. Rotating a
terminal pin also rotates the unpinned branch beyond it. For example, if the
highest pin is at the hip and all other pins are below it, rotating the hip pin
affects the upper body. Adding a pin above the hip creates a new boundary and
limits that propagation.

## Source map

| Area | Primary files | Responsibility |
| --- | --- | --- |
| Mode ownership | `kis_tool_transform.cc`, `kis_tool_transform.h` | Creates the Puppet strategy, selects it for Puppet mode, and supplies its preview image. |
| Options UI | `kis_tool_transform_config_widget.cpp`, `kis_tool_transform_config_widget.h`, `wdg_tool_transform.ui` | Puppet mode button, mesh visibility, expansion, and mode reset. |
| Interaction and preview | `kis_warp_transform_strategy.cpp`, `kis_warp_transform_strategy.h` | Pin hit-testing, move/delete/rotate gestures, cursor feedback, mesh-mask generation, overlay rendering, and preview deformation. |
| Persistent state | `tool_transform_args.cc`, `tool_transform_args.h` | Mode enum, pins, pin rotations, settings, identity/equality behavior, XML serialization, and generation of effective MLS constraints. |
| Final rendering | `kis_transform_utils.cpp` | Uses the same expanded Puppet constraints for device transforms and approximate need/change rectangles. |
| Regression coverage | `tests/test_animated_transform_parameters.cpp`, `tests/test_animated_transform_parameters.h` | Puppet state serialization, rotation identity, and generated-constraint checks. |

`wdg_tool_transform_ui.py` is generated UI output. Keep it synchronized with
`wdg_tool_transform.ui`; do not treat the generated Python file as the design
source.

## Data model and persistence

`ToolTransformArgs::PUPPET` was appended after the pre-existing transform modes
so the numeric values of older modes remain unchanged.

Puppet state consists of:

- `m_origPoints`: original pin positions;
- `m_transfPoints`: current pin positions;
- `m_puppetRotations`: one rotation angle in radians per pin;
- `m_puppetShowMesh`: whether the overlay is visible;
- `m_puppetExpansion`: mask expansion in image pixels.

`setPoints()` keeps the rotation vector aligned with the pin vectors.
`removePuppetPoint()` removes all three values at the same index. A Puppet
transform is non-identity when a pin has moved or any pin rotation is nonzero.

Transform XML uses the existing `warp_transform` element and adds these Puppet
values:

- `rotations`
- `showMesh`
- `expansion`

Older Puppet data without `rotations` remains readable: the loaded rotation
vector is resized to the pin count and missing values become zero. The mesh
visibility and expansion defaults also come from the `KisToolTransform` KConfig
group using the keys `puppetShowMesh` and `puppetExpansion`.

## Mesh mask and overlay

The visible mesh is a deformation preview, not the structure used by the MLS
solver.

`KisWarpTransformStrategy::Private::updatePuppetMask()` builds a cached binary
mask from the preview image:

1. Pixels with alpha greater than 8 are considered artwork.
2. A four-connected flood fill starts at the image boundary and marks exterior
   transparent pixels.
3. Transparent regions not reachable from the boundary are treated as enclosed
   artwork. This allows closed line art to receive a filled mesh.
4. Expansion is converted from image coordinates into source-image coordinates.
   Horizontal and vertical linear passes dilate the mask.
5. The mask is cached by source `QImage::cacheKey()`, original bounds, and
   expansion value.

At 0 px expansion the overlay is clipped at source-pixel resolution instead of
including an entire coarse cell whenever that cell touches the artwork. This
prevents the mesh from protruding by roughly one grid cell around the silhouette.

The displayed grid targets roughly 32 image pixels per cell, clamped to 4–48
rows and columns. Horizontal, vertical, and alternating diagonal edges are
sampled at source-pixel resolution. Only samples inside the mask are added to
the painter path, and each retained point is transformed through the same
effective Puppet constraints as the image preview.

Expected edge cases:

- Open line art does not enclose transparent interiors, matching the usual
  closed-contour requirement of puppet tools.
- A fully opaque layer produces a mask covering its entire bounds.
- Multiple disconnected opaque components share one deformation field.
- Expansion currently uses a separable rectangular dilation rather than a true
  Euclidean-radius dilation.

## Deformation model

The current implementation converts each user pin into hidden control points
and sends those controls to `KisWarpTransformWorker` in rigid mode. The user
continues to see and edit only the real pins.

### Local rigid constraints

Each pin generates five MLS controls:

- the pin center;
- right and left auxiliary points;
- lower and upper auxiliary points.

The local auxiliary radius is 28% of the nearest-pin distance, clamped to 8–64
image pixels. Moving a pin translates all five controls. Rotating a pin rotates
the four offsets around its transformed center. An untouched pin therefore
holds a small rigid neighborhood instead of acting as only a dimensionless
point constraint.

### Terminal-branch rotation

Small local controls were insufficient for rotating an unpinned body branch:
several stationary pins elsewhere could outweigh the rotated pin at long
distances. To address this, `puppetControlPoints()` constructs a Euclidean
minimum spanning tree over the original pin positions.

A pin with graph degree one is considered terminal. Three hidden guide points
are extended away from its sole neighbor at 0.75, 1.5, and 3.0 times the
neighbor distance. Moving or rotating the terminal pin transforms these guides
with it. This makes the open-ended region beyond that pin follow its rotation.
A newly placed pin changes the graph and naturally shortens or splits the open
branch.

The expanded controls are used consistently in:

- interactive image preview;
- mesh-overlay deformation;
- final paint-device rendering;
- approximate need/change rectangle calculations.

Do not apply the expanded controls only to the preview. Preview/final-render
disagreement is especially visible with terminal rotations.

## Interaction details

The Puppet strategy adds two Puppet-specific interaction modes:

- `DELETE_POINT`: selected by Alt-hovering/clicking a pin center;
- `ROTATE_PIN`: selected by hit-testing the annulus around a locked pin.

Center hits take priority over rotation-ring hits. During rotation, the strategy
measures the incremental angle between the previous and current mouse vectors
about the pin center and accumulates that angle in `m_puppetRotations`.

Global move, rotate, and scale gestures inherited from the normal Warp strategy
are intentionally disabled for Puppet mode. Empty-canvas dragging therefore
does not unexpectedly transform the entire pin set.

## Known limitations

1. **The solver is still global rigid MLS.** Hidden constraints improve locality,
   but there is no per-triangle rigidity energy or iterative ARAP solve.
2. **The displayed grid is not the solver topology.** It communicates deformation
   but cannot be inspected or edited as an actual triangulation.
3. **Pin topology is inferred only from Euclidean distance.** The minimum spanning
   tree can connect anatomically unrelated areas when limbs overlap or cross.
4. **Terminal guides are heuristic.** Their distances work well for ordinary
   limb/body chains but may over-propagate on unusual layouts or widely spaced
   pins.
5. **There is no explicit pin type.** Users cannot choose fixed, movable,
   rotation-disabled, rotation-corrected, or weighted pins.
6. **There is no ordering/depth control.** A folded limb cannot be explicitly
   declared in front of or behind another region.
7. **Opaque backgrounds mask the whole rectangle.** Automatic subject isolation
   is outside the current implementation.
8. **The full-resolution mask is built synchronously.** It is cached, but the
   first display on a very large image can still be expensive.
9. **Rotation rings can become visually crowded.** There is no selected-only or
   hover-only handle-display option yet.

## Recommended improvement path

### 1. Replace Euclidean pin topology with mesh-aware topology

Generate a real silhouette triangulation first, then connect pins by geodesic
distance over that mesh. This avoids linking nearby pixels that lie on different
overlapping limbs. Preserve the existing terminal-branch tests when changing
the graph.

### 2. Introduce a true constrained triangular deformation solver

Use a silhouette-conforming triangulation with positional and orientation
constraints, then solve with ARAP or a comparable rigidity-preserving method.
This would make the visible mesh and deformation topology identical, improve
bends around joints, and eliminate most hidden-control heuristics.

The solver should support:

- fixed and translated pins;
- explicit per-pin rotation;
- stable behavior outside the convex hull of pins;
- disconnected artwork components;
- preview-quality and final-quality solve settings;
- deterministic results suitable for transform masks and animation.

### 3. Make topology and pin strength user-controllable

Useful additions include pin strength, a fixed/unfixed toggle, rotation
correction, disabling rotation for selected pins, and an optional influence
radius. Any new state must be copied, compared, included in identity checks, and
serialized with backwards-compatible defaults.

### 4. Improve mask generation

Potential improvements are Euclidean-distance expansion, configurable alpha
threshold, selection-aware mask boundaries, optional treatment of holes, and
reuse of paint-device projection/selection masks without first converting the
entire preview to `QImage`.

### 5. Improve interaction and visualization

Consider selected/hovered rotation rings, a clearer rotation cursor, numeric
rotation entry, multi-pin transforms, snapping, undo granularity during long
drags, and visual differentiation between terminal, internal, fixed, and active
pins.

### 6. Add broader automated coverage

Recommended tests:

- pin-vector and rotation-vector alignment after add/delete/load;
- old XML without Puppet rotation data;
- identity with rotation-only changes;
- terminal detection for chains, branches, and coincident pins;
- upper-branch propagation with lower anchors held fixed;
- a new pin splitting a previously terminal branch;
- preview/final-render equivalence;
- mask generation for opaque art, closed line art, open line art, holes, and
  disconnected components;
- golden-image tests for move and rotate operations;
- UI tests for center drag, ring drag, Alt-delete, lock/unlock, apply, and reset.

## Build and verification

From the configured Windows development environment:

```bat
cmd.exe /d /s /c "call C:\Users\udtre\Projects\krita-dev\env.bat && cmake --build C:\Users\udtre\Projects\krita-dev\_build --target kritatooltransform test_animated_transform_parameters -j 2"
```

Run the focused state/constraint test:

```bat
cmd.exe /d /s /c "call C:\Users\udtre\Projects\krita-dev\env.bat && C:\Users\udtre\Projects\krita-dev\_build\bin\test_animated_transform_parameters.exe testPuppetTransformSerialization"
```

Install the plugin into the test prefix:

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P C:\Users\udtre\Projects\krita-dev\_build\plugins\tools\tool_transform2\cmake_install.cmake
```

Krita must be closed before installation because Windows locks the plugin DLL.
Restart Krita after installing.

Before handing off changes, also run the configured clang-format workflow on
modified C++ files and `git diff --check`.

## Manual regression checklist

- Puppet mode appears in the Transform Tool and switching among all transform
  modes still works.
- Pins can be added while Draw is active and manipulated only after locking.
- Dragging a center moves the intended region while untouched pins hold their
  areas.
- Dragging a ring rotates about the correct pin.
- A terminal hip/shoulder pin rotates the unpinned torso/limb beyond it.
- Adding a pin in that branch limits the previous terminal propagation.
- Alt-click deletes exactly one pin and leaves remaining rotations aligned.
- The 0 px mesh follows the silhouette without coarse-cell protrusion.
- Expansion changes the mesh boundary by the requested amount.
- Closed line-art interiors are covered; open contours remain open.
- Show mesh affects only the overlay, not the deformation result.
- Apply produces the same geometry as the preview.
- Reset restores the original image and Puppet state.
- Saved transform state restores pin positions, rotations, mesh visibility, and
  expansion.

## Invariants for future changes

- Keep the Puppet enum appended after existing transform modes unless a file
  format migration is introduced.
- Keep original points, transformed points, and rotations index-aligned.
- Use one effective-control generator for preview, overlay, final rendering, and
  bounds calculations.
- Missing serialized fields must retain safe backwards-compatible defaults.
- Mesh visibility must never alter rendered pixels.
- Do not infer successful interaction behavior from compilation alone; manually
  verify hit-testing, cursor mode, press/move/release symmetry, and apply/reset.
