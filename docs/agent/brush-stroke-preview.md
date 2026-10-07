# Brush Stroke Preview — agent design document

Status: **implemented on the user's current branch; interactive acceptance pending (2026-10-04).**
Implementation belongs on `krita-sol-gpu`, the primary development branch.
The older `krita-sol` branch is historical; do not merge from it for this work.
The preview image is RGBA8 and uses the CPU painting path. Scheduling must
still avoid competing with the user's CPU/GPU painting jobs.

## Goal

Show every preset in the **Brush Presets** docker as a rendered stroke (like
the stroke preview at the top of the Brush Editor, F5) with the preset name
below it, instead of the preset's stored icon. The stored icon stays in the
preset files and in the code, but this docker no longer displays it.

## Implementation notes (2026-10-04)

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
  modes (the tests toggle it), and the toolbar popup and Brush Editor keep
  their icon view and Display menu.
- `KisResourceItemChooser::setBottomBarLayout` explicitly restores the requested
  grid after the docker's responsive initialization has entered horizontal
  strip mode. Disabling responsiveness alone does not undo that previous
  layout. Preview mode keeps the grid at every docker height, uses available
  width for columns, and places tag/search/display controls below it (two
  control rows in narrow docks). Other chooser instances retain their existing
  responsive behavior. The regression test uses the actual popup wrapper and
  docker initialization order, then resizes and toggles the preview mode.
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
  `QStandardPaths::CacheLocation/brush-stroke-previews/v1`. Resource model
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

Build targets: `KisBrushStrokePreviewTest`, `kritapresetdocker`,
`kritamypaintop`. Rebuild/install their shared resources/image/brush/UI and
paint-op dependencies as needed. The new test links the current MyPaint
static implementation, replacing the registry factory after plugin loading,
so pre-install tests do not silently exercise an older installed plugin.
Do not run all UI tests. The test uses test-mode resource/config locations;
its optional `SOLSTICE_PREVIEW_BASELINE_DIR` writes comparison PNGs there.

### Automated verification (2026-10-04)

- Built and installed the feature and rebuilt shared dependencies into the
  configured `_install` tree; all 11 updated DLL hashes match `_build/bin`.
  Solstice was not launched or terminated by the agent. Interactive checks
  are pending the user's next session.
- `KisBrushStrokePreviewTest`: 30 passes, covering six engines, repeated
  rendering, canonical transparent pixels, cancellation, saved folder/memory/
  bundle resources, chooser sizing isolation, disk/empty-marker reuse,
  pruning, two consumers and user-stroke priority across a document image.
- Layout follow-up: reproduced the real docker's pre-existing horizontal
  strip, then verified 1100/420/280 px widths, a 100 px tall docker and
  preview off/on. Grid columns change with available width, controls remain
  below the view and wrap in the narrow case. Wide/compact screenshots were
  inspected. Installed the four rebuilt shared DLLs and verified their hashes.
- Filter follow-up: mouse/keyboard checkbox toggling keeps menus open; all/none,
  engine OR, bundle OR, engine/bundle/search intersections, toolbar isolation
  and preview/icon mode retention are covered. Two actual test bundles share
  one preset with a local copy: either bundle matches the deduplicated row,
  and bundle activation remains unchanged. `TestTagFilterResourceProxyModel`
  also passed all 12 cases for the existing tag/search/storage behavior.
  Dropdown and wide/compact footer screenshots were inspected; compact
  layout checks wait for the entire filter bar to fit inside the docker.
  Installed the five rebuilt shared DLLs and verified their build hashes.
- MyPaint ordinary and randomized fixtures: five additional process runs,
  both repeated-render comparisons passing in each run.
- Separate process cache check: run `testPersistentCache` twice with the
  same `SOLSTICE_PREVIEW_PERSISTENT_TEST_DIR`, and add
  `SOLSTICE_PREVIEW_EXPECT_CACHE_HIT=1` for the second process. It asserts
  zero renderer starts on the second run; both runs passed.
- Regression CTest suites: `KisGpuPaintDeviceTest`, `KisGpuProjectionTest`,
  `KisGpuBrushJobsTest`, `KisGpuStrokeTest`, and `KisMyPaintOpTest` all passed
  (5/5). GPU validation was enabled with the configured Vulkan SDK layer path.
