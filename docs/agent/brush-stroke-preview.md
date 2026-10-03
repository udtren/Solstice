# Brush Stroke Preview — agent design document

Status: **design approved by the user (2026-10-03), not implemented.**
Implementation belongs on `krita-sol` (custom feature work) and is merged
into `krita-sol-gpu` as needed. It does not touch GPU engine code.

## Goal

Show every preset in the **Brush Presets** docker as a rendered stroke (like
the stroke preview at the top of the Brush Editor, F5) with the preset name
below it, instead of the preset's stored icon. The stored icon stays in the
preset files and in the code, but this docker no longer displays it.

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

- `Solstice/BrushStrokePreview` (bool, default true): stroke previews in the
  Brush Presets docker; false restores the icon view. Shown in the docker's
  view menu.
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
6. Turn `Solstice/BrushStrokePreview` off: the icon view returns.

## Later

- A square "dab image" view (one dab of the brush tip), as a separate mode.
- Stroke previews in other choosers (toolbar popup, popup palette, Quick
  Access), reusing `KisBrushStrokePreviewCache`.
