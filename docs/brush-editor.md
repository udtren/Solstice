# Brush Editor

Solstice keeps the Brush Editor's option values in one shared model per brush
engine instead of inside the editor's pages. Every engine uses it. A change to
one option updates only that option in the brush preset, and the same model
shows options in the Tool Options docker.

## Engines

| Engine | Uses the shared model |
| --- | --- |
| Pixel Brush | Yes |
| Color Smudge | Yes |
| Sketch | Yes |
| Bristle | Yes |
| Hatching | Yes |
| Tangent Normal | Yes |
| Filter | Yes |
| Quick Brush | Yes |
| Curve | Yes |
| Grid | Yes |
| Particle | Yes |
| Shape | Yes |
| Spray | Yes |
| MyPaint | Yes |
| Deform | Yes |
| Clone | Yes |

## Differences from Krita

These apply to the engines that use the shared model:

- **Discarding a lock restores the previous value.** When you lock an
  option, switch to another preset and then unlock the option with
  **Discard**, the preset gets back the value it had before the lock. In
  Krita, editing anything in the Brush Editor while the lock was active
  replaced the previous value with the locked one.
- **Changes that do not affect the brush do not mark the preset as
  modified.** Changing only the level-of-detail settings at the bottom of the
  Brush Editor no longer adds the modified mark to the preset. In Krita, any
  edit in the Brush Editor did.

For every engine, the Brush Editor follows every brush switch: switching
back to the previous brush (Q) or between pen and eraser updates its brush
name and thumbnail too. In Krita they kept showing the previous brush while
the settings changed.

Everything else works as in Krita: presets are saved in the same format, and
the toolbar and resizing the brush on the canvas change the same values as
before. (Solstice has no On-Canvas Brush Editor; see the README.)

## Brush options in Tool Options

The pages of the engines that use the shared model (see the table above)
have an eye column at the left of the option list. Options with a checkbox (Size, Ratio, Spacing,
Texture, Masked Brush and so on) have a box there: click it to show the eye.
Those options then appear as checkboxes in the **Tool Options** docker of the
Freehand Brush, Line and other brush tools, under **Brush** below the tool's
own settings. Turning a checkbox on or off there does the same as in the
Brush Editor; the curves and other details stay in the Brush Editor.

Some settings inside the pages have an eye too, in front of their label:
the Pixel Brush tip's Diameter, Ratio, Fade (both values with their link),
Angle, Density and Spacing (or Size,
Angle and Spacing for an image tip), Precision, the Blending Mode, the
Painting Mode and the Texture Scale, and for Opacity and Flow the strength
bar at the top of the page and **Enable Pen Settings** (in Tool Options both
rows carry the option's name; the second is the checkbox). With the eye on, the
setting appears in
the **Brush** section with the same control, and changes in either place
apply to both. Tip settings appear only for the kind of tip the brush uses.
Sketch has eyes on Line width, Offset scale and Density, and Bristle on
Scale, Random offset, Shear and Density; tip settings that Bristle hides
have no eye. Tangent Normal has eyes on Elevation Sensitivity and the
Direction/Tilt Mix Value, Hatching on Angle, Separation, Thickness and its
three Graphical Tweaks, Filter on Smudge Mode, Quick Brush on Diameter and
Spacing, Curve on Line width, History size and Curves opacity, Grid on
Diameter, Grid width, Grid height, Scale, Random HSV and Random opacity,
Particle on Particles, Iterations, Gravity and Opacity weight, Shape on
Speed, Smoothing and Displace, Spray on Diameter, Aspect ratio, Angle, Scale
and Spacing, MyPaint on Radius Logarithmic, Hardness, Opacity and Eraser,
and Clone on its five check boxes (Healing, perspective, source point move and
reset, and cloning from all visible layers).
The Rectangle, Ellipse, Polygon and Polyline tools show the section too, at
the end of their options.

The section starts with the current brush's stroke preview and name, as in
the Brush Presets docker. A modified brush has "*" after its name, and its
preview follows the changes made in the Brush Editor, Tool Options or the
toolbar, shortly after they stop; the Brush Presets docker keeps showing the
saved brush.

The choice is remembered for each brush engine, so every Pixel Brush preset
shows the same options. The **Brush** heading collapses the section.

## Options that depend on others

Some Pixel Brush options save a value that depends on another option:

- **Painting Mode** is saved as Wash while a masked brush is enabled.
- **Lightness Strength** is saved as enabled only while the brush tip is in
  Lightness map mode.
- The **Masked Brush** size follows the brush tip size.

When the other option changes, Solstice saves these options again, so the
preset always matches what the Brush Editor shows.
