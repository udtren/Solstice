# Illustration filters: maintenance reference

This document owns Sharpness, Color Filter, Glow, Drop Shadow and Line Art
Extraction. User workflows are in [Effects filters](../effects-filters.md).
The complete investigation, proposals, dated results and measurements are in
[development history](wiki/history/illustration-filters-development.md).

All five have native CPU implementations. Sharpness, Color Filter and Glow
have general user confirmation dated 2026-10-15; Drop Shadow confirmation was
recorded 2026-10-16. **Line Art Extraction's interactive verification remains
pending.** The optional Drop Shadow on-canvas handle (D2) is deferred; the
implemented control is a dialog position pad. Do not infer completed interactive
checks from release inclusion, compilation or synthetic test images.

## Scope, integration and source map

Keep computation CPU-only under the user's 2026-10-15 policy. Existing GPU
display/compositing/tile synchronization is allowed, but filter math must not
call `KisGaussianKernel::applyGaussian()` or another GPU-dispatched helper.
These are Solstice algorithms inspired by requested controls, not replicas of
CLIP STUDIO PAINT pixels. Use original fixtures, not reference article artwork.

All five are in [solsticeeffects](../../plugins/filters/solsticeeffects/),
private static library `solsticeeffects`, module `kritasolsticeeffectsfilter`.
Each has `Kis<name>Filter.*`, `Kis<name>ConfigWidget.*` and a corresponding test
in [tests](../../plugins/filters/solsticeeffects/tests/). `LineArt` is the source
name for Line Art Extraction. Registration is in
[SolsticeEffectsPlugin.cpp](../../plugins/filters/solsticeeffects/SolsticeEffectsPlugin.cpp),
with module/test [CMake](../../plugins/filters/solsticeeffects/CMakeLists.txt).

| Filter / category | Persisted ID (configuration version 1) | Reduced-resolution processing | Alpha contract |
| --- | --- | --- | --- |
| Sharpness / Enhance | `solstice_sharpness` | Disabled | Preserve |
| Color Filter / Adjust | `solstice_color_filter` | Only when `adjustSharpness` is zero | Preserve |
| Glow / Artistic | `solstice_glow` | Disabled | Preserve; no exterior glow |
| Drop Shadow / Artistic | `solstice_drop_shadow` | Disabled | May add coverage behind original paint |
| Line Art Extraction / Other | `solstice_line_art` | Disabled | Multiply original coverage by extracted ink |

All support threading, normal filter strokes, filter layers and masks; all
disable filter-brush painting. `needsTransparentPixels()` is true for Drop
Shadow and explicitly false for Line Art. Keep IDs/property meanings stable.
Leave Unsharp Mask, Sharpen, Color to Alpha and PSD-compatible layer styles
unchanged. Menus are registry-driven; do not edit `krita5.xmlgui`.

Shared implementation dependencies:

- [KisCpuPlaneBlur](../../plugins/filters/solsticeeffects/KisCpuPlaneBlur.h)
  owns scalar CPU Gaussian, soft blur and maximum primitives described below.
- [KisSharpnessFilter::applyCpu](../../plugins/filters/solsticeeffects/KisSharpnessFilter.cpp)
  is reused by Color Filter with a caller-selected progress range.
- [KisEffectColorSampler](../../plugins/filters/solsticeeffects/KisEffectColorSampler.h)
  owns per-patch converters; [Native CPU effects](cpu-effects.md) documents its
  other consumers. [Color Reference](color-reference.md) shares the module too.
  Run affected consumers when changing shared helpers; no new global library
  abstraction is needed just to share private effect code.

## Common execution, color and cancellation

RGB/Alpha and Gray/Alpha U8/U16/F16/F32 plus CMYK/Lab U16 have recorded automated
coverage. RGB/Gray math uses native encoded channels, not ICC-linear physical
luminance. Sharpness, Color Filter and Glow use float RGB conversion for other
models, with a UI notice; conversion may change appearance. Drop Shadow works
in the native model, and Line Art converts only its measurement/black as needed.
No 8-bit intermediate is introduced. Integer output is bounded by its format;
float treatment and invalid-input rules are feature-specific below.

