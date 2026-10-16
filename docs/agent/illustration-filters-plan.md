# Illustration filters - implementation plan

Sharpness, Color Filter, Glow, Drop Shadow and Line Art Extraction.

## Status and decisions

Status: **stages S, C, G and D1 user verified; stage L implemented, built,
tested and installed; interactive Line Art Extraction verification pending**
(2026-10-16). All five planned filters now have native CPU implementations.
Stage D2 remains deferred.
The user requested implementing one feature at a time on 2026-10-15.
Work branch: `codex/illustration-filters`, created from the unmerged
`codex/cpu-effects-and-gmic` checkout with its existing uncommitted work retained.

The user asked (2026-10-15) whether five CLIP STUDIO PAINT filters could be
implemented in Solstice. The investigation found that all five can be built
as native filters. The user then asked for this plan and fixed the work order:

1. Sharpness
2. Color Filter
3. Glow
4. Drop Shadow
5. Line Art Extraction

The reference is a set of screenshots of CLIP STUDIO PAINT documentation that
the user supplied in the conversation. They are not stored in the repository.
They show the intended controls and visual results. They do not define an
algorithm, and the goal is not pixel-identical output. Use our own test
images, and do not bundle the reference artwork.

### Decisions and remaining stage-specific choices

| Decision | Current choice or proposal |
| --- | --- |
| Computation | CPU only, continuing the session's CPU-only policy. New filter math must not call helpers that can auto-dispatch to the GPU, such as `KisGaussianKernel::applyGaussian()` (`libs/image/kis_gaussian_kernel.cpp`, `runsOnGpu()`). |
| Branch | `codex/illustration-filters`, created 2026-10-15 from the existing effects checkout; its module is required by this plan. |
| Sharpness | Implementation choice, 2026-10-15: Strength 0-100, default 50; fixed Gaussian sigma 1 document pixel, truncated at radius 3; advanced detail size deferred pending artwork feedback. |
| Color Filter encoding | Implementation choice, 2026-10-15: native encoded RGB values, including temperature/tint gains; no ICC linearization that might clamp negative/HDR values. Artistic grading, not physical white balance. |
| Color Filter Gray | Implementation choice, 2026-10-15: native gray Brightness/Contrast/Sharpness only; disable Look and color controls with a notice. Unlike Film's float-RGB fallback, this preserves gray without an ICC round trip. |
| Color Filter presets | Implementation choice, 2026-10-15: switching Look preserves Amount/Adjust; slider edits mark Custom; Reset restores None/Amount 100/neutral Adjust. Store expanded Look values and a separate enable flag; preset ID is metadata only. |
| Glow outside opaque areas | Keep the original alpha by default (glow only where the layer has paint). An option to let the glow spread into transparent areas is a later refinement. |
| Glow controls and blur | Implementation choice, 2026-10-15: defaults Strength 50 / Effect area 50 / Blur range 30. Sigma = `0.0064*range^2` document pixels (0..64). Exact Gaussian through sigma 2, three variance-matched box passes beyond it. |
| Glow preview | Full-resolution only for this stage. LOD remains disabled because downsampling before the nonlinear bright-pass can lose small emitters; radius scaling alone does not establish visual equivalence. |
| Drop Shadow on-canvas handle | Stage D1 ships an offset pad inside the dialog. A handle on the canvas (stage D2) is built only if the stage 0 investigation shows it is feasible and the user approves it. |
| Drop Shadow controls | Implementation choice, 2026-10-15: Blur 0-100/default 20, Angle 0-359.9/default 45 degrees, Distance 0-1000/default 20 document pixels, Opacity 0-100/default 50, black shadow. Native document-space source-over; same CPU blur mapping as Glow. |
| Line Art controls | Implementation choice, 2026-10-16: Density 0-200/default 100; Black/default or Keep original; explicit Paper correction radius 8-256/default 32 document pixels. Zero density removes ink rather than bypassing extraction. |
| Line Art paper model | Local maximum and coverage-normalized CPU smoothing; dark estimates blend toward white to preserve solid/broad dark ink. RGB/Gray brightness is measured in native encoding; CMYK/Lab measurement uses float RGB. Keep original retains native channel bytes. LOD disabled. |
| Filter Gallery | Out of scope. A dialog showing every filter as a thumbnail, like CLIP STUDIO PAINT's Filter Gallery, is a separate user-interface project. |

Record each decision and its date here when the user confirms it.
Routine implementation choices are recorded separately from user confirmation;
stage-specific choices for later filters do not block the current stage.

## Investigation findings (2026-10-15)

| Filter | Reference controls | What Solstice already has | Gap |
| --- | --- | --- | --- |
| Sharpness | Strength | Unsharp Mask (`plugins/filters/unsharp/`, ID `unsharp`: `halfSize`, `amount`, `threshold`, `lightnessOnly`; reduced-resolution preview off). Sharpen (`convolutionfilters`, ID `sharpen`, fixed 3x3 kernel, no settings). Both are under Enhance. | The result is already possible. What is missing is a simple strength-only filter. Unsharp Mask blurs with `KisGaussianKernel::applyGaussian()`, which can run on the GPU for float RGBA devices. |
| Color Filter | Presets None, Vivid, Warm, Cool, Lemonade, Cobalt Blue, Aquamarine, Aurora, Sepia, Monotone, shown as thumbnails; Amount (0-100). Adjust tab: brightness, contrast, hue, saturation, color temperature, tint, sharpness. | HSV/HSL Adjustment, Color Balance, Color Adjustment (curves), ASC-CDL, Desaturate, Gradient Map (`plugins/filters/colorsfilters/`, `asccdl/`, `gradientmap/`). | There are no look presets, no amount control to blend a look with the original, no color temperature or tint, and no thumbnail selection. |
| Glow | Strength, effect area (which brightness glows), blur range | Nothing. The Outer Glow layer style (`libs/image/layerstyles/kis_ls_drop_shadow_filter.cpp`) glows around the layer's opacity, not around bright colors. | A new filter is needed. |
| Drop Shadow | Blur, angle, distance, opacity, shadow color, a position handle in the preview | The Drop Shadow layer style (`kis_ls_drop_shadow_filter.cpp`; settings in `libs/psdutils/psd.h`: angle, distance, size, spread, opacity, color, noise, contour, global light, knocks out). It is non-destructive and compatible with PSD files. | There is no filter form that can be applied to a layer or used in a filter mask, and there is no position handle. |
| Line Art Extraction | Density | Color to Alpha (`plugins/filters/colors/kis_color_to_alpha.cpp`), Threshold, Edge Detection, Pencil (which darkens contours but does not extract them) | Color to Alpha leaves an uneven gray paper partly opaque. The reference text points out this exact problem. Nothing removes the paper evenly. |

Framework facts that the plan relies on:

- `KisFilter::neededRect()`, `changedRect()`, `supportsLevelOfDetail()` and
  `needsTransparentPixels()` (`libs/image/filter/kis_filter.h`) define the
  source halo, growth into transparent areas and reduced-resolution preview.
- `KisBaseProcessor::createConfigurationWidget(parent, dev, useForMasks)`
  (`libs/image/kis_base_processor.h`) receives the paint device. Preset
  thumbnails can therefore be rendered from the current layer.
- `KisDlgFilter` is non-modal (`libs/ui/dialogs/kis_dlg_filter.cpp`,
  `setModal(false)`), and its preview is drawn on the canvas.
- The existing Solstice effects live in `plugins/filters/solsticeeffects/`
  (module `kritasolsticeeffectsfilter`, private static library
  `solsticeeffects`, shared helpers in `KisCpuEffectKernels.h`). Pencil
  already uses its own CPU smoothing to avoid GPU dispatch
  (`KisPencilFilter.cpp`).
