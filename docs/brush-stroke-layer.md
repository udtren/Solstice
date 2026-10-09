# Brush Stroke Layer

A Brush Stroke Layer is a paint layer that remembers the brush strokes drawn
on it. The goal, as with Clip Studio Paint's vector layers, is to draw line
art small and enlarge it later without blurring: enlarging the image draws
the remembered strokes again at the new size with the same brush.

The feature is being built in stages. This page describes what works now.

## Creating one

In the Layers docker, open the add-layer menu (**+**) and choose **Brush
Stroke Layer**. It appears below Paint Layer in the menu.

## Painting

- Paint with the brush tools (Freehand Brush, Dynamic Brush, Multibrush).
  Each stroke looks exactly as on a paint layer, and is remembered with the
  brush, colors and settings it was drawn with. The eraser is remembered as
  an erasing stroke.
- Other tools that paint pixels (shapes, fill, gradient and so on) refuse the
  layer, as they do a vector layer.
- Undo and redo remove and restore a stroke together with what it
  remembers. Moving the layer moves its strokes. Duplicating the layer keeps
  its strokes.

## Enlarging

![A small drawing on a Brush Stroke Layer and an enlarged copy drawn again at the new size](images/brush-stroke-layer.png)

**Image > Scale Image** and **Layer > Scale Layer** draw the remembered
strokes again at the new size with the same brush, so enlarged line art
stays sharp; the brush size grows with the scale. Undo returns the layer to
its earlier size and strokes.

The layer is redrawn only when its pixels are still exactly its remembered
strokes. If something else changed them (a filter, for example), it is
scaled like a paint layer instead, so no change is ever lost. Rotating or
mirroring, scaling within a selection, and the Transform tool also scale
the pixels as on a paint layer.

## Saving

Saving as `.kra` keeps the remembered strokes, so a reopened file can still
be enlarged sharply. Original Krita opens the layer as a normal paint layer
with the same pixels; saving the file again from Krita drops the remembered
strokes.

The file stores each stroke's brush settings, colors, pattern and gradient,
but not the brush tip images. On a computer without the brush tip that a
stroke used, the layer keeps its pixels and enlarges like a paint layer.
Other formats (PSD, ORA, PNG and so on) store the pixels only.

## Current limitations

- Filters and other non-brush edits change the pixels but are not
  remembered.