Read old transaction pixels so concurrent patches share one source. Neighborhood
operations own their converters, scalar planes and scratch output. Preserve
scratch origin/default pixel for optimized copies and hidden float RGB on moved
devices. Release source accessors before copy-back. Existing filter strokes own
selection feathering and Undo/Redo; selections constrain output, not the halo.

Widgets use parent ownership and suppress change signals while loading. Pixel
workers do not access views or run nested dialogs/event loops. Cancellation is
checked during rows/stages and before publishing scratch; helper output swaps
only on success. The final optimized device copy is not an independently
cancellable transaction: host rollback remains responsible for cancellation
after publication begins. Do not describe standalone calls as atomic.

Disabled native channels are preserved. Alpha preservation and transparent
pixel identity apply to Sharpness/Color Filter/Glow; Drop Shadow and Line Art
have deliberate exceptions. Mask composition can clear hidden RGB at zero
alpha, so mask-test comparisons ignore it there, while direct-filter tests
still require the documented preservation.

## Shared CPU plane primitives

[KisCpuPlaneBlur.cpp](../../plugins/filters/solsticeeffects/KisCpuPlaneBlur.cpp)
operates on `FloatPlane` document rectangles. Callers supply the complete halo;
insufficient coverage returns false without replacing output. Helpers do not
replicate edges, multiply alpha or unpremultiply implicitly. `FloatPlane`
checks size multiplication against vector capacity; this is not a memory budget.

### Gaussian and large-radius soft blur

`gaussianBlur()` uses separable float planes, double accumulation and float
intermediate storage. `gaussianHalo()` is `ceil(3*sigma)`, capped at sigma 128;
nonpositive/nonfinite sigma has zero radius. Poll cancellation each row and
publish only after both passes succeed. Sharpness always uses sigma 1/radius 3.

`softBlur()` uses that Gaussian through sigma 2, then three variance-matched
box blurs. Bound sigma to 128. Let
`r=floor((sqrt(1+4*sigma*sigma)-1)/2)`; three radius-r boxes have variance
`r*(r+1)`. Choose the nearest 0-3 boxes to enlarge to r+1, each adding
`(2*r+2)/3` variance. Small slider changes may share the same box combination.
This is one Gaussian approximation, not a multi-scale bloom or exact Gaussian.

Rolling double sums restart at multiples of 16 **document pixels**, using floor
for negative coordinates and identical operation order inside each block.
Starting sums at arbitrary patch boundaries changes rounding. The soft halo is
the sum of box radii **plus 45**, allowing 15 extra pixels per pass for numerical
rebasing. Keep this footprint even outside theoretical visual support; both
source reads and inverse dirty dependencies need it. Cancel per row/16-row
block; only publish after all passes. Inputs can be released channel by channel.

### Maximum

`maximumFilter()` is separable van Herk/Gil-Werman dilation: prefix/suffix maxima
over blocks of `2*r+1`, combining the suffix at window start and prefix at end.
It is linear in processed pixels rather than radius per pixel. Finite inputs
are required; callers sanitize invalid samples. The helper accepts radius
0-4096, while Line Art exposes only 8-256. Maxima have no accumulation rounding,
so block origins may be patch-local. Supply the full radius halo and poll
cancellation on each row/column before swapping the completed result.

Do not confuse alpha rules: Sharpness blurs brightness times coverage and
coverage separately, then divides; Glow blurs premultiplied emission without
that division; Drop Shadow blurs coverage; Line Art smooths maxima/support.

## Sharpness

`strength` is a double 0-100, default 50; nonfinite uses 50. Zero is byte
identity and has no halo. Otherwise both `neededRect()` and `changedRect()`
expand by 3, covering sigma-1 Gaussian and the limiter's 3x3 neighborhood.