- Filter categories come from `libs/image/filter/kis_filter_category_ids.h`.
  Menus are registry driven; do not edit `krita5.xmlgui`
  (`wiki/decisions/xmlgui-unchanged.md`).

## Architecture

### Module and IDs

Add all five filters to `plugins/filters/solsticeeffects/` and its tests to
`plugins/filters/solsticeeffects/tests/`. No new plugin directory and no
changes to shared libraries are planned. Proposed IDs; finalize them before
the first release that persists them:

| Filter | ID | Category |
| --- | --- | --- |
| Sharpness | `solstice_sharpness` | Enhance |
| Color Filter | `solstice_color_filter` | Adjust |
| Glow | `solstice_glow` | Artistic |
| Drop Shadow | `solstice_drop_shadow` | Artistic |
| Line Art Extraction | `solstice_line_art` | Other |

Once released, IDs and property meanings are compatibility contracts. Each
configuration carries `version` 1. Leave Unsharp Mask, Sharpen, Color to Alpha
and the layer styles unchanged.

### Shared CPU blur helper

Sharpness, Glow, Drop Shadow and Line Art Extraction all need smoothing. The
Sharpness stage delivers one shared helper in the `solsticeeffects` library:

- A separable Gaussian blur on float planes, computed on the CPU. It never
  calls `KisGaussianKernel::applyGaussian()` or another helper that can
  dispatch to the GPU.
- The primitive blurs scalar float planes, with no implicit alpha handling.
  Sharpness blurs brightness times coverage and coverage separately, then
  divides; Glow blurs premultiplied emission without this normalization, and
  Drop Shadow blurs a coverage mask. Never apply alpha twice.
- An explicit halo (`radius` mapped to a kernel half-width), used by
  `neededRect()`.
- Cancellation checks between rows.
- For large radii (Glow, Drop Shadow), a second path: either a cascade of box
  blurs or blurring at a reduced scale and resampling. The reduced grid must be
  anchored to document coordinates (aligned to the reduction factor), so a
  patch gives the same result as the full image. Stage G measures and chooses.

Line Art Extraction also needs a large-radius grayscale maximum (dilation).
Use a separable van Herk/Gil-Werman filter, which costs the same per pixel
for any radius. Add it to the same helper file in stage L.

### Common rules

These follow the existing effects (`docs/effects-filters.md`):

- Process RGB/Alpha U8, U16, F16 and F32 in the document's native channel
  values and profile, with no 8-bit intermediate. Integer output is limited
  to its format's range; float output is not clipped at 1.0 unless a filter
  explicitly says so. Other color models go through float RGB and back, and
  the dialog shows a notice. Gray/Alpha follows each filter's rule below.
- Keep disabled channels unchanged. Selections limit where the result is
  applied, not which source pixels are read.
- Results are anchored to document coordinates. The full image and any patch
  split must give the same result.
- Preview and final apply use the same code and settings. Enable
  reduced-resolution preview only after verifying that the radii scale
  correctly.
- Every change goes through the existing undoable filter stroke. Filter
  layers and filter masks are advertised only after their partial-update and
  save/reload tests pass.

## S: Sharpness

Controls: **Strength** (0-100, default to be set in stage 0). A
**Detail size** control (radius in document pixels) can be added as an
advanced setting if one fixed radius gives poor results on both line art and
painted texture. Decide in stage 0.

Calculation: unsharp masking of brightness only, using the shared CPU blur.

- Brightness detail = brightness - blurred brightness. Add
  `strength x gain x detail` to each color channel, keeping hue.
- Limit overshoot near hard edges so line art does not get bright halos. Use
  a soft clamp against the local minimum and maximum of the 3x3
  neighborhood, and test it on black lines over white.
- Colors are blurred with alpha weighting. Alpha itself is not sharpened.
- Strength 0 leaves the image unchanged.
- `neededRect()` grows by the blur halo. Reduced-resolution preview stays
  off, as for Unsharp Mask (detail smaller than a pixel at reduced resolution
  disappears).
- `changedRect()` also grows by the dependency halo, including the 3x3
  limiter's one-pixel footprint, so painting below a mask updates its neighbors.

The shared blur helper and its tests are part of this stage: compare it with a
reference float convolution, and check constant fields, alpha weighting and
patch equivalence.

Difficulty: medium, including the shared helper and alpha/partial-update tests.

## C: Color Filter

### Controls

- **Look** tab: a grid of thumbnails for None, Vivid, Warm, Cool, Lemonade,
  Cobalt Blue, Aquamarine, Aurora, Sepia and Monotone, plus **Amount**
  (0-100). Amount blends the look with the original: 0 means no change, and
  None means no change whatever the amount. Translate the names, and choose
  Solstice names where a literal translation would be unclear.
- Amount 0 and None bypass only Look. Adjust still applies; identity requires
  neutral Adjust controls too. Store look and adjustment parameters separately,
  and finalize preset-switch/reset/Custom behavior before stage C.
- **Adjust** tab: Brightness, Contrast, Hue, Saturation, Color temperature,
  Tint (each centered at 0) and Sharpness (0-100).

### Calculation

The look and adjustment steps run per pixel, in this documented order:

1. Look: a fixed set of internal parameters per preset (a tone curve,
   saturation, temperature/tint shift, shadow and highlight tinting, and a
   monochrome or sepia mix). Blend the result with the input by Amount.
2. Adjustments: brightness, contrast (pivot at mid-gray), hue rotation,
   saturation, then temperature and tint as white-balance gains on R/G/B.
   Stage C chose native encoded values for temperature/tint; see the decision
   table and implementation record for the HDR rationale and gain equations.
3. Sharpness: the Sharpness stage's calculation, run last. It is the only
   step that reads neighbors. `neededRect()` grows only when Sharpness is above
   0, and reduced-resolution preview is allowed only when it is 0.

Presets are starting points, like Film's presets. Selecting one copies its
values into the configuration, and editing a value marks the look as
**Custom**. Saved settings store the actual values, so later tuning of a
preset never changes an existing document. Loading saved settings never
reapplies a preset.

Gray/Alpha: stage C limits processing to native gray brightness, contrast and
Sharpness; all Looks and color adjustments are disabled with a notice.

### Thumbnails

Make one small copy of the layer when the dialog opens (for example 96 pixels
on the long side), and render each look from that copy. Render on a worker
thread or one thumbnail at a time, so the dialog never freezes. Rendering
must not touch the document. In filter masks, filter layers and other cases
where no usable device is available, use a built-in sample image instead.

CLIP STUDIO PAINT offers "apply to the selected layer" and "apply to
everything (adds a merged layer)". Solstice's existing equivalents are the
normal filter and a filter layer above the stack. A dedicated merge-and-apply
action is out of scope unless the user asks for it.

Difficulty: medium-high. Most of the work is color semantics, the interface
and tuning the looks.

## G: Glow

### Controls

- **Strength** (0-100): how bright the glow is.
- **Effect area** (0-100): which brightness glows. Low values make only the
  brightest parts glow; high values include more of the image.
- **Blur range** (0-100, mapped to a radius in document pixels; stage 0
  fixes the mapping and the maximum).

### Calculation

1. Bright pass: a smooth threshold on brightness, where the threshold comes
   from Effect area and has a soft knee, so there is no hard edge. Bright
   color = color x weight x alpha (premultiplied).
