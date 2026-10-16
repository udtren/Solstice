# CPU effects and G'MIC integration - implementation plan

## Status and decisions

Status: **F1-F6, CPU G'MIC 3.7.4.1 integration and CPU bridge optimizations
user-verified; automated runtime checks passed** (2026-10-15).
CPU bridge performance priorities 1 and 2 (output copies and native RGB
conversion) are implemented, locally installed and user-verified;
measurements below.
Work branch: `codex/cpu-effects-and-gmic`, created from `main` at the user's
request on 2026-10-15. Preserve unrelated edits. The user also specified that
G'MIC must be implemented last, after the six native effects.

User decision (2026-10-15): implement all six requested effects and integrate
G'MIC using **CPU computation only**. Do not port them to the GPU, introduce
compute shaders, or add a GPU implementation as a later phase of this plan.
Existing GPU features remain unchanged. Normal canvas display, compositing,
and synchronization with GPU-resident image tiles may still use existing GPU
infrastructure; that does not make the new filter calculation a GPU operation.

Deliverables:

1. Chromatic Aberration, Film Grain (the requested Noise effect), Film,
   extended Lens Blur, Crystal, and Pencil effects in the native filter UI.
2. A working, bundled G'MIC-Qt runtime using the existing native host bridge.
3. Configuration compatibility, preview/apply consistency, Undo/Redo,
   automated coverage, interactive verification, and reproducible packaging.

The six effects must work without G'MIC installed. G'MIC is a separate toolbox,
not their execution dependency. The reference defines intended controls and
visual effects, not a published algorithm or a promise of pixel-identical
CLIP STUDIO PAINT output. Use our own test images; do not bundle article artwork.

## Evidence and current implementation

Inspected on 2026-10-15:

| Area | Existing implementation | Consequence |
| --- | --- | --- |
| Noise | `plugins/filters/noisefilter/noisefilter.cpp`, `KisFilterNoise`; color/grayscale, level, opacity and saved seeds | Preserve Random Noise. Its random-color replacement/mixing is different from additive film grain. |
| Lens Blur | `plugins/filters/blur/kis_lens_blur_filter.cpp`; triangle through octagon, radius and rotation | Extend this filter, retaining its ID and old defaults. Roundness and highlight enhancement are missing. |
| CPU convolution | `libs/image/kis_convolution_painter.cc`; spatial worker or FFTW when available | Reuse CPU convolution for aperture blur and other CPU smoothing. No GPU FFT work. |
| Other four effects | No dedicated corresponding filter found in `plugins/filters/` | Add native filters; edge detection and color-processing code are implementation references. |
| Filter framework | `libs/image/filter/kis_filter.h`, `KisFilterConfiguration`, filter registry and configuration widgets | Reuse filter strokes, selection handling, settings serialization and standard dialogs. |
| G'MIC bridge | `plugins/extensions/qmic/`; targets `kritaqmic`, `kritaqmicinterface` | Host actions/bridge exist; they are not the G'MIC processing runtime. |
| Runtime discovery | `QMic::slotQMic()` looks up service `Krita/GMic` | A menu item does not prove the runtime can load. |
| Local installation | `<krita-dev-root>/_install` contains `kritaqmic.dll` and `libkritaqmicinterface.dll`; filename search found no G'MIC runtime | Confirm actual plugin discovery in phase G1; this is not an installed-runtime smoke test. |
| CI dependencies | `build-tools/github-actions/windows-dependencies.lock.json` has FFTW but no G'MIC entry | Add a pinned runtime build/provisioning path and package verification. |

Two bridge behaviors need attention during integration:

- `QMic::slotQMic()` disables actions before loading the runtime and has early
  failure returns. Restore valid action state on every exit, including missing
  DLL, invalid plugin, cancellation and unsuccessful launch.
- `KisImageInterface::gmic_qt_output_images()` currently coerces output modes
  other than `InPlace` to `InPlace`. Do not expose a choice that silently
  overwrites layers when the user requested a different destination.

## Architecture and scope

### Native effects

Proposed new directory: `plugins/filters/solsticeeffects/`, registered from
`plugins/filters/CMakeLists.txt`. Proposed module target:
`kritasolsticeeffectsfilter`. It contains five filters and plugin-local shared
CPU routines for grain, channel sampling, tonal adjustments and procedural
patterns. Keep helpers in this module (or a private static testable library);
do not make unrelated shared-library API changes just to share implementation.
Extend Lens Blur in its existing `kritablurfilter` target.

Proposed new IDs, to finalize before the first persisted release:
`solstice_chromatic_aberration`, `solstice_film_grain`, `solstice_film`,
`solstice_crystal`, `solstice_pencil`. Once released, IDs and property meanings
are compatibility contracts. Keep the existing Lens Blur and Random Noise
IDs and historical parameter behavior. Do not replace Random Noise with grain.

Use existing registry-driven filter menu categories, localized strings and
standard `KisConfigWidget` forms. Do not edit `krita/krita5.xmlgui` or overwrite
the user's customized menu. Provisional placement: Lens Blur under Blur,
Grain under Other, and the remaining effects under Artistic, subject to the
existing category IDs. Exact labels need not reproduce the reference product.

### CPU-only execution and responsiveness

- Filter math must run on CPU worker jobs with progress and cancellation.
  Use the existing filter patch scheduler where the algorithm is local.
  Do not add nested unrestricted thread pools on top of it.
- Audit helper calls: `KisGaussianKernel::applyGaussian()` can dispatch to the
  GPU. For these effects, use an explicit CPU convolution path instead of that
  auto-dispatch helper. Do not globally disable the GPU engine to force CPU
  execution. Apply the same check to indirect edge/blur helpers.
- Read source snapshots and write separate outputs for resampling/convolution;
  do not let later pixels read partially filtered data. Keep GUI access out of
  workers and apply document changes through existing undoable filter strokes.
- Define `neededRect()` and `changedRect()` from actual source dependencies.
  Include blur halos, channel displacement and crystal-cell support.
- Spatial parameters and random patterns are anchored to document coordinates,
  never the current patch or selection bounding box. Store seeds and any stable
  effect origin needed for reload/repeat. Recompute only when the user changes
  a setting or explicitly requests a new seed.
- Full-resolution preview and final apply use the same core and settings.
  Reduced-resolution previews may approximate spatial detail, but must not
  choose a new seed or move effect centers. Enable LOD support only after
  verifying parameter scaling and disclose approximation where applicable.
- Preserve selection feathering, channel masks and transparent pixels. Decide
  alpha semantics per filter before coding, including premultiplied sampling
  for spatial effects; do not produce black fringes or unbounded new opacity.

### Color and persistence

Target native RGB/RGBA U8, U16, F16 and F32 documents. Preserve the document's
profile and precision; do not silently reduce float documents to 8-bit.
Explicitly define the working color space and clipping policy for each effect.
Grayscale support and framework conversion of other color models must be
verified in phase 0; unsupported combinations must be reported or restricted
through the existing framework, not interpreted as raw RGB bytes.

Save the seed, spatial origin, effect controls and configuration version in
`KisFilterConfiguration`. Film presets expand to saved parameter values, so
later preset tuning cannot silently change old documents. Lens Blur configs
without new fields use roundness=0 and highlight enhancement=0 and reproduce
the existing result. Additive features must not change historical defaults.

Target native filter layers and filter masks as well as direct application.
Advertise support only after partial-update and save/reload tests pass.
G'MIC remains a dialog-driven operation; converting its entire catalog into
native non-destructive filters is outside this plan.

## Per-filter implementation

Difficulty includes UI, persistence and correctness, not just the pixel loop.
No calendar estimate is committed before phase 0 measurements.

| Effect | Required controls / behavior | CPU implementation | Difficulty |
| --- | --- | --- | --- |
| Chromatic Aberration | Translation and radial modes, strength, angle for translation; stable radial center | Per-channel inverse sampling with subpixel interpolation; displacement bounds determine input halo | Low-medium |
| Film Grain | Color/monochrome, strength, stable seed | Coordinate-based random perturbation of source color/luminance; zero strength is identity and alpha is preserved | Low |
| Film | Presets, effect type, aberration strength, grain strength | Shared aberration/grain kernels plus deterministic tonal curves, warm/sepia treatment and light-leak field | Medium |
| Lens Blur | Existing radius, 3-8 sides, rotation; add highlight enhancement and roundness through a circle | Normalized aperture kernel and CPU spatial/FFTW convolution, with defined highlight mapping and alpha handling | Medium; large-radius performance is the main risk |
| Crystal | Cell size, irregularity, seamless tiling, stable seed | Jittered-grid Voronoi assignment with deterministic representative colors and periodic coordinates when tiling | Medium |
| Pencil | Independent outline/hatching toggles, hatching size, roughness, angle, grayscale output | CPU edge extraction plus luminance-controlled procedural strokes and restrained grain/paper modulation | Medium-high; visual quality is the main risk |

### F1: Film Grain

Create a separate Film Grain filter. Reuse the coordinate/seed design of
`KisRandomGenerator2D`, not Random Noise's color replacement behavior.
Use one random value for monochrome and independent components for color.
Specify noise distribution, strength units and HDR behavior before freezing
the configuration. Test fixed-seed reproducibility across patch sizes, alpha
preservation, neutral gray and extreme brightness. Grain-size controls beyond
what the reference needs are optional refinements, not prerequisites.

### F2: Chromatic Aberration

