# Brush Stroke Preview — development notes

The stroke preview is implemented. This is the maintained contract; the
[original design and verification record](wiki/history/brush-stroke-preview-design.md)
is historical. The original automated record is dated 2026-10-04 and leaves
interactive acceptance pending; this documentation cleanup adds no new runtime
or manual verification. Use the feature-branch workflow in
[AGENTS.md](../../AGENTS.md#branch-policy) for maintenance.

User guide: [Brush Presets](../brush-stroke-preview.md).
Grouping: [Brush preset grouping](brush-preset-grouping.md).

## Scope and source locations

Brush Presets, the toolbar brush popup, the Brush Editor list and Quick Access
use the shared preview cache. The image is RGBA8 and generated on the CPU;
scheduling must avoid competing with the user's CPU/GPU painting jobs.
Stored preset icons and preset data remain unchanged.

| Area | Sources |
| --- | --- |
| Renderer and cache | `libs/ui/widgets/KisBrushStrokePreviewRenderer.*`, `KisBrushStrokePreviewCache.*` |
| Choosers and F5 integration | `libs/ui/widgets/kis_preset_chooser.*`, `kis_paintop_presets_chooser_popup.*`, `kis_preset_live_preview_view.*` |
| Docker | `plugins/dockers/presetdocker/presetdocker_dock.cpp` |
| Saved resource snapshots and facets | `KisResourceLocator.*`, `KisTagFilterResourceProxyModel.*`, `KisPresetDockerFilters.*` |
| Regressions | `libs/ui/tests/KisBrushStrokePreviewTest.cpp`, `libs/resources/tests/TestTagFilterResourceProxyModel.cpp` |

## Architecture and lifecycle

- `KisBrushStrokePreviewRenderer` owns one private RGBA8 image. The shared
  `enqueue`/`paintBackground` helpers preserve the F5 paths, pressure, brush
  adjustments and stripes. F5 retains its notification stroke and palette;
  the docker supplies a separate provider, 480x160 output, scaled brush size,
  fixed colors and seed. The provider includes a fixed gradient and pattern.
- The docker's transparent PNG is drawn on `#303030` in all themes. Its
  chooser is unsynchronized from `KisResourceItemChooserSync`, has a private
  width key `Solstice/BrushStrokePreviewWidth` (90-240, default 180), and uses
  a rectangular icon grid. The existing slider maps 30-80 to that width.
  Tooltips show names, not the resource tooltip's stored icons.
- The docker is fixed to stroke preview mode (user request 2026-10-07: turning
  the former "Stroke Previews" menu toggle off made the item disappear from
  the menu, and the icon view is not wanted). `enableStrokePreviewSetting()`
  turns the mode on, hides the Display section and its Thumbnails/Details
  actions, and renames the size section to "Preview Size". The setting
  `Solstice/BrushStrokePreview` is no longer read; existing values are
  ignored. `KisPresetChooser::setStrokePreviewMode()` still supports both
  modes (the tests toggle it).
- The toolbar brush popup and the Brush Editor list show stroke previews too
  (user request 2026-10-07). `KisPaintopBox` calls
  `enableStrokePreviewSetting()` on its `KisPaintOpPresetsChooserPopup`, so
  the popup matches the docker (filters, grouping, Preview Size only).
  `KisPaintOpPresetsEditor` calls `setStrokePreviewMode(true)` on its
  `presetWidget` without the docker filters (it has its own engine combo
  box) and hides its Display section and Thumbnails/Details actions; its size
  section is "Preview Size". All instances share
  `Solstice/BrushStrokePreviewWidth` and `Solstice/BrushPresetGrouping`,
  read when each instance enables preview mode or creates its filter bar.
  The preset strip retains icons. The Popup Palette has since been removed;
  see [Popup Palette removal](popup-palette-removal.md).
- `KisResourceItemChooser::setBottomBarLayout` explicitly restores the requested
  grid after the docker's responsive initialization has entered horizontal
  strip mode. Disabling responsiveness alone does not undo that previous
  layout. Preview mode keeps the grid at every docker height, uses available
  width for columns, and places tag/search/display controls below it (two
  control rows in narrow docks). Other chooser instances retain their existing
  responsive behavior. The regression test uses the actual popup wrapper and
  docker initialization order, then resizes and toggles the preview mode.
- Quick Access (2026-10-07): its Resources dialog embeds a `KisPresetChooser`
  in stroke preview mode with the docker filters, and its palette and Grid
  Edit brush items use the cache through `QuickAccessStrokePreviews` (see
  `quick-access.md`).
- Grouping by engine or bundle (2026-10-07) is described in
  [`brush-preset-grouping.md`](brush-preset-grouping.md).
- `KisPresetDockerFilters` adds two per-instance checkable dropdown menus:
  engines use stored `paintopid` metadata and registry display names; bundles
  use storage IDs/locations and display names, plus an explicit non-bundle
  group. It enumerates unfiltered active presets so choosing one facet does
  not remove the other facet's options. It does not load preset images.
  Resource/storage changes refresh the options; dirty/thumbnail-only changes
  do not trigger a scan. Selections last for the docker instance and survive
  preview/icon toggling, with all entries selected for a new instance.
- `KisTagFilterResourceProxyModel::setAdditionalFilters` combines OR within
  each metadata/storage list with AND across facets and existing tag/search
  filters. Empty enabled lists match nothing. The additional state is private
  to this model; storage activation and shared resource models are untouched.
  `KisResourceItemChooser::setBottomBarWidget` hosts the dropdowns alongside
  the existing controls, wrapping into extra rows for narrow docks. The docker
  now retains this bottom layout in icon mode as well.
- Resource rows are grouped by name, filename and MD5 in the existing model.
  `activeStorageIdsForIndex` queries every active stored copy of that identity
  (and resource type); both option enumeration and filtering use those IDs.
  Inspecting only the representative row's StorageId would hide bundles whose
  presets also exist locally or in another bundle. Results stay deduplicated.
- `KisResourceLocator::resourceSnapshot` bypasses the dirty cache and checks
  the expected saved checksum before/after loading. Its storage helper is
  independently tested for folder, memory and bundle storage. No reload on
  a cached resource, resource-database writes or preset/icon saves are used.
  The earlier assessment's private `storageByLocation` cannot be called by
  UI code; the public snapshot API deliberately encapsulates that operation.
- Cache entry names hash the saved preset MD5 plus sorted linked/embedded
  resource signatures, with the render dimensions; the directory is
  `KisSolsticePaths::cacheDir() + "/brush-stroke-previews/v1"` (normally
  `%APPDATA%\Solstice\cache\brush-stroke-previews\v1`). Resource model
  updates invalidate in-memory lookup state; unchanged keys can still use
  existing disk PNGs. A stale saved checksum is never persisted as `.none`.
- Visible requests are collected on show/resize/scroll/model changes. The
  process-wide cache tracks each consumer independently. Disk reads, PNG
  decoding, atomic writes and pruning use QtConcurrent; delegate painting
  only reads the decoded LRU (300 images). Resource snapshots are prepared
  on the GUI thread because the resource interfaces/database are GUI-owned.
- Generation is serial. All document images are checked for idle state;
  existing/new/loading documents are observed. `KisImage::sigStrokeStarted`
  requests cancellation before a new document stroke is submitted. Worker
  callers deliver to the GUI through Qt's automatic connection. The idle
  poll also covers other work and image replacement. Cancellation is
  cooperative, not preemptive: a running paint job must return first.
- The renderer polls completion without `waitForDone()` on the GUI. Timeout
  requests cancellation after five seconds; the cache only starts another
  render after the private scheduler is idle. Hidden/scrolled-away/painting
  cancellations discard results and do not persist a failure marker.
- `FreehandStrokeStrategy::setPreviewRandomSeed` initializes both per-dab
  and per-stroke RNGs before scheduling. Ordinary strokes and F5 retain
  random seeds. Only a private proxy gets `Solstice/StrokePreviewSeed`;
  MyPaint disables tool-dependent slow tracking for docker previews. Its
  fresh libmypaint brush already initializes its RNG deterministically;
  setting `MYPAINT_BRUSH_STATE_RNG_SEED` does not reseed libmypaint 1.6.1.
  This property is never saved to a preset. The docker clears RGB under zero
  alpha before caching, since MyPaint can leave different invisible RGB
  values even with identical dab output. Visible pixels remain unchanged.
  See [libmypaint 1.6.1 brush implementation](https://github.com/mypaint/libmypaint/blob/v1.6.1/mypaint-brush.c)
  for the RNG initialization and inactive legacy RNG state field.

## Cache

The disposable cache lives under `KisSolsticePaths::cacheDir()` in
`brush-stroke-previews/v1`. Entries use the saved preset/resource fingerprint
and `480x160` size, with `.png` for an image and `.none` for an empty result.
Do not persist a failure for a stale checksum or an interrupted consumer.
Timeout requests cancellation after five seconds and waits for the private
scheduler to become idle before completion; a timeout can produce an empty
result. Normal user-painting/hide/scroll cancellations are interruptions.

Keep the 300-image decoded cache, 60-day disk age limit and 256 MiB disk budget.
Pruning touches only preview cache files; old render-version directories are
discarded. A change to rendering that invalidates cached pixels needs a render
version change. Do not package this cache with resource bundles or profiles.

## Build, test, and install

Build targets: `KisBrushStrokePreviewTest`, `kritapresetdocker`,
`kritamypaintop`. Rebuild/install their shared resources/image/brush/UI and
paint-op dependencies as needed. The test links the current MyPaint
static implementation, replacing the registry factory after plugin loading,
so pre-install tests do not silently exercise an older installed plugin.
Do not run all UI tests. The test uses test-mode resource/config locations;
its optional `SOLSTICE_PREVIEW_BASELINE_DIR` writes comparison PNGs there.
Run it offscreen with fonts: `QT_QPA_PLATFORM=offscreen` and
`QT_QPA_FONTDIR=C:\Windows\Fonts`. Without fonts the text metrics differ;
`testResponsiveDockerLayout` then fails its 1100x100 check, because the
filter bar (with the grouping dropdown) wraps one more row.

For cross-process persistence, run `testPersistentCache` twice with the same
`SOLSTICE_PREVIEW_PERSISTENT_TEST_DIR`; set
`SOLSTICE_PREVIEW_EXPECT_CACHE_HIT=1` on the second process to assert that it
does not start the renderer. Use the general
[build/test/install procedure](development-workflow.md) and fully restart
Solstice after installing rebuilt libraries/plugins.

## Invariants

- Preserve the Brush Editor's live-preview paths, sizes, backgrounds and normal
  random behavior. Only dedicated cached previews use deterministic seeds.
- Never modify presets, bundles or resource database entries to render previews.
  Render the saved version even when the current preset is dirty.
- Never wait for painting jobs on the GUI thread. Observe document activity and
  request cancellation before user painting; cooperative cancellation cannot
  preempt a paint-op job instantaneously.
- Cached previews use private fixed resources, not the canvas's current colors.
- Unsupported/failed previews leave an empty area and the name; no icon fallback.
- Preserve `Solstice/BrushStrokePreviewWidth` and shared grouping compatibility.
  The old `Solstice/BrushStrokePreview` toggle is ignored.
- Resolve the disposable cache with `KisSolsticePaths::cacheDir()`; do not write
  to the legacy Krita cache or include previews in resource bundles.

## Verification evidence

[The archived automated results](wiki/history/brush-stroke-preview-design.md#automated-verification-2026-10-04)
record deterministic rendering, cache persistence/invalidation, cancellation,
chooser layout, saved resource snapshots, filters and F5 before/after comparisons.
Those results describe the tested revision; rerun the relevant tests for a
behavior change. The manual checks below remain the interactive checklist.

## Manual checks

1. Open the Brush Presets docker:
   - cells fill progressively;
   - names are shown under the strokes;
   - roundmarker, experiment and clone presets show empty previews with
     names.
2. Restart: all previews appear immediately (read from the cache).
3. Change and save a preset: only its preview is rendered again. Change
   without saving: the preview stays, and `*` appears.
4. Paint while previews are generating: no stutter. Generation resumes
   after the stroke.
5. Toolbar brush popup: stroke previews, filters and grouping as in the
   docker; Brush Editor list: stroke previews with its engine filter.
6. The display menus of the docker, the toolbar popup and the Brush Editor
   list show only Preview Size (no Display section, no Stroke Previews
   toggle).

## Pending enhancements

A square dab-image mode and previews in the remaining preset strip are separate
refinements. The archived proposal to extend the Popup Palette is superseded
by [its removal](popup-palette-removal.md); it is not a current work item.