2. Blur the bright pass with the shared helper (the large-radius path when
   needed). Stage 0 compares one Gaussian with a sum of several radii
   (a bloom pyramid); keep whichever looks better at equal cost.
3. Combine with Screen in normalized values, scaled by Strength. For float
   values above 1.0, define and test the rule in stage 0 (for example, add
   the glow without clipping). Strength 0 means no change.

Alpha: by default the original alpha is kept, and transparent pixels are
untouched (see the decision table). Both `neededRect()` and `changedRect()`
grow by the blur dependency halo: unchanged alpha does not mean neighboring
colors are unaffected by an input edit. Reduced-resolution preview is allowed
after the radius scaling is verified, because Glow is a soft effect.

Risk: performance with large radii on 4K images. Measure in stage 0, and
optimize the CPU path (reduced-scale blur, bounded buffers) instead of
lowering final quality.

Difficulty: medium.

## D: Drop Shadow

### D1: the filter

Controls: **Blur**, **Angle** (0-359.9), **Distance** (document pixels),
**Opacity** (0-100), **Shadow color** (`KisColorButton`), and an
**offset pad**: a small square in the dialog where dragging a point sets Angle
and Distance together, standing in for the reference's on-canvas handle.

Calculation:

1. Shadow mask = source alpha, moved by the offset (bilinear sampling for
   fractional offsets).
2. Blur the mask with the shared helper (single channel).
3. Shadow = color x mask x opacity.
4. Draw the original over the shadow, so the shadow appears only where the
   original is not fully opaque.

The angle is the direction the shadow falls, clockwise in canvas coordinates,
matching Chromatic Aberration. The layer style uses a Photoshop light angle,
which is a different convention; document both and do not reuse one for the
other.

Bounds: `needsTransparentPixels()` returns true. `changedRect()` = rect
plus the rect moved by the offset and grown by the blur halo. `neededRect()`
= rect plus the rect moved back by the offset and grown by the blur halo.
Include the bilinear sample footprint and round fractional bounds outward.
Selections limit where the shadow is drawn.

Where it works: a layer with transparency, such as a character on its own
layer, or a filter mask on such a layer (the non-destructive form). On an
opaque or flattened layer the shadow is hidden under the image, the same as in
CLIP STUDIO PAINT. A filter layer works on the composite below it and is
usually opaque, so the user guide should recommend the filter mask. The
layer style stays available, unchanged, for PSD-compatible shadows.

Gray/Alpha works directly (the color is converted to gray). Other models use
the document color space for the shadow color; test CMYK and Lab at U16.

Difficulty: medium.

### D2: on-canvas handle (optional)

Investigate in stage 0 before committing:

- whether the canvas can receive input while the filter dialog's preview
  stroke is running, and how tools and shortcuts interact with that;
- a canvas decoration that draws the handle, its lifetime when views and
  documents change, and switching windows while the dialog is open;
- the filter mask and filter layer property dialogs, which reuse the
  configuration widget;
- press/release symmetry and focus conflicts with the existing tool.

Build D2 only if the user approves after these findings. Difficulty: high.

## L: Line Art Extraction

### Controls

- **Density**: 0-200, default 100. Higher values retain more faint lines;
  zero removes ink rather than bypassing extraction.
- **Line color**: Black (default) or Keep original (retains native source
  color channels, useful for colored pencil).
- **Paper correction radius**: 8-256 document pixels, default 32. Explicit
  rather than image-derived: image size alone does not identify line width.
  A fixed persisted radius also keeps patch/filter-mask dependencies stable.

### Calculation

1. Brightness, limited to 0-1.
2. Paper estimate: a large-radius grayscale maximum (removes lines, keeps the
   paper), then the shared blur to remove blockiness. The radius must be well
   above line width.
3. Flat-field correction: normalized = brightness / paper, limited to 0-1.
   This evens out shadows and gradients from photographing paper.
   Stage L must define a positive denominator floor and a fallback for an
   unreliable paper estimate; test solid black, wide dark regions and
   transparent boundaries explicitly so the estimate does not erase ink.
4. Ink = 1 - normalized. A noise floor removes paper grain and JPEG noise.
   Density sets the gain and the floor through a smooth curve.
5. Output alpha = source alpha x ink amount. Output color is black, or the
   source color when Line color is Keep original.

The output has a transparent background. It never adds opacity, so
`needsTransparentPixels()` stays false. `neededRect()` and `changedRect()`
grow by the sum of the maximum-filter and smoothing support radii.
Gray/Alpha works directly; RGB uses brightness only. Use as a filter
mask is a non-destructive option.

Test with our own scans and photos: white paper, off-white or colored paper,
uneven lighting, pencil, ink, and JPEG compression. A machine-learning line
extractor through Vision ML (`docs/agent/vision-ml.md`) is out of scope; it
would need a model with a suitable license.

Difficulty: medium-high. The main risk is quality on photographed paper.

## Work order and completion gates

| Stage | Work | Completion gate |
| --- | --- | --- |
| 0 | Confirm the decision table; fix ranges, defaults and color rules per filter; investigate D2; measure CPU baselines for blur radii | Decisions recorded here with dates; test images prepared (our own) |
| S | Sharpness and the shared CPU blur helper | Identity at 0, helper matches the reference convolution, patch equivalence, no GPU dispatch in the call graph |
| C | Color Filter | Looks and adjustments persist separately; Amount 0/None bypass Look and are identity only with neutral Adjust; thumbnails do not block the dialog |
| G | Glow, plus the large-radius blur path | Identity at 0, patch equivalence at large radii, measured preview and apply times |
| D | Drop Shadow D1 (D2 only if approved) | Correct growth bounds, filter mask save/reload, angle convention documented |
| L | Line Art Extraction and the large-radius maximum | Even paper removal on the test scans; Density behaves monotonically |
| Final | Cross-filter regressions and documentation | All automated and manual checks recorded, including unverified items |

Ask the user for an interactive check after each stage, and record the result
in an implementation record section at the end of this document, as
`cpu-effects-and-gmic-plan.md` does.

## Verification

Automated tests (one test file per filter in
`plugins/filters/solsticeeffects/tests/`, following the existing ones):

- zero or neutral settings give identical bytes where defined as bypass;
  Line Art density zero instead produces zero ink opacity;
- whole image versus patches, including negative coordinates;
- transparent and semi-transparent edges, alpha handling and disabled
  channels;
- RGB/Alpha and Gray/Alpha at U8/U16/F16/F32, CMYK/Alpha and Lab/Alpha at U16;
  float values outside 0-1;
- selection clipping, cancellation, configuration XML round trips, and widget
  load/save without unwanted change signals;
- for Drop Shadow and Glow, `neededRect()`/`changedRect()` against the
  pixels actually read and written;
- run with the GPU engine on and off; results must agree and the new code must
  not dispatch GPU work.

Manual checks for every filter: preview on/off, fast slider changes, Cancel,
Apply, Undo/Redo, reapply, save and reopen, selections, filter layers and
masks while painting under them, and reduced-resolution previews where enabled.

Performance: 2480x3508 and 4096x4096, RGBA8 and RGBA32F, small and large radii.
Record preview latency, final apply time and peak memory, as medians of three
runs with build and hardware stated.

