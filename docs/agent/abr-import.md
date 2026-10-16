# Photoshop brush (ABR) import - maintenance reference

This is the current technical contract, checked against the implementation on
2026-10-16. The [user guide](../photoshop-brushes.md) owns import workflows and
visible limitations. The [development history](wiki/history/abr-import-development.md)
preserves the original investigation, staged plan, measurements and dated
verification. Early unsupported lists and interim texture rules in that history
must not override the implementation described here.

## Scope and source ownership

An ABR is a read-only `AdobeBrushLibrary` resource storage. It exposes sampled
tips, readable patterns and converted Pixel Brush presets, with file tags and
preset folder tags. It is treated as a bundle by the existing preset/tip filters.
There is no ABR exporter or Photoshop rendering-equivalence guarantee.

| Source | Responsibility |
| --- | --- |
| [KisAbrParser.h](../../libs/brush/KisAbrParser.h), [KisAbrParser.cpp](../../libs/brush/KisAbrParser.cpp) | Byte/section reader, sampled-tip decoding, raw pattern/descriptor/hierarchy sections and warnings |
| [kis_abr_brush_collection.cpp](../../libs/brush/kis_abr_brush_collection.cpp) | Collection loading, stable resource identities, display names, pattern reading, conversion and folder association |
| [KisAbrPresetConverter.h](../../libs/brush/KisAbrPresetConverter.h), [KisAbrPresetConverter.cpp](../../libs/brush/KisAbrPresetConverter.cpp) | Descriptor-to-Pixel-Brush settings and hierarchy interpretation |
| [KisAbrStorage.cpp](../../libs/brush/KisAbrStorage.cpp) | Typed iteration/lookups, immutable source presets, reload and tags |
| [KisResourceLocator.cpp](../../libs/resources/KisResourceLocator.cpp) | Resource reload/snapshots and database/tag synchronization |
| [KisPresetDockerFilters.cpp](../../libs/ui/widgets/KisPresetDockerFilters.cpp), [kis_predefined_brush_chooser.cpp](../../plugins/paintops/libpaintop/kis_predefined_brush_chooser.cpp) | ABR bundle filtering; UI ownership is in [Brush preset grouping](brush-preset-grouping.md#brush-tips-2026-10-10) |
| [KisBrushStrokeLayerIO.cpp](../../libs/ui/KisBrushStrokeLayerIO.cpp) | Embedding an ABR tip into a stroke-layer KRA; owned by [Brush Stroke Layer](brush-stroke-layer.md#embedded-resources) |

The parser, collection, storage and converter belong to `kritalibbrush`.
It links `kritaimage` and `kritapsdutils`, not `kritalibpaintop`: the latter
depends on the brush library. Keep that layering when maintaining conversion.

## Parsing and resource identity

### Versions, data and limits

`KisAbrParser::parse()` supports versions 1/2 (legacy brush lists) and 6-10
(subversions 1/2, `8BIM` sections). Other versions/subversions fail with a
warning. It resets `Contents`, collects readable tips and raw `patt`, `desc`
and `phry` payloads, and reports success when tips or descriptor bytes exist.
A successful parse does not prove that any presets can be converted.

- Sampled tips accept 8/16-bit, uncompressed or PackBits RLE data. The internal
  image is inverted gray in RGB32 (white means no paint); a 16-bit sample uses
  its high byte. This is decoding support, not preservation of 16-bit precision.
- Dimensions must be positive and each edge at most 8192. The decoded-byte
  budget is 512 MiB for a legacy list or each `samp` section parse. It is not
  a total-file or total-process memory limit: the collection reads the file
  into memory, and pattern/descriptor parsing has separate behavior.
- Legacy version-2 names are read up to 4096 UTF-16 code units; longer names
  are skipped. Legacy computed tips are counted and skipped.
- Reader operations check requested ranges. Unreadable entries produce
  warnings or stop the affected sequence when the next boundary is unavailable;
  partial contents may survive. Section headers may omit four-byte padding.
  Do not describe these checks as complete validation of arbitrary damaged data.

The collection reads patterns first, then requires at least one readable
sampled tip before it converts descriptors or associates folders. A file with
only computed preset descriptors therefore produces no converted presets,
even though `KisAbrPresetConverter` itself supports computed round tips.
Without samples, collection loading succeeds only if patterns were recovered;
pattern-only data without descriptors also fails the parser's success test.
Preserve these distinctions when diagnosing an apparently supported version.

### Stable tip names and hashes

For sampled tips without a stored name, retain the old resource filename
`<file-base>_<original-index>`. Skipped entries keep their original indices.
Version-2 stored names remain resource keys. Display names prefer the stored
name, otherwise the first descriptor name referring to `sampledData`, otherwise
the legacy key. A display-name improvement must not rename existing resource
identities used by presets, tags and the database.

Tip md5 is generated from the decoded image's PNG data. Existing 8-bit results
were retained by the parser migration; corrected 16-bit images differ from the
old broken decoding and therefore can have different hashes. Database sync
does not refresh an already registered tip's display name when its filename
and md5 are unchanged. The user can remove/reimport an old library to refresh
those names; do not silently migrate keys to force an update.

### Patterns

Read each length-delimited `patt` entry with
`KisAslReader::readPsdSectionPattern()`, so an unsupported color mode need not
hide following entries. Unreadable modes such as CMYK/Lab are skipped and
counted in the log. Truncated outer lengths can stop the sequence.

Create `KoPattern` resources as `<Idnt>.pat`, using the embedded name (or
identifier fallback) and the decoded resource-data hash. The converter resolves
textures by `Idnt`, not a free-form display-name match.

## Preset conversion

Read descriptors once through `KisAslReader::readFillLayer()`; the collection
uses the XML both for tip naming and conversion. Conversion requires a
registered `paintbrush` engine. Preset resource filenames are
`<file-base>_preset_<descriptor-position>.kpp`, starting at 1 and retaining
positions of skipped presets. Empty display names become `<file-base> <number>`.

Use `KisBrushModel::BrushData::write()` for the tip. For curve, texture and
masking options, write the same properties/XML used by native Pixel Brush
settings without adding a circular dependency on `kritalibpaintop`.
Read descriptor integer values back as signed 32-bit values; negative brightness
must not become an enormous unsigned value.

### Mapping

| ABR setting | Current conversion |
| --- | --- |
| `sampledBrush` / `sampledData` | Resolve file tip by ID; if there is exactly one tip in the source map, it is the fallback for an unresolved ID. Store `abr_brush` signature with md5, filename and name |
| `computedBrush` | Native automatic circular tip, roundness as ratio, fade = 1 - hardness |
| `Dmtr`, `Angl`, `Spcn` | Diameter clamped 1-5000; degrees to radians; percent spacing clamped 0.01-10, automatic spacing disabled. Sampled scale = diameter / longest tip edge |
| `Rndn` | Computed-tip ratio, or a constant Ratio option for sampled tips |
| `useTipDynamics`: size/angle/roundness | Size, Rotation and Ratio options with their minimum values and sensors |
| `bVTy` 1/2/3/4 | Fade, pressure, declination and tangential-pressure sensors; fade length is clamped 1-9999 |
| `bVTy` 5/6/7/8 | Locked drawing angle, drawing angle, rotation, rotation. Initial rotation (7) is an approximation and is logged |
| `jitter` | Random sensor; angle jitter uses Rotation strength. Other dynamics combine control with a curve from 1 - jitter to 1 |
| `useScatter`, `bothAxes` | Scatter strength clamped 0-5, X enabled, Y enabled for both axes |
| Main `Cnt ` | Approximate count by dividing spacing by count clamped 1-16, with minimum spacing 0.01; it does not emit several simultaneous dabs |
| `usePaintDynamics`: `opVr`, `prVr`; tool `Opct`, `flow` | Opacity/Flow sensors, minima and tool strengths |
| Tool `Md  ` | Native composite operation through `compositeOpFor()`; unknown modes default to Over |
| `dualBrush` | Masking brush, native blend mapping, master-size coefficient = secondary/main diameter, secondary spacing/scatter |
| Paint mode | Wash (`PaintOpAction = 2`) |

### Curves, texture and color

Every curve option needs at least one sensor. Constant options use pressure
with a flat `0,1;1,1;` curve. Set `<id>UseSameCurve=false` so per-sensor
minimum/fade/jitter curves are not replaced by the identity common curve.
The early stage-4 settings did not do this; do not restore them.

For a resolvable texture, store both pattern signatures and the filename/name.
Scale is percentage/100 clamped 0.01-10; contrast is
`1 + textureContrast / 50` clamped 0-2. Brightness is
`textureBrightness / 150`, kept positive in sign when `InvT` is true and
negated otherwise, then clamped -1 to 1. Set Invert only from `InvT`:
Subtract, Height and Linear Height do not require an additional inversion.
Offsets start at zero with random offsets disabled. Depth and minimum depth
map to Texture Strength with their sensors.

`texturingModeFor()` maps Multiply, Subtract, Darken, Overlay, Color Dodge/Burn,
Linear Dodge/Burn, Hard Mix, Height and Linear Height to the native texture-mode
values; unknown modes default to Multiply. Keep those serialized numeric values
aligned with native texture options.

Color dynamics are implemented. Foreground/background jitter `clVr` maps to
Mix (1 foreground, 0 background); hue `H   ` sets random Hue strength (up to
jitter times 180 degrees either way). Saturation jitter `Strt` and purity,
and brightness jitter `Brgh`, map to Saturation/Value using the curve interval
`(shift - jitter + 1)/2` to `(shift + jitter + 1)/2`, clamped 0-1.
A false `colorDynamicsPerTip` selects `fuzzystroke`; true or absent selects
`fuzzy`. Do not list color dynamics or the main scatter count as wholly absent.

### Unsupported and approximate behavior

Recognized unsupported settings are counted by name and logged by the collection:
tip flipping/flip jitter, secondary scatter count and flip, wet edges, noise,
brush pose, texture protection, missing texture resources or missing secondary
tips. Initial rotation is logged as an approximation. A missing primary tip
without the single-tip fallback skips the preset.

Other settings such as smoothing, `tiltScale` and `brushGroup` are not mapped;
the converter does not enumerate every unknown descriptor key in its warnings.
Logs are diagnostics, not a complete lost-settings report. Main scatter count
and dual-brush conversion are approximations. Darken/Color Burn masks or a
secondary brush much larger than the main tip can yield faint or empty strokes.

## Storage, reload and previews

`KisAbrStorage` exposes `Brushes`, `Patterns` and `PaintOpPresets`.
Lookups dispatch `.pat` to patterns, `.kpp` to presets and other names to tips.
The resource iterator lazily loads the collection and holds resources through
its collection pointer. `tags()` also loads contents before building tags.

`resource()` must return a clone for presets: editing it must not mutate the
converted source version retained by the collection. `loadVersionedResource()`
restores cloned source settings, name and image. It supports preset reload even
though `supportsVersioning()` is false; the ABR itself is not rewritten.
Do not assume every iterator accessor independently clones the preset.

Do not assign an invented md5 to a converted preset. Leave the initial hash
empty for the resource/database machinery to calculate consistently. Preview
snapshots are resolved by stored identity/hash; a conflicting synthetic hash
previously suppressed ABR stroke previews. The
[preview reference](brush-stroke-preview.md) owns snapshot/cache behavior.

## Folder tags

`presetFolders()` interprets the `phry` descriptor's `hierarchy` list.
Group tokens (`Grup`/`group`), `groupEnd` and preset tokens can be represented
by descriptor class, a sole child's key, or an enumeration/text value.
Other tokens count as presets. Nested lists within groups are also walked.

Associate preset positions with the `desc` order, including skipped positions.
Ignore excess tokens or entries for presets that were not converted; unlisted
presets remain at the top level. Stray closing tokens are ignored and open
groups close at the end. Paths are capped at 32 folder names; deeper names
collapse into that path (this is not a general recursion-depth limit).
Missing/unreadable hierarchy leaves only file tags.

Each resource type retains its file tag. Additional folder tags apply only to
presets, with URL `<filename.abr>/<folder>/<subfolder>` and display name
`<file-base> / <folder> / <subfolder>`. A parent tag includes descendant
presets. Resource-database synchronization adds storage tags on startup, so
new folder tags can appear for an already registered library without changing
its tip keys.

Synthetic folder forms have regression coverage. Real exported ABR files
containing nonempty folder hierarchies were not available for the recorded
manual check; their compatibility remains unconfirmed.

## Build, test and install

For implementation changes follow [Development workflow](development-workflow.md).
Build `kritalibbrush`, affected dependencies and the relevant targets:

| Test target | Coverage |
| --- | --- |
| `TestAbrParser` | Bundled version-6.2 tips/naming; synthetic version-6/9 raw and 16-bit RLE; version-2 names/computed skips; truncation/mutation/oversized dimensions; patterns with unsupported entries between readable ones |
| `TestAbrStorage` | Tip iteration, file tags, resource items and lookup |
| `KisAbrPresetTest` | Converted settings read by native option types, actual painting, clone/reload isolation, folder forms/tags/depth, color dynamics/scatter-count approximation and texture-depth sign/inversion |
| `KisBrushStrokeLayerTest` | Related ABR embedding: `testAbrTipSaved`; run when resource identity/serialization changes |
| `KisBrushStrokePreviewTest` | Related preview and tip-bundle filtering when storage/UI integration changes |

Declarations are in [brush tests](../../libs/brush/tests/CMakeLists.txt) and
[Pixel Brush tests](../../plugins/paintops/defaultpaintops/brush/tests/CMakeLists.txt).
The preset suite uses its isolated profile and bundled resources; its basic
painting test covers 12 bundled presets, not every Photoshop brush or version.

Example after configuring the development environment (Windows `cmd.exe`;
substitute local paths):

```bat
call <krita-dev-root>\env.bat
cmake --build <krita-dev-root>\_build --target kritalibbrush TestAbrParser TestAbrStorage KisAbrPresetTest -j 2
set QT_QPA_PLATFORM=offscreen
set QT_QPA_FONTDIR=C:\Windows\Fonts
ctest --test-dir <krita-dev-root>\_build -R "TestAbrParser|TestAbrStorage|KisAbrPresetTest" --output-on-failure
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\libs\brush\cmake_install.cmake
```

Install every rebuilt shared dependency/plugin as well. Restart Solstice after
DLL installation; never terminate the user's application to unlock files.

## Manual checks and verification status

For relevant implementation changes:

1. Import a representative ABR in Manage Resource Libraries. Verify its preset,
   tip and pattern resources, file tags and bundle filters. Reimport an old
   library to check improved display names while existing tip references work.
2. Draw with sampled and computed presets, pressure minima/fade, opacity/flow,
   scatter, texture and dual brush. Check color dynamics with different
   foreground/background colors and with per-tip/per-stroke randomization.
3. Exercise Height/Linear Height/Subtract textures and explicit inversion.
   Record faint/empty brush names and settings; do not infer parity with
   Photoshop from successful import or one painted stroke.
4. Edit a converted preset, reload it and confirm source settings return.
   Check stroke previews and startup responsiveness with existing libraries.
5. With an actual ABR containing folders, verify nested tags include descendant
   presets and preserve file tags. This still needs real-file confirmation.
6. Draw an ABR tip on a Brush Stroke Layer, save/reopen its KRA and enlarge;
   use the owning feature's embedding and replay checks.

The original plan records successful parser/storage/preset test runs and user
confirmation of stages 1-5 and finishing fixes on 2026-10-10. Folder confirmation
was limited to unchanged behavior with files lacking folders. Historical sample
counts, timings and diagnostics remain in
[development history](wiki/history/abr-import-development.md); they are not
current performance promises or exhaustive format coverage.

This extraction adds no runtime changes and claims no new native build,
installation or interactive test. The computed-only collection limitation and
the scope of parser budgets above were checked in the code during extraction,
not newly reproduced with interactive files.
