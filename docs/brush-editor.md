# Brush Editor

Solstice keeps the Brush Editor's option values in one shared model per brush
engine instead of inside the editor's pages. A change to one option now
updates only that option in the brush preset. This is the groundwork for
showing Brush Editor options in the Tool Options docker later.

## Engines

| Engine | Uses the shared model |
| --- | --- |
| Pixel Brush | Yes |
| Deform | Yes |
| Other engines | Not yet; they work as in Krita |

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

Everything else works as in Krita: presets are saved in the same format, and
the toolbar, the On-Canvas Brush Editor and resizing the brush on the canvas
change the same values as before.

## Brush options in Tool Options

The Pixel Brush and Deform pages of the Brush Editor have an eye column at
the left of the option list. Options with a checkbox (Size, Ratio, Spacing,
Texture, Masked Brush and so on) have a box there: click it to show the eye.
Those options then appear as checkboxes in the **Tool Options** docker of the
Freehand Brush, Line and other brush tools, under **Brush** below the tool's
own settings. Turning a checkbox on or off there does the same as in the
Brush Editor; the curves and other details stay in the Brush Editor.

Some settings inside the pages have an eye too, in front of their label:
the Pixel Brush tip's Diameter, Ratio, Angle, Density and Spacing (or Size,
Angle and Spacing for an image tip), Precision, the Blending Mode, the
Painting Mode and the Texture Scale. With the eye on, the setting appears in
the **Brush** section with the same control, and changes in either place
apply to both. Tip settings appear only for the kind of tip the brush uses.
The Rectangle, Ellipse, Polygon and Polyline tools show the section too, at
the end of their options.

The section starts with the current brush's stroke preview and name, as in
the Brush Presets docker (the preview shows the saved brush; a modified
brush has "*" after its name).

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