Build and install use the existing module:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritasolsticeeffectsfilter -j 2"
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && ctest --test-dir <krita-dev-root>\_build -R plugins-filters-solsticeeffects --output-on-failure"
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\filters\solsticeeffects\cmake_install.cmake
```

Do not terminate a running Solstice to unlock DLLs; ask the user to close it,
then restart fully.

## Documentation

- Add each filter to `docs/effects-filters.md` only when it is implemented,
  with its controls, supported formats and limitations.
- Add the actual touch points to `feature-inventory.md` as they are
  introduced.
- Add README name/link entries only for shipped filters.
- Keep this document's status, stage records and actual file names current.
- After each committed stage, update the agent wiki (`wiki/index.md`) and
  append to `wiki/log.md`.

## References

- User-supplied screenshots of the CLIP STUDIO PAINT documentation for Drop
  Shadow, Glow, Color Filter, Line Art Extraction and Sharpness (2026-10-15;
  not stored).
- `cpu-effects-and-gmic-plan.md`: the CPU-only policy, module layout and test
  pattern this plan follows.
- Local guides: `codebase-map.md`, `extension-points.md`, `coding-rules.md`,
  `development-workflow.md`, `wiki/decisions/xmlgui-unchanged.md`.

## Stage S implementation record (2026-10-15)

Implemented on `codex/illustration-filters`. The user requested one feature
at a time and confirmed Sharpness operation on 2026-10-15, authorizing stage C.
Existing uncommitted
six-effect/G'MIC work and unrelated files were preserved.

### Sources and behavior

- `KisSharpnessFilter.*`: ID `solstice_sharpness`, Enhance category;
  configuration version 1, `strength` double 0-100, default 50. No GPU
  dispatch, filter-brush support or reduced-resolution preview. Normal
  filter strokes, filter masks and layers use the same processing code.
- `KisSharpnessConfigWidget.*`: one Strength slider; loading settings blocks
  change signals. RGB and Gray use native values; other color models show
  the float-RGB fallback notice. Gray sharpening is computed directly from
  its gray channel instead of an RGB round trip.
- `KisCpuPlaneBlur.*`: CPU separable convolution of scalar float planes,
  double accumulation with float intermediate storage. Sigma is capped at
  128; nonpositive/nonfinite sigma gives radius zero. Support is
  `ceil(3*sigma)`. The caller must supply the complete input halo; insufficient
  coverage returns false. No edge replication, implicit alpha multiplication,
  unpremultiplication or GPU-capable image helper is used. Each row checks
  cancellation, and output is published only after both passes complete.
  Stage G adds a separate large-radius path; the original Gaussian helper
  and Sharpness computation are unchanged.
- Sharpness uses fixed sigma 1, radius 3. Brightness is native Gray, or
  `.2126*R + .7152*G + .0722*B` in native RGB channel encoding (not a claim of
  profile-independent physical luminance). Blur brightness times alpha and
  alpha separately, then divide. Transparent/nonfinite samples do not
  contribute. Detail gain is `2*strength/100`.
- Limit the signed brightness delta with `limit*tanh(delta/limit)`, where
  limit is the available headroom to the visible 3x3 brightness min/max.
  Skip numerically flat deltas within `1e-6*max(1,abs(brightness))`. Apply the
  same delta to RGB; integer encoding can clip channels, float encoding
  retains negative/HDR values. Preserve alpha, disabled native channels and
  fully transparent source pixels. A hard black/white edge is already sharp
  and is deliberately left unchanged by the limiter.
- `neededRect()` and `changedRect()` both grow by 3 while enabled, and neither
  grows at Strength 0. Old transaction pixels provide a consistent source
  across concurrent patches. A patch uses scratch output and only copies it
  back after cancellation checks; no partial patch is published on cancel.
- Registered in `SolsticeEffectsPlugin.cpp`; module remains
  `kritasolsticeeffectsfilter`, helper code remains in private static library
  `solsticeeffects`. No shared image/UI library implementation was changed.

### Validation

`KisSharpnessFilterTest` covers an independent 2D Gaussian reference at four
sigmas, patch equivalence, zero identity, RGB/Gray U8/U16/F16/F32 and CMYK/Lab
U16, negative coordinates, moved devices, concurrent patches, alpha and
disabled channels, flat HDR/hidden colors, soft versus hard edges, partial
invalidation, feathered selection, Undo/Redo and cancellation in three phases.
It also checks silent widget loading, XML round trips, installed plugin
registration and actual filter-mask application after XML restoration.
Mask comparison ignores hidden RGB only where alpha is zero, because the
standard mask compositor may clear those values; direct filter checks still
require byte-exact preservation there.

Observed: **19 passed, 0 failed, 1 skipped** (timing opt-in skipped) with the
review-image opt-in enabled. All **six solsticeeffects CTest suites passed**.
The timing-only run passed separately. UI and strength 0/50/100 comparison
PNGs were generated from a synthetic pattern and visually inspected.
`kritasolsticeeffectsfilter` and the branch-dependent `kritaversion` DLL were
built and installed in the local test prefix. No new commit or release was
created.

CPU timings: Ryzen 9 9950X, Windows 11, Clang 21.1.6, Qt 6.8.0,
RelWithDebInfo/O2. Median of three single-call filter applications to a
varying color/alpha pattern; source generation excluded. Strength 50,
sigma 1. These are processing times, not measured UI preview latency or
parallel stroke timings.

| Image | RGBA8 | RGBA32F |
| --- | --- | --- |
| 1024x1024 | 68 ms | 71 ms |
| 2480x3508 | 577 ms | 580 ms |
| 4096x4096 | 1136 ms | 1120 ms |

The benchmark process's peak working-set counter was observed at about
1.15 GiB during its F32 workload, including test source construction and
whole-image scratch buffers. This observation is not an isolated per-filter
allocation measurement. Normal stroke jobs may split the work into patches.
Logs and images are in the task scratch directory (`sharpness-tests.txt`,
`sharpness-ctest.txt`, `sharpness-benchmark.txt`, `sharpness-widget.png`,
`sharpness-comparison.png`). Reproduce with:

```bat
set QT_QPA_PLATFORM=offscreen
set QT_QPA_FONTDIR=C:\Windows\Fonts
set KRITA_NO_ASSERT_MSG=1
<krita-dev-root>\_build\bin\KisSharpnessFilterTest.exe
set SOLSTICE_BENCHMARK_SHARPNESS=1
<krita-dev-root>\_build\bin\KisSharpnessFilterTest.exe benchmarkCpu
```

User hand-off result: **operation confirmed on 2026-10-15**. This is the
user's general acceptance, not a claim that every individual manual case was
reported. The regression checklist remains: Strength 0/50/100 on line art and
painted texture, preview/Cancel, Apply/Undo/Redo, soft selections, transparent
edges, painting below a mask/layer, `.kra` save/reopen and GPU engine on/off.

## Stage C implementation record (2026-10-15)

Implemented after the user's Sharpness confirmation, on the same branch.
The user confirmed Color Filter operation on 2026-10-15 and authorized stage G.

### Sources and persistence

- `KisColorFilter.*`: ID `solstice_color_filter`, Adjust category, version 1;
  CPU only, threaded filter strokes, masks and layers; no filter-brush support.
- `KisColorFilterConfigWidget.*`: Look grid with ten thumbnails, Amount,
  seven Adjust sliders, Custom label and full reset. Hue is -180..180 degrees,
  Sharpness 0..100, other Adjust controls -100..100. Defaults are None,
  Amount 100 and neutral Adjust.
- `preset` is metadata (0..9 in the displayed order, -1 Custom).
  `lookEnabled` controls Look independently. The `look` prefix stores expanded
  `Brightness`, `Contrast`, `Hue`, `Saturation`, `Temperature`, `Tint`,
  `Curve`, `Monochrome`, `Sepia`, `ShadowR/G/B`, `HighlightR/G/B` values.
  `amount` blends Look only. The `adjust` prefix stores the seven visible
  adjustment values. Preset switching rewrites only Look values and metadata;
  Amount/Adjust remain. Loading copies properties without invoking presets or
  emitting change signals. Reset restores every default.
- Actual preset recipes are in `applyPreset()`. They are original Solstice
  values, not a reconstruction of CLIP STUDIO's private algorithms.

### Math and boundaries

For each visible pixel, decode native RGB (or float RGB conversion for
CMYK/Lab), apply Look, blend it by Amount, then apply Adjust. Keep the entire
result and its Sharpness halo in an RGBA32F device with the working RGB
profile. The intermediate is never quantized to U8/U16/F16. Finally apply
Sharpness and encode once to the original format, copying only enabled color
channels. Gray uses its native gray value and ignores all Look/color controls.

The grading primitive adds `brightness/200`, then applies contrast around
0.5 with gain `2^(contrast/100)`. Hue rotates about the RGB gray axis;
saturation mixes around `.2126*R + .7152*G + .0722*B` with gain
`1+saturation/100`. Temperature/tint gains are
`2^(.004*T+.0015*tint)`, `2^(-.003*tint)`,
`2^(-.004*T+.0015*tint)` for R/G/B. These operate on native encoded channels;
there is no claim of calibrated temperature, physical luminance or
profile-independent appearance. This follows the earlier effects' native
channel convention and avoids TRC conversions that can constrain HDR values.

Looks may additionally apply a bounded SDR tone-curve perturbation
`(curve/100)*(v-.5)*v*(1-v)` inside 0..1 (identity continuation outside),
quadratic shadow/highlight tints using clamped brightness, monochrome mixing,
and sepia mixing with brightness gains `(1.12,.99,.78)`. All coefficients are
decoded once per patch, with bounded finite values. Nonfinite input colors
are preserved and excluded from Sharpness; unrepresentable intermediate RGB
is not written back. Float output is not clamped to 0..1.

`KisSharpnessFilter::applyCpu()` exposes the same private-module CPU kernel
with a caller-selected progress range; the standalone filter still delegates
to it with 0..100. Color Filter maps grading to 0..30, sharpening to 30..85
and final encoding to 85..100. It does not invoke a nested filter stroke or
GPU-capable helper. The sharpness radius is 3 when enabled, otherwise zero;
both neededRect and changedRect use this halo. LOD is supported only when
Sharpness is zero. The halo is color-transformed before sharpening.

Original transaction pixels are the source for every patch. Scratch output
inherits the destination origin/default pixel so the final optimized copy
preserves hidden RGB in transparent pixels even for moved devices. Alpha and
disabled channels are copied byte-for-byte. Cancellation discards scratch
output before publication; selection composition and Undo/Redo remain in the
standard filter framework.

### Thumbnail lifetime

The widget locates its own ancestor main window, verifies that its active
node owns the supplied device, and uses `tryBarrierLock(true)` to capture a
copy-on-write snapshot of the current full-resolution frame. It never waits
for a stroke or uses another window's global active view. Detach the snapshot's
default bounds from live animation/LOD state before queuing work.

One queued callback reduces the snapshot to at most 96x72, keeping aspect
ratio and never upscaling small images. Subsequent callbacks render one Look
at a time, with Amount 100 and neutral Adjust. Qt context-bound callbacks are
discarded on widget destruction. Only private snapshots are processed;
document pixels are never modified. When there is no safe owning view/device,
the image is busy, the layer is empty, or `useForMasks` is true, use the
built-in synthetic sample. The caption states which source is used. Canvas
preview, not the static thumbnails, reflects current sliders.

### Validation and performance

`KisColorFilterTest` covers RGB/Gray U8/U16/F16/F32 and CMYK/Lab U16, native
alpha/hidden-pixel preservation, all ten looks, Amount 0 with non-neutral
Adjust, independent expanded-value/Amount equations, Gray restrictions, HDR,
the color-then-Sharpness order, concurrent patches at negative coordinates,
moved devices, partial invalidation, disabled channels, feathered selection,
Undo/Redo, cancellation in four phases and monotonic progress. Widget checks
cover silent load, expanded-value XML persistence, Custom/preset/reset
semantics, completed thumbnails without config signals and destruction while
thumbnail work is queued. Actual filter-mask application after XML restoration
is compared to direct filtering. Full `.kra` document IO and actual canvas
interaction remain manual checks.

Final Color Filter run: **20 passed, 0 failed, 1 skipped** (timings opt-in;
timing run passed separately). All seven solsticeeffects CTest suites passed.
Look/Adjust widget PNGs from
the synthetic sample were visually inspected. The module was built and
installed in the local test prefix; no commit or release was created.

CPU timing environment matches stage S: Ryzen 9 9950X, Windows 11, Clang
21.1.6, Qt 6.8.0, RelWithDebInfo/O2. Aurora, Contrast 12, Amount 100;
median of three single-call applications, source construction excluded.

| Image | Sharpness | RGBA8 | RGBA32F |
| --- | --- | --- | --- |
| 2480x3508 | 0 | 769 ms | 729 ms |
| 2480x3508 | 50 | 1279 ms | 1292 ms |
| 4096x4096 | 0 | 1448 ms | 1432 ms |
| 4096x4096 | 50 | 2559 ms | 2625 ms |

These are processing times, not UI preview latency or parallel stroke
timings. Full-workload peak memory was not isolated; a process working-set
peak counter of about 1.16 GiB was observed during the F32 workload, including
test source construction and scratch buffers. Timings and artifacts live in
the task scratch directory as `color-filter-tests.txt`, `color-filter-ctest.txt`,
`color-filter-benchmark.txt`, `color-filter-look.png`, `color-filter-adjust.png`.

Reproduce with the stage S test environment and `KisColorFilterTest.exe`;
use `-o <log-path>,txt` for QtTest output on Windows. Set
`SOLSTICE_BENCHMARK_COLOR_FILTER=1` and select `benchmarkCpu` for timings;
`SOLSTICE_COLOR_FILTER_ARTIFACT_DIR=<directory>` enables review PNGs.

Manual hand-off: restart Solstice, open Filter > Adjust > Color Filter;
check the ten Looks, Amount 0/50/100, None plus Adjust, switching Looks while
keeping Adjust, reset, preview/Cancel, Apply/Undo/Redo, reapply, selections,
and transparent edges. Check layer-snapshot thumbnails and the sample fallback,
switch/close documents while opening/closing the dialog, paint below a filter
mask/layer, and save/reopen with a custom Look. Compare GPU engine on/off and
Instant Preview with Sharpness 0 versus enabled. **Operation confirmed by the
user on 2026-10-15**; this general acceptance does not assert that every
individual manual case above was reported.

## Stage G implementation record (2026-10-15)

Implemented after Color Filter's user confirmation. Drop Shadow D1 is next,
after the Glow hand-off; no other stage was implemented in this change.

### Sources and settings

- `KisGlowFilter.*`: ID `solstice_glow`, Artistic category, configuration
  version 1. `strength`, `effectArea`, `blurRange` are doubles 0..100, defaults
  50/50/30. Nonfinite controls fall back to defaults; out-of-range values clamp.
- `KisGlowConfigWidget.*`: three sliders, explanations for area/range and a
  notice that alpha is retained. Other color models display the RGB conversion
  notice. Configuration loading blocks change signals.
- `KisCpuPlaneBlur.*`: adds `softBlur()` and `softBlurHalo()`; unchanged
  `gaussianBlur()` remains used by Sharpness. This is private-module code;
  no shared Krita library or GPU implementation changed.
- Registered in `SolsticeEffectsPlugin.cpp` and the existing CMake module.
  Supports threaded strokes, filter masks and filter layers, but not filter
  painting or LOD. Standard filter-stroke preview/apply and Undo/Redo are used.

### Bright-pass, blur and composite

RGB uses its native profile/channel encoding. Gray reads/writes its native
gray channel without conversion. CMYK/Lab use the existing patch-local
float-RGB converters. Compute positive-channel brightness
`L=.2126*max(R,0)+.7152*max(G,0)+.0722*max(B,0)` (native gray for Gray).
With threshold `1-effectArea/100`, let
`t=clamp((L-threshold+0.1)/0.2,0,1)` and `weight=t*t*(3-2*t)`.
Emission is `max(channel,0)*weight*clamp(alpha,0,1)`. Transparent/nonfinite
input does not emit. Each scalar emission plane is blurred independently;
there is **no division by blurred alpha**, which would incorrectly brighten
low-coverage emitters.

Sigma is `0.0064*blurRange^2`: default 5.76, maximum 64 pixels. Sigma zero
copies emission (a highlight-brightening effect with no spread). Through
sigma 2 use the existing exact separable Gaussian; above it approximate one
Gaussian with three separable box blurs. The box radii are integers, so small
slider changes can share a radius combination. This is not a pixel-exact
Gaussian at large radii.

For source channel `c` and blurred emission `e`, let `g=e*strength/50` and
write `c+g-clamp(c,0,1)*min(g,1)`. This is Screen when c and g are in 0..1,
with continuous additive excess outside SDR. It never reduces a finite
source channel, does not introduce a discontinuity at 1, and does not clamp
float output to 1. Integer output is limited at encoding. Nonfinite source
colors and unrepresentable float results are preserved rather than published.

Keep alpha, disabled channels and fully transparent source pixels byte-exact.
Strength zero returns immediately. Use native scratch output with matching
origin/default pixel before copying it back, retaining hidden RGB on moved
devices. Reads use old transaction data, so parallel jobs see the same input.
Cancellation discards unpublished output. Selections are applied by the
standard framework after processing, and do not restrict the source halo.

### Large-radius helper and patch invariance

For sigma s, choose `r=floor((sqrt(1+4*s*s)-1)/2)`. Three radius-r boxes
have variance `r*(r+1)`. Replacing one by radius r+1 adds `(2*r+2)/3` to the
variance. Use the nearest number (0..3) of larger boxes to match the requested
variance. The helper caps sigma at 128, like the exact Gaussian helper.

Both axes use rolling double sums with float intermediate planes. Rebuild
each sum at multiples of 16 **document pixels**, using mathematical floor at
negative coordinates; perform additions/subtractions in the same order even
when a patch starts inside that block. This avoids patch-dependent rounding
from starting a rolling sum at each arbitrary patch boundary. Inputs must
include this numerical footprint as well as the visual blur support.

`softBlurHalo()` is the sum of the three radii plus 45 (15 pixels per pass),
used symmetrically as a conservative bound. Default Glow halo is 61 pixels;
maximum is 236. Exact Gaussian mode uses `ceil(3*sigma)`. Both neededRect
and changedRect grow by the returned halo; Strength zero grows neither.
The additional footprint is needed for numerical consistency, even where
the theoretical box kernel no longer has visual support. Helper output is
swapped in only after all passes succeed. Cancellation is checked per row
or 16-row block; input planes are released as their channel finishes.

### Validation and algorithm choice

`KisGlowFilterTest` checks the box approximation against an independent direct
convolution of three known box kernels, incomplete-halo rejection, cancelled
output preservation, and exact patch equality at sigma 0/1/3/8/64. Filter
tests cover RGB/Gray U8/U16/F16/F32 and CMYK/Lab U16, Strength 0 identity,
concurrent patches and negative/moved coordinates, blur ranges 0/10/30/100,
hidden colors, byte-exact alpha, disabled channels, feathered selections,
Undo/Redo, partial invalidation, silent widget load, XML restoration, installed
plugin registration and actual filter-mask application. Numeric tests cover
SDR/negative/HDR Screen, proportional source coverage and increasing Effect
area. The CMYK pattern test uses Effect area 100 to ensure its dark converted
colors actually emit light; a default bright-pass can correctly leave it unchanged.

Half-float overflow and nonfinite source preservation are also covered.
Observed: **22 passed, 0 failed, 1 skipped** (timing opt-in). All **eight
solsticeeffects CTest suites passed**. Synthetic Glow strength 0/50/100 and
widget images were generated and visually inspected; no reference artwork
was bundled. The module was built and installed into the test prefix.

An opt-in 256x256 comparison used exact Gaussian, three boxes and a two-scale
mixture (half sigma and sqrt(1.75) sigma with equal weights, same variance).
At sigma 32, observed single-run timings were 14/1/3 ms respectively;
box-versus-Gaussian RMS error was about 0.0020 for a unit-valued disk/square
source (about 0.0029 at sigma 4). These are small illustrative measurements,
not general quality bounds or performance guarantees. The comparison image
showed a similarly smooth, broad falloff for Gaussian and three boxes; the
two-scale mixture retained stronger central cores and cost more. Stage G
therefore uses one Gaussian-like blur instead of a multi-scale bloom.

Reduced-resolution preview stays off: a nonlinear bright-pass after image
downsampling can miss small highlights even with a correctly scaled radius.
Native preview/final math is shared. GPU engine on/off canvas checks,
full `.kra` save/reopen and actual UI behavior remain interactive checks.

CPU measurements: Ryzen 9 9950X / Windows 11, Clang 21.1.6, Qt 6.8.0,
RelWithDebInfo/O2. Strength 50, Effect area 50, varying color/alpha pattern,
median of three single-call applications, source construction excluded.

| Image | Blur range | RGBA8 | RGBA32F |
| --- | --- | --- | --- |
| 2480x3508 | 10 | 503 ms | 499 ms |
| 2480x3508 | 100 | 1008 ms | 980 ms |
| 4096x4096 | 10 | 969 ms | 987 ms |
| 4096x4096 | 100 | 1912 ms | 1866 ms |

These are processing times, not actual UI preview latency or parallel stroke
timings. A process peak-working-set counter of about 1.34 GiB was observed
during the F32 workload, including source construction and scratch planes;
this is not an isolated per-filter memory measurement. The timing-only run
passed separately. No commit or release was created.

Logs and images in the task scratch directory: `glow-tests.txt`,
`glow-ctest.txt`, `glow-benchmark.txt`, `glow-comparison.png`,
`glow-widget.png`, `glow-blur-strategies.png`. Reproduce with the stage S test
environment and `KisGlowFilterTest.exe -o <log-path>,txt`.
`SOLSTICE_GLOW_ARTIFACT_DIR=<directory>` enables the comparison PNGs;
`SOLSTICE_BENCHMARK_GLOW=1` with `benchmarkCpu` enables full-image timings.

Manual hand-off: restart Solstice and open Filter > Artistic > Glow. Check
Strength 0/50/100, Effect area on dark versus bright regions, Blur range
0/30/100, transparency/soft selections, preview/Cancel, Apply/Undo/Redo,
reapply, painting below a filter mask/layer, and save/reopen. Verify the same
appearance with the GPU engine on/off. **User confirmed Glow operation on
2026-10-15 and requested the next stage.**

## Stage D1 implementation record (2026-10-15)

Implemented after the user's Glow confirmation, on the existing
`codex/illustration-filters` branch. Line Art Extraction is next after this
hand-off; D2 remains deferred. No shared library or canvas input code changed.

### Files and configuration

- `KisDropShadowFilter.*`: ID `solstice_drop_shadow`, Artistic category,
  configuration version 1. Threaded patches, filter masks/layers and
  transparent-pixel processing are enabled. Painting and LOD are disabled.
- `KisDropShadowConfigWidget.*`: four sliders, a `KisColorButton` and the
  owned position-pad widget. Slider object names are `dropShadowBlur`,
  `dropShadowAngle`, `dropShadowDistance`, `dropShadowOpacity`; color/pad
  names are `dropShadowColor` and `dropShadowOffsetPad`.
- `SolsticeEffectsPlugin.cpp`, module CMake and test CMake register the
  filter, sources and `KisDropShadowFilterTest` target.

Properties: `blur` 0..100/default 20, `angle` 0..359.9/default 45,
`distance` 0..1000/default 20, `opacity` 0..100/default 50, `shadowColor`
as serialized KoColor/default black. The calculation wraps angles by 360
and clamps bounded controls; nonfinite controls fall back to defaults.
The color picker disables alpha; only `opacity` controls shadow opacity.
XML retains the chosen color and its profile. Loading the widget is silent.

### Computation and bounds

Only CPU code is used. Convert the shadow color once per patch into the
document color space using an owned converter, avoiding a shared mutable
conversion cache. Source colors stay in their native channel encoding,
including Gray, CMYK and Lab; no RGB round trip is required for this filter.
RGB/Gray U8/U16/F16/F32 and CMYK/Lab U16 are tested.

Sample alpha at `(x-dx,y-dy)` with bilinear interpolation onto a scalar
plane aligned to destination document coordinates. Clamp finite source
coverage to 0..1 and treat nonfinite coverage as zero for shadow generation.
Snap offsets within 1e-10 of an integer to that integer, so cardinal angles
do not accidentally add a fractional sampling footprint. Blur with the
stage G helper, sigma `0.0064*blur^2`: exact Gaussian through sigma 2,
three variance-matched boxes beyond it. The largest sigma is 64.

For original straight color C and alpha a, shadow color T and blurred
coverage m, let `b = clamp(m,0,1)*opacity*(1-a)`. Output alpha is `a+b`;
output straight color is `(C*a + T*b)/(a+b)`. With a=0, ignore hidden RGB
entirely. Fully opaque pixels and pixels with no shadow contribution retain
their original bytes. Invalid source alpha or a nonfinite/overflowing color
result leaves the original pixel intact. Float colors are not clamped to 1.
Copy back only enabled color/alpha channels; alpha can be disabled separately.
Opacity zero is byte identity and adds no dependency bounds.

For helper halo h, union the original rect with the offset rect expanded by
h. In neededRect use `(-dx,-dy)`; in changedRect use `(dx,dy)`. Lower edges
use floor and upper edges ceil, including the bilinear footprint. The halo
includes the box helper's document-anchored numerical footprint. Default
halo is 51 pixels; maximum is 236. An outer transaction supplies the stable
source when patches run concurrently. Scratch output retains device origin
and default pixel; cancellation exits before any copy-back.

Selections constrain the applied result, not the source footprint. A filter
mask on transparent paint is the recommended non-destructive form. An
opaque composite under a filter layer hides the shadow. The PSD-compatible
layer style and its distinct light-angle convention remain unchanged.

### Position pad lifecycle

The dialog owns the pad, which owns no canvas/view pointer and installs no
event filter. Left press starts a drag; matching release ends it. Mouse
ungrab, window deactivation and hide also clear the drag. Other buttons do
not change settings. Arrow keys move by one document pixel, Shift by ten.
Coordinates use right=0 and down=90 degrees. The pad radius is at least 32
document pixels and otherwise the distance rounded up to a multiple of 32;
it stays fixed during a drag and readjusts after release. Slider signals
are blocked when updating Angle/Distance together. The real preview remains
the ordinary filter preview on the canvas. On-canvas handles are not part
of D1, and no shared tool/shortcut behavior is changed.

### Validation and measurements

Built and installed `kritasolsticeeffectsfilter` in the test prefix.
`KisDropShadowFilterTest`: **18 passed, 0 failed, 1 skipped** (opt-in timing).
All **nine solsticeeffects CTest suites passed**. Coverage includes:

- Ten color/depth formats, parallel patch equality at negative/moved
  coordinates, opacity-zero identity and opaque-pixel preservation.
- Independent native-channel source-over values, half-pixel bilinear
  offsets, cardinal bounds and hidden HDR colors under zero alpha.
- Small/large blur patch equality, changedRect erasure/partial updates,
  disabled color and alpha channels, soft selections and Undo/Redo.
- Silent UI load, settings XML with a non-default color, left/right-button
  and keyboard pad behavior, interrupted drag, cancellation during sampling,
  before blur and during encoding, and actual registered filter-mask application.

The analytic color test specifies exact native F32 channel values: a Qt
named color converted through ICC is not a suitable exact numeric oracle.
Synthetic hard/soft shadow images and a widget image were rendered and
visually inspected; reference artwork was not bundled. Full application
input behavior, `.kra` save/reopen and GPU-engine on/off appearance still
need the user's interactive check.

CPU measurements: Ryzen 9 9950X / Windows 11, Clang 21.1.6, Qt 6.8.0,
RelWithDebInfo/O2; default angle/distance/opacity, varying color/alpha
pattern. Median of three single-call applications, excluding construction:

| Image | Blur | RGBA8 | RGBA32F |
| --- | --- | --- | --- |
| 2480x3508 | 10 | 421 ms | 425 ms |
| 2480x3508 | 100 | 650 ms | 632 ms |
| 4096x4096 | 10 | 839 ms | 879 ms |
| 4096x4096 | 100 | 1236 ms | 1219 ms |

These are kernel application times, not UI latency or parallel filter-stroke
times. Timing-only checks passed separately. No commit or release was made.
Reproduce using the stage S environment and `KisDropShadowFilterTest.exe
-o <log-path>,txt`. `SOLSTICE_DROP_SHADOW_ARTIFACT_DIR=<directory>` enables
PNG output; `SOLSTICE_BENCHMARK_DROP_SHADOW=1` with `benchmarkCpu` enables
full-image timings. Scratch records: `drop-shadow-tests.txt`,
`drop-shadow-ctest.txt`, `drop-shadow-benchmark.txt`,
`drop-shadow-comparison.png`, `drop-shadow-widget.png`.

Manual hand-off: restart Solstice, select a layer with a transparent
background and open Filter > Artistic > Drop Shadow. Check blur 0/20/100,
angle 0/90/180/270, distance including fractional values, opacity 0/50/100,
colored shadows, pad drag/arrows/Shift, and preview Cancel/Apply/Undo/Redo.
Check soft selections, painting/erasing below a filter mask, save/reopen,
and the same appearance with the GPU engine on/off. **User confirmed Drop
Shadow operation and requested Line Art Extraction (recorded 2026-10-16).**

## Stage L implementation record (2026-10-16)

Implemented after the user's Drop Shadow confirmation on the existing
`codex/illustration-filters` branch. All five filters are implemented; Line
Art's interactive check is still pending. No GPU kernel or shared library
was changed. D2 and Filter Gallery remain outside this implementation.

### Files and persisted settings

- `KisLineArtFilter.*`: `solstice_line_art`, category Other, version 1.
  Threading and adjustment layers/masks supported; painting and LOD disabled;
  `needsTransparentPixels()` false because extraction never adds coverage.
- `KisLineArtConfigWidget.*`: `lineArtDensity`, `lineArtColor` and
  `lineArtPaperRadius` controls. Widget loading blocks signals; no document
  or canvas pointers are retained.
- `KisCpuPlaneBlur.*`: new scalar CPU `maximumFilter()` helper. Existing
  Gaussian/box calculations remain unchanged.
- Module registration/CMake and test CMake add the filter and
  `KisLineArtFilterTest`.

Properties: `density` double 0..200/default 100; `paperRadius` integer
8..256/default 32; `lineColor` string `black`/default or `original`.
Unknown color strings fall back to black; density is bounded with a
nonfinite fallback of 100; radius is bounded. Zero density produces zero
ink, not identity. Preserve these property meanings in saved masks/layers.

The radius is explicit in document pixels. A radius inferred from image
size cannot distinguish thin pencil from broad ink at the same resolution;
it could also change mask output on a canvas resize. Users can select a
radius larger than line width, while keeping it small enough to follow
lighting variations. No image-derived default changes the saved settings.

### CPU paper estimate

1. Read native RGB brightness `0.2126 R + 0.7152 G + 0.0722 B`, or native
   gray, limited to 0..1. These are encoded-channel artistic measurements,
   not ICC-linear luminance. Other models use the existing private-per-patch
   float RGB sampler for measurement only.
2. Pixels with zero/invalid alpha or nonfinite measured color have brightness
   sentinel -1. They are missing observations, not white paper. Apply the
   square grayscale maximum with radius r. For each valid maximum, set a
   support plane to 1; missing maxima become 0 with support 0.
3. Blur maxima and support separately with sigma r/4, using the stage G CPU
   helper (Gaussian at sigma <=2, three boxes above it). Divide the smoothed
   maximum by smoothed support, with a support threshold 1e-6 and fallback
   estimate 1. This avoids artificial dark paper near transparent boundaries.
4. A dark local maximum cannot distinguish dark paper from a broad ink fill.
   Let `t=smoothstep(0.35,0.65,estimate)`, and use denominator
   `max(0.35, 1-t+t*estimate)`. Reliable light paper uses flat-field
   normalization; unreliable dark regions blend toward white, preserving
   solid black and broad dark ink instead of dividing black by black.

`maximumFilter()` is a separable van Herk/Gil-Werman maximum: prefix and
suffix maxima over blocks of length `2*r+1`; combine the suffix at the
window start with the prefix at its end. Cost is linear in the processed
pixels rather than radius per pixel. Finite input is required (the filter
sanitizes it). Maximum has no accumulation rounding; patch-local block
origins yield the same maximum. Inputs must include the full halo, or the
helper rejects them without writing output. A scratch result is swapped in
only after success, and cancellation is polled on every row/column.

### Ink and channel handling

Let `dark=1-clamp(brightness/denominator,0,1)`, `gain=density/100` and
`floor=0.06-0.02*gain`. Ink is
`min(1, gain*dark*smoothstep(floor,2*floor,dark))`. This gives a smooth noise
floor; raising density both lowers the floor and raises gain, so ink is
monotonic in density for a fixed image/radius. At zero density, skip paper
estimation and output zero ink for valid source pixels.

Output alpha is source alpha times ink. Black writes native black color
channels (an owned converter maps black into non-RGB/Gray spaces). Keep
original copies native source channel bytes, with no encode/conversion round
trip, including negative/HDR values. Brightness alone is bounded. Existing
fully transparent pixels and visible pixels with invalid alpha/color stay
unchanged; their hidden colors cannot affect nearby paper estimates.
Only enabled color/alpha channels are copied back, and selections restrict
the final application. Alpha must be enabled to remove paper.

`neededRect()` and `changedRect()` grow by `r + softBlurHalo(r/4)`;
default radius 32 gives halo 100, maximum radius 256 gives halo 492.
Density zero grows neither. Source reads use old transaction data for
concurrent patches. The blur helper retains document-coordinate anchoring,
including its numerical halo, so full and partitioned results match exactly.
Scratch output retains the source device origin/default pixel. Cancellation
before copy-back leaves the device untouched. No GPU-dispatch helper is used.

### Quality limits and verification

This is a local dark-mark extractor for light paper. It is not semantic
line recognition. A broad midtone region can resemble paper; abrupt cast
shadows can resemble ink. Dark/strongly colored paper, bright lines on dark
paper and heavy shading may need manual correction. Increasing density may
also reveal noise. Real photographed drawings still need user evaluation;
generated charts are not a substitute for that check.

`KisLineArtFilterTest`: **18 passed, 0 failed, 1 skipped** (opt-in timing).
All **ten solsticeeffects CTest suites passed** after rebuilding affected
tests. Coverage includes:

- The maximum helper against independent brute-force windows at radii
  0/1/3/8/32/256, negative coordinates, patches, inadequate halo rejection
  and interrupted-output preservation.
- RGB/Gray U8/U16/F16/F32 and CMYK/Lab U16; concurrent patch equality;
  no alpha increase; transparent preservation; byte-exact Keep original.
- White/off-white paper including transparent borders; solid black and
  broad dark fills; analytic semi-transparent line values; monotonic density
  0/25/50/100/150/200; invalid/hidden HDR data and visible nonfinite input.
- A generated colored-paper chart with slow illumination gradients, grain,
  ink and pencil lines compressed as JPEG quality 85. Default extraction
  leaves sampled paper below alpha 3/255 while sampled ink exceeds 100/255
  and pencil exceeds 25/255 (means over 150 rows).
- Radii 8/32/256, changedRect clearing, disabled color/alpha channels,
  feathered selections, Undo/Redo, silent widget loading, settings XML,
  actual registered filter-mask application and cancellation at sampling,
  stage boundaries and encoding.

Generated comparison and widget PNGs were visually inspected: paper is
transparent, fine colored strokes remain, density 200 strengthens lines.
No reference artwork is bundled. `.kra` save/reopen, actual canvas preview,
interactive controls and GPU-engine on/off appearance remain manual checks.

CPU measurements: Ryzen 9 9950X / Windows 11, Clang 21.1.6, Qt 6.8.0,
RelWithDebInfo/O2, Density 100, Black, varying color/alpha pattern. Median
of three single-call applications, source construction excluded:

| Image | Paper radius | RGBA8 | RGBA32F |
| --- | --- | --- | --- |
| 2480x3508 | 32 | 641 ms | 620 ms |
| 2480x3508 | 256 | 1009 ms | 971 ms |
| 4096x4096 | 32 | 1255 ms | 1230 ms |
| 4096x4096 | 256 | 1890 ms | 1851 ms |

These are processing times, not actual preview latency or parallel filter
stroke times. Peak memory and UI latency were not measured in this stage.
The timing-only run passed separately. The module was built and installed
in the test prefix. No commit or release was created.

Use the stage S test environment and `KisLineArtFilterTest.exe -o
<log-path>,txt`. `SOLSTICE_LINE_ART_ARTIFACT_DIR=<directory>` enables
`line-art-comparison.png` and `line-art-widget.png`;
`SOLSTICE_BENCHMARK_LINE_ART=1` with `benchmarkCpu` enables the timings.
Task scratch logs: `line-art-tests.txt`, `line-art-ctest.txt`,
`line-art-benchmark.txt` and `line-art-test-build.log`.

Manual hand-off: restart Solstice and open Filter > Other > Line Art
Extraction on a scan/photo layer. Check Density 0/100/200, Black/Keep
original, fine versus thick lines at different paper radii, transparent
edges, soft selections, preview/Cancel/Apply/Undo/Redo and reapply. Check
painting under a filter mask, `.kra` save/reopen, and matching appearance
with the GPU engine on/off. **User verification pending.**
