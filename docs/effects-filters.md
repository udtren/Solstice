# Effects filters

Solstice's new effects use CPU processing. Film Grain, Chromatic Aberration,
Film, Crystal, the Lens Blur extension, Pencil, Sharpness, Color Filter, Glow
Drop Shadow, Line Art Extraction and Color Reference are implemented.
[G'MIC-Qt](gmic.md) is also available as a separate CPU filter dialog.

## Color Reference

Choose **Filter > Adjust > Color Reference** to recolor a layer from an
image or gradient. Selections, filter masks and filter layers are supported.

- **Image** offers eight original palettes and **Load Image...** for your
  own references. You can also drag image files from your file manager onto
  the thumbnail area, including several files at once. The last successfully
  added image is selected. Failed imports and the 50-image limit are reported
  below the list. Select a thumbnail to apply its colors. Up to 50 user
  images can be stored in addition to the built-ins.
- **Gradient** offers the tagged gradient chooser and editor. Edits affect
  the filter's copy of the gradient.
- **Amount** blends with the original, from 0 to 100. At 0, nothing changes.
- **Keep brightness**, on by default, retains the original tonal structure.
  Turn it off to follow the reference's brightness as well as its colors.

The filter maps dark-to-light tones to reference colors. Different original
hues with the same brightness can become the same color at Amount 100;
lower Amount to retain more of the original variation. References containing
mixed colors at similar brightness may produce muted tones.

Load supports PNG, JPEG, TIFF, BMP, TGA, PSD/PSB, WebP and KRA/KRZ through
installed importers. Layered images use their composite. The library stores
small sRGB PNG copies in the resource folder's `colorreference/` directory
(normally `%APPDATA%\Solstice\resources\colorreference`). Originals are
untouched. **Delete** removes a user copy after confirmation; built-in
palettes cannot be deleted. Library changes persist even if you cancel the
filter dialog.

Applied colors are embedded in filter settings. Deleting a reference or
opening the document on another computer does not change an existing filter
layer or mask. A missing reference shows a notice while retaining saved colors.

Alpha, fully transparent pixels and disabled channels are preserved. Colors
are processed through floating-point sRGB and converted back to the document
profile; grayscale documents stay grayscale. Imported reference palettes are
SDR. With Keep brightness off, full Amount replaces out-of-range HDR tones
with reference endpoint colors. Profile conversion can also limit out-of-gamut
colors. Transparent gradient stops do not make the layer transparent.

This is Solstice's own implementation, not a pixel-identical reproduction of
CLIP STUDIO PAINT. `.clip`, photo-library and camera import are unsupported.
Interactive confirmation is pending.

Technical record: [Color Reference](agent/color-reference.md).

## Sharpness

Choose **Filter > Enhance > Sharpness**. **Strength** ranges from 0 (no
change) to 100, with a default of 50. It strengthens fine brightness detail
while limiting light/dark halos around hard edges. It uses a fixed detail
scale; use the existing Unsharp Mask when a configurable radius is needed.

Alpha is preserved. Fully transparent pixels do not contribute hidden color
to nearby edges; selections restrict the applied result and disabled channels
remain unchanged. RGB/Alpha and Gray/Alpha are processed directly at U8, U16,
F16 and F32, preserving the document profile and float HDR range. CMYK/Lab
and other models use float RGB conversion and show a notice in the dialog.

The same filter is available for filter masks and layers. Reduced-resolution
preview is disabled to retain fine detail. Automated checks cover conversion,
partial updates, mask application and configuration XML, cancellation and
Undo/Redo. The user confirmed Sharpness operation on 2026-10-15.

Technical record: [Illustration filters plan](agent/illustration-filters-plan.md).

## Color Filter

Choose **Filter > Adjust > Color Filter**. The **Look** tab offers None,
Vivid, Warm, Cool, Lemonade, Cobalt Blue, Aquamarine, Aurora, Sepia and
Monotone. **Amount** blends the Look with the original (0-100). These are
Solstice's own looks; they do not reproduce another application's pixels.

The **Adjust** tab contains Brightness, Contrast, Hue, Saturation, Color
temperature, Tint and Sharpness. All start at zero. Hue uses degrees
(-180 to 180); Sharpness ranges from 0 to 100; the other controls range from
-100 to 100. Positive temperature warms the image; positive tint shifts it
toward magenta, negative toward green. These are artistic color adjustments,
not calibrated Kelvin values. Sharpness runs after the color adjustments.

Amount 0 and None turn off **Look only**; Adjust still applies. Selecting
another Look keeps Amount and Adjust. Editing a slider labels the settings
Custom. **Reset Settings** restores None, Amount 100 and neutral Adjust.
Saved settings retain the actual Look values, so tuning presets in a later
version will not change a saved filter mask or layer.

Thumbnails show the selected layer's opening snapshot when it can be safely
captured. Otherwise, including mask/layer configuration dialogs and a busy
image, they show a labeled sample image. They show each Look at Amount 100
with neutral Adjust; the canvas preview shows the actual combined settings.
Thumbnails do not refresh when painting or changing settings.

RGB/Alpha U8, U16, F16 and F32 retain the document profile, with no 8-bit
intermediate. Float RGB may remain negative or exceed 1.0; integer output
is limited by its format. Processing uses the document's native channel
encoding. Alpha, fully transparent pixels and disabled channels are preserved;
selections restrict the applied result. **Gray/Alpha supports only Brightness,
Contrast and Sharpness**; convert to RGB for colored Looks. CMYK/Lab and other
models use float RGB conversion and show a notice.

Filter masks and layers are supported. Reduced-resolution preview is allowed
only with Sharpness 0. Automated tests cover these formats, patch updates,
selection/Undo/Redo, cancellation and mask configuration restoration.
The user confirmed Color Filter operation on 2026-10-15.

Technical record: [Illustration filters plan](agent/illustration-filters-plan.md).

## Glow

Choose **Filter > Artistic > Glow** to softly brighten highlights and spread
their light into nearby painted areas. The controls range from 0 to 100:

- **Strength** controls the amount of light; 0 leaves the image unchanged.
  Default: 50.
- **Effect area** controls which parts emit light. Low values select only
  bright highlights; higher values include darker colors. Default: 50.
- **Blur range** controls the light's spread in document pixels. Zero
  brightens the selected highlights without spreading them. Default: 30.

Alpha and fully transparent pixels are preserved. This filter does not create
an outer glow beyond the painted silhouette. Semi-transparent sources emit
proportionally less light. Selections limit the applied result, and disabled
channels remain unchanged. Blur range controls the spread, not a guarantee
that every affected pixel becomes brighter as it increases.

RGB/Alpha and Gray/Alpha U8, U16, F16 and F32 are processed directly in the
document's channel encoding and profile. CMYK/Lab and other color models use
float RGB conversion and show a notice. There is no 8-bit intermediate.
Float RGB retains negative/HDR values; Screen blending is extended smoothly
outside the usual 0-1 range. Integer output is bounded by its format.

Large blur ranges use a CPU approximation to Gaussian blur. Filter masks and
layers use the same calculation. Reduced-resolution preview is disabled to
keep small highlights and final output consistent. The user confirmed Glow
operation on 2026-10-15. Automated tests cover formats, patches, transparency,
selections, Undo/Redo, cancellation and filter-mask settings.

Technical record: [Illustration filters plan](agent/illustration-filters-plan.md).

## Drop Shadow

Choose **Filter > Artistic > Drop Shadow** on a layer with transparency.
The filter creates a shadow underneath the original paint. For editable
settings, add a filter mask to that layer. An opaque background hides the
shadow, so applying it to a flattened image usually has no visible effect.

- **Blur** (0-100, default 20): 0 gives a hard shadow; larger values soften it.
- **Angle** (0-359.9 degrees, default 45): the direction the shadow falls;
  0 points right, 90 down, 180 left and 270 up.
- **Distance** (0-1000 document pixels, default 20): the offset from the paint.
- **Opacity** (0-100%, default 50): 0 leaves the image unchanged.
- **Shadow color** (default black): the color of the shadow.
- **Position**: drag the point in the dialog's pad to change Angle and
  Distance together. Arrow keys move by 1 document pixel; Shift moves by 10.
  The displayed radius adjusts to the distance and stays fixed during a drag.

The position pad is in the dialog; this stage has no on-canvas handle.
The existing Drop Shadow layer style remains available. Its light-angle
convention is different from this filter's shadow-direction angle.

The filter can add alpha in transparent areas and combines the original over
the shadow. Fully opaque pixels remain unchanged. Selections limit the
applied result; disabled color or alpha channels remain unchanged. It uses
CPU processing in the document's own color space: RGB/Gray U8, U16, F16 and
F32, plus CMYK/Lab U16, are covered by automated checks. The chosen color is
converted to that space, including gray for Gray/Alpha. Float HDR colors
are retained without an 8-bit intermediate.

Filter masks and layers are supported, but a filter layer often sees an
opaque composite and therefore hides the shadow. Reduced-resolution preview
is disabled; preview and final application use the same calculation.
Automated tests cover partial updates, settings restoration, feathered
selections, Undo/Redo, cancellation and the position pad. The user confirmed
Drop Shadow operation on 2026-10-16.

Technical record: [Illustration filters plan](agent/illustration-filters-plan.md).

## Line Art Extraction

Choose **Filter > Other > Line Art Extraction** on an imported scan or photo
of dark drawing marks on light paper. The filter estimates the paper's
brightness, reduces mild lighting unevenness and produces lines on a
transparent background. A filter mask keeps the original drawing editable.

- **Density** (0-200, default 100): higher values retain more faint marks and
  make lines more opaque. **Zero removes all ink; it is not a bypass.**
- **Line color**: **Black** (default) outputs black lines; **Keep original**
  retains the source color channels, useful for colored pencil.
- **Paper correction radius** (8-256 document pixels, default 32): use a
  radius larger than the line width. Increase it for thick lines; reduce it
  when the paper's lighting changes over a shorter distance.

Start with the default radius, then adjust Density while checking faint
strokes and the background. For a large scan with wide strokes, increase
the radius before increasing Density. Very dark paper, abrupt cast shadows,
broad shaded areas and bright marks on dark paper are not reliably separated.
The filter deliberately keeps dark areas when it cannot find reliable light
paper nearby; it does not recognize drawing content or use an AI model.

Processing is CPU only. Output opacity never exceeds the original opacity.
Fully transparent pixels and disabled channels remain unchanged; selections
restrict where the result is applied. Keep alpha enabled to remove the paper.
RGB/Gray U8, U16, F16 and F32 are supported directly; paper measurement in
CMYK/Lab uses floating-point RGB. Keep original preserves native colors
without a conversion round trip, including float negative/HDR values.
Brightness used for paper detection is limited to 0-1. Black is converted
to the document color space for non-RGB/Gray documents.

Filter masks and layers are available. Reduced-resolution preview is disabled
to keep thin lines and the final result consistent. Automated checks cover
formats, paper correction, density, patches, transparency, channel selection,
Undo/Redo, cancellation and mask settings. Interactive verification is pending.

Technical record: [Illustration filters plan](agent/illustration-filters-plan.md).

## Film Grain

Choose **Filter > Other > Film Grain** on a paint layer. Adjust the preview,
then apply the result or cancel. Undo restores the previous image. The same
filter is available for filter layers and filter masks.

- **Strength** controls the amount of grain, from 0 (unchanged) to 100.
- **Monochrome** applies the same brightness-channel perturbation across the
  color channels. With this off, each color channel has independent grain.
- **Seed** determines the grain pattern. Retain it to reproduce the pattern,
  or choose **New Seed** to generate a different one.

The pattern stays anchored to image pixels when settings are previewed or
portions of a filter layer/mask are recalculated. Settings preserve the seed.
Selections limit application; existing alpha and disabled channels are retained,
and fully transparent pixels are left unchanged. Random Noise remains a
separate filter with its original behavior.

### Film Grain scope

RGB/Alpha and Gray/Alpha processing is tested at 8-bit and 16-bit integer and
16-bit and 32-bit floating-point depths. Grain is added in the document's
native channel values without changing its color profile. Integer values are
bounded by the format; float values can remain negative or exceed 1.0.
Other color models use noise in their native channels and have not yet been
visually verified.

## Chromatic Aberration

Choose **Filter > Artistic > Chromatic Aberration**. The effect offsets the
red and blue channels in opposite directions while keeping green in place.
It also works in filter layers and filter masks.

- **Translation** uses a constant offset. **Strength** is each channel's
  displacement in full-resolution pixels (0-100); zero leaves the image unchanged.
  **Angle** is clockwise in canvas coordinates: at 0 degrees red moves right
  and blue moves left; at 90 degrees red moves down and blue moves up.
- **Radial** increases displacement away from **Center X/Y**. Strength is
  reached at **Reference radius**, and remains capped at that amount farther
  away. Red moves outward and blue inward. Center coordinates and radius are
  initially based on the canvas and are saved with the effect; changing a
  selection or recalculating a portion of the image does not move them.

The effect preserves the existing silhouette and opacity; it does not grow
colored fringes into transparent areas. Subpixel sampling accounts for alpha,
so hidden colors in transparent pixels do not bleed into visible pixels. If a
shifted sample has no visible content, that channel retains its original color.
The layer's off-canvas pixels can contribute. Selections limit the applied
result, not the source that is sampled. Disabled channels are retained.

RGB/Alpha U8, U16, F16 and F32 use the document's native channel values and
profile. Float RGB retains negative and HDR values; there is no 8-bit
intermediate. Other color models are converted through floating-point RGB and
back, with a notice in the dialog. This can alter color appearance; grayscale
output remains grayscale and therefore cannot show colored fringes. Automated
coverage includes Gray/Alpha at all four depths and CMYK/Alpha and Lab/Alpha at
16-bit integer; their artistic appearance still needs visual review.

## Film

Choose **Filter > Artistic > Film**. This combines chromatic aberration,
color treatment, light leak and grain in one preview and one undoable operation.
It is also available in filter layers and filter masks.

The **Look** tab provides three starting presets:

| Preset | Appearance |
| --- | --- |
| Vintage | Sepia color treatment, visible grain, color shift and warm light leak |
| Modern | Subtle cool treatment, lighter grain and color shift |
| Warm | Warm color treatment and a mild light leak; grain and color shift off |

These are Solstice looks, not exact simulations of a particular film stock.
Select a preset, then adjust **Color treatment** (None, Fade, Sepia, Warm or
Cool), **Color strength**, **Chromatic aberration**, **Grain strength** and
**Light leak strength**. Editing any setting changes the preset label to
**Custom** while retaining the other values. Zero disables each component;
setting all strengths to zero leaves the image unchanged. None disables color
treatment regardless of Color strength.

The **Details** tab contains the aberration angle, color/monochrome grain,
grain seed and **New Seed**, plus the light leak's X/Y position and radius in
document pixels. Chromatic aberration uses the same translation mode and
pixel-strength units as the standalone filter. The orange light leak has a
smooth falloff; increasing its radius spreads it over more of the image.
Its initial position is the middle of the canvas's right edge.

Settings save the actual component values, seed and light-leak geometry.
Loading saved settings does not reapply a preset, and selecting another preset
keeps the seed and leak position/radius. Selections and partial filter-layer
updates do not relocate the effect. A later canvas resize does not
automatically rescale the saved geometry.

Film preserves opacity and transparent pixels. Disabled channels are retained
in the final output. Selections can still sample neighboring source pixels for
the color shift. Processing order is color shift, color treatment/light leak,
then grain. RGB/Alpha U8/U16/F16/F32 keeps its profile and native depth, with
integer clipping at each stage and no explicit clipping of float HDR values.
Color treatment is artistic math in the document's native channel encoding;
it is not a physical exposure simulation. As with Chromatic Aberration,
non-RGB color treatment uses floating-point RGB conversion and back, which
can change appearance; grain uses native color channels. Grayscale stays gray.

## Crystal

Choose **Filter > Artistic > Crystal** to turn the image into polygonal color
cells. It also works in filter layers and filter masks.

- **Cell size** controls the approximate spacing in document pixels (1-512).
- **Irregularity** moves cell centers within their grid squares. Zero produces
  regular square cells; 100% allows the full range of randomized positions.
- **Seed** fixes the cell pattern. **New Seed** creates another pattern.
- **Seamless tiling** fits the cells to the saved **Tile bounds**, initially
  the canvas bounds. Width/height need not divide evenly by Cell size.

Each cell takes its color from the source pixel nearest its center. Colors
are sampled from the original layer, including outside a selection; selections
only limit where the result is applied. The original opacity and disabled
channels are preserved. Fully transparent pixels remain untouched. If a cell's
representative pixel is transparent, the original color is retained there,
so cells at a transparent silhouette may not have a uniform color.

With tiling enabled, cell distance and color sampling wrap across the tile
edges. Use the canvas as one tile to export a repeating texture. For a larger
image containing repeated tiles, each output tile samples its own corresponding
source tile. Matching source tiles produce matching output; different source
tiles are not replaced with copies of one tile. Because opacity is preserved,
non-repeating alpha can still produce seams. Without tiling, off-canvas source
pixels can contribute, and an empty representative retains the original color.

Cell geometry, seed and tile bounds are saved. Preview updates, selections and
partial filter-layer updates do not move the cells. Resizing the canvas does
not automatically resize saved tile bounds. RGB/Alpha and Gray/Alpha at
U8/U16/F16/F32, plus CMYK/Alpha and Lab/Alpha at U16, have automated coverage.
Native channel bytes are copied without color conversion or an 8-bit
intermediate, preserving the profile and float HDR values.

## Lens Blur

Choose **Filter > Blur > Lens Blur**. The existing polygon shapes, radius and
rotation remain available, along with two new controls:

- **Roundness** changes the polygon toward a circular aperture. At 0%, it
  retains the original polygon; at 100%, it is circular and shape/rotation
  no longer affect the result.
- **Highlight enhancement** emphasizes bright colors in the blurred image.
  Zero disables enhancement. Increasing it makes small bright areas more
  prominent without increasing the opacity of the bokeh just to brighten it.

Both controls default to zero. Older saved settings without them retain the
original calculation and appearance. Settings preserve the new values in
filter masks/layers as well as the normal filter dialog.

Lens Blur blurs opacity and can spread a shape into transparent surroundings,
as it did before this extension. Disabled channels remain unchanged. With
enhancement enabled, source alpha weights the colors, so hidden colors in
transparent pixels do not bleed into visible blur. Alpha is blurred normally;
locking alpha retains the original opacity while colors are still sampled
with coverage weights. Selections limit the applied result, but surrounding
source pixels contribute to the blur.

Enhancement operates on native RGB or gray channel values using a continuous
highlight curve; it is an artistic effect, not a physical exposure simulation.
Uniform colors stay uniform. Integer formats stay within their format limits;
float negative/HDR values are retained without an 8-bit intermediate or clipping
at 1.0. CMYK, Lab and other models use floating-point RGB conversion and back
when enhancement is enabled; the dialog warns that this can change appearance.
Roundness without enhancement uses the original native color-space processing.

Lens Blur retains its existing filter-mask, filter-layer, filter-brush and
reduced-resolution preview support. At reduced resolutions, the radius is
scaled and rounded down as before, so the preview is an approximation of the
full-resolution aperture. Final output uses the full-resolution radius.
Calculation is CPU-only, using FFTW when available and spatial convolution
otherwise. The fallback becomes much slower with large radii. Large full-image
calls also need substantial temporary memory; interactive performance depends
on the host's patch scheduling and image size.

## Pencil

Choose **Filter > Artistic > Pencil** to add pencil-like outlines and strokes.
It is also available in filter layers and filter masks.

- **Outlines** and **Outline strength** control the darkening of existing
  image edges. This strengthens contours; it does not extract a separate
  transparent line-art layer.
- **Hatching** and **Hatching strength** add strokes according to image
  darkness and lighten the underlying color to suggest paper.
- **Hatching size** sets stroke spacing in document pixels (1-32).
- **Roughness** varies stroke position, length and intensity. Zero produces
  regular parallel lines.
- **Angle** sets the stroke direction clockwise from horizontal (0-180
  degrees). Zero and 180 have the same appearance.
- **Seed** preserves the irregular pattern; **New Seed** changes it.
- **Grayscale output** removes color while retaining the pencil shading.

Outlines and hatching can be enabled separately. Turning both off, or setting
both strengths to zero, leaves the image unchanged even with Grayscale output
selected. Disabling a component retains its saved settings. The pattern is
anchored to document pixels, so preview updates and partial filter-mask/layer
updates do not move it. Canvas resizing does not scale the saved stroke size.

Pencil preserves original opacity, fully transparent pixels and disabled
channels. Transparent source colors do not bleed into the outlines. Selections
limit application while neighboring image pixels can contribute to contours.
This is an artistic pencil effect, not a simulation of physical graphite or
a pixel-identical reproduction of another application's filter.

RGB/Alpha uses native channel values and retains its profile, with no 8-bit
intermediate. Integer outputs are bounded by their format; floating-point
output is not clipped to 0-1. Brightness analysis is bounded to that range,
so values above white do not create extra hatching. Other models, including
Gray/Alpha, convert through floating-point RGB and back, which can change
appearance; the dialog displays a notice. Automated coverage includes
RGB/Alpha and Gray/Alpha at U8/U16/F16/F32 and CMYK/Alpha and Lab/Alpha at U16.

## Scope and development status

Film Grain, Chromatic Aberration, Film, Crystal and Pencil process at full document
resolution. Reduced-resolution filter previews and filter-brush painting are
not enabled for those five filters. Large images may take
longer to preview because these are CPU effects. Normal canvas display can
still use the existing GPU engine.

Development status (2026-10-15): the user confirmed all six effects, including
Pencil. Automated coverage includes pixels, parallel patches, configuration,
selections, cancellation and Undo/Redo. The implementation plan retains the
manual regression checklist. This
page describes the development branch, not a newly published release.
