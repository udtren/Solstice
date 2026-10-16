# Color Reference - implementation plan

A new `solstice_color_reference` filter that recolors a layer from a
reference image or a gradient.

## Status and decisions

Status (2026-10-16): **implemented, built, tested and installed**;
interactive confirmation pending.
The maintained implementation contract, decisions, tests and manual checks
are in [Color Reference development notes](color-reference.md).
The proposal below is archived design context; the maintained notes supersede
its algorithm, storage, threading and color-space proposals.

The user asked (2026-10-15) whether CLIP STUDIO PAINT's Color Reference could
be implemented, and then asked for this plan as a new
`solstice_color_reference` filter. The user excluded two ways of loading
images: from the tablet/phone photo library and from the camera. Only
loading image files and deleting images are in scope.

The reference is a set of screenshots of CLIP STUDIO PAINT documentation that
the user supplied in the conversation. They are not stored in the repository.
CLIP STUDIO PAINT does not publish its algorithm, so the goal is a similar
effect, not identical pixels. Do not bundle CLIP STUDIO PAINT's preset images
or the reference artwork.

### Decisions needed before stage CR0 ends

| Decision | Proposal (not yet confirmed by the user) |
| --- | --- |
| Computation | CPU only, as for the other Solstice effects (`cpu-effects-and-gmic-plan.md`, `illustration-filters-plan.md`). |
| Image mode calculation | Average color per brightness bin of the reference image (see "Calculation"). The alternative, matching the layer's whole color distribution, would need statistics from the entire layer and so would change with the processed area and with later edits under a filter layer. |
| Reference image storage | A dedicated folder in the resource directory, managed by the filter's widget, at most 50 images. Not Krita pattern resources (they would appear in every pattern list) and not a new resource type (more work and no benefit for a list of 50). |
| Preset images | Eight built-in references made by us, generated from color ramps or our own photos/paintings with a clear license. |
| Menu | Filter > Adjust > Color Reference. |
| Branch and order | Implement after stage C (Color Filter) of `illustration-filters-plan.md`, on the same branch, so the thumbnail grid can be shared. Color Reference does not depend on Glow, Drop Shadow or Line Art Extraction. |

Record each decision and its date here when the user confirms it.

## Reference behavior

From the supplied screenshots:

- It recolors one layer; a selection limits the result.
- **Reference**: Image or Gradient.
  - Image: eight preset images plus images the user loads from files
    (.png, .jpg, .tif, .tiff, .bmp, .tga, .clip, .psd, .psb, .webp), up to 50.
    The selected image can be deleted.
  - Gradient: gradients from registered gradient sets, with an edit button.
- **Amount** (0-100, default 100): how strongly the correction is applied.
- **Keep brightness**: when on, the original image's brightness is kept and
  only the colors come from the reference. When off, the result follows the
  reference's colors more closely, including its brightness.
- Preview on/off, OK, Cancel.

## What already exists

| Feature | Location | Behavior | Gap |
| --- | --- | --- | --- |
| Gradient Map | `plugins/filters/gradientmap/` (`KisGradientMapFilter`, ID `gradientmap`, category Map) | Each pixel's `intensityF()` picks a color from the gradient; Blend, Nearest and Dither modes. The gradient is stored as XML in the configuration (`gradientXML`, plus `md5sum`/`gradientName`), so filter layers and masks keep working. Alpha is the minimum of the pixel's and the gradient's. | No amount, no brightness keeping. Its Blend mode equals gradient mode at Amount 100 with Keep brightness off. |
| Color Transfer | `plugins/filters/fastcolortransfer/` (`KisFilterFastColorTransfer`, ID `colortransfer`, category Colors) | Reinhard transfer of Lab mean and deviation from a reference image file. The widget loads the file through `KisImportExportManager` and stores its statistics (`meanL`... `sigmaB`) and `filename` in the configuration. | No amount, no brightness keeping, no image list. Not threaded and not available for filter layers. Converts through 16-bit Lab, so float documents lose precision. Source statistics are taken from the processed rectangle, so results depend on the area processed. |
| Gradient widgets | `libs/ui/widgets/gradient/KisGradientChooser.h`, `KisGenericGradientEditor.h` | Resource chooser with tags, and the editor Gradient Map uses | Reusable for gradient mode |
| Import | `plugins/impex/` has png, jpeg, tiff, qimageio (bmp), tga, psd, webp and kra | `KisImportExportManager` reads all of them; Color Transfer already uses it (`kis_wdg_fastcolortransfer.cpp`) | .clip cannot be read (proprietary format) |
| Resource folder | `KoResourcePaths::getAppDataLocation()` (`%APPDATA%\Solstice\resources` by default, `settings-location.md`); Vision ML keeps its user models under it (`plugins/visionml/src/VisionML.cpp`) | Pattern for a plugin-owned user folder | - |
| Thumbnail grid | Color Filter, stage C of `illustration-filters-plan.md` (`KisColorFilterConfigWidget`, in progress on `codex/illustration-filters`) | One small copy of the layer, rendered per look | Share the code once stage C is done |

