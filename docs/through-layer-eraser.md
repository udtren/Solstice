# Through-Layer Eraser

The **Through-Layer Eraser Tool** erases through several layers with one
stroke, so a mistake that spans the line art and the colors underneath is
gone in one pass. It was added in Solstice 0.7.0 and is Solstice's own
implementation of the idea behind the through-layer eraser of CLIP STUDIO
PAINT.

![One stroke of the Through-Layer Eraser Tool removes a corner of a red frame and a purple block painted on separate layers, and the Layers docker thumbnails of both layers update](images/through-layer%20eraser%20tool.gif)

The tool is in the brush section of the toolbox, next to the Multibrush Tool.
It has no default shortcut; you can assign one in **Settings > Configure
Solstice > Keyboard Shortcuts**.

## What it erases

| Selected layers only | Erases |
| --- | --- |
| Unchecked (default) | every visible paint layer in the image |
| Checked | the selected layers, and the layers inside the selected groups |

Select several layers in the Layers docker with Ctrl or Shift, then check
**Selected layers only** in Tool Options. You do not have to select a paint
layer to use the tool: it also works with a group selected, or with a layer
it cannot paint on.

These layers are always left alone:

- hidden layers, and everything in a hidden group;
- locked layers, and everything in a locked group;
- alpha locked layers;
- layers that are not paint layers, such as vector, filter, fill, clone and
  file layers, and masks.

If nothing can be erased, a message says so and the stroke does not start. In
isolated mode, only the isolated layer or group is erased.

## Brush and undo

The tool erases with the current brush preset, size, opacity and flow, and
keeps the Freehand Brush Tool's stabilizer options. It does not need Eraser
Mode to be on, and the mode does not change what it does.

A selection limits the erasing as it does for any brush. A layer's own local
selection applies to that layer.

The whole stroke is one undo step, whatever the number of layers.

## Things to know

- Instant Preview is not used for these strokes.
- Each stroke updates every layer it erases, so a large brush on many layers
  costs more than an ordinary brush stroke.
- Brushes that take their pixels from the layer, such as Smudge or Filter
  brushes, erase little or nothing: the tool draws the stroke on its own and
  uses it only to decide what to erase.
- On a [Brush Stroke Layer](brush-stroke-layer.md) the erasing is not recorded
  as one of its strokes. The layer reports that it no longer matches its
  strokes, as it does for other edits made without the brush.
