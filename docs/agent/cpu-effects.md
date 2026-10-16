# Native CPU effects: maintenance reference

This document owns the implemented Film Grain, Chromatic Aberration, Film,
Crystal, Lens Blur extension and Pencil contracts. User workflows are in
[Effects filters](../effects-filters.md). The complete proposal, phase results
and measurements remain in [development history](wiki/history/cpu-effects-and-gmic-development.md).
[G'MIC](gmic.md) is a separate runtime and is not a dependency of these effects.

The user's 2026-10-15 CPU-only decision remains in force: do not port this
filter math to GPU compute or add a GPU phase. Existing GPU tile synchronization,
canvas display and compositing are permitted. These are Solstice algorithms,
not a promise of pixel-identical output from the reference application.

## Source map and integration

| Responsibility | Sources / targets |
| --- | --- |
| Five effects and their widgets | [solsticeeffects](../../plugins/filters/solsticeeffects/), `KisFilmGrainFilter`, `KisChromaticAberrationFilter`, `KisFilmFilter`, `KisCrystalFilter`, `KisPencilFilter` and corresponding `ConfigWidget` files |
| Registry and build | [SolsticeEffectsPlugin.cpp](../../plugins/filters/solsticeeffects/SolsticeEffectsPlugin.cpp), [CMakeLists.txt](../../plugins/filters/solsticeeffects/CMakeLists.txt); private static library `solsticeeffects`, module `kritasolsticeeffectsfilter` |
| Shared grain/aberration kernels | [KisCpuEffectKernels.h](../../plugins/filters/solsticeeffects/KisCpuEffectKernels.h); implementations in the standalone filter `.cpp` files |
| Native RGB and private converted sampling | [KisEffectColorSampler.h](../../plugins/filters/solsticeeffects/KisEffectColorSampler.h) |
| Crystal geometry | [KisCrystalLattice.cpp](../../plugins/filters/solsticeeffects/KisCrystalLattice.cpp) |
| Lens Blur | [kis_lens_blur_filter.cpp](../../plugins/filters/blur/kis_lens_blur_filter.cpp), [KisLensBlurHighlights.cpp](../../plugins/filters/blur/KisLensBlurHighlights.cpp), `kis_wdg_lens_blur.*`, `wdg_lens_blur.ui`; module `kritablurfilter` |
| CPU convolution | [kis_convolution_painter.cc](../../libs/image/kis_convolution_painter.cc): spatial/FFTW workers |
| Automated checks | [effect tests](../../plugins/filters/solsticeeffects/tests/), [Lens Blur tests](../../plugins/filters/blur/tests/KisLensBlurFilterTest.cpp) |

The effects module also hosts [Illustration filters](illustration-filters-plan.md)
and [Color Reference](color-reference.md). Their contracts remain in those
documents. Shared sampler/kernel changes require their affected regressions.
Do not move helpers into shared application libraries merely for convenience.
Menus are registry-driven; do not edit `krita5.xmlgui` or replace customized
menus. Existing Random Noise remains independent of Film Grain.

## Common execution and persistence rules

All six use configuration version 1. IDs and property meanings are persisted
contracts. All support direct application, filter layers and filter masks.
The five `solstice_*` effects disable LOD and filter-brush painting; Lens Blur
retains both. Do not infer reduced-resolution support from threading support.

- Use existing CPU filter jobs, progress and host transactions; no GUI access,
  nested dialogs, event loops or unrestricted thread pools in pixel workers.
  In particular, do not substitute `KisGaussianKernel::applyGaussian()`, which
  can dispatch to GPU computation, for an explicit CPU path.
- Neighborhood effects read `oldRawData()` or old-data snapshots from the
  common source transaction. Each patch owns its scratch devices/converters.
  A series of in-place calls without a shared transaction is not equivalent
  to one filter stroke. Preserve scratch offsets/default pixels for optimized
  copies of float/HDR and hidden color data.
- Grain, cells, hatching and effect centers use document coordinates, including
  negative coordinates. Save seeds and geometry; changing patch, selection or
  canvas bounds must not silently regenerate/rescale them. Widgets initialize
  missing geometry once, suppress change signals while loading and use ordinary
  parent ownership without retaining a source device or view manager.
- The host handles selection coverage and one undoable apply. For the five
  new filters, alpha, fully transparent destination pixels and disabled native
  channels are preserved. Lens Blur deliberately has different alpha behavior.
- Cancellation is checked at rows/stage boundaries. Scratch can be discarded
  before commit, but cancellation during final copy or direct grain writes
  relies on the enclosing transaction rollback. Standalone calls are not
  promised atomic. Release accessors before optimized scratch copy-back.

| Effect | Persisted ID | Color policy |
| --- | --- | --- |
| Film Grain | `solstice_film_grain` | Add noise to native normalized color channels; no profile conversion |
| Chromatic Aberration | `solstice_chromatic_aberration` | Native RGB; other models through default RGBA F32 and back |
| Film | `solstice_film` | Native RGB for tone/aberration; non-RGB conversion for those stages; grain remains native |
| Crystal | `solstice_crystal` | Copy native representative color bytes; no conversion or averaging |
| Lens Blur | `lens blur` | Native convolution; enhancement decodes RGB/Gray natively, converts other models through RGBA F32 |
| Pencil | `solstice_pencil` | Native RGB; other models, including Gray, through RGBA F32 |

RGB U8/U16/F16/F32 is covered for all six. Gray/Alpha at those depths is also
tested, and all except Grain have recorded CMYK/Lab U16 tests. Grain can use
other models' native channels, but their visual behavior is not verified.
There is no 8-bit intermediate. Integer conversion bounds output; float paths
retain negative/HDR values rather than clipping at 1. Color conversion can
change gamut/appearance; tested conversion is not lossless color fidelity.
`ColorSampler` owns converters per patch instead of using the non-thread-safe
`convertPixelsTo()` helper. Artistic RGB math uses native encoding, not a
physical scene-linear exposure model.

## Film Grain

Menu: Filter > Other > Film Grain. Properties: `strength` 0-100 (default 30),
`monochrome` false, nonnegative `seed`. Default configuration generates the
seed once; a missing seed reads zero. New Seed is an explicit user operation.

Noise is uniform additive perturbation with amplitude `strength/200`.
Monochrome uses one delta for every color channel; color mode salts streams
by channel display position rather than RGB/BGRA byte order. Signed document
pixel coordinates seed the field. Zero strength is identity; nonfinite source
components are left unchanged. Alpha/disabled channels are copied byte-exactly,
even when they contain nonfinite data. No source halo is required.

Keep whole-image versus patch equality, eight depth/model cases, monochrome
equality, hidden color/alpha, channel flags, HDR, selection, seed changes and
widget/XML round trips in `KisFilmGrainFilterTest`.

## Chromatic Aberration

Menu: Filter > Artistic > Chromatic Aberration.

| Property | Meaning / default |
| --- | --- |
| `mode` | 0 translation (default), 1 radial |
| `strength` | 0-100 document pixels per shifted channel, default 3 |
| `angle` | Clockwise canvas degrees, default 0 |
| `centerX`, `centerY` | Saved document coordinates; widget uses canvas pixel center; programmatic fallback (0,0) |
| `radius` | Reference radius, at least 1; widget uses half canvas diagonal; programmatic fallback 1000 |

Translation displacement is `d = strength * (cos(angle), sin(angle))`.
Radial displacement is `d = strength * (p-center) / max(radius, length(p-center))`.
Read red at `p-d`, blue at `p+d`; native RGB green remains byte-identical.
Both dependency rectangles expand by `ceil(strength)+1`, or zero at zero
strength. Capped radial displacement also bounds off-canvas reads.

Bilinear samples weight colors by alpha and unpremultiply the result. Zero
weight, transparent or nonfinite samples do not contribute; an empty footprint
keeps the original channel. Destination alpha is unchanged, so the silhouette
does not expand and a uniform solid shape has no exterior fringe. Patches
write separate output before commit. Saved radial geometry does not follow
later canvas resizing automatically.

Keep analytic fractional shifts, radial capping, concurrent irregular patches,
negative coordinates/moved offsets, converted color models, HDR, dependency
halos, feathered selection, Undo/Redo and widget/XML geometry persistence in
`KisChromaticAberrationFilterTest`.

## Film

Menu: Filter > Artistic > Film. The Look/Details widget expands presets into
explicit saved values. Loading a preset label never overwrites stored values;
editing marks the label Custom. Rendering does not read `preset`.

| Property | Meaning / default |
| --- | --- |
| `preset` | 0 Custom, 1 Vintage (default), 2 Modern, 3 Warm; descriptive only |
| `toneType` | 0 None, 1 Fade, 2 Sepia (default), 3 Warm, 4 Cool |
| `toneStrength` | 0-100, default 60 |
| `aberrationStrength` | 0-100 translation pixels, default 1.5 |
| `aberrationAngle` | -180 to 180 clockwise degrees, default 90 |
| `grainStrength`, `monochrome` | 0-100, default 20; monochrome true |
| `seed` | Generated once by default configuration; missing value zero |
| `lightLeakStrength` | 0-100, default 35 |
| `lightLeakX`, `lightLeakY` | Widget initializes rightmost canvas pixel/vertical midpoint; programmatic fallback (1000,500) |
| `lightLeakRadius` | At least 1; widget uses half canvas diagonal; programmatic fallback 750 |

| Preset | Tone / strength | Aberration | Grain | Leak |
| --- | --- | ---: | ---: | ---: |
| Vintage | Sepia / 60 | 1.5 | 20 | 35 |
| Modern | Cool / 25 | 0.75 | 8 | 8 |
| Warm | Warm / 70 | 0 | 0 | 10 |

Selecting a preset retains seed/leak geometry and sets monochrome=true and
aberration angle=90. Each component can be neutralized independently. All
effective strengths zero returns without conversion; None disables tone only.

Pipeline: snapshot the patch plus aberration halo, apply the shared translation
kernel, tone/light leak, then shared grain. Scratch has no transaction so each
stage sees the previous stage. Copy only enabled native color channels back;
intermediate stages see all channels. There is no registry re-entry or extra
Undo step. Both halos are `ceil(aberrationStrength)+1`, or zero when disabled.

For `l=.2126R+.7152G+.0722B`, tone targets are Fade `.08+.86v`, Sepia
`(1.12l+.04,l+.025,.78l+.01)`, Warm `(1.06R+.025,G+.01,.90B)` and Cool
`(.93R,G+.01,1.08B+.02)`. Interpolate by `toneStrength/100`. Then add
`max(0,1-v) * e * (1,.38,.08)`, where
`e=lightLeakStrength/100 * exp(-2*distanceSquared/radiusSquared)` and `v` is
the tone result. Exposure leaves components above 1 unchanged. Integer output
clips at each native-format stage. Worker-local progress maps the stages;
cancellation before copy-back discards scratch and during copy relies on rollback.

Keep standalone/shared-kernel equality, analytic tone/leak/HDR, all-component
identity, saved numerical overrides, seed/geometry across canvas changes,
parallel patches, masks/channels, Undo/Redo, cancellation and widget signals
in `KisFilmFilterTest`.

## Crystal

Menu: Filter > Artistic > Crystal. Properties: `cellSize` 1-512 (default 20),
`irregularity` 0-100 (default 100), nonnegative generated `seed`, `tiling` false,
`tileX`/`tileY` integers in +/-1e9, `tileWidth`/`tileHeight` 1-1e6. The widget
initializes missing bounds from the canvas; fallback is (0,0,1024,1024).
Saved bounds survive canvas changes.

One site per grid cell is its center plus independently hashed X/Y jitter in
`[-.5,.5) * irregularity/100` cell units. Pixel centers select the nearest
site with ties resolved by row then column. Grid origin is document zero;
the representative pixel is the floor of the site's continuous position.
Zero jitter makes square cells; size 1 with zero jitter is exact identity.

The own-cell site is within the cell diagonal. Search each axis through
`ceil(nextafter(diagonal,+inf)/step)` cells to include every winner/tie;
square nonperiodic grids normally use 5x5. Cache only the current cell's
candidates. Keep independent wider-search/exhaustive periodic reference tests.

For tiling, cell count is `max(1,round(tileDimension/cellSize))`, with exact
spacing `tileDimension/cellCount`. Modulo wraps negative coordinates, IDs
and distances; each canonical candidate is visited at most once. Sample the
wrapped representative in the source period containing the output pixel.
Repeated source tiles give repeated results, but differing source tiles keep
their own colors. Preserved nonperiodic alpha can still cause seams.

Nonperiodic dependency rectangles expand by `ceil(diagonal)+1`. Periodic
rectangles round outward to whole source periods touched; a tiny edit can
therefore recalculate a canvas-sized tile. Identity has no halo. Neither grid
nor bounds depend on the selection. Copy enabled native representative color
bytes, preserving original alpha; transparent representatives keep the original
color. No averaging, conversion or hidden-color bleed is introduced.

Keep fractional sizes, extreme jitter, narrow/nondivisible periods, negative
coordinates, repeated 3x3 rendering, dirty-region containment, native bytes/HDR,
parallel patches, transparent samples, saved bounds, cancellation and Undo/Redo
in `KisCrystalFilterTest`.

## Lens Blur extension

Menu: Filter > Blur > Lens Blur; retain ID `lens blur`. Existing defaults are
`irisShape="Pentagon (5)"`, `irisRadius=5`, `irisRotation=0` and computed
`halfWidth`/`halfHeight`. English shape identifiers remain persisted even when
the UI is translated. New `irisRoundness` and `highlightStrength` are 0-100
doubles, default zero; missing/nonfinite values render as zero.

Both new values zero preserve legacy polygon rasterization, convolution,
channel flags and saved half-size halos. Do not silently repair historical
even kernels or alter old saved artwork. For nonzero new settings, prepend
zero rows/columns to make support odd, preserving coefficient/anchor offsets:
the FFT worker's cache sizing otherwise creates patch wraparound seams.
Derive halos from current geometry instead of stale saved half sizes:
`neededRect` expands by twice the half-size, `changedRect` by the half-size.

Roundness interpolates polygon radial boundaries toward the circumcircle
with 64 angular samples per sector. Exactly 100% uses a 256-segment circle,
independent of shape/rotation. Preserve antialiased QPainter rasterization and
kernel-sum normalization. Zero radius is identity. LOD scales and truncates
the integer radius as before; enhancement/roundness do not scale. A reduced
preview is an approximation, not full-resolution/downsample equality.

Rounded-only processing uses native `KisConvolutionPainter`. Enhancement:

1. Decode old halo pixels into RGBA32F scratch. RGB/Gray use native normalized
   channels (gray replicated); other models use private converters.
2. Let `f=1+8*strength/100`, `t=.75`, `u=t/f`. Map color `v` to `v/f` for
   `v<=t`, otherwise `v-t+u`.
3. Convolve mapped color and coverage with the CPU aperture, using alpha
   premultiplication/unpremultiplication. Invert below `u` by multiplying by
   `f`, otherwise add `t-u`.
4. Encode enabled native channels. Alpha is never highlight-amplified:
   enabled alpha receives blurred coverage, disabled alpha stays original.
   Zero resulting coverage retains original hidden color; nonfinite/hidden
   source colors are excluded and source alpha is bounded to [0,1].

This continuous convex piecewise mapping preserves constants and enhances
sparse highlights without power overflow or explicit HDR clipping. It is
component-wise artistic processing and can change color balance.

Prepare the legacy repeat-border domain explicitly (`canvas bounds | output
rect`, or exact source bounds without a finite canvas), retaining wrap-around
access. Scratch uses `BORDER_IGNORE` so its own extents cannot create borders.
Always alpha-weight scratch, even if destination alpha is locked. Production
uses automatic CPU FFTW/spatial choice; helper engine preferences exist for
comparison tests. Preparation/postprocessing check rows; convolution uses its
existing cancellation stages. Progress may remain zero during preparation or
reach completion before postprocessing finishes.

Keep frozen pre-extension byte equality for missing/zero fields, circle
invariance, stale halos, LOD equivalent radius, parallel patches, hidden colors,
channel flags, HDR, partial/dirty updates, cancellation and spatial/FFTW
agreement in `KisLensBlurFilterTest`. Float/engine tests use tolerances and do
not promise cross-platform bit identity.

## Pencil

Menu: Filter > Artistic > Pencil. Properties: `outlines` and `hatching` true,
`outlineStrength` 0-100 default 60, `hatchStrength` 0-100 default 65,
`hatchSize` 1-32 document pixels default 4, `roughness` 0-100 default 50,
`angle` 0-180 clockwise degrees default 45, `grayscale` false, generated
nonnegative `seed` (missing zero). Numeric parameters are bounded; nonfinite
values use defaults. Angle 180 canonicalizes to zero. Toggles retain values.
Both disabled/effective strengths zero is identity even with grayscale set.

Outlines build coverage-weighted native luminance (.2126/.7152/.0722), bounded
to [0,1] for analysis, over a two-pixel halo. A CPU 3x3 binomial (1,2,1 outer
product) smooths weighted light/coverage; a 3x3 Sobel estimates gradients.
Uncovered neighbors use center light. Outline ink is
`.85 * strength * min(1,3*gradientMagnitude)` with normalized strength.

Hatching uses rotated document-pixel centers, floor indexing and spacing
`hatchSize`. Three neighboring lines contribute antialiased strokes with
seeded along-line variation. Zero roughness gives regular seed-independent
lines. Darkness controls width/strength. Combined ink multiplies
`(1-outlineInk)*(1-hatchInk)`; hatching lifts base color toward paper white
by `.32*hatchStrength` before darkening. Grayscale substitutes original
luminance. There is no texture dependency, learned model or line-extraction
output. Hatching alone has no halo; active outlines expand both halos by 2.

Output float values are not clipped; only analysis is bounded. Nonfinite
source luminance is left alone. The two preparation fields use 8 bytes/pixel
each plus halos; input is released before output allocation. This accounting
is not a measured process peak. Progress is 0-25% preparation, 25-45%
smoothing, 45-99% shading (0-99% for hatching only), then completion after copy.

Keep outline/hatch/combined modes, transparent coverage, angle/seed semantics,
Gray/converted models, HDR, parallel patches, identity, partial/dirty updates,
selection, flags, Undo/Redo, cancellation stages and widget/XML persistence
in `KisPencilFilterTest`.

## Build, install and verification

Use [Development workflow](development-workflow.md) and its environment script.
Targets are `kritasolsticeeffectsfilter`, `kritablurfilter` and
`KisFilmGrainFilterTest`, `KisChromaticAberrationFilterTest`, `KisFilmFilterTest`,
`KisCrystalFilterTest`, `KisPencilFilterTest`, `KisLensBlurFilterTest`.
Install both plugins before installed-registry integration checks:

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\filters\solsticeeffects\cmake_install.cmake
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\filters\blur\cmake_install.cmake
ctest --test-dir <krita-dev-root>\_build -R "plugins-filters-(solsticeeffects-Kis(FilmGrain|ChromaticAberration|Film|Crystal|Pencil)FilterTest|blur-KisLensBlurFilterTest)$" --output-on-failure
```

Install any affected shared libraries too, and restart Solstice fully. Do not
terminate the user's process to unlock DLLs. For changed shared helpers, run
the affected illustration/Color Reference suites in addition to these six.

Manual regression checks:

1. On representative artwork, compare preview on/off and Apply; rapid slider
   changes, Cancel, reapply, Undo/Redo, feathered selections and channel locks.
   Compare GPU Engine on/off display and transparent/HDR/non-RGB cases.
2. Paint beneath filter masks/layers at patch boundaries; save/reopen `.kra`
   with custom seeds/geometry, then resize canvas. XML tests alone do not
   prove document reopening or UI behavior.
3. Grain: both modes and seed changes. Aberration: translation 0/90 degrees,
   radial off-center settings. Film: all presets/tabs and independent neutral
   components, retained geometry and saved overrides.
4. Crystal: regular cells, tiny/nondivisible periodic bounds, repeated opaque
   tile export and opposite-edge mask updates; check nonperiodic alpha seams.
5. Lens Blur: legacy/new settings, all shapes, roundness/rotation/highlights,
   zoomed previews, filter brush, wrap-around, alpha locks and older configs.
6. Pencil: outlines, hatching, combined/grayscale, disabled identity, size,
   roughness, angle and seed. Review portraits/flat art/dark scenes/fine lines.

The 2026-10-15 record reports all six suites passing and user confirmation of
each effect. No per-scenario interactive checklist was supplied; do not infer
that every `.kra`, mask, color-model or GPU-display case was individually
checked. [History](wiki/history/cpu-effects-and-gmic-development.md) owns exact
counts, installed-DLL comparisons, synthetic review images and measurements.

## Performance and remaining verification

Opt-in `benchmarkCpu` switches are `SOLSTICE_BENCHMARK_FILM_GRAIN`,
`SOLSTICE_BENCHMARK_CHROMATIC_ABERRATION`, `SOLSTICE_BENCHMARK_FILM`,
`SOLSTICE_BENCHMARK_CRYSTAL`, `SOLSTICE_BENCHMARK_LENS` and
`SOLSTICE_BENCHMARK_PENCIL` (set to 1). Film/Crystal/Lens/Pencil support
`SOLSTICE_<NAME>_ARTIFACT_DIR=<existing-directory>` for synthetic review images.

The recorded timings are single CPU calls including scratch/copy work, not
threaded dialog latency. Crystal periodic performance, most peak-memory
cases, interactive cancellation latency and GPU-resident input transfers are
unmeasured. Lens enhancement needs a 16-byte/pixel RGBA32F halo plus FFT kernel
and channel arrays; its historical process high-water reading includes test
infrastructure and is not a per-patch memory budget. Large-radius spatial
fallback grows roughly with output area times kernel area; do not apply FFTW
timings to builds without FFTW or promise real-time 4K previews.

This reference was extracted on 2026-10-16 against
`39b556e1e807cca9deb01f08d236142e53fe9292`. Documentation/source checks do not
constitute a new native test run, installation or interactive verification.
