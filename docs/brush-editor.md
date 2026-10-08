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

## Options that depend on others

Some Pixel Brush options save a value that depends on another option:

- **Painting Mode** is saved as Wash while a masked brush is enabled.
- **Lightness Strength** is saved as enabled only while the brush tip is in
  Lightness map mode.
- The **Masked Brush** size follows the brush tip size.

When the other option changes, Solstice saves these options again, so the
preset always matches what the Brush Editor shows.