Brightness is native gray or `.2126R+.7152G+.0722B`. Blur brightness times
coverage and coverage separately; transparent/nonfinite observations do not
contribute. Detail delta is `2*strength/100 * (brightness-blurredBrightness)`.
Skip deltas within `1e-6*max(1,abs(brightness))` to preserve flat fields.
Limit signed delta by `limit*tanh(delta/limit)`, with limit the headroom to the
visible 3x3 brightness min/max. A hard black/white edge is deliberately unchanged.
Apply the same delta to RGB or directly to gray, preserving alpha/disabled
channels/fully transparent pixels. Integer encoding can clip; floats retain
negative/HDR values. Other models use private RGB converters.

No adjustable detail-size control is implemented. Preserve fixed-radius and
full-resolution behavior until an explicit refinement is requested.

## Color Filter

### Persisted controls and rendering order

Defaults: None, `amount=100`, neutral Adjust. `preset` is metadata (0-9 in
display order; -1 Custom), not a rendering instruction. `lookEnabled` controls
Look independently. Expanded `look` properties are `Brightness`, `Contrast`,
`Hue`, `Saturation`, `Temperature`, `Tint`, `Curve`, `Monochrome`, `Sepia`,
`ShadowR/G/B` and `HighlightR/G/B`. `adjust` properties are the seven visible
controls: `Brightness`, `Contrast`, `Hue`, `Saturation`, `Temperature`, `Tint`,
`Sharpness`. Hue is -180..180; Sharpness/Amount 0..100; other sliders -100..100.

Preset order is None, Vivid, Warm, Cool, Lemonade, Cobalt Blue, Aquamarine,
Aurora, Sepia, Monotone. [applyPreset()](../../plugins/filters/solsticeeffects/KisColorFilter.cpp)
owns the recipes. Selecting a preset rewrites Look values/metadata, retaining
Amount and Adjust. Editing any slider marks Custom; loading saved properties
does not reapply a recipe; Reset restores all defaults. None/Amount zero
bypasses **Look only**. Gray ignores all Look/color controls and uses native
Brightness, Contrast and Sharpness; the widget disables unsupported controls.

Decode old pixels, apply Look, blend by Amount, apply Adjust, then Sharpness.
Stage the result and sharpening halo in RGBA32F with the working profile;
encode once to the original format. Intermediate processing sees all color
channels; only enabled native color channels are copied back. Alpha and hidden
RGB are preserved. Nonfinite input is excluded from sharpening, and nonfinite
staged RGB is not written back. Float output is not explicitly clipped to 0..1.

The grading primitive adds `brightness/200`, applies contrast about .5 with
gain `2^(contrast/100)`, rotates hue about the RGB gray axis and scales
saturation about `.2126R+.7152G+.0722B` by `1+saturation/100`. Temperature T
and tint gains are `2^(.004*T+.0015*tint)`, `2^(-.003*tint)` and
`2^(-.004*T+.0015*tint)` for R/G/B. These are artistic encoded-channel gains,
not calibrated white balance. Additional grading terms are the curve
`(curve/100)*(v-.5)*v*(1-v)` inside 0..1 only, quadratic shadow/highlight tints
from clamped brightness, monochrome mixing and sepia gains `(1.12,.99,.78)`.
Decode finite bounded coefficients once per patch.

`applyCpu()` reuses Sharpness without a nested stroke: grading progress 0-30,
sharpening 30-85, encoding 85-100. Transform the halo before sharpening.
Both rectangles expand by 3 only when `adjustSharpness>0`; LOD is allowed only
when it is zero. Neutral Look/Adjust and zero Sharpness return byte identity.

### Thumbnail ownership

[KisColorFilterConfigWidget](../../plugins/filters/solsticeeffects/KisColorFilterConfigWidget.cpp)
finds its own ancestor main window and verifies its active node owns the
supplied device. `tryBarrierLock(true)` captures a copy-on-write full-resolution
frame without waiting for a stroke. Detach its default bounds from live
animation/LOD state before queuing work; do not use another window's active view.