- F5 before/after PNGs match exactly for Pixel, Color Smudge, MyPaint and
  Filter. Spray and Sketch retain F5's random seeds; their before/after
  images were visually checked for matching geometry, sizing and background.
- The offscreen docker layout was rendered and inspected with
  `QT_QPA_FONTDIR=C:\Windows\Fonts`: landscape cells, names and empty Clone
  preview are present. This does not validate tablet input or perceived
  responsiveness in a real session; the manual checks below remain required.

The design and original assessment below record the rationale and approved
scope. The implementation notes above describe the current source.

## Decisions approved by the user

| Topic | Decision |
| --- | --- |
| Shape | Stroke previews are always landscape. Square tiles are reserved for a later "dab image" view. |
| Item layout | Preview image on top, brush name below (one item = one cell, see "Item layout"). |
| Generation cost | Rendered once, stored locally; later sessions only read the stored image (see "Cache"). |
| Colors | Fixed colors, not the canvas foreground/background colors. |
| Engines that cannot be previewed | An empty preview area (name still shown). Never fall back to the icon. |
| Modified (dirty) presets | Preview the saved version of the preset, not the unsaved changes. |
| Scope | Only the Brush Presets docker for now. Other choosers (toolbar brush popup, Brush Editor preset list, preset strip, popup palette, Quick Access) keep their current look; they may adopt the preview later. |

## Current behavior (what is reused or replaced)

- **Stroke preview in the Brush Editor:** `libs/ui/widgets/kis_preset_live_preview_view.{h,cpp}`
  (`KisPresetLivePreviewView`, created in `libs/ui/forms/wdgpaintopsettings.ui`,
  minimum 320x60).
  - It paints into a private `KisImage` (RGBA 8-bit) with `FreehandStrokeStrategy`
    and a `KisResourcesSnapshot` built from a cloned proxy preset. It is
    asynchronous: a `NotificationStroke` reports completion, and the layer is
    converted to a `QImage`.
  - The brush size is clamped to 3–25 px. Sketch and Spray brushes keep their
    real size; Spray brushes and texture scale are additionally adjusted.
  - The stroke path is engine-dependent: a horizontal pressure ramp for
    sketch, curve and particle brushes, and an S-curve with pressure for
    everything else (MyPaint gets fixed curve timings).
  - Background: alternating gray stripes for color smudge, deform and filter
    brushes, the palette window color otherwise. The stroke color is the
    palette text color.
  - No preview for `roundmarker`, `experimentbrush` and `duplicate` (the
    widget shows "No Preview for this engine").