Start with parallel channel offsets, then radial displacement. Define sign,
channel order and radius normalization consistently. Use a stable image-space
center independent of the processed rectangle. Sample transparent edges with
a documented alpha rule; include a full-source snapshot or correctly bounded
read area. Test fractional offsets, negative coordinates, off-canvas content,
selection boundaries and zero-strength identity.

### F3: Film

Compose shared CPU routines in a fixed documented order: channel displacement,
tonal treatment/light leak, then grain. Provide initial Vintage, Modern and
Warm presets as independently tuned looks, not asserted copies of CSP pixels.
Warm can disable aberration/grain; each constituent must also be individually
neutralizable. Use document-space light-leak coordinates. Avoid re-entering
separate filter dialogs or creating separate Undo steps for constituent effects.

### F4: Crystal

Place one deterministic seed per regular grid cell and jitter it according to
irregularity. Choose the nearest seed with stable tie-breaking. The neighbor
search must cover the full permitted jitter range; a small hard-coded search
window needs a proof or exhaustive validation against a reference search.

The baseline color is a source sample at the seed position. Cell-average color
is an optional later refinement requiring additional source support/reduction;
do not make it a hidden prerequisite. At zero irregularity the result should
be a regular cell pattern.

For tiling, derive a periodic seed lattice from the document tile dimensions,
including sizes not divisible by the requested cell size. Wrap both distance
and source sampling. Define edge behavior for non-periodic source images and
test a repeated 3x3 render, not just matching first/last pixel rows. Save/reload,
selection clipping and partial recomputation must not move the cells.

### F5: Lens Blur extension

Keep the existing polygon kernel as the exact legacy path. Generate a rounded
aperture interpolating toward a circular shape for new settings, with stable
normalization, rotation and bounds. Add highlight enhancement via an explicitly
defined pre/post tonal mapping; zero enhancement follows existing math.
Handle alpha separately from highlight amplification and avoid hard HDR clipping.

Reuse `KisConvolutionPainter` and FFTW for CPU computation. Measure large radii,
temporary buffer usage and parallel patches before changing scheduling. FFTW
availability changes performance, not intended output; document fallback cost.
Do not substitute a Gaussian approximation that loses the requested aperture
shape. New settings must update halo bounds and LOD behavior.

### F6: Pencil

Implement independent outline and hatching buffers. Use CPU smoothing and
gradient/edge extraction for outlines. Build hatching from deterministic,
directional strokes whose density/strength follows luminance; roughness
modulates stroke length and intensity. Combine them while retaining source
color unless grayscale output is enabled. Define the both-disabled case as
identity and preserve alpha.

Start with the user-selected fixed stroke angle. Contour-following strokes,
learned style transfer and extra texture-resource dependencies are outside
the initial scope. Validate portraits, flat-color art, dark scenes, fine lines
and gradients; a successful compile cannot establish acceptable pencil quality.

## G'MIC integration

### G1: Select, pin, build and load a compatible native runtime

Inspect the Krita-native G'MIC distribution, including `amyspark/gmic`, and
the relevant KDE dependency recipe before choosing a source revision. The
generic GreycLab G'MIC-Qt build instructions list other host targets; do not
assume an arbitrary GIMP/standalone binary implements the Krita interface.
Exact source version, build options and runtime target names remain phase G1
outputs, not guessed commands in this plan.

Build against Solstice's installed `kritaqmicinterface` headers/library and
the same Qt major, compatible Qt build, compiler ABI and C++ runtime as the
host. Verify `KRITA_QMIC_INTERFACE_VERSION`, the plugin interface IID, metadata
and `Krita/GMic` discovery. Build the host interface first, then the runtime.
Keep local and CI dependency prefixes distinct; do not copy an unrelated
prebuilt Qt DLL over Solstice's dependency set.

Pin source archives/commits, checksums, patches, CImg/standard-library inputs
and required generated data. Eliminate floating build-time downloads. Use the
dependency lock/mirror workflow where suitable, or an equally pinned source
build manifest for the runtime if it must be built after the host interface.
Record provenance, build flags, licenses and required source availability.

Use the normal CPU G'MIC engine and compatible OpenMP runtime. Confirm actual
parallel execution on a representative heavy filter. No CUDA/OpenCL/Vulkan
backend or external GPU-only helper is introduced. Additional online filters
and external-command extensions are not blanket guarantees of CPU compatibility;
document the bundled baseline separately.

Exit: the local installation loads the real dialog, lists bundled filters
offline and completes one filter application with Undo/Redo.

### G2: Host behavior, data round trips and failure recovery

- Fix action restoration on all `QMic::slotQMic()` exit paths. Reapply Last
  must reflect whether a valid previous operation exists, not simply whether
  the dialog was once opened.
- Preserve the native bridge; do not replace it with a PNG/CLI round trip.
  Verify planar float conversion, channel order, alpha, grayscale, profiles,
  F16/F32 values and out-of-range behavior on each selected test filter.
  G'MIC filter-specific clipping must not be advertised as universal HDR support.
- Test supported input-layer modes and output layer count/order, names,
  opacity, blend modes, offsets and selections. Initially expose only output
  modes the host correctly implements. Hide/reject unsupported destinations;
  never silently redirect them to in-place editing. Additional destinations
  require explicit implementation and their own Undo tests.
- Check cancellation, empty output, load errors, missing dependencies, repeated
  launches, multiple windows and document/view lifetime. Verify allocation
  size overflow and failure handling for image dimensions and layer counts.
- Keep results within the existing `KisProcessingApplicator`/layer synchronization
  transaction. Cancellation before acceptance must leave the document unchanged.
- Inspect G'MIC's own settings/cache/update paths and use the Solstice profile
  policy from `settings-location.md`. Avoid writing to stock Krita's settings.
  Record actual resolved paths; do not assume Qt's application name controls
  every G'MIC path. Test first launch offline and retain an offline filter set
  if an optional filter-definition update fails.

### G3: CI and distribution

Integrate provisioning/build after the required host interface is available in
`build-tools/github-actions/build-windows.ps1`. Follow `github-actions.md` for
lock/mirror/checksum handling and isolated runner paths. Include runtime DLLs,
plugin metadata, filter resources and notices in the final portable ZIP.
Record versions/revisions in build metadata. Update packaging assertions so a
package cannot succeed with only the bridge and a nonfunctional menu entry.

Validate a fresh extraction without development PATH entries, developer caches
or separately installed G'MIC. Check all dependent DLLs and a real filter run,
not just file presence. Preserve desktop portability; document verified
platforms and leave untested Linux/macOS runtime packaging explicitly unverified.
Creating a Release is not part of this documentation task; when later released,
follow `versioning.md`, including the same-day website update.

## Work order and completion gates

| Stage | Work | Completion gate |
| --- | --- | --- |
| 0 | Freeze native pixel/alpha/coordinate contracts, inspect filter mask support, measure CPU baselines | Written defaults/support matrix and reproducible input fixtures; defer G'MIC investigation to G1 |
| 1 | F1 Film Grain | Deterministic CPU result, persistence and preview/apply checks |
| 2 | F2 Chromatic Aberration | Both displacement modes, correct halos/alpha and identity tests |
| 3 | F3 Film | Shared routines, presets, stable saved parameters and one undoable apply |
| 4 | F4 Crystal | Repeatable cells and seamless periodic behavior under partial updates |
| 5 | F5 Lens Blur | Legacy compatibility, new aperture/highlight controls and bounded CPU cost |
| 6 | F6 Pencil | Controls work independently; representative visual results reviewed |
| G1-G3 | Runtime selection/build, bridge checks, distribution | Real offline launch/apply/Undo from clean packaged installation |
| Final | Cross-filter regressions and documentation | All required automated/manual checks recorded, including unverified items |

G'MIC is implemented **after F1-F6**, by user decision on 2026-10-15. Do not
start G1-G3 before the native effects are complete. The native effects must
not become wrappers around its scripts.
G'MIC integration is medium-high difficulty due to ABI, dependencies and
packaging, not new image-processing algorithms. No GPU phase is scheduled.

## Verification, build and installation

Automated tests should exercise observable behavior:

- Zero-strength/disabled identity, known channel shifts, unchanged constant
  fields under normalized blur, and predictable crystal assignments.
- Fixed-seed whole-image versus tiled processing, tile boundaries, negative
  coordinates, partial updates and selection feathering.
- Transparent/semitransparent edges, enabled channels, supported bit depths,
  grayscale conversion policy and representative wide-gamut/HDR values.
- Configuration round trips, missing/new fields, old Lens Blur fixtures,
  unchanged Random Noise behavior, and saved filter-layer/mask recomputation.
- G'MIC bridge conversion/layer synchronization tests plus launch-error action
  recovery. Tests must distinguish missing runtime from successful execution.
- CPU-only audit of the new call graph and execution with GPU Engine both off
  and on; results must agree. Existing GPU tile synchronization is permitted,
  but new filter math must not dispatch a GPU convolution/transform worker.

Manual checks for every native effect: preview on/off, rapid slider changes,
Cancel, Apply, Undo/Redo, reapply, save/reopen, selections and partial-opacity
edges. Check filter layers/masks while painting beneath them and after reopen.
Inspect LOD previews at several zoom levels. For G'MIC, also test offline
startup, input/output layer modes, reapply, multiple windows and a missing-DLL
failure followed by a successful retry after repair/restart.