Context-bound `QTimer::singleShot` callbacks run on the widget's event loop,
not a worker pool. One callback reduces the snapshot to at most 96x72 without
upscaling, then one Look is rendered per callback with Amount 100/neutral
Adjust. Destruction drops pending callbacks. Document pixels are never modified;
thumbnails remain static while the canvas preview reflects sliders. Mask/layer
dialogs, busy/empty devices or absent safe owners use a labeled synthetic
sample. This sequencing does not establish measured UI latency.

## Glow

`strength`, `effectArea`, `blurRange` are doubles 0-100, defaults 50/50/30.
Nonfinite values use defaults, other values clamp. Strength zero is identity.
Sigma is `.0064*blurRange^2`, default 5.76/max 64. Both dependency rectangles
expand by `softBlurHalo(sigma)`, default 61/max 236; zero strength adds no halo.
Range zero brightens highlights without spatial spread.

Use native RGB/gray or converted RGB for other models. Positive brightness is
`L=.2126*max(R,0)+.7152*max(G,0)+.0722*max(B,0)` (native positive gray).
For threshold `1-effectArea/100`, set
`t=clamp((L-threshold+.1)/.2,0,1)` and `weight=t*t*(3-2*t)`.
Emission is `max(channel,0)*weight*clamp(alpha,0,1)`. Transparent/nonfinite
input does not emit. Blur each scalar plane independently; **do not divide by
blurred alpha**, which would brighten low-coverage sources incorrectly.

For original channel c and nonnegative blurred emission e, let
`g=e*strength/50`; output is `c+g-clamp(c,0,1)*min(g,1)`. This is Screen in
SDR with continuous additive excess outside it. It does not decrease a finite
working channel or explicitly clip float HDR; integer output is bounded at
encoding. Nonfinite source or overflowing output retains original pixels.
That working-channel property is not a guarantee about every native CMYK
component after color conversion. Alpha, disabled channels and fully transparent
pixels remain byte-exact, so Glow cannot extend the silhouette.

Keep LOD disabled: downsampling before the nonlinear bright-pass can erase
small emitters even if radius scaling is correct. Large-radius processing is
the shared three-box approximation; initial proposals for reduced-grid or
multi-scale processing are not the current algorithm.

## Drop Shadow

Properties: `blur` 0-100/default 20, `angle` 0-359.9/default 45 clockwise
degrees, `distance` 0-1000/default 20 document pixels, `opacity` 0-100/default
50, `shadowColor` serialized KoColor/default black. Rendering wraps angles by
360; bounded/nonfinite controls clamp/fall back. The picker disables color
alpha; opacity alone controls shadow strength. XML retains color/profile.
Angle right=0, down=90 differs from the layer style's Photoshop light angle.

Convert shadow color once per patch using an owned converter to the native
document space (including Gray/CMYK/Lab); source colors do not take an RGB
round trip. Sample source alpha bilinearly at `(x-dx,y-dy)` into a destination-
aligned plane, treating nonfinite coverage as zero and bounding finite coverage
to [0,1]. Snap offsets within 1e-10 of an integer before sampling. Blur with
sigma `.0064*blur^2` (max 64).

For native straight color C/alpha a, tint T and blurred coverage m, set
`b=clamp(m,0,1)*opacity*(1-a)` with normalized opacity. Output alpha is `a+b`
and color `(C*a+T*b)/(a+b)`. Ignore hidden RGB when a=0. Opaque/no-contribution
pixels retain original bytes; invalid alpha or overflowing/nonfinite output
leaves originals intact. Copy only enabled color/alpha channels; alpha can be
locked independently. Opacity zero is identity with no expanded bounds.

For halo h, `neededRect()` unions the original with the -offset rectangle
expanded by h; `changedRect()` uses +offset. Floor lower edges and ceil upper
edges to include bilinear support. h includes numerical box-rebase support
(default 51/max 236). A mask on transparent paint is the recommended editable
form; a filter layer often sees opaque composited paint that hides its shadow.

### Position pad

