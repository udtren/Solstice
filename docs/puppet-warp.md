# Puppet Warp

<img src="../images/puppet_warp_1.png" alt="Puppet Warp mesh" width="50%">

Puppet Warp is a native mode of Krita's Transform Tool for posing characters
and reshaping raster artwork with movable and rotatable pins. Its mesh follows
the visible artwork and fills the interiors of closed line art.

The behavior reference is Clip Studio Paint's Puppet Warp feature:
<https://tips.clip-studio.com/en-us/articles/10496>.

## How to use it

1. Select the layer, layers, group, or selection to transform.
2. Activate the Transform Tool and choose **Puppet**.
3. While **Draw** is active, click the artwork to place pins at joints and areas
   that should stay stable.
4. Click **Lock Points** when the pins are ready.
5. Drag a pin's center to move that part of the artwork.
6. Drag the pin's outer ring to rotate around it.
7. Alt-click a pin center to remove it.
8. Apply or reset the transform with the normal Transform Tool controls.

Untouched pins anchor their surrounding areas. A terminal pin also controls the
unpinned branch beyond it. For example, when the highest pin is at the hip,
rotating it can turn the upper body while lower pins continue to hold the legs.
Adding another pin above the hip creates a new boundary and limits the effect.

## Mesh controls

- **Show mesh** toggles the overlay without changing the rendered result.
- **Expansion** extends the detected mesh boundary from 0 to 64 image pixels.
- At 0 px, the mesh is clipped closely to visible artwork instead of extending
  by whole grid cells.
- Closed line art receives a filled mesh. Gaps in a contour can prevent the
  enclosed area from being detected.

## Multiple layers and groups

Puppet Warp supports one layer, multiple selected layers, or a selected group.
Multiple layers are combined for the preview and mesh, but the result is
applied separately to each eligible layer. The original layer and group
structure is preserved rather than flattened.

Locked, hidden, non-editable, or unsupported node types may be skipped.

## Current limitations

- An opaque background causes the mesh to cover the layer's entire rectangular
  bounds.
- Multiple disconnected pieces use one shared deformation field.
- Overlapping limbs or unusually arranged pins can occasionally produce less
  natural deformation.
- There are currently no per-pin strength, influence-radius, or depth-order
  controls.

Agent-facing implementation, verification, and improvement notes are maintained
separately in [`agent/puppet-warp.md`](agent/puppet-warp.md).