Gradient Map and Color Transfer stay unchanged.

## Architecture

### Module, ID and configuration

Add the filter to `plugins/filters/solsticeeffects/` (module
`kritasolsticeeffectsfilter`) with its test in `tests/`. ID
`solstice_color_reference`, category Adjust, configuration `version` 1.
Proposed properties (final names fixed in stage CR0):

| Property | Meaning |
| --- | --- |
| `source` | 0 image, 1 gradient |
| `amount` | 0-100, default 100 |
| `keepBrightness` | bool, default to be set in stage CR0 |
| `lut` | The computed brightness-to-color table (see below), encoded as text, for example base64 float RGB |
| `referenceId` | Image mode: the preset ID or the user image's file name, for display and reselection only |
| `gradientXML` | Gradient mode: the gradient, as Gradient Map stores it |

The table is the only input the pixel calculation needs. A filter layer or
mask therefore keeps its result after the reference image is deleted, renamed
or moved, or the file is opened on another computer. `referenceId` and
`gradientXML` let the widget show and edit the reference; when the image is
missing, the widget shows that, and the stored table still applies.

### Calculation

Both kinds of reference are reduced to one brightness-to-color table, for
example 256 entries of float RGB in the document's color space:

1. **Gradient**: sample the gradient at each entry, in the document's color
   space (`KoCachedGradient`, as Gradient Map does).
2. **Image**: convert the reference to the document's color space. Sort its
   pixels into bins by brightness and average the color of each bin,
   weighting by alpha. Fill empty bins by interpolating between the nearest
   filled bins; extend the ends with the nearest filled bin. Stage CR0
   decides whether averaging happens on linear or encoded values. Build the
   table in the widget, on a worker thread, when the reference changes, and
   store it in the configuration. Downscale large references first (for
   example to 512 pixels on the long side); a cap on input size keeps loading
   fast and the table stable.

Applying the table to each pixel:

1. Brightness `t` of the pixel, bounded to 0-1. Use the same measure that
   built the table. Stage CR0 chooses it: `intensityF()` (consistent with
   Gradient Map) or a luminance from the profile's RGB primaries.
2. Look up the color at `t`, interpolating between entries.
3. **Keep brightness on**: give the looked-up color the original pixel's
   brightness, keeping its hue and saturation, for example by replacing the
   lightness in a hue/chroma/luma representation. Define the out-of-gamut
   rule (reduce chroma rather than clip channels). **Off**: use the looked-up
   color.
4. **Amount**: mix the original and the result. Amount 0 leaves the image
   unchanged.

Alpha and disabled channels are kept; fully transparent pixels are
untouched. The calculation is per pixel, so `neededRect()` and
`changedRect()` are the identity, it runs in parallel patches, and
reduced-resolution preview is allowed. Filter layers and masks are supported
after their save/reload tests pass.

Color models follow the other effects: RGB/Alpha U8, U16, F16 and F32 in
native values and profile; float values outside 0-1 are not clipped except
for the bounded brightness lookup. Gray/Alpha and other models go through
float RGB and back with a notice in the dialog (grayscale output stays gray,
so colored references have no visible color there).

### Reference image library

- Folder: `<resource folder>/colorreference/` (from
  `KoResourcePaths::getAppDataLocation()`). Copy loaded files into it, so a
  later move or deletion of the original does not affect the list. Keep the
  original file name, adding a number when it clashes.