[KisDropShadowConfigWidget](../../plugins/filters/solsticeeffects/KisDropShadowConfigWidget.cpp)
owns the pad; there is no canvas/view pointer or installed event filter.
Left press starts a drag and matching release ends it. Ungrab, deactivation
and hide clear dragging; other buttons do not change settings. Arrows move
1 document pixel, Shift 10. Radius is at least 32 and otherwise distance
rounded up to a multiple of 32; it stays fixed during dragging and readjusts
on normal release. Signals are blocked while Angle/Distance update together.
The actual preview is the ordinary canvas filter preview, not this pad.

## Line Art Extraction

Properties: `density` double 0-200/default 100; `paperRadius` integer
8-256/default 32; `lineColor` string `black` (default) or `original`.
Unknown strings use black; nonfinite density uses 100; controls are bounded.
Radius is explicit document pixels so canvas resizing does not change saved
mask semantics. **Density zero removes ink for valid source pixels; it is not
identity.** Alpha must be enabled for paper removal.

Paper estimation:

1. Measure native RGB brightness `.2126R+.7152G+.0722B` or native gray and
   bound to [0,1]. Other models use private RGB conversion for measurement.
   Zero/invalid alpha or nonfinite measured color becomes sentinel -1,
   meaning missing observation, not white paper.
2. Apply the square maximum radius r. Valid maxima get support 1; missing
   maxima become value/support zero. Smooth maxima and support separately with
   `softBlur()` at sigma r/4. Divide if support exceeds 1e-6, otherwise use
   estimate 1, avoiding artificial dark paper near transparent boundaries.
3. For estimate E, set `t=smoothstep(.35,.65,E)` and denominator
   `max(.35,1-t+t*E)`. Dark unreliable estimates blend toward white to preserve
   solid black/broad dark ink rather than dividing black by black.

Let `dark=1-clamp(brightness/denominator,0,1)`, `gain=density/100`,
`floor=.06-.02*gain`. Ink is
`min(1,gain*dark*smoothstep(floor,2*floor,dark))`, monotonic in density at fixed
image/radius. Zero density skips paper estimation. Output alpha is original
alpha times ink. Black writes native black (owned conversion for non-RGB/Gray);
Keep original copies native source color bytes, including negative/HDR values.
Only brightness is bounded. Existing transparent pixels and visible invalid
alpha/color stay unchanged; disabled native channels are copied unchanged.

Both dependency rectangles expand by `r+softBlurHalo(r/4)`: default 100,
maximum 492; zero density has no halo. Retain numerical blur support and old
transaction reads for exact patch agreement, including negative/moved origins.
Extraction never increases coverage or needs initially transparent pixels.

This is local dark-mark extraction, not semantic recognition or an AI model.
Broad midtones can resemble paper; abrupt shadows can resemble ink. Dark or
strongly colored paper, bright marks on dark paper and heavy shading may need
manual correction. Higher density can reveal noise. Real photographed drawings
still need interactive evaluation; generated JPEG charts are limited evidence.

## Build, tests and verification evidence

Use [Development workflow](development-workflow.md) and its environment script.
Build `kritasolsticeeffectsfilter` and these targets:

| Target | Important regression coverage |
| --- | --- |
| `KisSharpnessFilterTest` | Independent Gaussian reference, flat/HDR/hidden pixels, soft versus hard edges, partial invalidation |
| `KisColorFilterTest` | Expanded Look values/Amount, Gray restrictions, grading-before-sharpening, Custom/reset, thumbnail completion/destruction |
| `KisGlowFilterTest` | Independent three-box reference, incomplete halo/cancel preservation, sigma 0/1/3/8/64 patch equality, HDR/half overflow, emission coverage |
| `KisDropShadowFilterTest` | Native source-over oracle, fractional/cardinal bounds, shadow erasure, alpha locks, pad input/interrupted drag |
| `KisLineArtFilterTest` | Brute-force maximum reference 0/1/3/8/32/256, missing/invalid observations, monotonic density, colored JPEG paper, no alpha increase |

All include format coverage, parallel patches/negative coordinates, selections,
Undo/Redo, cancellation, silent widget loading, XML restoration and actual
registered mask application. Use native numeric values for analytic color tests;
Qt named-color ICC conversion is not an exact-channel oracle.

