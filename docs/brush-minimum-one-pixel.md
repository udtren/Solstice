# At least 1 pixel

**At least 1 pixel** keeps thin brush lines visible. When pen pressure, or
another sensor such as Fade or Speed, makes the brush smaller than one pixel,
for example at the light start and end of an inking stroke, Solstice draws it
at 1 pixel instead of letting the line fade out or break into dots. The idea
comes from the brush size setting of the same name in CLIP STUDIO PAINT.

## Turning it on or off

The option is a check box in **Tool Options** of the **Freehand Brush Tool**
(and the Multibrush Tool), below **Snap to Assistants**. It is on by default,
also for profiles that existed before the option was added. The setting is
remembered when you restart Solstice.

It is a tool setting, not part of the brush preset: switching brushes does
not change it, and turning it on or off does not mark a brush as modified.
The same setting applies to the other tools that paint with the brush, such
as the Line Tool and the shape tools.

With the option off, brushes behave exactly as before.

## What it changes

- Only dabs that would be smaller than 1 pixel are changed. Larger dabs,
  and a stroke at normal pressure, are unchanged.
- A dab whose size is zero, for example at zero pressure, is drawn at 1
  pixel too.
- The size is the brush tip's diameter (for an image tip, its larger side).
  A flat tip made thin with **Ratio** can still draw a side thinner than 1
  pixel.
- The masking brush of a brush with **Masked Brush** is not changed.
- Instant Preview shows the same line as the final result.
- Strokes on a [Brush Stroke Layer](brush-stroke-layer.md) remember the
  setting they were drawn with, and keep it when the layer redraws them.
  Strokes recorded before the option existed are redrawn without it.
- Brush preset stroke previews are drawn without the option.

It works with these brush engines: Pixel Brush (including the
[GPU brush](gpu-engine.md)), Color Smudge, Filter, Tangent Normal, Hatching,
Clone and Quick Brush. The other engines (Sketch, Bristle, Deform, Spray,
Curve, Grid, Particle, Shape and MyPaint) compute their sizes in their own
way and are not affected.

Technical notes: [agent document](agent/brush-minimum-one-pixel.md).