- **Load** (the reference's ①): a file dialog with the formats
  `KisImportExportManager` can import, limited to raster images and .psd/.psb
  /.kra, read through the importer without opening a document window. Reject
  the 51st image with a message. Report unreadable files without adding them.
- **Delete** (the reference's ④): removes the selected user image from the
  folder after confirmation. Presets cannot be deleted.
- Loading from the photo library and from the camera (the reference's ② and
  ③) are excluded by the user.
- Thumbnails are generated from the files when the widget opens and cached in
  memory; regenerate them if loading 50 thumbnails each time proves slow
  (measure in stage CR1).
- Presets ship under `share/krita/colorreference/` (or are generated in code)
  and are listed before user images.

### Widget

- A Reference switch: Image / Gradient.
- Image: thumbnail grid (presets, then user images) with Load and Delete
  buttons below it.
- Gradient: `KisGradientChooser`, which offers tags as gradient sets, and an
  edit button that opens the gradient editor for a copy stored in the
  configuration, as Gradient Map does.
- Amount slider and Keep brightness checkbox.
- Thumbnails showing the result on the current layer, as in Color Filter,
  are optional; the reference's grid shows the reference images themselves.

Filter layer and mask property dialogs reuse the widget. In those, the image
list still works because it does not depend on the layer.

## Stages and completion gates

| Stage | Work | Completion gate |
| --- | --- | --- |
| CR0 | Confirm the decision table; choose the brightness measure, linear or encoded averaging, the table size and encoding, and the Keep brightness method | Decisions recorded here with dates; our own test references prepared |
| CR1 | Filter and gradient mode | Amount 0 is identity; gradient mode at Amount 100 with Keep brightness off matches Gradient Map Blend within a stated tolerance; patch equivalence; formats |
| CR2 | Image mode and the table builder | Deterministic table for a given image; empty-bin filling; table stored and reused without the file |
| CR3 | Reference image library and presets | Load/delete, the 50-image limit, unreadable files, clashing names, presets |
| CR4 | Filter layers and masks | Save/reload, missing reference image, reduced-resolution preview |
| Final | Documentation and manual checks | All checks recorded, including unverified items |

## Verification

Automated tests (`plugins/filters/solsticeeffects/tests/`):

- Amount 0 gives identical bytes; transparent pixels, alpha and disabled
  channels are kept;
- gradient mode against Gradient Map, as stated in CR1;
- Keep brightness keeps the measured brightness within tolerance;
- the table builder: a uniform image gives a constant table, a two-color
  image gives its two colors at their brightness, empty bins are filled,
  alpha weighting;
- whole image versus patches; RGB/Alpha and Gray/Alpha at U8/U16/F16/F32 and
  CMYK/Alpha and Lab/Alpha at U16; float values outside 0-1;
- configuration XML round trip, loading without the reference file, and
  widget load/save without unwanted change signals;
- library operations on a temporary folder: load, limit, delete, name clash,
  unreadable file. Tests that load documents use `setFileBatchMode(true)` and
  must not open dialogs.

Manual checks: each preset and a loaded image of every listed format; gradient
mode with tags and the edit button; Amount 0/50/80/100; Keep brightness on and
off; preview on/off, Cancel, Apply, Undo/Redo; a selection; a filter layer and
a mask after deleting the reference image and after reopening the file.

Build, test and install use the existing module:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritasolsticeeffectsfilter -j 2"
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && ctest --test-dir <krita-dev-root>\_build -R plugins-filters-solsticeeffects --output-on-failure"
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\filters\solsticeeffects\cmake_install.cmake
```

Bundled presets also need their install rule (in the module's
`CMakeLists.txt`) and a check that the portable package contains them. Do not
terminate a running Solstice to unlock DLLs; ask the user to close it, then
restart fully.

## Documentation

- Add the filter to `docs/effects-filters.md` when it ships: controls, the
  image library folder, supported formats, and the differences from CLIP
  STUDIO PAINT (no .clip, no photo library or camera).
- Add the touch points to `feature-inventory.md` and a README name/link entry
  for the shipped filter.
- Keep this document's status and actual file names current, and record each
  stage at the end of this document.
- After each committed stage, update the agent wiki and append to
  `wiki/log.md`.

## References

- User-supplied screenshots of the CLIP STUDIO PAINT Color Reference
  documentation (2026-10-15; not stored).
- `illustration-filters-plan.md` (Color Filter thumbnails, CPU-only rule) and
  `cpu-effects-and-gmic-plan.md` (module layout, test pattern).
- `settings-location.md` (resource folder), `vision-ml.md` (an existing
  plugin-owned user folder).