```bat
cmake --build <krita-dev-root>\_build --target kritasolsticeeffectsfilter KisSharpnessFilterTest KisColorFilterTest KisGlowFilterTest KisDropShadowFilterTest KisLineArtFilterTest -j 2
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\filters\solsticeeffects\cmake_install.cmake
ctest --test-dir <krita-dev-root>\_build -R "plugins-filters-solsticeeffects-Kis(Sharpness|Color|Glow|DropShadow|LineArt)FilterTest$" --output-on-failure
```

Install before registry checks, install any affected shared libraries too, and
fully restart Solstice. Do not terminate the user's process to unlock DLLs.
For shared code changes run the other affected module tests, including native
CPU effects/Color Reference. Offscreen QtTest setup on Windows uses
`QT_QPA_PLATFORM=offscreen`, `QT_QPA_FONTDIR=C:\Windows\Fonts` and
`KRITA_NO_ASSERT_MSG=1`; log with `-o <log-path>,txt`.

The 2026-10-15/16 stage records report each suite passing and ten module suites
passing after Line Art. Those counts describe that stage, not the module's
current total. General acceptance dates are at the top; they do not establish
every manual scenario. Release inclusion and cloud package checks are recorded
in [Versioning](versioning.md) and [the beta.6 package record](github-actions.md#effects-layer-comps-and-gmic-package-check-2026-10-16),
and do not complete pending Line Art interaction checks.

### Manual regression checklist

1. For each effect compare preview on/off, rapid slider changes, Cancel,
   Apply/Undo/Redo/reapply, soft selections, channel locks and transparent
   edges. Paint/erase beneath a mask/layer, including dependency edges, save
   and reopen `.kra`, and compare GPU Engine on/off display.
2. Sharpness: strength 0/50/100 on line art and painted texture, hard edges
   versus soft details, native Gray and converted artwork.
3. Color Filter: all Looks, Amount 0/50/100, None plus Adjust, reset/preset
   switching, saved Custom values, snapshot/sample thumbnails, closing or
   switching documents while callbacks are pending, Instant Preview with
   Sharpness zero versus nonzero.
4. Glow: strength 0/50/100, dark/bright emitters, range 0/30/100, small
   highlights, partial alpha and masks. Confirm no exterior opacity growth.
5. Drop Shadow: blur 0/20/100, cardinal and fractional offsets, opacity
   0/50/100, colored shadows, pad drag/arrows/Shift/interrupted drag, soft
   selections and clearing the old shadow when paint is erased below a mask.
6. Line Art (pending): actual scans/photos, density 0/100/200, Black/Keep
   original, fine/thick marks at several radii, uneven/colored paper, noise,
   transparent boundaries and alpha locks; preview, `.kra` and display parity.

### Performance and remaining work

Set `SOLSTICE_BENCHMARK_<NAME>=1` for `benchmarkCpu`, or
`SOLSTICE_<NAME>_ARTIFACT_DIR=<directory>` for synthetic review PNGs. Names are
`SHARPNESS`, `COLOR_FILTER`, `GLOW`, `DROP_SHADOW`, `LINE_ART`.
[History](wiki/history/illustration-filters-development.md) retains exact
hardware, workloads, timing/working-set observations and blur comparisons.
These are single-call CPU measurements, not UI/parallel-stroke latency or
isolated per-filter memory budgets. Line Art peak memory was not measured.

Remaining work is Line Art interactive/real-artwork verification. D2 would
require explicit user approval after assessing non-modal preview/input,
decoration lifetime across views, mask/layer dialogs, shortcuts and matching
press/release behavior. It is not implicitly authorized by this archive split.
Filter Gallery, merge-and-apply, learned line extraction, adjustable Sharpness
detail size and Glow growth into transparent areas are unimplemented proposals,
not scheduled phases.

Extracted on 2026-10-16 against `efbd2b8e89550456e174b232a09adba8dd2405ef`.
Source/documentation review here is not a new build, installation, native test
run or interactive verification.