- **Preset lists:** `libs/ui/widgets/kis_preset_chooser.{h,cpp}`.
  - `KisPresetChooser` wraps a `KisResourceItemChooser` and paints items with
    `KisPresetDelegate`, which draws `KisResourceThumbnailCache::getImage(index)`
    (the preset's stored icon).
  - View modes: `THUMBNAIL` (icon grid, no text) and `DETAIL` (icon and name
    in a row).
  - The item size comes from `KisConfig::presetIconSize()` (kritarc
    `presetIconSize`, default 60).
- **Where `KisPresetChooser` is used:**
  - `KisPaintOpPresetsChooserPopup` (`kis_paintop_presets_chooser_popup.cpp`),
    which serves both the **Brush Presets docker**
    (`plugins/dockers/presetdocker/presetdocker_dock.cpp`) and the toolbar
    brush popup;
  - the Brush Editor's preset list (`kis_paintop_presets_editor.cpp`);
  - `wdgpresetselectorstrip.ui`.

  So the new appearance must be switched on per instance, and only the
  docker turns it on.

## Target design

### Components

1. **`KisBrushStrokePreviewRenderer`** (new, `libs/ui/widgets/` or
   `libs/ui/brushpreview/`): renders one preset to a `QImage` without a
   widget.
   - The rendering logic of `KisPresetLivePreviewView::setupAndPaintStroke()`
     and `paintBackground()` is moved into this class, parameterized by size
     and colors. `KisPresetLivePreviewView` then uses the renderer, so the
     Brush Editor and the docker draw identical strokes.
   - Behavior of the Brush Editor preview must not change (sizes, paths,
     backgrounds, the excluded engines and their message).
   - Asynchronous like today: one small `KisImage` per job, completion through
     a notification stroke, never `waitForDone()` on the GUI thread.
   - The resource provider is a private, fixed one (see "Colors"), not the
     canvas resource manager. It must provide everything presets read from
     canvas resources (foreground/background color, current pattern and
     gradient, opacity, flow, size), so the rendering does not depend on the
     user's current canvas state.
2. **`KisBrushStrokePreviewCache`** (new, process-wide, GUI-thread API):
   - `preview(presetIndex)` returns the stored image, a "no preview" marker,
     or nothing yet. In the last case it queues a render and emits
     `previewReady(md5)` when done.
   - It owns the render queue, the disk store, the in-memory LRU of decoded
     images, and invalidation.
3. **Delegate and chooser changes** (`kis_preset_chooser.{h,cpp}`):
   - `KisPresetChooser::setStrokePreviewMode(bool)` switches to a new
     `KisStrokePreviewDelegate`, or to a third view mode used only in stroke
     preview mode.
   - `KisPaintOpPresetsChooserPopup::setStrokePreviewMode(bool)` forwards it.
   - `PresetDockerDock` enables it. The toolbar popup and the editor do not.

### Item layout

```
+--------------------------------------+
|                                      |
|          stroke preview (3:1)        |
|                                      |
+--------------------------------------+
| Brush name (elided, one line)        |
+--------------------------------------+
```

- Cell width W, preview height W/3, then one text line (font height + 4 px
  padding). The name is elided in the middle when too long. Selection and
  hover are drawn around the whole cell; the dirty `*` and the existing
  locked/storage indicators stay in the corners of the preview.
- W follows the existing size slider. The value stored in
  `KisConfig::presetIconSize()` maps to `W = 3 × presetIconSize`, so the
  preview height equals today's icon size. Whether the docker keeps the shared
  size key or gets its own key (`Solstice/BrushStrokePreviewWidth`) is decided
  at implementation (it must not change the toolbar popup's icon size).
- Grid flow: as many cells per row as fit the docker width (the existing
  `IconGrid` list view mode with a non-square grid size). The view-mode menu
  (Thumbnails/Details) is hidden or ignored in stroke preview mode, because
  the layout always shows the name.
- An engine without a preview, or a failed render: the preview area stays
  empty (background only) and the name is shown. The stored icon is not
  drawn.

### Rendering parameters

- Render size: 480x160 device pixels, one fixed size per cache entry, scaled
  down when painted. High-DPI screens with larger cells may render at
  2x (720x240) later; the size is part of the cache key.
- Brush size: the editor's clamping, scaled from its 60 px height to the
  render height (3–25 px at 60 px becomes about 8–67 px at 160 px). The exact
  factor is a rendering constant and part of the cache key.
- Path: the editor's paths, mapped to the render rectangle.
- Colors (fixed, part of the render version):
  - stroke: near-white `#E8E8E8` on the docker's dark theme;
  - background: transparent, so the cell background shows through;
  - color smudge, deform and filter brushes: the editor's gray stripes
    (`#505050` / `#8C8C8C`), with a white stroke;
  - foreground `#E8E8E8` and background `#000000` in the private resource
    provider.

  Open question for a light theme: a near-white stroke on a light cell is
  hard to see. Either keep a fixed dark background inside the preview area,
  or keep two color sets keyed by theme lightness. Decide when implementing;
  both stay "fixed colors".
- Excluded engines (empty preview): `roundmarker`, `experimentbrush`,
  `duplicate`, plus any engine that times out (see the queue).

### Which preset version is rendered

- The **saved** version: the cache key uses the resource's stored MD5
  (the `KisAbstractResourceModel::MD5` column/role of the resource database,
  `libs/resources/KisResourceModel.h`, which changes only when the preset is
  saved).
- If the chooser's in-memory preset is dirty, the renderer must not use it.
  It loads a clean copy of the stored resource (to be verified at
  implementation: `KisResourceLocator` / `KisResourceStorage` loading by
  storage location and filename, or `KisResourceModel` with the dirty state
  bypassed). The dirty `*` indicator is still shown on the cell.

### Cache

- **Location:** `QStandardPaths::CacheLocation` (`%LOCALAPPDATA%\krita\cache`
  on Windows), subdirectory `brush-stroke-previews/v<N>/`. Not inside the
  resource folder: previews are disposable and must not travel with bundles
  or backups.
- **Entry:** `<md5>-<width>x<height>.png` (RGBA). A "no preview" result is
  stored as a zero-byte `<md5>-<w>x<h>.none` file, so excluded or failing
  presets are not retried every session.
- **Key:** preset MD5, render size, and the render version `N`. Bump `N`
  whenever paths, colors, clamping, or the renderer change: old directories
  are deleted at startup.
- **Lifetime:**
  - Entries are reused across sessions, so after the first run the docker
    only reads PNG files.
  - A preset that is saved (new MD5), a changed render version, or deleted
    cache files cause a new render of only the affected presets.
  - Pruning: at startup, remove entries not used for 60 days or beyond
    256 MB (oldest first). Recording use through file modification time is
    enough.
- **In memory:** LRU of decoded `QImage`s (about 300 entries). The
  `QListView` asks only for visible items.

### Render queue

- Renders are requested only for visible items, most recently requested
  first. Requests for items scrolled out of view are dropped before they
  start.
- One render at a time, on the image scheduler's worker threads (as the
  editor preview does). The queue pauses while a canvas stroke is in
  progress (any view's image has running strokes) and resumes after, so
  painting is never slowed. It also pauses while the docker is hidden.
- Timeout: a render that has not finished after 5 s is cancelled and stored
  as "no preview" for this render version.
- First run with a few hundred presets: the docker fills progressively
  (empty cells with names first). Expected tens to hundreds of ms per preset.
  Measure and record the real numbers when implementing.

### Settings

- `Solstice/BrushStrokePreview` (bool, default true): originally the docker's
  stroke preview toggle; no longer read since the docker was fixed to stroke
  previews (2026-10-07).
- Optional size key, see "Item layout".

## Files expected to change

| File | Change |
| --- | --- |
| new `libs/ui/widgets/KisBrushStrokePreviewRenderer.{h,cpp}` | Renderer extracted from the live preview. |
| new `libs/ui/widgets/KisBrushStrokePreviewCache.{h,cpp}` | Queue, disk store, LRU, invalidation. |
| `libs/ui/widgets/kis_preset_live_preview_view.{h,cpp}` | Use the renderer (behavior unchanged). |
| `libs/ui/widgets/kis_preset_chooser.{h,cpp}` | Stroke preview mode, new delegate and layout. |
| `libs/ui/widgets/kis_paintop_presets_chooser_popup.{h,cpp}` | Forward the mode; hide the view-mode actions in that mode. |
| `plugins/dockers/presetdocker/presetdocker_dock.cpp` | Enable the mode. |
| `libs/ui/CMakeLists.txt` | New sources. |
| `docs/brush-stroke-preview.md`, this document, `AGENTS.md` index, `docs/agent/feature-inventory.md`, `README.md` | Documentation (when implemented). |

## Invariants

- The Brush Editor's live preview looks exactly as before.
- No preset file, bundle, or resource database entry is modified; icons stay
  untouched and are still used by every other chooser.
- Rendering never blocks the GUI thread and never runs while the user paints.
- A render never uses the canvas resource manager or the user's current
  colors; results depend only on the saved preset and the render version.
- Empty preview, never the icon, when a preset cannot be rendered.

## Tests (to write with the implementation)

- Renderer: the same preset rendered twice gives identical images. Excluded
  engines return "no preview". A dirty preset renders its saved version.
- Cache:
  - A stored entry is read instead of rendered (render count stays 0 in the
    second run).
  - A new MD5 or render version renders again.
  - "No preview" markers are honored.
  - Pruning removes old entries.
- Editor preview: before/after image comparison for a set of default presets
  (pixel brush, color smudge, spray, sketch, MyPaint, filter).
- Do not run the whole `libs-ui-*` suite or `kis_kra_saver_test` on the
  desktop (blocking dialogs). Run the new tests individually.

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
5. Toolbar brush popup and Brush Editor list: unchanged icons.
6. The docker's display menu shows only Preview Size (no Display section, no
   Stroke Previews toggle); the toolbar popup still offers Thumbnails/Details.

## Original implementation assessment (2026-10-04, before implementation)

The feature is feasible with the existing native stroke system, but it is
not implemented by this assessment. Keep the approved docker-only scope.
The code inspection identified the following concrete integration details:

1. **Saved resources:** `KisResourceLocator::resource()` may return the dirty
   cached object. Do not call `reloadResource()` on that object: it can discard
   the user's unsaved preset changes. Resolve the storage/filename from the
   resource model, then load an independent resource through
   `storageByLocation()->resource(type + '/' + filename)`. The base storage
   implementation creates a new resource and calls `loadVersionedResource`.
   Test folder, bundle and memory storage separately; verify the loaded MD5
   still matches the requested saved MD5 before writing a cache entry. A save
   or deletion during loading must discard/requeue the obsolete result.
2. **Resource snapshot:** `KisResourcesSnapshot` dereferences its provider and
   reads more than FG/BG: pattern, gradient, effective composite op, opacity,
   eraser/alpha lock, mirroring, exposure and canvas resources requested by
   the preset. The docker needs a fully initialized private provider with
   fixed defaults and cloned dependencies. Keep the editor adapter's current
   provider behavior so F5 gradients and other existing behavior stay intact.
   The "no canvas resource manager" invariant applies to docker generation.
3. **Randomness:** `FreehandStrokeStrategy` installs its own
   `KisStrokeRandomSource` into painting information. A fixed path alone does
   not make random/scatter presets deterministic. Add a preview-only seed
   injection path, leaving ordinary strokes and F5 behavior unchanged, before
   claiming the repeated-render pixel test passes. Linked patterns/brush tips
   also need dependency fingerprints or explicit cache invalidation when
   changed; saved preset MD5 alone does not identify all rendered inputs.
4. **Painting priority and timeout:** `KisImage::isIdle()` can gate new jobs,
   but polling it cannot stop a preview already running on another image.
   Track every document image after a main window exists; never initialize
   `KisPart` from a plugin constructor. Add a coordinated activity/cancellation
   mechanism before enabling the docker by default. Cancellation is
   cooperative: a 5-second timer requests cancellation, then completion must
   be acknowledged before destroying the private image or starting another
   preview. Never call `waitForDone()` on the GUI thread. Hide/scroll/painting
   cancellations are not failed presets and must not create `.none` files.
   The scheduler cannot promise instantaneous preemption of a paint-op job;
   responsiveness must be measured with real painting before rollout.
5. **Chooser isolation:** `KisPresetChooser::setIconSize()` currently updates
   `KisResourceItemChooserSync`, a shared singleton. A new settings key alone
   would still resize other choosers. Use a per-instance grid-size path and
   `Solstice/BrushStrokePreviewWidth` for the docker. Keep the shared icon
   size, chooser mode and toolbar popup untouched. Recommended fixed preview
   background: dark `#303030`, including light UI themes, with the approved
   near-white stroke; smudge/filter backgrounds retain the approved stripes.
6. **Cache lifecycle:** visible item requests should be rebuilt after each
   scroll/filter/model reset; keep stable saved-resource keys, not QModelIndex
   values across asynchronous work. Use request generations to ignore late
   completions. Decode/read/prune cache files outside delegate painting and
   perform PNG/marker writes atomically. Prune only the dedicated preview
   cache directories. Register consumers so hiding one docker does not pause
   another visible docker. Keep the 300-image/256-MB limits and 60-day policy.

Recommended implementation order, with a working/tested boundary per bundle:

| Bundle | Deliverable | Required verification |
| --- | --- | --- |
| A | Headless renderer, explicit parameters, saved-resource loader and preview seed control. Capture F5 baselines before extracting its rendering logic. | Pixel/smudge/spray/sketch/MyPaint/filter before-after images, excluded engines, dirty preset unchanged, deterministic repeat and cancellation lifetime. |
| B | Disk/LRU cache, visible-request queue, document activity coordination and cancellation/timeout handling. | Second-process cache hit without rendering, dependencies/MD5/version invalidation, bounded memory, pruning, two windows, hide/scroll/cancel races, no GUI wait. |
| C | Docker-only delegate, private size setting and menu toggle, user documentation and installation. | Names/indicators, empty unsupported previews, unchanged toolbar/F5 icons and sizes, cache reuse after restart, real painting while generation is active. |

Do not put a widget-per-preset workaround into the docker: it would multiply
image schedulers and cannot satisfy the queue/lifetime constraints. Do not
call the design complete solely because cache/unit tests pass; F5 parity and
painting responsiveness are release gates. No configuration, presets or cache
files were changed during this assessment.

## Later enhancements

- A square "dab image" view (one dab of the brush tip), as a separate mode.
- Stroke previews in other choosers (toolbar popup, popup palette, Quick
  Access), reusing `KisBrushStrokePreviewCache`.