Performance fixtures: 2480x3508 and 4096x4096 images, RGBA8 and RGBA32F, small
selection and full canvas; representative small/large blur radii, grain, cell
sizes and pencil settings. Record preview latency, final-apply time, peak
memory, cancellation responsiveness, thread count, build and hardware. Measure
three runs and report medians; set realistic budgets from phase 0 rather than
promising real-time 4K CPU previews. Optimize algorithm, CPU tiling, bounded
buffers and scheduling where needed. Do not solve a performance issue by GPU
porting or by silently changing final-render quality.

Use `development-workflow.md` commands with the active environment script:

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritasolsticeeffectsfilter kritablurfilter kritaqmic kritaqmicinterface -j 2"
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && ctest --test-dir <krita-dev-root>\_build -R <actual-effect-and-qmic-test-regex> --output-on-failure"
```

The new module/test names are planned until introduced; resolve actual test
names using `ctest -N`. G'MIC runtime build commands must be added after G1
selects its source revision. Install all rebuilt libraries/plugins through
their `cmake_install.cmake`, including shared libraries if touched. Native
module install directories are `plugins/filters/solsticeeffects`,
`plugins/filters/blur` and `plugins/extensions/qmic`. Install the runtime's
dependencies/resources too. Never terminate a running Solstice to unlock DLLs;
ask the user to close it if installation is blocked, then fully restart.

## Documentation and planned touch points

The initial planning change added this document and its `AGENTS.md` entry.
Only implemented effects are listed in the user guide and feature inventory;
do not describe pending effects or the G'MIC runtime as shipped.

During implementation:

- Maintain this document's phase status and actual file/target names.
- Add user guides `docs/effects-filters.md` and `docs/gmic.md`, with controls,
  CPU-only behavior, supported formats/modes and known limitations. Add README
  name/link entries only when the corresponding functionality is available.
- Add actual touch points to `feature-inventory.md`: the new filter directory
  and parent CMake file, Lens Blur sources/forms/tests, necessary qmic fixes,
  runtime build/lock inputs and packaging verification. These are proposed
  areas, not changes made by this planning task.
- Update `github-actions.md` when the runtime build/package route is implemented;
  record settings behavior in the G'MIC agent section or a dedicated document.
- After each committed implementation task, ingest relevant decisions/pitfalls
  into the agent wiki and append `wiki/log.md`, linking rather than duplicating
  this feature plan.

## References

- [Requested effects and controls (CSP official article)](https://tips.clip-studio.com/ja-jp/articles/8926).
- [G'MIC architecture and CPU/OpenMP processing](https://gmic.eu/).
- [G'MIC-Qt front end and host build instructions](https://github.com/GreycLab/gmic-qt).
- [Krita-native G'MIC distribution candidate](https://github.com/amyspark/gmic).
- Local guides: `codebase-map.md`, `extension-points.md`, `coding-rules.md`,
  `development-workflow.md`, `github-actions.md`, `settings-location.md`,
  `versioning.md`, `wiki/decisions/xmlgui-unchanged.md`.

External references were inspected on 2026-10-15. Verify the selected release's
actual sources and build interfaces in G1; none is pinned by this plan alone.

## F1 implementation record (2026-10-15)

- Implementation: `plugins/filters/solsticeeffects/KisFilmGrainFilter.*`,
  `KisFilmGrainConfigWidget.*`, `SolsticeEffectsPlugin.cpp` and plugin metadata.
  Module `kritasolsticeeffectsfilter` links the private static library
  `solsticeeffects`; the only existing source hook is the parent filter CMake
  directory list. Random Noise and the existing blur/GPU implementations are
  unchanged.
- Filter ID: `solstice_film_grain`, configuration version 1. Properties:
  `strength` (0-100, default 30), `monochrome` (default false), `seed`
  (0-2147483647, generated once by defaultConfiguration; missing seed reads 0).
  Loading a configuration widget preserves its seed and does not trigger a
  change signal. New Seed explicitly chooses a new field.
- Math: uniform additive noise in native normalized color channels with
  amplitude `strength / 200`, anchored to signed document pixel coordinates.
  Monochrome uses the same delta for each color channel. Color mode salts
  streams by display position, independent of RGB/BGRA storage order.
  Integer output clamps at its representable bounds; floating-point output
  retains negative/HDR values. There is no color-profile conversion, 8-bit
  intermediate, GPU compute or call to an automatically GPU-dispatched helper.
- Alpha and disabled channels are copied unchanged at the byte level; fully
  transparent pixels are untouched. Processing uses the existing CPU patch
  scheduler and checks cancellation at scanline boundaries.
- Tested format scope: RGBA and Gray/Alpha U8, U16, F16 and F32. Other color
  models use their own normalized channels, not an implicit RGB conversion;
  visual behavior on those models is not yet verified. LOD and brush-filter
  painting are disabled; direct filters, filter layers and masks use the same
  deterministic full-resolution path. The user confirmed operation on
  2026-10-15; no individual manual-test checklist was supplied.
- Test target: `KisFilmGrainFilterTest`; CTest name:
  `plugins-filters-solsticeeffects-KisFilmGrainFilterTest`. Coverage includes
  eight format cases, whole/patch equality across negative coordinates, zero
  strength, transparent pixels, alpha/channel preservation, monochrome output,
  float range, selection clipping, seed changes and widget/XML round trips.
  Final run: 15 passes, no failures, one opt-in benchmark skipped; that
  benchmark passed separately. CTest passed (1/1). The installed-plugin
  integration test discovers the module through the registry and applies it.
- Opt-in timing: `SOLSTICE_BENCHMARK_FILM_GRAIN=1` enables `benchmarkCpu`.
  These are single-call CPU timings, not the threaded filter-dialog latency.
  Initial local RelWithDebInfo run (2026-10-15, three trials per case, strength
  30, color mode, seed 1234567, uniform source, CPU-resident input; Solstice
  remained open): RGBA8 2480x3508 **280 ms**, RGBA8 4096x4096 **529 ms**,
  RGBA32F 2480x3508 **265 ms**, RGBA32F 4096x4096 **529 ms** (medians).
  This establishes a first implementation baseline, not an idle-system or
  application-preview benchmark. Peak memory and interactive cancellation
  latency have not been measured.
- Install: `cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P
  <krita-dev-root>\_build\plugins\filters\solsticeeffects\cmake_install.cmake`.
  Restart Solstice to discover the newly installed module.
  Built and installed successfully; build/install DLL SHA-256 values match.
- User guide: `docs/effects-filters.md`. User confirmation received on
  2026-10-15: operation OK, continue. This authorizes proceeding to F2 without
  asserting that each manual scenario above was separately exercised.

## F2 implementation record (2026-10-15)

- Implementation: `plugins/filters/solsticeeffects/KisChromaticAberrationFilter.*`
  and `KisChromaticAberrationConfigWidget.*`, registered in the existing module.
  Menu: Filter > Artistic > Chromatic Aberration. ID
  `solstice_chromatic_aberration`, configuration version 1.
- Properties: `mode` (0 translation, 1 radial; default 0), `strength`
  (0-100 full-resolution pixels per shifted channel; default 3), `angle`
  (degrees clockwise in canvas coordinates; default 0), `centerX`, `centerY`
  (document coordinates), `radius` (reference radius, minimum 1 pixel).
  The widget initializes missing geometry from the canvas pixel center and
  half its diagonal, then persists it. Programmatic configs without geometry
  use center (0,0), radius 1000. Saved geometry does not follow a later canvas
  resize or selection change automatically.
- Translation uses `d = strength * (cos(angle), sin(angle))`. Radial mode uses
  `d = strength * (p-center) / max(radius, length(p-center))`. Inverse sampling
  reads red at `p-d`, blue at `p+d`; green remains byte-identical in native RGB.
  Radius capping bounds dependencies even for off-canvas content.
  `neededRect()` and `changedRect()` conservatively expand by
  `ceil(strength)+1` pixels, or zero for zero strength. LOD and brush-filter
  painting are disabled.
- Bilinear sampling weights color by source alpha and unpremultiplies the
  result. Zero-weight/transparent/non-finite sample components do not
  contribute. An empty footprint retains the destination's original channel.
  Alpha and disabled channels are byte-preserved, and fully transparent
  destination pixels are untouched. The silhouette is not expanded; a solid
  uniform-color shape on transparency does not acquire exterior fringes.
- Every source read uses `oldRawData()` under the framework's transaction.
  Patches write a separate temporary device and commit only after sampling;
  a single in-place call is also safe without an outer transaction. Multiple
  calls that comprise one operation must share the source transaction, as
  `KisFilterStrokeStrategy` does. Temporary output uses the same device offset
  and default pixel so raw-copy optimization preserves float/HDR/hidden RGB.
  Cancellation is checked at row boundaries and before committing the patch.
- Native RGBA U8/U16/F16/F32 preserves the source profile and operates on
  normalized native channels, with no float clipping or 8-bit intermediate.
  Non-RGB models convert through the registry's default RGBA F32 space and
  back using per-patch converters (not the non-thread-safe `convertPixelsTo`
  helper). The UI explains conversion; original alpha and native channel
  flags are retained. Grayscale remains grayscale. Native color-space
  conversion can change gamut/appearance and is not promised lossless.
- All new calculations and sampling are CPU code. No GPU helper, shader or
  GPU convolution path is called. The existing filter framework still handles
  GPU/CPU tile synchronization and document compositing.
- Test target: `KisChromaticAberrationFilterTest`; CTest name:
  `plugins-filters-solsticeeffects-KisChromaticAberrationFilterTest`.
  Tests cover both modes, four RGBA and four Gray/Alpha depths, CMYK/Lab U16,
  parallel irregular patches, moved-layer offsets, negative coordinates,
  transparent colors, alpha/channel preservation, analytic fractional shifts,
  radial center/capping, HDR, partial render, feathered selection, transaction
  Undo/Redo and widget/XML persistence. Plugin-registry loading is also tested.
  Run the installation step before this integration test.
- Build and install targets remain `kritasolsticeeffectsfilter` plus the two
  test targets. The test installation has been updated. CTest passed **2/2**,
  including the Film Grain regression suite. The F2 QtTest run reported
  **25 passed, 0 failed, 1 opt-in benchmark skipped**.
- Opt-in timing: set `SOLSTICE_BENCHMARK_CHROMATIC_ABERRATION=1` and run
  `KisChromaticAberrationFilterTest benchmarkCpu`. Initial local
  RelWithDebInfo measurement (2026-10-15, three trials per case, opaque uniform
  gray, strength 3.75, translation angle 37 degrees, radial center at canvas
  midpoint and radius half its diagonal; Solstice closed):

  | Format / size | Translation median | Radial median |
  | --- | ---: | ---: |
  | RGBA8 2480x3508 | 766 ms | 817 ms |
  | RGBA8 4096x4096 | 1563 ms | 1633 ms |
  | RGBA32F 2480x3508 | 738 ms | 752 ms |
  | RGBA32F 4096x4096 | 1434 ms | 1470 ms |

  These are single-call CPU timings including temporary patch output and
  copy-back, not threaded dialog latency. The timing test passed separately.
  Peak memory, interactive cancellation latency and GPU-resident input costs
  are not measured. The built and installed module hashes match.
- Manual regression checklist: compare Translation at 0/90 degrees and Radial
  mode on artwork; check rapid slider changes, preview toggle, Cancel,
  Apply/Undo/Redo and reapply. Paint beneath filter masks/layers, including near
  patch boundaries; save/reopen a document with an off-center radial setting.
  Compare with GPU Engine enabled and disabled. Automatic tests do not prove
  these UI, .kra persistence or display-path cases.
- The user confirmed Chromatic Aberration operation on 2026-10-15 and requested
  F3 Film. No individual manual-test checklist was supplied; do not infer that
  every scenario above was separately checked.

## F3 implementation record (2026-10-15)

### Sources, integration and persisted settings

- Implementation: `plugins/filters/solsticeeffects/KisFilmFilter.*` and
  `KisFilmConfigWidget.*`; registered by `SolsticeEffectsPlugin.cpp`, menu
  Filter > Artistic > Film. ID `solstice_film`, configuration version 1.
- `KisCpuEffectKernels.h` exposes plugin-local CPU entry points for grain and
  chromatic aberration, implemented in their existing filter source files.
  The standalone filters delegate to the same kernels, preserving their
  existing math, IDs and property meanings. `KisEffectColorSampler.h` contains
  the shared native-RGB / per-patch-converter sampler extracted from F2.
  No shared application library, menu XML, dependency or GPU code was changed.
- The widget has Look and Details tabs. It uses only parent-owned widgets,
  never retains the source device, and emits no configuration-change signal
  while loading. Explicit preset selection emits one change; editing any
  setting marks the descriptive preset label Custom. New Seed is explicit.

| Property | Meaning / default |
| --- | --- |
| `preset` | Descriptive label only: 0 Custom, 1 Vintage (default), 2 Modern, 3 Warm. Rendering never reads this value. |
| `toneType` | 0 None, 1 Fade, 2 Sepia (default), 3 Warm, 4 Cool |
| `toneStrength` | Color-treatment interpolation, 0-100; default 60 |
| `aberrationStrength` | Translation offset in document pixels, 0-100; default 1.5 |
| `aberrationAngle` | Clockwise canvas angle, -180 to 180 degrees; default 90 |
| `grainStrength` | F1 noise strength, 0-100; default 20 |
| `monochrome` | Same grain delta on all native color channels; default true |
| `seed` | 0-2147483647, generated by defaultConfiguration; missing seed reads 0 |
| `lightLeakStrength` | Warm exposure intensity, 0-100; default 35 |
| `lightLeakX`, `lightLeakY` | Saved document coordinates, widget initially uses rightmost canvas pixel and vertical pixel midpoint; programmatic fallback (1000, 500) |
| `lightLeakRadius` | Smooth falloff scale, at least 1 pixel; widget initially uses half the canvas diagonal; programmatic fallback 750 |

Presets expand to explicit numerical settings only when chosen. Loading a
saved preset name never overwrites stored component values. They retain the
seed and leak geometry, set monochrome=true and aberration angle=90:

| Preset | Tone / strength | Aberration | Grain | Leak |
| --- | --- | ---: | ---: | ---: |
| Vintage | Sepia / 60 | 1.5 px | 20 | 35 |
| Modern | Cool / 25 | 0.75 px | 8 | 8 |
| Warm | Warm / 70 | 0 px | 0 | 10 |

### Pixel pipeline and invariants

1. Read the source transaction's old pixels into a local scratch device for
   `neededRect(patch)`, preserving its offset and default pixel for raw copies.
   The scratch has no transaction, so subsequent stages see current output.
2. Apply the shared F2 translation kernel, then color treatment/light leak,
   then the shared F1 grain kernel. These are CPU functions; there is no
   registry lookup, nested dialog, extra Undo command or GPU-dispatched helper.
3. Copy only enabled native color channels back to the original patch.
   Opacity/hidden RGB at zero alpha remain untouched. Intermediate stages see
   all channels; channel flags mask only the final result. The enclosing filter
   stroke provides a single undoable operation and selection feathering.

The source halo and inverse dirty halo are `ceil(aberrationStrength)+1`, or
zero at zero strength. Tone, grain and leak need no neighbors. Leak geometry
and grain seed are saved and anchored to image coordinates, never patch or
selection bounds. Multiple patches use the host's common source transaction.
With all component strengths zero the filter returns without any conversion.
Each component can be independently disabled; None also disables tone.

Native RGB uses its original profile/channel encoding. Color treatment is
deliberately artistic, not physical scene-linear exposure. With RGB values
`v` and `l = .2126R + .7152G + .0722B`, tone targets are:

- Fade: `.08 + .86v` per channel.
- Sepia: `(1.12l + .04, l + .025, .78l + .01)`.
- Warm: `(1.06R + .025, G + .01, .90B)`.
- Cool: `(.93R, G + .01, 1.08B + .02)`.

Interpolate from the original to the target using `toneStrength/100`. Then
`e = lightLeakStrength/100 * exp(-2*distanceSquared/radiusSquared)` and
`v += max(0, 1-v) * e * (1, .38, .08)`. Float HDR is not explicitly clipped,
and exposure leaves components already above 1 unchanged. Integer output
clips at each native-format stage, matching separate F1/F2 applications.
Non-RGB tone/aberration converts through default RGBA32F and back with private
converters; native channel flags and alpha are preserved. Grain follows F1's
native-channel rule. No promise of lossless non-RGB appearance is made.

Progress uses a worker-local `KoProgressProxy` mapping stages into one progress
range, without QObject/timers, nested event processing or extra threads.
Both shared kernels still check the original updater for cancellation at
scanlines; Film also checks between stages and during final copy. Cancellation
before copy-back discards scratch data; cancellation during copy-back relies
on the enclosing host stroke's transaction rollback. Partial copy-back is not
advertised as an atomic standalone API call.

### Verification and next step

- Build targets: `kritasolsticeeffectsfilter`, `KisFilmFilterTest`,
  `KisFilmGrainFilterTest`, `KisChromaticAberrationFilterTest`. Install via the
  existing `plugins/filters/solsticeeffects/cmake_install.cmake`; the module
  was rebuilt and installed, with matching build/install SHA-256 hashes.
- CTest passed **3/3**, including F1/F2 regression suites after kernel
  extraction. F3 test covers RGBA/Gray U8/U16/F16/F32, CMYK/Lab U16, parallel
  irregular patches across negative coordinates and moved-layer offsets,
  zero identity, transparent/disabled channels, shared-kernel equality in four
  RGB depths, analytic tone/leak/HDR values, partial output, feathered selection,
  one transaction Undo/Redo, cancellation before commit, widget/XML persistence,
  preset changes, saved numerical overrides and geometry across canvas changes.
- The QtTest run with review-image generation reported **19 passed, 0 failed,
  1 opt-in timing test skipped**. Normal runs skip both timing and image
  generation. `SOLSTICE_FILM_ARTIFACT_DIR=<existing-directory>` enables
  `renderReviewArtifacts`: a widget screenshot and a four-panel synthetic
  before/preset image made from our own fixture. They were visually inspected;
  this does not substitute for interactive artwork review in Solstice.
- Timing: `SOLSTICE_BENCHMARK_FILM=1` enables `KisFilmFilterTest benchmarkCpu`.
  Local RelWithDebInfo run on 2026-10-15, Solstice closed, three trials per case,
  opaque uniform gray, Vintage component strengths, fixed seed 1234567,
  aberration angle 37 degrees, leak centered on the right edge and radius half
  the canvas diagonal:

  | Format / size | CPU median |
  | --- | ---: |
  | RGBA8 2480x3508 | 1214 ms |
  | RGBA8 4096x4096 | 3126 ms |
  | RGBA32F 2480x3508 | 1580 ms |
  | RGBA32F 4096x4096 | 2590 ms |

  This is one CPU call including scratch copies and all three processing
  stages, not the threaded filter-dialog latency. The timing test passed
  separately. Peak memory, GUI cancellation latency and GPU-resident input
  costs have not been measured.
- Manual regression checklist: restart Solstice, open Film, compare all three
  presets, change settings in both tabs, compare preview/apply, cancel rapid
  updates, Undo/Redo and reapply. Check painting beneath a Film mask/layer,
  save/reopen with custom seed and leak position, and GPU Engine on/off display
  parity. Existing automated tests do not establish these .kra/UI cases.
- The user confirmed Film operation on 2026-10-15 and requested continuation.
  No individual manual-test checklist was supplied; do not infer that every
  .kra, mask, color-model or GPU-display case above was specifically checked.
- Next stage at that hand-off: F4 Crystal (implemented below). G'MIC remains
  deferred until after F6.

## F4 implementation record (2026-10-15)

### Files and configuration

- `plugins/filters/solsticeeffects/KisCrystalFilter.*`,
  `KisCrystalLattice.*`, `KisCrystalConfigWidget.*` and
  `tests/KisCrystalFilterTest.cpp`; registered by `SolsticeEffectsPlugin.cpp`.
- Menu: Filter > Artistic > Crystal. ID `solstice_crystal`, configuration
  version 1. CPU only; threading, adjustment layers and filter masks enabled;
  filter-brush painting and reduced-resolution processing disabled.
- Properties: `cellSize` double 1-512 (default 20), `irregularity` double
  0-100 (default 100), `seed` nonnegative int, `tiling` bool (default false),
  `tileX/Y` int (-1e9 to 1e9), `tileWidth/Height` int (1-1e6).
  Initial seed is randomized; rendering never regenerates it. Missing tile
  bounds are initialized by the widget from the canvas, then saved. Without
  usable finite canvas bounds, defaults are `(0,0,1024,1024)`.
  Loading configurations emits no configuration-change signal; changing canvas
  size does not overwrite saved bounds. Widgets have ordinary parent ownership
  and hold no paint device or view-manager reference.

### Geometry and bounded source dependencies

There is one site per grid cell, positioned at its center plus independently
hashed X/Y jitter in `[-0.5,0.5) * irregularity/100` cell units. Sampling uses
pixel centers, with ties resolved by row then column. Nonperiodic grid origin
is document zero. Cell color is the source pixel nearest the winning site
(floor of its continuous position with pixel centers at half integers).
Zero irregularity produces a regular square mosaic; size 1 and zero
irregularity are an exact identity.

The query's own-cell site is no farther than the cell diagonal. Thus candidate
cells within `ceil(nextafter(diagonal,+inf)/step)` in each axis cover every
potential winner, including ties. Nonperiodic square grids normally search
5x5 cells. Only the current grid cell's candidates are cached locally, with
no global state or unbounded cache. Geometry tests compare against an
independent wider search (nonperiodic) and every canonical site (periodic).

Periodic dimensions use `max(1,round(tileDimension/cellSize))` cells, with
exact spacing `tileDimension/cellCount`. Integer modulo maps negative and
positive document coordinates into the same local period. Site IDs and
distances wrap; each canonical candidate is visited at most once. This matters
for one-pixel-wide or highly anisotropic tiles. A wrapped representative is
sampled inside the source period containing the output pixel. Consequently:

- A canvas-sized tile wraps sampling to its opposite edge.
- Repeated source tiles yield bit-identical repeated output, even when tile
  dimensions do not divide by cell size.
- Different neighboring source tiles retain different colors. This is not an
  infinite repeat of one canonical source tile. Preserved nonperiodic alpha
  can also introduce seams; the UI tooltip and user document explain this.

Nonperiodic `neededRect` and `changedRect` expand by `ceil(diagonal)+1` to
include the representative pixel and reverse dependencies. Periodic bounds
round outward to all whole source periods touched by the requested rectangle;
opposite-edge samples and dirty updates are therefore included. This may
recalculate a whole canvas-sized tile for a small source edit, but never
requests an unbounded rectangle. Identity has no halo. Selection bounds never
define the grid or tile geometry.

### Pixel processing and lifecycle

Read old source pixels from the host's transaction into a separate output
device with matching offset and default pixel. Copy only enabled native color
channels from the representative; preserve original alpha and all disabled
channels exactly. Transparent output pixels stay unchanged, and a transparent
representative retains the original color, avoiding hidden-color bleed. No
color-space conversion, averaging, GPU dispatch or extra Undo operation is
involved. Float negative/HDR values survive as native bytes. The common host
source transaction makes parallel patches independent of completion order.

Cancellation is checked at scanlines and before copy-back. Iterators and
source accessors are released before the optimized final copy. Cancellation
before copy-back discards scratch pixels; interruption during final copying
relies on the enclosing host transaction's rollback. Progress and ownership
follow the existing native filter framework.

### Verification and hand-off

- Built `kritasolsticeeffectsfilter` and `KisCrystalFilterTest`, and installed
  the plugin with the existing module-local `cmake_install.cmake`. Build and
  installed DLL SHA-256 hashes match.
- CTest passed **4/4** for `plugins-filters-solsticeeffects`, including Film
  Grain, Chromatic Aberration and Film regression suites.
- Crystal QtTest with review images: **19 passed, 0 failed, 1 opt-in timing
  test skipped**. Covers installed registration, exhaustive nearest-site search,
  fractional cell size, zero/full jitter, several seeds, negative coordinates,
  tiny/narrow/nondivisible tiles, dependency containment, all ten supported
  color-model/depth combinations, native representative bytes and alpha,
  transparent samples, concurrent irregular patches, identity, repeated 3x3
  rendering, periodic dirty-region recomputation, partial output, feathered
  selection, disabled channels, Undo/Redo, float HDR, cancellation, widget/XML
  persistence and saved bounds across different canvas sizes.
- `SOLSTICE_CRYSTAL_ARTIFACT_DIR=<existing-directory>` enables review output:
  `crystal-widget.png`, `crystal-source.png`, `crystal-3x3.png`. The synthetic
  source is generated by the test; no reference-article artwork is bundled.
  The widget and repeated result were visually inspected. This does not prove
  interactive Solstice behavior.
- `SOLSTICE_BENCHMARK_CRYSTAL=1` enables `KisCrystalFilterTest benchmarkCpu`.
  Local RelWithDebInfo measurement on 2026-10-15 with Solstice closed, three
  trials per case, opaque uniform gray, size 20, irregularity 100, seed 1234567,
  tiling off:

  | Format / size | CPU median |
  | --- | ---: |
  | RGBA8 2480x3508 | 569 ms |
  | RGBA8 4096x4096 | 1167 ms |
  | RGBA32F 2480x3508 | 603 ms |
  | RGBA32F 4096x4096 | 1201 ms |

  This measures one CPU call including scratch allocation/copy, not threaded
  dialog latency. Periodic performance, peak memory, GUI cancellation latency
  and GPU-resident input costs have not been measured.
- Manual regression checklist: restart Solstice; open Crystal; compare preview and
  apply while changing size, irregularity, seed and tiling; verify regular cells
  at zero irregularity; cancel, Undo/Redo and reapply. Export/repeat an opaque
  tile with nondivisible dimensions. Paint beneath a filter mask/layer near
  opposite tile edges, then save/reopen with a custom seed and bounds. Compare
  GPU Engine on/off display and representative non-RGB artwork.
- The user confirmed Crystal operation on 2026-10-15 and requested continuation.
  No individual manual-test checklist was supplied; do not infer that every
  listed UI, .kra or display scenario was separately checked.
- Next stage at that hand-off: F5 Lens Blur extension (implemented below),
  then F6 Pencil, then G'MIC.

## F5 implementation record (2026-10-15)

### Integration and compatibility

- Extends the existing `plugins/filters/blur/` module `kritablurfilter`:
  `kis_lens_blur_filter.cpp`, `kis_wdg_lens_blur.cpp`, `wdg_lens_blur.ui`,
  new plugin-local `KisLensBlurHighlights.*` and `tests/KisLensBlurFilterTest.cpp`.
  No shared-library or GPU-engine changes. No new menu action or XMLGUI edit.
- Retains ID `lens blur`, configuration version 1, menu Filter > Blur > Lens
  Blur, existing shapes/radius/rotation and existing capabilities: filter
  masks/layers, filter-brush painting, threading and LOD.
- Adds `irisRoundness` and `highlightStrength`, doubles in 0-100, default 0.
  Missing/nonfinite values render as zero. Both zero use the unchanged legacy
  polygon rasterization, channel flags, convolution and persisted half-size
  halo behavior. Tests compare against an independent frozen pre-extension
  implementation across six shapes, two radii and four RGB depths, both with
  missing properties and explicit zero values.
- The existing widget serializes both values. Loading blocks outgoing change
  signals. Shape restoration now maps the stored English identifier to each
  translated item correctly. Non-RGB/non-gray devices show a notice about
  highlight conversion; widget ownership remains parent-based.

### Rounded aperture and support

For roundness `r` in (0,1), sample each polygon sector at 64 angular steps.
At sector angle `theta`, interpolate the polygon radial boundary
`R*cos(pi/n)/cos(theta-pi/n)` toward circumradius `R`. At exactly 1, use a
256-segment circle independent of shape and rotation. The existing QPainter
antialiased rasterization and kernel-sum normalization are retained. This
is an aperture convolution, not a Gaussian approximation.

An even kernel exposed patch-dependent FFT wraparound at the first output
column: the shared FFT worker assumes odd support when sizing its input cache.
For **new nonzero settings only**, prepend zero rows/columns as necessary to
make the matrix odd-sized, preserving coefficients, normalization and their
offsets relative to the anchor. Keep historical even-kernel behavior when both
new values are zero; do not silently alter old saved artwork or the shared
convolution engine. A test caught this before hand-off; rounded/enhanced
parallel-patch and constant-field tests now pass.

For new settings, derive half-size from actual aperture geometry at the current
LOD instead of trusting saved `halfWidth/halfHeight`. `neededRect` expands by
twice that half-size, `changedRect` by the half-size. Zero radius returns
unchanged. LOD uses the historical scaled integer radius (fraction truncated);
roundness and enhancement do not scale. A LOD-1 render is tested against the
equivalent reduced integer radius at LOD 0; preview remains an approximation,
not a claim of equal full-size/downsampled convolution.

### Highlight math, alpha and CPU pipeline

The rounded-only path uses native `KisConvolutionPainter` exactly as before,
with the new normalized aperture. Enhancement uses private scratch devices:

1. Read old transaction pixels over the source halo. RGB and Gray/Alpha decode
   their native normalized channel values into RGBA32F scratch; gray is copied
   to all three components. There is no profile/gamma conversion for those
   models. Other models use private per-call RGB float converters and back;
   appearance is not promised lossless. No 8-bit intermediate is used.
2. For strength `s`, let `f=1+8*s/100`, threshold `t=.75`, split `u=t/f`.
   Map each color `v` to `v/f` when `v<=t`, otherwise `v-t+u`.
3. Convolve mapped color and coverage with the normalized aperture on the CPU.
   The existing engine premultiplies/unpremultiplies using alpha. Invert the
   color map after convolution: `v*f` below `u`, otherwise `v+t-u`.
4. Encode to the original format and copy only enabled native channels.
   Enhancement never transforms alpha. Enabled alpha receives blurred
   coverage; disabled alpha retains the original. With zero resulting coverage,
   retain the original hidden color. Source alpha is bounded to [0,1]; hidden
   or nonfinite source color values are excluded from the mapped input.

The map is continuous, convex and piecewise linear. It preserves constant
fields, emphasizes sparse highlights and avoids power-function overflow or
clipping float HDR at 1.0. It is applied per color component in the native
encoding (or converted RGB for other models), so it is an artistic brightness
effect rather than physical scene-linear exposure; color balance can change.

Explicitly prepare the repeat-border halo using the same source-domain rule
as legacy Lens Blur (`canvas bounds | output rect`, or source exact bounds
when no finite canvas bounds exist). Wrap-around source access is retained
when enabled. Scratch convolution uses `BORDER_IGNORE`, so scratch extents
cannot introduce new borders. Read the shared host transaction's old pixels;
parallel jobs own their converters, kernels and scratch. Selections are applied
by the filter framework after processing.

`KisConvolutionPainter` selects FFTW or spatial CPU workers; it does not call
GPU-dispatched Gaussian helpers. The internal highlight helper accepts an
engine preference for spatial/FFTW comparison tests; production uses the
normal automatic CPU choice. Alpha weighting is always enabled in scratch,
even when destination alpha is locked. Final native copy applies channel flags.

Preparation and postprocessing check cancellation per row; the convolution
worker reports progress and checks cancellation between its existing stages.
Preparation may hold progress at zero, and convolution may report completion
before postprocessing/copy finishes. No nested event loop, timer or extra
thread pool is created. Cancellation before copy-back leaves the source alone;
cancellation during copying relies on the host transaction rollback. Scratch
outputs match native offsets/default pixels for exact optimized copies.

### Verification and manual hand-off

- Build targets `kritablurfilter`, `KisLensBlurFilterTest`; install via
  `<build>/plugins/filters/blur/cmake_install.cmake`. The installed-plugin test
  checks discovery and new zero defaults. Build and installation completed.
  Build/install DLL SHA-256 values match.
- QtTest with review artifacts: **20 passed, 0 failed, 1 opt-in benchmark
  skipped**. Normal runs skip artifacts too. CTest passed **5/5** across the
  four `solsticeeffects` suites and `plugins-filters-blur-KisLensBlurFilterTest`.
- Coverage: legacy equality, RGBA/Gray U8/U16/F16/F32, CMYK/Lab U16, rounded
  and enhanced parallel patches at negative coordinates and moved offsets,
  zero-radius identity, circle rotation/shape invariance, stale saved halos,
  LOD radius equivalence, normalized constants and negative/HDR values,
  highlight increase without alpha increase, hidden-color suppression,
  selections/partial output, channel flags, Undo/Redo, cancellation, XML/widget
  persistence, spatial/FFTW agreement and dirty-region recomputation.
  Float/engine comparisons use numerical tolerances, not a promise of bit
  identity across FFT sizes/platforms. Legacy comparison is byte-exact.
- `SOLSTICE_LENS_ARTIFACT_DIR=<existing-directory>` enables a widget screenshot
  and `lens-comparison.png` (own synthetic point-light fixture: original,
  triangle, circle, circle with enhancement). Both were visually inspected;
  real-artwork and interactive application checks remain separate.
- Manual regression checklist: restart Solstice, open Lens Blur, compare zero/new
  settings, all shapes, roundness/rotation and highlight strength; verify
  preview/apply, zoomed-out previews, rapid changes, Cancel, Undo/Redo and
  reapply. Check transparent edges, partial selection and channel locks;
  paint beneath a filter mask/layer, save/reopen new and old settings, and
  compare GPU Engine on/off display. Filter-brush and canvas wrap-around
  interaction remain manual checks. Automated XML tests do not establish .kra
  reopening or GUI behavior.
- The user confirmed Lens Blur operation on 2026-10-15 and requested Pencil.
  No individual checklist was supplied; do not infer that every listed manual
  scenario was checked.
- Next stage at that hand-off: F6 Pencil (implemented below), then G'MIC.

### CPU cost and temporary memory

`SOLSTICE_BENCHMARK_LENS=1` enables `KisLensBlurFilterTest benchmarkCpu`.
Measured 2026-10-15 on AMD Ryzen 9 9950X, Windows 11, Clang 21.1.6,
RelWithDebInfo, FFTW enabled, Solstice closed. Opaque uniform gray input;
pentagon, rotation 37 degrees, roundness 55%, highlight strength 60%. Three
trials per case, medians below, one CPU call including preparation and output:

| Format / size | Radius 5 | Radius 50 |
| --- | ---: | ---: |
| RGBA8 2480x3508 | 1516 ms | 1725 ms |
| RGBA8 4096x4096 | 2783 ms | 3141 ms |
| RGBA32F 2480x3508 | 1506 ms | 1707 ms |
| RGBA32F 4096x4096 | 2759 ms | 3098 ms |

A second run of the same cases sampled the process's OS peak working-set
counter once per second: **1,437,093,888 bytes (about 1.34 GiB)**. This is a
process-wide high-water measurement including source, test infrastructure,
scratch and FFT allocations, not incremental filter memory or per-patch memory.
The input halo is RGBA32F (16 bytes/pixel); the FFT worker additionally keeps
one complex-double kernel array and four channel arrays. The prepared device
is released before allocating the final native output. Large single calls
therefore consume considerable memory; host scheduling was not changed and
no new thread pool was added.

These measurements are not interactive dialog latency, small-selection cost,
or multi-job peak memory. GUI cancellation latency and GPU-resident input
transfer costs remain unmeasured. Spatial fallback produces the same tested
result but its arithmetic grows with output area times kernel area (roughly
radius squared); its large-image timing was not measured. Do not claim the
FFTW numbers for builds without FFTW, or promise real-time 4K previews.

## F6 implementation record (2026-10-15)

### Integration and configuration

- `plugins/filters/solsticeeffects/KisPencilFilter.*`,
  `KisPencilConfigWidget.*`, `tests/KisPencilFilterTest.cpp`; registered in
  `SolsticeEffectsPlugin.cpp` and included in the existing module/test targets.
- Menu Filter > Artistic > Pencil; ID `solstice_pencil`, configuration version
  1. CPU only, threading and filter layers/masks enabled. No reduced-resolution
  processing or filter-brush painting. No shared-library or GPU changes.
- Properties: `outlines`/`hatching` bools default true; `outlineStrength`
  0-100 default 60; `hatchStrength` 0-100 default 65; `hatchSize` 1-32 default
  4 document pixels; `roughness` 0-100 default 50; `angle` 0-180 default 45
  degrees clockwise from horizontal; `grayscale` false; nonnegative int
  `seed`, randomized once in the default configuration. Missing seed is zero.
  Numeric rendering parameters are bounded and nonfinite values use defaults.
- Widgets use parent ownership and retain no device/view reference. Loading
  settings suppresses configuration signals. Component toggles disable their
  controls without discarding values; the saved seed controls all roughness.
  Angle 180 is canonicalized to zero. Both components disabled or both
  effective strengths zero give exact identity, even with grayscale selected.

### CPU shading and source dependencies

The outline path constructs a coverage-weighted luminance field from old
transaction pixels over the output rectangle plus two pixels. RGB luminance
uses coefficients 0.2126/0.7152/0.0722 in native channel encoding, bounded to
0-1 for analysis. Invisible or nonfinite source samples contribute no weight.
A CPU 3x3 binomial kernel (outer product of 1,2,1) smooths the field, dividing
weighted light by coverage. A 3x3 Sobel gradient then detects contours;
uncovered neighbors use center light. Hidden RGB cannot produce false edges.
Outline ink is `0.85 * strength * min(1,3*gradientMagnitude)`.

Hatching uses rotated document-pixel centers, with floor-based indexing for
negative coordinates. Spacing is `hatchSize`. Three neighboring procedural
lines contribute antialiased strokes; saved-seed hashes interpolate along
each line to vary its center and intensity. Roughness increases broken-stroke
and fine-grain variation; zero gives regular parallel lines independent of
seed. Darkness controls stroke width and strength. There is no external paper
texture or per-render randomness.

The independently calculated outline and hatch masks combine as
`(1-outlineInk)*(1-hatchInk)`. Hatching lifts the underlying color toward paper
white by `0.32 * hatchStrength`, then the combined ink darkens it. Grayscale
uses the original luminance as the base instead of each original component.
This is an artistic colored-pencil treatment, not isolated line extraction.
The final masks are fused per pixel, avoiding full intermediate RGB images.

With outlines enabled and nonzero, both needed/changed rectangles expand by
two pixels. Hatching alone has no source halo. Coordinates, seed and spacing
do not depend on selection or patch extents. CPU loops implement all smoothing
and shading directly; no Gaussian helper with GPU auto-dispatch is called.

### Color, ownership and cancellation

The existing private `ColorSampler` decodes native RGB without profile
conversion and converts other models through RGBA32F with per-call converters.
Gray therefore also takes the conversion path. The widget warns non-RGB users
that appearance may change. No 8-bit intermediate is introduced. Output float
values are not clipped at 1 or zero; only the analysis field is bounded.

Read the host transaction's old pixels and accumulate a separate output device
with matching offsets/default pixel. Copy only enabled native color channels;
original alpha, hidden colors of fully transparent pixels and disabled
channels remain byte-exact. Nonfinite output-source luminance is left alone.
Release source accessors and iterators before the optimized copy-back.
There is no extra thread pool, timer, GUI access or global mutable state.

Outline preparation reports 0-25%, smoothing 25-45%, and final shading 45-99%;
hatching-only shading reports 0-99%. Each stage checks cancellation at rows;
copy-back checks it again and completion reports 100%. Cancellation before
copy-back discards scratch, while interruption during copy relies on host
transaction rollback. The two preparation fields each use 8 bytes per pixel
plus their small halos; the input field is released before output allocation.
This is allocation accounting, not a measured peak working set.

### Verification and manual hand-off

- Built and installed `kritasolsticeeffectsfilter`; built the five effect
  test targets. Install with the module-local `cmake_install.cmake`.
- CTest passed **6/6**: five `plugins-filters-solsticeeffects` suites plus
  `plugins-filters-blur-KisLensBlurFilterTest`.
- Pencil QtTest with review images: **19 passed, 0 failed, 1 opt-in benchmark
  skipped**. Coverage includes installed registration; outline-only,
  hatch-only and combined processing in RGBA/Gray U8/U16/F16/F32 and CMYK/Lab
  U16; concurrent irregular patches at negative coordinates/moved offsets;
  transparent coverage and constant-field behavior; angle/seed/size semantics;
  grayscale and negative/HDR output; identity; partial output, dirty-region
  recomputation, feathered selection, disabled channels, Undo/Redo;
  widget/XML persistence and cancellation in all processing stages.
- `SOLSTICE_PENCIL_ARTIFACT_DIR=<existing-directory>` enables the opt-in
  `renderReviewArtifacts` test: `pencil-widget.png` and
  `pencil-comparison.png`. The latter uses an original synthetic portrait,
  gradients, fine lines and a tonal ramp, with original, outlines, hatching,
  combined, grayscale and coarse/rough panels. Both artifacts were visually
  inspected; no article artwork is bundled.
- `SOLSTICE_BENCHMARK_PENCIL=1` enables `KisPencilFilterTest benchmarkCpu`.
  Measured 2026-10-15 on AMD Ryzen 9 9950X, Windows 11, Clang 21.1.6,
  RelWithDebInfo; three trials per case, opaque uniform gray, both components
  at default strengths, roughness 50%, size 4.75, angle 37, seed 1234567:

  | Format / size | CPU median |
  | --- | ---: |
  | RGBA8 2480x3508 | 830 ms |
  | RGBA8 4096x4096 | 1610 ms |
  | RGBA32F 2480x3508 | 798 ms |
  | RGBA32F 4096x4096 | 1541 ms |

  These measure a single CPU call including scratch/copy, not interactive
  threaded dialog latency. Peak memory, GUI cancellation latency and
  GPU-resident input transfer costs remain unmeasured.
- User confirmed Pencil on 2026-10-15 and authorized proceeding to G'MIC.
  Regression checklist: restart Solstice; compare preview/apply for outlines,
  hatching, combined and grayscale; adjust size/roughness/angle/seed; turn
  both components off; cancel rapid changes, Undo/Redo and reapply. Check
  transparent edges, channel locks, selections and non-RGB artwork. Paint
  beneath a filter mask/layer and save/reopen a custom seed/configuration.
  Compare GPU Engine on/off display. Automated XML tests do not prove .kra
  reopening or GUI behavior.
- G'MIC follows below, CPU only and last in the implementation sequence.

## G'MIC implementation record (2026-10-15)

The user confirmed the G'MIC implementation and CPU bridge optimizations
working on 2026-10-15. This completes the interactive hand-off for the local
Windows build. The confirmation does not extend the documented platform,
packaging or individual-filter coverage.

### Runtime and reproducible build

The archived `amyspark/gmic` tree was inspected. The selected Krita-native
successor is [vanyossi/gmic v3.7.4.1](https://github.com/vanyossi/gmic/releases/tag/v3.7.4.1),
built from `gmic-3.7.4.1.tar.gz`, SHA-256
`15674b544264fbc3f020be6fae27477aee6ac49316416f538e578a18ddb23879`.
The archive includes CImg and `gmic_stdlib_community.h`; no floating downloads
are needed during compilation. No suitable ext_gmic binary recipe was found
in the inspected local dependency inventory, so the runtime is built after
the host interface, rather than inserted as an unrelated prebuilt dependency.

`build-tools/gmic/runtime.lock.json` pins the archive and resources;
`runtime-patches.json` applies exact checked replacements, and
`build-runtime.py` verifies the archive and rejects unsafe extraction paths.
Re-running the build accepts already-applied patches. It uses Solstice's
installed `kritaqmicinterface`, Qt6/KF6 and LLVM-MinGW ABI. The plugin IID stays
`org.kde.krita.KritaGmicPluginInterface`, interface version `0x05010001`, service
`Krita/GMic`, runtime `krita_gmic_qt.dll`. No interface layout was changed.

Configuration: `GMIC_QT_HOST=krita-plugin`, `BUILD_WITH_QT6=ON`, Release,
`ENABLE_SYSTEM_GMIC=OFF`, `ENABLE_DYNAMIC_LINKING=OFF`, `ENABLE_CURL=OFF`,
`ENABLE_FFTW3=ON`, `ENABLE_LTO=OFF`; OpenMP is required and checked in the cache.
The local compiler is LLVM-MinGW 20251118/Clang 21.1.6 with `-fopenmp=libomp`.
G'MIC/CImg and FFTW compute on the CPU; no GPU backend is added. Qt's optional
network access to filter definitions is separate from the disabled CImg curl
integration. External commands and optional downloaded filters remain outside
the verified bundled baseline.

Build/install the bridge first:

```bat
call <krita-dev-root>\env.bat
cmake --build <krita-dev-root>\_build --target kritaqmic KisQmicIntegrationTest kis_qmic_tests -j 2
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\extensions\qmic\cmake_install.cmake
python build-tools\gmic\build-runtime.py --work <krita-dev-root>\gmic --install <krita-dev-root>\_install --deps <dependency-prefix> --compiler-bin <toolchain-root>\bin --jobs 2
```

`--archive <verified-local-tar.gz>` enables an offline source build. Local
dependencies and application share `_install`; CI uses separate prefixes.
The script installs `lib/kritaplugins/krita_gmic_qt.dll`, four `share/gmic`
assets (`gmic_cluts.gmz`, `gmic_denoise_cnn.gmz`, `gmic_lightleaks.gmz`,
`gmic_scale2x_cnn.gmz`), and `share/gmic/source` with the exact source archive,
manifest, patches, script and notices. G'MIC-Qt is GPL-3.0-or-later; preserve
the archive's G'MIC/CImg CeCILL/CeCILL-C and other source notices. Existing
Qt/KF/FFTW/compiler runtime notices continue to apply.

### Host invariants and repairs

- `QMic` restores actions on every exit via a scope guard. Reapply stays
  disabled until this window accepts output, including after action-manager
  refreshes. A process-wide guard prevents nested runtime launches across
  windows; both actions are disabled while running.
- Configure paths before constructing the runtime. `GMIC_PATH` is
  `KisSolsticePaths::defaultResourceDir()/gmic`; the patched QSettings macros
  use `SOLSTICE_GMIC_SETTINGS`, set to `configFilePath("gmic-qt.ini")`.
  Windows data packs are read from the installation's `share/gmic` and copied
  to the profile when absent. Stock Krita settings are not reused.
- Runtime patches change the displayed host name, clean up shared buffers and
  the image interface on early returns, and report allocation/conversion
  exceptions during output transfer. The native runtime exposes Active, All,
  ActiveAndBelow and ActiveAndAbove; only InPlace output is enabled.
- `KisImageInterface` captures the launching image, active and selected nodes,
  keeps the view manager weak, rejects a changed/closed target, locked input
  layers, invalid dimensions/channels and unsupported output destinations.
  Empty output is a no-op rather than deleting all input layers. Off-canvas
  size queries use the union with canvas bounds, matching the crop export.
- `KisQMicImage` validates positive dimensions, 1-4 channels and a maximum
  buffer size bounded by signed-int bytes before allocation. This rejects
  overflow; it does not promise that every allocation below the limit fits
  available RAM. Input allocation failure returns no images; output failure
  reports an error without starting the host applicator.
- RGB U8/U16/F16/F32 uses planar float conversion with G'MIC's 255 unit value.
  F32 negative/HDR values survive the bridge; individual filters can clip.
  Non-RGB conversion uses the existing RGB fallback, now clearing/resetting
  the output device just like the fast path so smaller results leave no stale
  pixels. The AllVisible/AllInvisible predicate was also corrected (these
  two modes remain disabled in the pinned runtime's UI).
- The existing `KisProcessingApplicator` integrates layer changes. Worker
  commands no longer consult `KisPart` or the current main window. A composite
  transaction holds pixel, metadata, layer-count and node-selection commands.
  Initial execution consumes first-redo guards once; later Undo/Redo replays
  the recorded commands rather than rebuilding/doubling them. Pixel commands
  precede metadata commands so offset restoration does not translate pixels
  twice. Selection replacement uses the existing copy composite with coverage.
  The interface follows the retained active layer after each accepted output,
  so a second Apply cannot read a layer removed by the first operation.

### CPU bridge optimization (2026-10-15)

The user requested performance priorities 1 and 2 only. No GPU kernels,
thread-count changes, OpenMP policy changes or LTO changes were introduced.

- The pinned runtime's `Host/KritaPlugin/host.cpp` now keeps a const reference
  to each CImg output instead of copying it. A depth-one image is copied
  directly into `KisQMicImage`; a volume copies the first z slice of each
  channel using `width * height * depth` as its source channel stride. This
  preserves the previous `get_slice(0)` semantics. Keep the final owning copy:
  the asynchronous host applicator can outlive the runtime's CImg list.
- The depth-one output path performs one payload copy instead of three.
  A 4096x4096 four-channel float image occupies 256 MiB, so this removes two
  256 MiB temporary payloads per result. This is a buffer-size calculation,
  not a measured process peak-RSS reduction; other filter buffers remain.
- `KisQmicRgbConverter.h` dispatches once on native RGBA U8/U16/F16/F32.
  Scaling and planar/interleaved conversion run directly over paint-device
  tile rows with their actual stride, removing the temporary float tile and
  per-tile virtual transformation. Output alpha presence is dispatched once.
  `KoColorSpaceMaths` and its arithmetic order preserve LUT scaling, clipping,
  rounding and float/HDR semantics. Non-RGB documents retain the existing
  color-managed fallback. Input runtime ownership/copies remain unchanged.

`testPlanarReference` compares against independently assembled native pixels
for all four depths, 1-4 input channels and unit values 1/255, including
negative/HDR samples, partial tiles, negative origins and moved devices.
It passed both before and after optimization. The actual-runtime fixture
also generates 1-4 channels at depths 1 and 3 with distinct channel/z values,
checks the first-slice result and restores the document with Undo each time.
When changing synthetic commands, clear the stored `Arguments` too: the
headless runtime saves the previous command there after a run.

Opt-in converter timing (no runtime filter execution or output-copy timing):

```bat
set QT_QPA_PLATFORM=offscreen
set QT_QPA_FONTDIR=C:\Windows\Fonts
set SOLSTICE_BENCHMARK_GMIC_TRANSFER=1
<krita-dev-root>\_build\bin\KisQmicIntegrationTest.exe testPlanarReference benchmarkTransfer
```

Ryzen 9 9950X / Windows 11, Clang 21.1.6, Qt 6.8.0, RelWithDebInfo host
build; square solid RGBA image with origin (-19,11), preallocated planar
buffer, reused output device. Median of three iterations after one warm-up.
Input means native pixels to G'MIC, output means G'MIC to native pixels.
Times in milliseconds (before -> after):

| Size | Depth | Input | Output |
| --- | --- | --- | --- |
| 2048 | U8 | 11.21 -> 10.54 | 19.85 -> 22.11 |
| 2048 | U16 | 11.25 -> 10.73 | 20.73 -> 21.30 |
| 2048 | F16 | 11.45 -> 9.16 | 24.56 -> 22.11 |
| 2048 | F32 | 8.98 -> 6.53 | 16.77 -> 13.85 |
| 4096 | U8 | 44.57 -> 32.26 | 78.95 -> 73.94 |
| 4096 | U16 | 50.13 -> 30.80 | 79.19 -> 73.53 |
| 4096 | F16 | 46.36 -> 31.02 | 102.69 -> 80.59 |
| 4096 | F32 | 36.37 -> 25.60 | 66.71 -> 52.61 |

The measured 4096 input conversions improved by 28-39%; output gains were
smaller for integers. The 2048 integer output measurements were slightly
slower, so do not promise a universal conversion speedup. These timings do
not establish an end-to-end filter speedup, and exclude the separate runtime
copy reduction. Logs: `gmic-transfer-before.txt` and
`gmic-transfer-after-final.txt` in the task's scratch artifacts.

Validation: eight relevant CTest suites passed. The installed-runtime suite
reported 12 passed, 0 failed, 1 skipped (the opt-in transfer benchmark),
including channel/slice correctness, Undo/Redo and dialog/menu cancellation.
The standalone opt-in conversion/reference run reported 4 passed, 0 failed.
The runtime and host interface were rebuilt and installed locally.
The user confirmed both optimization priorities working on 2026-10-15.

### Integration validation and regression checks

`KisQmicIntegrationTest` covers size validation, RGBA U8/U16/F16/F32 and Gray
fallback, negative/HDR float samples, layer names/opacity/offsets and growth,
layer removal, feathered selection and repeated Undo/Redo. F32 comparisons
allow 2e-7 rounding from multiplying/dividing by 255; integer/F16 samples are
byte-exact. The installed-runtime test is opt-in:

```bat
set QT_QPA_PLATFORM=offscreen
set QT_QPA_FONTDIR=C:\Windows\Fonts
set KRITA_NO_ASSERT_MSG=1
set SOLSTICE_TEST_GMIC_RUNTIME=1
set GMIC_SYSTEM_PATH=<krita-dev-root>\_install\share\gmic
<krita-dev-root>\_build\bin\KisQmicIntegrationTest.exe -o <scratch>\gmic-integration.txt,txt
```

The fixture creates an isolated profile and a minimal plugin directory (LCMS,
paint ops/tools/shapes and the two G'MIC modules), uses a real `KisApplication`
and native view, and avoids unrelated Python/ML dock initialization. It applies
`add 10` through the actual CPU runtime, checks exact RGB output and document
Undo/Redo, rejects unsupported/empty output, loads the full offline dialog,
then tests menu launch/cancel twice and action restoration. The synthetic
engine command is removed before catalog launch because it is not a GUI filter
name. Never force-close a parent while a nested warning dialog is active.

Before the optimization, observed: **11 passed, 0 failed, 0 skipped** with
the real installed runtime;
the offline catalog displayed **691** filters. `SOLSTICE_GMIC_ARTIFACT_DIR`
enables a dialog screenshot. `SOLSTICE_BENCHMARK_GMIC=1` enables a Windows-only
CPU-time observation: on Ryzen 9 9950X, 2048x2048 RGBA, eight `blur 20` calls,
406 ms wall time and 9547 ms process CPU time (23.5 CPU/wall). This demonstrates
parallel CPU work for that workload, not a speed guarantee for every filter.

The eight relevant CTest suites (the six native effects plus both G'MIC suites)
passed. Python syntax, PowerShell parsing, source-checksum rejection and an
idempotent rebuild of the pinned runtime passed. A fresh independent staging
directory containing 113 binaries with recursively checked DLL dependencies,
Qt plugins, application resources and G'MIC data also passed all 11 integration
checks with PATH restricted to its own bin directory and Windows System32,
fresh settings, and no developer G'MIC cache. This validates the runtime's
dependency closure; it is not a claim that the full release ZIP or cloud build
has been produced/tested.

Manual hand-off completed by user confirmation on 2026-10-15. Retain the
following checklist for future regressions: restart Solstice, use
Filter > Start G'MIC-Qt on artwork,
compare preview/Apply/OK, Undo/Redo, Re-apply and Close. Try a soft selection,
multiple paint layers, alpha and a non-RGB document; cancel a slow filter,
reopen the dialog, and verify that another document is unchanged. Optional
online update failure, forced missing-runtime/dependency scenarios and every
individual catalog filter have not been exhaustively exercised. No new
release or cloud workflow was published by this task. Linux/macOS runtime
packaging remains unverified.
