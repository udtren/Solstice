# Color Reference - development notes

## Status and scope

2026-10-16: implementation authorized after the user accepted the Layer Comps
resize fix. CR0-CR4 implemented, built, tested and installed.
Included in **0.2.0-beta.6**; see the [release ledger](versioning.md#history)
and [package check](wiki/history/github-actions-development.md#effects-layer-comps-and-gmic-package-check-2026-10-16).
Interactive confirmation remains pending; publication does not complete it.

User guide: [Effects filters](../effects-filters.md#color-reference).
The [original plan](color-reference-plan.md) is historical; this document
supersedes its proposals. CLIP STUDIO PAINT's unpublished algorithm is not
reproduced. No supplied artwork or third-party presets are bundled.
Photo-library/camera import were excluded by the user; .clip is unsupported.

## Decisions under implementation authorization

- CPU-only native filter, ID `solstice_color_reference`, configuration version
  1, **Filter > Adjust > Color Reference**.
- Image mode is a tone-to-color mapping using the reference's distribution.
  The original absolute-bin proposal was rejected because it largely preserves
  brightness even with Keep brightness off. Weighted quantile positions allow
  the reference's tonal balance to change the result.
- Equal-brightness source hues converge at Amount 100. Reference colors in
  the same brightness bin are averaged and can become muted. Lower Amount
  retains original color differences. This is an intentional first-version
  limitation, not whole-image color-distribution transfer.
- The original untagged document-native table proposal was rejected. Tables
  have a fixed encoded-sRGB color space and versioned encoding, so document
  profile changes cannot reinterpret their RGB as another profile's values.
- Keep brightness defaults on; Amount defaults to 100; default preset Amber.
- Eight original procedural palettes: Amber, Ocean, Rose, Forest, Sunset,
  Lavender, Copper and Moonlight. Generated in code; no asset installation.
- 50 **user images**, in addition to the eight presets. Store normalized
  previews instead of entire large PSD/KRA files. Import preserves the name
  stem, adding -2, -3, etc. for collisions.
- Use the tagged gradient chooser and a preview in the main widget. Edit opens
  the generic gradient editor on a private copy; only OK commits the change.
  Hide resource add/update buttons. The image grid displays references.
- Bounded table construction and thumbnail loading run synchronously. Opening
  50 stored thumbnails plus eight presets measured 20 ms. No background worker
  is used. File drops defer import through a widget-bound GUI callback (see
  [Library and widget lifecycle](#library-and-widget-lifecycle)); the import
  itself uses a wait cursor and remains synchronous. Large-file responsiveness
  needs manual review.

These are implementation choices within the user's authorization, not
separate confirmations of every design option.

## Files and targets

In `plugins/filters/solsticeeffects/`:

| Files | Responsibility |
| --- | --- |
| `KisColorReferenceFilter.*` | Defaults and per-pixel CPU filter |
| `KisColorReferenceResources.*` | Tables, gradient XML, presets, reference library/import |
| `KisColorReferenceConfigWidget.*` | Controls and missing-reference preservation |
| `tests/KisColorReferenceFilterTest.cpp` | Numeric, library, widget and document integration tests |
| `CMakeLists.txt`, `tests/CMakeLists.txt`, `SolsticeEffectsPlugin.cpp` | Build and registration |

Targets: `solsticeeffects`, `kritasolsticeeffectsfilter`,
`KisColorReferenceFilterTest`. Gradient Map, Color Transfer and existing
Solstice effect algorithms are unchanged. The plugin constructor registers
the filter only; document import happens on explicit user interaction.

## Persistence contract

| Property | Meaning |
| --- | --- |
| `source` | 0 image, 1 gradient |
| `amount` | Double 0-100, default 100 |
| `keepBrightness` | Bool, default true |
| `lut` | Active SCR1 table; the only reference input during pixel processing |
| `imageLut` | Last image table, retained across source switching |
| `gradientLut` | Last gradient table, retained across source switching |
| `referenceId` | `preset:0` through `preset:7`, or `user:<filename>.png`; display only |
| `gradientXML` | Baked stop/segment gradient for editing |

SCR1 is base64 of ASCII `SCR1` plus 1024 RGB triples, big-endian IEEE float32
via QDataStream SinglePrecision. RGB is encoded sRGB using
`sRGB-elle-V2-srgbtrc.icc`. Alpha is omitted. Length, magic, finite values and
magnitude <=65504 are validated. Invalid/unknown tables do nothing. Encoding
changes must retain a reader for this version.

Normal configuration XML embeds all tables; rendering needs neither files
nor a gradient resource. Variable foreground/background stops are baked when
edited. Opening the widget does not regenerate tables. Missing images show a
notice; the saved colors remain active, including after changing Amount or
switching source modes. Deletion removes the library copy without changing
the active table. Cloning configuration retains channel flags.

## Calculation and color management

Working space is encoded-sRGB float32. Brightness is
`0.2126 R + 0.7152 G + 0.0722 B` on encoded channels: artistic luma, not
linear-light luminance or CIE lightness.

Image preparation:

1. Import into a temporary batch-mode KisDocument, refresh projection and wait
   for jobs. No document window opens. Layered files use the composite.
2. Make a thumbnail <=512 pixels per side, convert with its profile to sRGB,
   and store an 8-bit sRGB PNG. Reference palettes are SDR, including HDR imports.
3. Accumulate alpha-weighted RGB means and coverage in 256 brightness bins.
   Ignore transparent pixels; reject references with no visible pixels.
4. Place occupied bins at the midpoints of cumulative coverage intervals
   (weighted quantile positions). Interpolate 1024 samples and extend endpoints.
   Uniform references yield constant tables.

Gradient preparation samples KoAbstractGradient::colorAt() at 1024 equally
spaced positions in working RGB. Transparent-stop interpolation follows the
gradient implementation; gradient alpha does not replace layer alpha.

Pixel processing converts to working RGB, clamps only the brightness lookup
coordinate and interpolates table colors. Keep brightness shifts the mapped
RGB to input luma. For luma in 0-1, chroma is reduced around that luma until
RGB fits 0-1, instead of clipping individual channels. Outside SDR, luma and
chroma are unbounded. Amount blends source/result in working encoded RGB.
Convert back to the document profile/depth/model and copy only enabled color
channels over original bytes. Preserve alpha and fully transparent pixels.

Amount 0 returns before conversion (exact bytes). Invocation-owned converters
avoid sharing KoColorSpace's cached converter across concurrent patches.
Read transaction old pixels, stage output in a device with matching space,
offset and default pixel, release accessors, then copyAreaOptimized. Cancellation
before copyback discards scratch output; host strokes handle cancellation
during copyback. Needed/changed rectangles are identity; threading and LoD
are supported.

HDR: unbounded luma preservation is tested in working-space F32. ICC conversion
may constrain other profiles' negative/out-of-gamut values. Keep brightness
off at Amount 100 intentionally replaces HDR tones with reference endpoints
outside the lookup interval. Integer output is bounded by its format.
Gray/CMYK/Lab convert through float RGB; grayscale stays gray.

Gradient Map parity is limited: it uses intensityF() (0.30/0.59/0.11 after
QColor conversion), a device-extent-dependent cache and gradient-limited
alpha. Neutral sRGB input with an opaque smooth gradient is compared within
0.005. Colored input need not match. Direct smooth two-stop gradient sampling
is checked within 0.00004, allowing KoStopGradient's 15-bit mixing weights.

## Library and widget lifecycle

Folder: `KoResourcePaths::getAppDataLocation()/colorreference/`, normally
`%APPDATA%/Solstice/resources/colorreference/`. Tests inject a temporary folder.
Only basename PNG IDs are accepted; traversal and symlinks are rejected.
QSaveFile writes atomically. Failed import does not add an entry.

File chooser: PNG, JPEG, TIFF, BMP, TGA, PSD, PSB, WebP, KRA, KRZ, subject to
installed importers. Original files are untouched. Delete asks confirmation;
built-ins cannot be deleted. Library changes persist independently of Cancel
in the filter dialog.

File drag-and-drop (2026-10-16): the image QListWidget viewport accepts local
file URLs with the same extensions as the chooser, including multiple files.
Remote URLs, folders, plain text, internal item drags and move-only drops are
rejected. Drops use CopyAction even if the source offers MoveAction. A
viewport event filter consumes drag enter/move/drop instead of invoking the
list model's internal drop/reorder behavior. The file list is copied into a
context-bound zero-delay callback, allowing native drag completion before
import; destroying the widget cancels it. Import is guarded against reentry.

The chooser and drops share importImages(): refresh once per batch, select
and scroll to the last success, preserve settings on total failure, report
errors beneath the list, and stop when 50 user images have been stored.
Failed files do not block later valid files. Originals are never modified.

Widget owns controls; signal blockers prevent configuration loading and canvas
resource attachment from replacing saved tables or emitting changes. The
generic gradient editor owns private clones inside a parented modal dialog.
A scroll area isolates its large minimum width from the main filter widget.
Canvas resources come from the
host view; no view pointer is retained. Import is never called from configuration().

## Verification and handoff

Tests cover image tables/invalid data, 10 model/depth combinations, Amount 0,
alpha/hidden pixels/channel flags, concurrent patches, rect/LoD contracts,
gradient XML/sampling/HDR, Gradient Map comparison, cross-profile conversion,
temporary library import/delete/collision/limit/unreadable files, widget XML
and missing references, selection/Undo/Redo/cancellation, and actual .kra
save/reload of a filter layer plus mask with a missing reference.

Use the shared env.bat, then:

```bat
cmake --build <krita-dev-root>\_build --target kritasolsticeeffectsfilter KisColorReferenceFilterTest -j 2
ctest --test-dir <krita-dev-root>\_build -R plugins-filters-solsticeeffects --output-on-failure
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\filters\solsticeeffects\cmake_install.cmake
```

Offscreen UI: `QT_QPA_PLATFORM=offscreen`, `QT_QPA_FONTDIR=C:\Windows\Fonts`.
Set `SOLSTICE_COLOR_REFERENCE_ARTIFACT_DIR` to an existing folder for widget
screenshots and original procedural-art comparisons. Tests use
testui.h/KISTEST_MAIN for the gradient resource database. Every filter passed
to process() needs a local resources snapshot, including Gradient Map.

Manual checks pending: all presets and import formats, gradient tags/editing,
Amount 0/50/80/100, brightness toggle, Preview/Cancel/Apply, Undo/Redo,
selection, missing-reference layer/mask after reopening, resize and large imports.
Do not terminate Solstice for DLL installation; ask the user to close it if locked.

### Verification record (2026-10-16)

- Built the module and all eleven Solstice effects test executables.
- Effects regression ctest: **11/11 suites passed**, 16.74 s.
- After the final keyboard-selection/deduplication UI fix, reran the dedicated
  test with screenshot generation: **20 passed**, 0 failed/skipped, 2.60 s
  (QtTest includes init/cleanup in this count).
- Checked image and gradient widgets at 560x430 and compared three original
  procedural-art recolor examples with brightness on/off. Main controls fit
  without the generic editor forcing a wider minimum size.
- Verified batch PNG import and rejected invalid files; other format import
  plugins were not exercised interactively. Fifty reference previews loaded
  in 20-22 ms in the test environment.
- Native .kra round trip preserves the projection and both filter tables for
  a mask and adjustment layer whose reference filename does not exist.
- No new compiler warnings. New C++ files formatted completely; touched
  tracked and new files checked for whitespace errors.
- Installed `kritasolsticeeffectsfilter.dll`; SHA-256 matches the build:
  `6BB9667BB17CFF844B8ECAE54D6AABDA356B5E0AB6887177E74491BA964F2838`.
- Offscreen warnings concern unavailable window-system features, test
  translations and Gradient Map's unused dither resource; no failed checks.
- Interactive acceptance remained pending at this initial handoff, before
  the release commit. Later publication is recorded under Status and scope;
  it does not add interactive acceptance evidence.

### File-drop follow-up (2026-10-16)

- User requested adding reference images by dragging them into the Image grid.
- Added explicit viewport drop acceptance and shared batch import with the
  existing file chooser. No filter algorithm or serialized properties changed.
- Built module/test without compiler warnings. Dedicated suite: **20 passed,
  0 failed, 1 skipped** (opt-in screenshots), 2.97 s. The new test sends actual
  Qt drag-enter/move/drop events to the viewport, including over a preset and
  empty space; checks multiple files, bad input, CopyAction, source preservation,
  failed-batch identity, the 49-to-50 boundary and queued-callback destruction.
- Installed DLL SHA-256 matches the build:
  `7114DEB43960D7E6726C299EA514DA6083AB2700B481B4924C898502A83052DB`.
- C++ formatting and whitespace checks passed. Native Explorer-to-Solstice
  drag interaction remains for the user to confirm after restarting.
