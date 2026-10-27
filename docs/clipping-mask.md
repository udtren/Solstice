# Clipping Mask

A clipped layer shows only where the layer directly below it has pixels,
as in Photoshop, Clip Studio Paint and SAI. You do not need a group, and
the layer you clip to keeps its name.

> **In development.** Clipping Mask is on a development branch and is not in
> a released build yet. This page describes the current state and changes
> with it.

## Clipping a layer

- **New Clipping Layer**: adds a paint layer above the active layer and clips
  it to that layer. It is in the Layers docker's context menu under
  **Group**, and you can give it a shortcut in **Settings > Configure
  Solstice > Keyboard Shortcuts**.
- **Alt+click** the inherit alpha icon of a layer row to clip or unclip that
  layer. The icon then shows an orange arrow, and an orange bar beside the
  thumbnail marks the clipped layer.
- **Toggle Clipping Mask**: clips or unclips the selected layers. It is in
  the context menu under **Toggle Locks & Visibility**.

All of these can be undone.

## How clipping works

- The layer you clip to is the *base*. It is the nearest layer below that is
  not clipped itself. Several clipped layers in a row all clip to the same
  base.
- A paint layer, a group or any other layer can be a base. A pass-through
  group cannot be a base, and layers inside a pass-through group are not
  clipped.
- Each clipped layer keeps its own blend mode and opacity. The base's blend
  mode and opacity apply to the base and all of its clipped layers together.
- Hiding the base also hides its clipped layers. A transparency mask on the
  base also clips the layers above it.
- A clipped layer with nothing below it is shown normally.
- A layer cannot use clipping and inherit alpha at the same time: switching
  one on switches the other off. Inherit alpha works as before.

## Baking clipped layers

**Merge Down** (Ctrl+E) on a clipped layer merges it into its base. The
result keeps the base's name, blend mode and opacity, and looks the same as
before. Merging two clipped layers gives one clipped layer.

## Files

`.kra` files keep clipping. Krita itself ignores it and shows clipped layers
unclipped. PSD import and export do not support clipping yet.

## Current limitations

- A layer style on the base applies only to the base, not to its clipped
  layers.
- Copy, Destination In and Destination Atop are not supported as the base's
  blend mode.
- Merge Selected, Flatten Layer and Flatten Image have not been checked with
  clipped layers yet.

Technical notes are in the [agent plan](agent/clipping-mask-plan.md).
