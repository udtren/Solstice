---
type: log
---

# Wiki log

Append-only. One entry per ingest, lint pass or restructuring, newest last.
Format: `## YYYY-MM-DD <kind>: <subject>`, then what changed and the sources.

## 2026-10-07 setup: wiki created

- Created the index with conventions and workflows; `AGENTS.md` points to it.
- Source: the user's request after comparing Karpathy's LLM wiki pattern
  (gist 442a6bf555914893e9891c11519de94f) and nashsu/llm_wiki.

## 2026-10-07 restructure: GPU engine phase records

- Moved `docs/agent/gpu-engine.md` "Review status" and every phase record
  (phases 4.1-4.97, about 4,750 non-empty lines) verbatim into six
  `history/` pages. Headings were promoted one level, and relative links were
  adjusted. A line-by-line comparison with the previous version matched.
- `gpu-engine.md` keeps status, decisions, architecture, plan, build/test,
  source maps, configuration, invariants, checklist and risks. Its new
  "Phase history" section lists every moved section.
- Rewrote the anchor links in `README.md`, `docs/gpu-engine.md` and
  `paint-trace-baseline-runs.md`, and the section references in
  `feature-inventory.md`, `gpu-engine-handoff.md` and
  `gpu-work-priorities.md`.

## 2026-10-07 ingest: GPU affine and Liquify work, session pitfalls

- New pages:
  - `concepts/cpu-gpu-bit-parity.md`;
  - `concepts/krita-copy-semantics.md`;
  - `concepts/qt-numeric-and-geometry.md`;
  - `decisions/gpu-phase-numbering.md`;
  - `decisions/xmlgui-unchanged.md`;
  - `pitfalls/build-format-test.md`;
  - `benchmarks/transform-and-filter-costs.md`.
- Sources:
  - commits `29912c4daf` (GPU Liquify, phase 4.97) and `3828911e98` (GPU affine,
    phase 4.94);
  - `history/gpu-phases-4.93-.md`;
  - qtbase v6.8.0 `src/gui/painting/qpolygon.cpp`;
  - the installed Qt 6.8 `qnumeric.h`;
  - this session's build and test experience.

## 2026-10-07 ingest: settings location phase 1

- `pitfalls/build-format-test.md`: KConfig main config names and the early
  first open. Source: KConfig 6.7.0 `kconfig.cpp`, `KisSolsticePathsTest`.

## 2026-10-07 ingest: Solstice versioning

- New page `decisions/versioning.md`. Source: `docs/agent/versioning.md` and
  the version uses found in `kra_converter.cpp`, `KisResourceLocator.cpp`,
  `KisResourceCacheDb.cpp` and `libkis/Krita.cpp`.

## 2026-10-07 ingest: README benchmark refresh

- `benchmarks/transform-and-filter-costs.md`, section "README refresh
  (2026-10-07)": all README rows remeasured (three fresh processes each),
  with the affine and Liquify GPU rows added to the README.

## 2026-10-07 ingest: test MIME database and known failing tests

- `pitfalls/build-format-test.md`:
  - the MIME database embedded in tests, and the empty storage location
    fix;
  - the list of known failing tests, with a baseline comparison (each of
    them also fails without the change).

## 2026-10-07 ingest: settings location complete

- The original decisions and findings are preserved in
  [settings/profile development history](history/settings-location-development.md).
  These include the relative KConfig names, the mounted kritarc defaults, the import rules,
  the Solstice defaults and the verification.
- Pitfall recorded in that history (phase 5): starting `krita.exe`
  directly does not work in the development environment; use `run-krita.bat`.

## 2026-10-07 ingest: GPU Gaussian blur family (phase 4.98)

- `history/gpu-phases-4.93-.md`: the phase 4.98 record.
- `concepts/cpu-gpu-bit-parity.md`: the tolerance rule for the FFT-based
  Gaussian convolution.
- `concepts/krita-copy-semantics.md`: the Copy op's batch-wide HDR clamping
  with a selection.
- `pitfalls/build-format-test.md`: installing plugins after a vtable change;
  PowerShell `Select-Object -First` ending a build.
- `benchmarks/transform-and-filter-costs.md`: GPU Gaussian blur rows.
- `decisions/gpu-phase-numbering.md`: 4.98 done, next 4.99.

## 2026-10-07 ingest: GPU Puppet Warp mesh rendering (phase 4.96)

- `history/gpu-phases-4.93-.md`: the phase 4.96 record.
- `concepts/krita-copy-semantics.md`: RGBA32F composites depend on the tile
  address (malloc alignment, SIMD/scalar split).
- `concepts/cpu-gpu-bit-parity.md`: which Puppet Warp results are compared
  bit for bit.
- `pitfalls/build-format-test.md`: Git Bash `sed -i` and CRLF files.
- `benchmarks/transform-and-filter-costs.md`: the Puppet Warp row.
- `decisions/gpu-phase-numbering.md`: 4.96 implemented.

## 2026-10-07 ingest: Puppet Warp mesh with the Accurate preview

- `history/gpu-phases-4.93-.md`: phase 4.96 reverted and reapplied; the
  Accurate preview never built the mesh (fixed, `docs/agent/puppet-warp.md`).

## 2026-10-08 ingest: Android leftovers removed

- `pitfalls/build-format-test.md`: resolving preprocessor conditionals with a
  script, and editing `.ui` files that the user's tool regenerates.

## 2026-10-08 lint: first pass

- Every page is listed in the index. Frontmatter: all pages have `type`,
  `updated`/`status` and `sources`; history pages have no `related`, which
  the index now states as the rule for archives.
- References (scripted, `git ls-files` and `git grep -w`): 93 paths and
  relative links, and the class/function names in the non-history pages, all
  exist. Two non-symbols were flagged and are fine (`Q_OS_ANDROID` named as
  removed, the `VK_LAYER_PATH` environment variable).
- Contradictions fixed:
  - `docs/agent/gpu-engine.md` said `Solstice/GpuEngine` defaults to false;
    the embedded default kritarc sets true since 2026-10-07 (the code
    fallback stays false).
  - `decisions/xmlgui-unchanged.md`: the user's local `krita5.xmlgui` now
    lives in the Solstice profile (`%APPDATA%\Solstice`).
  - `pitfalls/build-format-test.md`: `updated` date.
- Benchmarks: all dated 2026-10-07 (current builds); nothing stale.

## 2026-10-08 ingest: test Fontconfig configuration

- `pitfalls/build-format-test.md`: why tests warned "Cannot load default
  config file" and how the test mains now find the installed `fonts.conf`.

## 2026-10-08 ingest: known failing tests 13 -> 5

- `pitfalls/build-format-test.md`: the remaining failures, and the causes
  and fixes of eight others (shared test profile, Qt 6 UTF-16 byte order
  mark and NUL handling, model signal pairing, `QRectF::toRect()`, test mains
  without `KRITA_PLUGIN_PATH`).

## 2026-10-08 ingest: brush option shared model phase 2a

- `pitfalls/build-format-test.md`: the test resource database has no brush
  tips or patterns (a bundle is added in the test `main()`), resource models
  must exist before `addStorage()`, parity references are checked against
  the old code, a brush tip option created alone leaks its page, and
  `Copy-Item` keeps the old modification time.
- Lesson recorded in `brush-option-shared-model-plan.md` (phase 2a): an
  option must notify only changes of what it writes. The masking brush
  notified the end of its preserve mode while the editor was reading the
  brush tip, which started a full rewrite inside the read (safe asserts on
  Shift + drag resize).

## 2026-10-08 ingest: brush option shared model phase 2b

- Lesson recorded in `brush-option-shared-model-plan.md` (phase 2b): with
  per-option writes, an option whose written data is baked from another
  option's state (painting mode, Lightness Strength, the masking size
  coefficient) must be written again when that option changes;
  `KisPaintOpOptionsModel::addDependency()` declares it. Each dependency is
  covered by a test that fails without it.
- `KisLockedPropertiesProxy::setProperty()` writes nothing to settings
  without an update listener, so a test that needs a full write of a model
  writes the options directly instead of `writeAll()`.

## 2026-10-08 ingest: Brush Presets scroll to the selected preset

- Feature document `brush-preset-scroll.md`. Lesson: `QAbstractItemView`
  scrolls to a new current item only with `autoScroll` on, also later when a
  hidden view is shown, so turning it off around a programmatic
  `setCurrentIndex()` keeps the position without affecting drag auto scroll.
  Manually checked 2026-10-08.

## 2026-10-08 ingest: brush options in Tool Options, phase 3a

- Feature document `tool-options-brush.md`. Lessons: a delegate that draws
  an extra column must shift the rectangle it passes to
  `QStyledItemDelegate::editorEvent()` too, or the checkbox hit area stays
  under the new column; a stable, untranslated id per option (the
  options-model id) is what kritarc stores, because labels repeat ("Size"
  in General and Masked Brush) and are translated.

## 2026-10-08 ingest: brush options in Tool Options, phase 3b

- Feature document `tool-options-brush.md` (page parameters, mirrors,
  preset preview, shape tools). Lessons: a copy of a lager-bound control
  must write through the control the way its connection listens
  (`setSpacing()` and `setCompositeOp()` do not emit, so the copy emits the
  signal) and read the control after the option's change signal, queued,
  because the model updates some controls with blocked signals; a page's
  own tabs must not hide a parameter, so only an explicit mode widget (the
  Auto or Predefined tip page) does; a script that assigns ids from
  variable names in a line can pick the wrong one (Painting Mode got the
  masked brush's id), so tests check that ids are unique.

## 2026-10-08 ingest: Opacity and Flow strength in Tool Options

- `tool-options-brush.md`: curve options without a row checkbox (Opacity,
  Flow) register their strength bar and Enable Pen Settings as page
  parameters in `KisCurveOptionWidget`; checkable curve options keep the
  row checkbox. README highlight with a screenshot
  (`docs/images/tool-options-brush.png`, shown at the Brush Presets image's
  width of 480).

## 2026-10-08 ingest: Fade as one Tool Options parameter

- `tool-options-brush.md`: a group box whose grid holds mirrorable controls
  is mirrored cell by cell; the auto tip's Fade (two values and the link
  button) is one parameter, hidden for the Soft mask type through its mode
  widget `PageFade`. The README screenshot was replaced.

## 2026-10-08 ingest: Brush Editor fixes (previous preset, scroll bar color)

- Two Krita bugs, recorded in `feature-inventory.md`:
  `KisPaintopBox::slotSwitchToPreviousPreset()` (Q) and
  `slotInputDeviceChanged()` (pen/eraser) changed the settings but not the
  Brush Editor's name and thumbnail, because only `resourceSelected()` and
  `restoreResource()` called `m_presetsEditor->resourceSelected()`;
  `KisCategorizedListView` replaces its active Window color with the text
  color, which its scroll bars inherited (white while focused). Lesson: a
  palette role changed on a scroll area reaches its scroll bars; set the
  role back on the bars, and again on each PaletteChange.

## 2026-10-08 ingest: Color Smudge on the shared model (phase 4)

- `brush-option-shared-model-plan.md`: Color Smudge's 22 options live in
  `KisPaintOpOptionsModel` with four dependencies (Smudge Length, Paint
  Thickness and Overlay Mode on the brush tip; Smudge Radius on Smudge
  Length). Lesson: the bundled presets use neither the new smudge engine nor
  a lightness tip, so parity variants must force the dependent bakes
  (`SmudgeRadiusVersion=2` with an out-of-range radius, a color tip from
  `RGBA_brushes.bundle`); otherwise the references prove nothing.
  `SOLSTICE_BRUSH_TEST_MAIN_WITH_BUNDLES` loads extra bundles in tests.
- `tool-options-brush.md`: Opacity and Flow's Enable Pen Settings rows carry
  only the option's name, keeping the label column narrow. README images
  moved to a new Screenshots section; the Tool Options image was replaced.

## 2026-10-08 ingest: Sketch and Bristle on the shared model (phase 4)

- `brush-option-shared-model-plan.md`: Sketch and Bristle (Hairy) options
  live in `KisPaintOpOptionsModel`, without dependencies. Their engine pages
  register their main sliders as Tool Options parameters. Parity helpers
  moved to `KisPaintOpParityTestUtils.h` for the next engines.
- `tool-options-brush.md`: `KisBrushOptionWidget::hideOptions()` drops the
  parameters of hidden tip controls. Lesson: Bristle's hide list names
  `KisBrushChooser/Spacing`, which no longer exists, so the predefined tip's
  spacing stays visible; check what a hide list really hides before
  asserting on it.
- `todo.md` (new): the user's task backlog; first entry, ABR import
  improvements with storytold/photocraft as a reference.

## 2026-10-08 query: missing .kra thumbnails

- New .kra files saved by Solstice showed no Explorer thumbnail. The files
  were fine (valid `preview.png` and `mergedimage.png`); no `.kra`
  thumbnail handler was registered, because Krita's shell extension comes
  with Krita's installer and Krita was no longer installed. Older files
  showed thumbnails from the Windows thumbnail cache only. Bundling the
  extension is recorded in `todo.md`.

## 2026-10-08 ingest: Tangent Normal, Hatching, Filter, Quick Brush (phase 4)

- `brush-option-shared-model-plan.md`: four more engines on the shared
  model, without dependencies. Filter's bake (fallback filter while none is
  set) is `KisFilterOptionModel::bakeOptionData()`, reached through the
  exported `KisBrushBasedOptionStates::bakeFilterOption()`. Lesson: libpaintop
  model classes are not exported; a plugin can only call libpaintop code
  marked `PAINTOP_EXPORT`, so bakes go through `KisBrushBasedOptionStates`.
  Bundled presets never lack a filter, so the fallback has its own test.
- Pitfall: clang-format re-sorts include blocks and rewraps the public
  constructor of upstream files; `restore_upstream`-style fixes put the HEAD
  include order back and append new includes.

## 2026-10-08 ingest: Quick Adjust controls as in Tool Options

- `quick-access.md`: the HueSVC popup and Quick Brush Adjustments use
  `KisDoubleSliderSpinBox`, `KisSliderSpinBox` and `KisAngleSelector`
  instead of `QSlider` rows and a custom dial; rotation still drives the
  `brushRotation` resource. README main image replaced.
- Pitfall: a child widget left out of a layout keeps the default geometry
  (0, 0, 100 × 30) and stays shown with its parent, above earlier siblings.
  The popup's unused docker columns covered the Size bar's left part and
  took its clicks; hide such widgets. An offscreen probe test (widget
  geometry, simulated clicks, `grab()`) separated this from the slider's own
  behavior before the fix.
- A long prefix in a value bar widens its layout column; give such bars a
  horizontal `QSizePolicy::Ignored` when columns must stay equal.

## 2026-10-09 ingest: Curve, Grid, Particle, Shape (phase 4); live Tool Options preview

- `brush-option-shared-model-plan.md`: four more engines without a brush tip
  on the shared model, no dependencies. Only Spray and MyPaint remain.
  Parity tests come from a scaffold script (static library, test target,
  bundled presets, references written by the old code); a template
  placeholder that silently fails to match leaves a test uncompilable, so
  check the generated file.
- `tool-options-brush.md`: the Tool Options preview renders a modified
  preset itself (debounced clone render with its own
  `KisBrushStrokePreviewRenderer`); the cache keeps rendering saved presets
  only. Lesson: a renderer with a running stroke must outlive its owner
  until `finished`, so detach it and delete it then.

## 2026-10-09 ingest: Spray and MyPaint on the shared model (phase 4)

- `brush-option-shared-model-plan.md`: Spray's shape page reads the spray
  area's diameter and scale through cursors on the SprayOp state but writes
  only its own data, so no dependency. MyPaint's options all patch
  `MyPaint/json`; `KisPaintOpOptionsModel::addSharedKey()` seeds a
  per-option write with the preset's current document. Lesson: the model's
  per-option write starts from an empty scratch configuration, so an option
  that read-modify-writes a shared key silently drops everything else in
  it; the full rewrite hid this because `resetSettings()` keeps the key.
  A parity test against the full rewrite alone does not catch it; test a
  single-option edit against the preset's document.
- Only Clone (`duplicate`) is left before phase 5.

## 2026-10-09 ingest: Clone on the shared model; phase 4 complete

- `brush-option-shared-model-plan.md`: Clone (`duplicate`), missing from the
  plan's engine list, now uses the model; every brush engine does, so
  phase 5 (retiring the clear-and-rewrite path) can start. Lesson: check
  the engine list against the settings widgets that lack
  `setOptionsModel()` instead of trusting a plan's list.

## 2026-10-09 ingest: shared model phase 5

- `brush-option-shared-model-plan.md`: the clear-and-rewrite in
  `slotGuiChangedCurrentPreset()` is only a warned safety net; locks write
  the model's option; Uniform Properties stay as they are (decision and
  reasons in the plan).
- Pitfall: Windows PowerShell 5.1 drops an empty-string argument to a
  native program, so `script.py "" "a,b"` arrives as `script.py "a,b"`.
  The formatting helper then formatted two upstream files whole; they were
  restored from HEAD and the edits reapplied. The helper now takes `-` for
  an empty list; never pass `""` positionally from PowerShell.

## 2026-10-09 ingest: Popup Palette and On-Canvas Brush Editor removed

- `popup-palette-removal.md` (new): what was removed and what was kept.
  Lesson: Krita's right-click "Show Popup Widget" input action is shared
  with the tools' context menus (`KoToolBase::popupActionsMenu()`); remove
  the popup widget, never the action or its profile entries. A removed
  plugin's DLL stays in the test installation until deleted by hand.

## 2026-10-09 ingest: Tool Options brush section without headings

- `tool-options-brush.md`: Enable Pen Settings sits beside its strength bar
  when both are shown; the "Brush" heading and the category headings are
  gone (they took too much room), replaced by thin lines; nothing folds.
  Lesson: a setting a test changes (kritarc) persists between test runs, so
  a failed run leaks state into the next; reset such settings in `init()`.

## 2026-10-09 ingest: brush stroke layer plan, stage 1

- `brush-stroke-layer-plan.md` (new): a layer that records brush strokes
  and redraws them at a new resolution. Stage 1 (`KisBrushStrokeReplayTest`)
  shows a recorded stroke replays pixel-identically with a fixed seed and
  redraws sharply at 4x. Lessons: the dab rendering queue needs an update
  job (`KisAsynchronousStrokeUpdateHelper::UpdateData`) before the stroke
  ends, or nothing is drawn; freehand smoothing sends curve jobs, so record
  the jobs, not only points.

## 2026-10-09 ingest: brush stroke layer, stage 2a

- `brush-stroke-layer-plan.md`: `KisBrushStrokeLayer` derives from
  `KisPaintLayer` (not `KisExternalLayer`), so Wash/indirect painting, the GPU
  wash preview and `.kra` saving as a paint layer come for free. Strokes are
  recorded in `FreehandStrokeStrategy` and join the stroke's undo command
  through `KisPainterBasedStrokeStrategy::setAdditionalUndoCommand()`.
  Lesson: a stroke's undo is one parent command built in
  `finishStrokeCallback()`; extra state that must undo with the pixels goes
  in as a child of it, not as a separate undo step.

## 2026-10-09 ingest: brush stroke layer, stage 2b

- `brush-stroke-layer-plan.md`: scaling redraws the recorded strokes through
  `KisRedrawableLayerInterface`, asked by `KisTransformProcessingVisitor`
  before it resamples a paint layer. The layer first redraws its record at
  the current size and compares it with its pixels; any other edit falls back
  to resampling. Lessons: an interface used with `dynamic_cast` across
  libraries needs an out-of-line destructor in the defining library; running
  a private image's stroke and waiting for it inside the main image's
  processing job did not deadlock.

## 2026-10-09 ingest: brush stroke layer, stage 2c

- `brush-stroke-layer-plan.md`: the strokes are saved in a `.kra` as a
  `layers/<layer>.brushstrokes` file (QDataStream) beside the pixels, and
  the layer element stays a `paintlayer` with an extra attribute, so
  original Krita opens it as a paint layer. Lessons: `KisPaintOpPreset::toXML()`
  looks up linked resources, which asserts off the GUI thread, and `.kra`
  saving runs on another thread; a preset loaded with the global resources
  must get a resources snapshot (`cloneWithResourcesSnapshot()`) on the GUI
  thread before it is used in image processing.
- Follow-up (same day): a layer with Color Smudge blur strokes fell back to
  resampling in an RGBA float document. `KisUsageLogger` diagnostics showed
  only fully transparent pixels differed in color (black vs white). Lesson:
  compare redraws by premultiplied color; the color of a transparent pixel is
  not content. Log fallback reasons to `krita.log` so they can be read after
  a manual repro.

## 2026-10-10 ingest: brush stroke layer with the Transform Tool

- `brush-stroke-layer-plan.md`: a Free Transform that only scales and moves
  redraws the strokes once, when the transform is applied
  (`InplaceTransformStrokeStrategy::redrawRecordedStrokes()`; the overlay
  preview strategy redraws instead of its final pixel transform). The redraw
  interface now takes the content before the transform, because the
  in-place stroke keeps it in its device cache while the device shows the
  preview. Lessons: in `finishAction()` the last update may still be
  pending (updates are throttled by a timer), so read `currentTransformArgs`
  inside the job; in tests, force the update with
  `KisAsynchronousStrokeUpdateHelper::UpdateData(true)`, and never call
  `image->waitForDone()` while a stroke is open (it waits forever).
- Tests comparing redraws of smudge strokes must compare premultiplied
  images; the color under alpha 0 varies with the random seed.

## 2026-10-10 ingest: brush stroke layer, exact redraws and brush tips in .kra

- `brush-stroke-layer-plan.md`: the strokes' brush tips and textures are
  saved with the strokes (the recorded preset's resources snapshot, written
  with `saveToDevice()`, loaded through `KisResourceLoaderRegistry`; ABR tips
  cannot be saved). Two causes made live strokes differ from their redraw:
  Pixel Brush dabs rendered on several threads draw random numbers (image
  pipes with `sel0:random`) in thread order, and the brush tool starts the
  dab spacing from the last hover position. Recorded strokes and redraws now
  render dabs sequentially (`setSequentialDabRendering()`), and the start of
  each copy's `KisDistanceInformation` is recorded (format version 2).
- Lessons: a redraw that differs only at the stroke's outline points to dab
  placement (spacing start), not color; to tell a live-only difference from
  a replay one, feed the recorded jobs through a live recording stroke in a
  test and compare both ways; a random-choice pipe brush makes the default
  multithreaded dab rendering nondeterministic, so exact replay needs
  sequential dabs. `FreehandStrokeTest` has four failures that predate this
  work (textured 17, Mix dull, two LOD strokes).

## 2026-10-10 ingest: ABR import plan, phase 1

- `abr-import-plan.md` (new, from the todo backlog): photocraft (MIT or
  Apache-2.0, Rust) reads ABR tips, preset descriptors, patterns and folders;
  Solstice reads only the tips (versions 1, 2, 6), misreads 16-bit tips and
  drops presets and patterns. Phase 1 saves ABR tips with brush stroke layers
  as PNG (`KisAbrBrush` has no file of its own). Lesson: preset brush tips are
  found by md5 and file name (`KisPredefinedBrushFactory`), so any resource
  class with the same signature can stand in for the original. ABR files are
  already storages (`AdobeBrushLibrary`); the Brush Presets bundle filter
  lists only storages with presets, so ABR files appear there once ABR
  presets exist (phase 4).

## 2026-10-10 ingest: ABR import, phase 2

- `abr-import-plan.md`: `KisAbrParser` replaces the old ABR parser (versions
  6-10, 16-bit tips, bounds and size limits, fuzzed in tests) and tips are
  named after the presets that use them, read with `KisAslReader::readFillLayer()`
  (a version-16 descriptor to XML). Lessons: keep a resource's file name
  when renaming what users see, because presets and the resource database
  identify tips by file name and md5; the resource database does not refresh
  the names of unchanged resources of an existing storage, so renamed tips
  show only after the library is imported again.

## 2026-10-10 ingest: ABR import, phase 3

- `abr-import-plan.md`: ABR `patt` patterns become pattern resources of the
  ABR storage (`<identifier>.pat`, as PSD and ASL patterns), read with
  `KisAslReader::readPsdSectionPattern()` one block at a time. Lesson: that
  reader stops at the first pattern it cannot read (CMYK, Lab), so a section
  of length-prefixed blocks is best split by the caller; a storage plugin can
  serve several resource types if its iterator, `resourceItem()` and
  `resource()` agree on the type of each URL.

## 2026-10-10 ingest: brush tip bundle filter

- `brush-preset-grouping.md`: `KisPresetDockerFilters` takes a resource type;
  the Brush Editor's predefined tips get the bundle facet and grouping, and
  ABR libraries count as bundles. The tip list now shares the editor's extra
  width (stretch 3:2). Lesson: a Qt box layout without stretch factors gives
  extra width to the widgets with expanding policies, so a list next to
  expanding spin boxes never grows.

## 2026-10-10 ingest: ABR import, phase 4

- `abr-import-plan.md`: ABR brush presets become Pixel Brush presets of the
  ABR storage. Option keys are written as `.kpp` stores them, because the
  option data classes live in `kritalibpaintop`, which `kritalibbrush` cannot
  link; tests read them back with those classes. Lessons: every curve option
  needs at least one sensor (a constant one is pressure with a flat curve);
  descriptor integers come out unsigned; Krita's texture brightness is
  subtracted and its height modes subtract the mask, so Photoshop's depth
  modes need an inverted pattern; a storage must hand out copies and support
  `loadVersionedResource()` for presets to be reloadable; and it must not set
  an md5 of its own on presets, because the stroke preview only renders when
  a reloaded preset's md5 equals the database's.

## 2026-10-10 ingest: grouped Brush Presets scroll position

- `brush-preset-grouping.md`: selecting a preset relayouts the list, and
  Qt's plain `QListView::doItemsLayout()` sets a scroll range without the
  group headers, clipping the position. The grouped layout saves the scroll
  value first and restores it after its own geometry update. Lesson: a view
  that adds rows to Qt's layout must restore anything Qt clamps in between.

## 2026-10-10 ingest: ABR import, phase 5

- `abr-import-plan.md`: the `phry` section's `hierarchy` token list
  (`Grup` with `Nm  `, `preset`, `groupEnd`) becomes preset tags named
  `<file> / <folder> / ...`; preset tokens map by order to the `desc`
  presets. The format has no public specification and none of the local
  files has folders, so the reader is lenient (class, single child key or
  value names the token; nested lists close themselves; stray ends ignored;
  depth capped at 32). Storage tags are re-added by `synchronizeDb()` on
  every start, so already registered libraries gain new tags without a
  re-import; `KisAbrStorage::tags()` must load the file first.

## 2026-10-10 ingest: ABR import, finishing work

- `abr-import-plan.md`: color dynamics map to the Pixel Brush's Mix and
  HSV options, and the scatter count divides the spacing. Pitfall found on
  the way: a curve option written with `<id>UseSameCurve` true takes the
  `commonCurve` for every sensor (`KisCurveOption::generateSensors()`), so
  per-sensor curves were silently replaced by the identity and "constant"
  options followed pressure; generated options must write it false. Also
  learnt from real brushes: in Krita's and Photoshop's subtract and height
  texture modes the pattern's lightness is taken from the paint, so the
  pattern must not be inverted, and Photoshop inverts the pattern before
  its brightness while Krita inverts after. Verify such mappings against
  real presets (authors' settings reveal the intended direction), not just
  against the documentation of another mode.

## 2026-10-10 ingest: input-to-display re-baseline, GPU brush by default

- `gpu-work-priorities.md`, `paint-trace-baseline-runs.md`: after phases
  4.81-4.92 the GPU brush reaches input to display in about 5 ms against
  18-22 ms on the CPU (one process per mode). The CPU brush's minimum update
  period (10 ms) and the 40 ms first-update wait were the CPU path's main
  delay and now apply only as adaptive periods; the GPU brush is on by
  default (`Solstice/GpuBrush`). Lessons: an opt-in feature's gains never
  reach the user until it is the default, so check what the user's own
  launcher enables before reporting a speedup; strokes whose later stages run
  on the CPU (the masking brush composite, its Wash preview and merge) must
  stay on the CPU brush, or tiles ping-pong between GPU and CPU on every
  update; and never read the environment (`qgetenv` takes a global lock) per
  dab on worker threads: it slowed every path, CPU included, by more than
  half for dense brushes. Decide once per stroke.

## 2026-10-10 ingest: Solstice application icon and splash

- `solstice-visual-branding-todo.md`: the user's icon and splash artwork
  replaced the gray placeholders. Masters live in
  `krita/pics/branding/source/`; generated sizes go to `Next/` (the
  development build's variant) and `default/`, the splash to
  `krita/data/splash/` as an 8-bit copy. Lesson: artwork exported from an
  RGBA float document embeds a linear profile (`sRGB-elle-V2-g10.icc`);
  convert from the embedded profile to sRGB before scaling or compositing,
  or the generated assets come out dark and lose subtle tints. Also: the
  splash text was already top-right (`Qt::AlignRight`); read the layout code
  before describing it.

## 2026-10-10 ingest: document icons and README header

- `solstice-visual-branding-todo.md`: `.kra`/`.krz` icons are drawn from the
  application icon (a page with a folded corner; `.krz` labeled "KRZ").
  README now opens with the splash (480 px wide, centered), a separator and
  a one-line description before the main window screenshot; the `# Solstice`
  heading was dropped because the splash carries the name.
- README introduction rewritten (same day, user request): three columns
  (GPU acceleration, workflows from Clip Studio Paint, Krita refined) in an
  HTML table; links inside the table are HTML anchors because Markdown is not
  rendered in table cells.

## 2026-10-10 ingest: GPU masking brush composite (phase 4.101)

- Masked strokes on RGBA32F use the GPU brush; the masking composite runs on
  the GPU, bit-identical to the CPU in the ten modes without strength
  ([gpu-engine.md](../gpu-engine.md), "Masking brush";
  [history](history/gpu-phases-4.93-.md#gpu-masking-brush-composite-phase-4101)).
- Lesson: for float parity, match the CPU's `compositetype` (float, not
  double) with `precise float`; only divisions go through double.
- Lesson: per-batch fixed costs (a masking composite plus a projection merge)
  make the GPU minimum update period counterproductive when dabs arrive
  slowly from CPU generation; masked strokes keep the adaptive period.
- User manual check OK on 2026-10-10.

## 2026-10-10 ingest: new splash artwork and README screenshot

- The user replaced the splash master (`krita/pics/branding/source/
  solstice-splash.png`, a linear-profile export of a float document). The
  app splash `krita/data/splash/solstice-splash.png` was regenerated the same
  way as before: converted to sRGB, composited over white, 8-bit RGB PNG at
  full size (3146x1920); it is compiled into `krita.dll` through `splash.qrc`.
- The README main image `docs/images/solstice-main-window.webp` is the user's
  new screenshot, used as supplied.

## 2026-10-10 ingest: Explorer thumbnails (Krita Shell Extension)

- Windows packages ship the Krita Shell Extension 1.2.4d (MIT) in `shellex/`
  with per-user registration scripts
  ([windows-shell-thumbnails.md](../windows-shell-thumbnails.md)); the todo
  item moved there.
- Decision: register in HKCU only (no administrator rights, Krita's HKLM
  registration stays intact and wins again after unregistering); thumbnails
  only, no file association or property handler.
- Lesson: `package-complete.py --pre-zip-hook` adds files to the ZIP without
  changing the upstream packager.
- Lesson: registry scripts get a `-TestRoot` so the real write/delete code
  can be tested under a throwaway HKCU key; only `Software\Classes` is
  redirected for the 32-bit view.
- User manual check OK on 2026-10-10.

## 2026-10-10 ingest: version 0.2.0-alpha

- Solstice version raised to `0.2.0-alpha` (MINOR, a milestone) at the
  user's request before a trial build; the Krita compatibility version is
  unchanged ([versioning.md](../versioning.md), "History").

## 2026-10-10 ingest: resized application icon

- The user resized the icon master (`krita/pics/branding/source/
  solstice-icon-1024.png`); the application icons (Next and default, ICO,
  scalable resource) and the `.kra`/`.krz` document icons were regenerated
  the same way as before.
- Pitfall: the document icon tool draws the "KRZ" label with system fonts;
  run it without `QT_QPA_PLATFORM=offscreen`, which has no fonts and draws
  empty boxes.

## 2026-10-10 ingest: Rest Note enable setting, Custom tab color buttons

- Rest Note is off by default (`Solstice/RestNoteEnabled`, Custom tab); the
  plugin registers the docker only when it is on, and a loaded docker stops
  at once when it is turned off ([rest-note.md](../rest-note.md)).
- Lesson: every registered docker is created with each main window, hidden
  or not, so a timer docker keeps running while hidden; gating the factory
  registration is the reliable "off".
- The Custom tab's foreground color buttons use a fixed size policy like the
  Cursor tab's outline color button.
- Pitfall: `wdggeneralsettings.ui` has mixed line endings; match edits with
  the section's own endings.
- User manual check OK on 2026-10-10.

## 2026-10-11 ingest: HueSVC shape button and swatches

- HueSVC chooses its own shape (hue bar + square by default, or square in a
  hue ring) from a header button and no longer follows the Advanced Color
  Selector's `colorSelectorConfiguration`; its swatches match the Wide Gamut
  Color Selector's 24 px toggle ([quick-access.md](../quick-access.md)).
- Lesson: a fresh profile showed a triangle because HueSVC inherited the
  Advanced Color Selector's default; shared configuration keys make one
  docker's defaults leak into another.
- Pitfall: an off-screen `KisVisualColorSelector` used for previews needs a
  color space and a color (`slotSetColorSpace()`, `slotSetColor()`), or it
  renders nothing.
- User manual check OK on 2026-10-11.

## 2026-10-11 ingest: default workspace and first layout

- The user's "Solstice" workspace became `krita/data/workspaces/Default.kws`
  and the embedded `[MainWindow] State`, so a new profile's first layout and
  the Default workspace match ([settings-location.md](../settings-location.md)).
- Lesson: the first-start layout comes from the embedded kritarc, not from
  `Default.kws`; changing only the workspace file leaves new profiles as
  they were.
- User manual check OK on 2026-10-11.

## 2026-10-11 ingest: solstice.exe

- The Windows launchers are `solstice.exe` and `solstice.com`; `krita.dll`
  and the application name `krita` stay ([executable-name.md](../executable-name.md)).
- Lesson: before `KAboutData`, Qt takes the application name from the
  executable, so renaming the launcher needs an explicit
  `setApplicationName("krita")`; the profile paths were already explicit.
- Pitfall: CMake installs do not remove the old `krita.exe`/`krita.com`.
- User manual check OK on 2026-10-11.

## 2026-10-11 ingest: software Vulkan devices refused

- GPU device selection refuses CPU-type devices (software Vulkan, common
  in virtual machines), which would be slower than the CPU paths
  ([gpu-engine.md](../gpu-engine.md), "Configuration and compatibility").
- Decision: integrated and virtual GPUs stay accepted when they meet the
  feature requirements; `KRITA_GPU_ALLOW_SOFTWARE_DEVICE=1` re-enables
  software devices for diagnostics.
- User manual check OK on 2026-10-11.

## 2026-10-11 ingest: GPU textured dabs (phase 4.102)

- RGBA32F circle dabs with a pattern texture in the 14 alpha-only modes are
  generated and textured on the GPU ([gpu-engine.md](../gpu-engine.md),
  "Textured dabs"; [history](history/gpu-phases-4.93-.md#gpu-textured-dabs-phase-4102)).
  300px textured strokes: CPU 54 ms, GPU brush 57 -> 15 ms.
- Lesson: keep the CPU fallback on the real CPU code (an applier object)
  instead of a second C++ copy of the math; test the shader copy per mode.
- Pitfall: adding unused code to an existing exact shader variant changed
  its results (RGBA16F by one half ULP, the Soft circle no longer
  bit-exact); new features get their own shader variants.
- User manual check OK on 2026-10-11.

## 2026-10-11 ingest: README benchmark refresh, masked update period

- README benchmarks measured again with the 2026-10-07 method plus two
  textured stroke rows ([benchmarks](benchmarks/transform-and-filter-costs.md#readme-refresh-2026-10-11));
  the stale "no GPU advantage at 64px" sentence and the "dabs, textures and
  masking on the CPU" status were corrected.
- Decision: masked strokes keep the adaptive update period; after phase
  4.102 the GPU minimum period made no measurable difference
  ([gpu-engine.md](../gpu-engine.md), "Masking brush").
- User check OK on 2026-10-11.

## 2026-10-11 ingest: GPU image tip dabs (phase 4.103)

- RGBA32F dabs of predefined image tips (GBR, PNG, SVG, ABR) in alpha-mask
  mode are generated on the GPU, also textured
  ([gpu-engine.md](../gpu-engine.md), "Image tip dabs";
  [history](history/gpu-phases-4.93-.md#gpu-image-tip-dabs-phase-4103)).
- Decision (user, "A2"): `KisQImagePyramid::createImage()` no longer uses
  QPainter, whose output depends on the CPU; the shared integer code is at
  most one 8-bit step from upstream and about twice as slow on the CPU
  ([parity](concepts/cpu-gpu-bit-parity.md), technique 8).
- 160px rotated Wash with mirrors: CPU 26 ms, GPU canvas only 32, GPU
  brush 22; at 48px no gain.
- User manual check OK on 2026-10-11.

## 2026-10-11 ingest: faster CPU image tip transform

- `KisImageTipSampler::render()` gained AVX2 kernels and a gray-level path
  (same bytes as the SSE2 and three-channel paths, tested on every case);
  the CPU image tip transform went from about 2x to about 1.3x QPainter's
  time ([gpu-engine.md](../gpu-engine.md), "Image tip dabs", CPU speed).
- Pitfall: an interpolated opaque tip has alpha 0xfffe, so the unpremultiply
  runs for nearly every pixel; measure by disabling pieces before
  optimizing (the gathers and the factor table were not the cost).
- User manual check OK on 2026-10-11.

## 2026-10-11 ingest: masking coefficient after a canvas resize

- Bug fix (user report): after a canvas resize, the first Brush Editor edit
  rewrote `MaskingBrush/MasterSizeCoeff` for the old size, so masked strokes
  became thin and stayed thin
  ([shared-model development history](history/brush-option-shared-model-development.md),
  phase 5 fix).
- Lesson: an option whose value derives from another (`addDependency()`)
  must be re-read when the other changes outside the editor, not only
  rewritten when it changes inside it.
- User manual check OK on 2026-10-11.

## 2026-10-11 ingest: GPU colour, Lightness and Gradient image tips (phase 4.104)

- Image stamp, Lightness map and Gradient map tips are generated on the GPU
  from 256-entry tables made with the CPU code
  ([gpu-engine.md](../gpu-engine.md), "Image tip dabs", Applications;
  [history](history/gpu-phases-4.93-.md#gpu-colour-lightness-and-gradient-image-tips-phase-4104)).
  120px textured colour stamp: CPU 26 ms, GPU brush 10 ms.
- Lesson: when an output depends on a few 8-bit inputs, a table built by
  calling the CPU code is exact by construction and avoids copying color
  math (HSL, LCMS) into a shader; check, do not assume, that the inputs
  are independent (the stamp's per-channel conversion).
- Decision: the gradient branch's transparent pixels take their own color
  (invisible upstream change, needed for a per-pixel description).
- User manual check OK on 2026-10-11.

## 2026-10-11 query: masking brush mask on the GPU (deferred)

- Measured before building: skipping the CPU mask composite entirely would
  take a 300px masked stroke on the GPU brush from 32 to 24 ms and change
  nothing at 150px; GPU mask dabs would save at most about 5 ms more. The
  work needs a GrayA8 GPU tile format and an integer Alpha Darken (exact is
  possible: the U8 op is integer arithmetic). Deferred by the user
  ([gpu-work-priorities.md](../gpu-work-priorities.md)).
- Lesson: measure an upper bound (temporarily skip the work) before
  building; summed CPU time of parallel jobs overstated the wall-clock gain.

## 2026-10-11 ingest: RGBA16F image tips and textures (phase 4.105)

- RGBA16F image tips and textured dabs are generated on the GPU
  ([gpu-engine.md](../gpu-engine.md), "RGBA16F image tips and textures";
  [history](history/gpu-phases-4.93-.md#rgba16f-image-tips-and-textures-phase-4105)).
- Pitfall: for `half`, read the C++ types of every expression: Imath's half
  has no arithmetic operators (float), KoColorSpaceMaths uses double, and
  each half result rounds double -> float -> half.
- Lesson: when the stroke tests have a tolerance (RGBA16F 0.002), add an
  exhaustive exact test of the math.
- User manual check OK on 2026-10-11.

## 2026-10-12 ingest: RGBA16F masking brush composite (phase 4.106)

- Masked RGBA16F strokes use the GPU brush; the masking composite repeats
  `KisMaskingBrushCompositeOp<half>` bit for bit
  ([gpu-engine.md](../gpu-engine.md), "Masking brush";
  [history](history/gpu-phases-4.93-.md#rgba16f-masking-brush-composite-phase-4106)).
- RGBA16F masked 300px textured stroke: CPU brush (the only path before)
  129.5 ms, GPU brush 36.4 ms.
- Lesson: the half arithmetic rules ([gpu-engine.md](../gpu-engine.md),
  "RGBA16F image tips and textures") carry over to any `half` composite.
- User manual check OK on 2026-10-12.

## 2026-10-12 ingest: missing Layers docker thumbnails

- `KisLayerThumbnailCache` keyed its records by `KisNodeWSP`; a destroyed
  node's key compared as `nullptr` and hid other layers' thumbnails (1x1
  placeholder dots). Keyed by address now, with the weak pointer in the
  record; test `KisLayerThumbnailCacheTest`.
- Lesson: [weak pointers as ordered map keys](pitfalls/weak-pointer-map-keys.md).
- Not related to the GPU engine: GPU strokes update thumbnails correctly.
  Thumbnails of GPU-composited groups take longer (52 ms in a probe) and
  switch to the coarse bounds mode, as upstream does for slow layers.

## 2026-10-12 ingest: FreehandStrokeTest repaired

- The four long-standing `FreehandStrokeTest` failures were stale or missing
  references and a missing resource, not regressions
  ([build-format-test.md](pitfalls/build-format-test.md), Tests): textured 17
  and Mix dull references regenerated from the deterministic current
  results, `freehand-lod` references added, the LOD tests seeded and given
  the Krita 3 bundle. 15 full runs pass.
- Open: an intermittent tile-sized loss in the LOD auto-brush stroke (3 of
  92 runs earlier, 0 of 95 later); cause unknown.
- Lesson: before blessing references, check determinism (identical output
  over runs) and look at the image; a 0x0 reference means a missing file.

## 2026-10-12 query: intermittent loss of part of a Wash stroke

- Reproduced in one process (about 1 in 2,500 strokes under load), not
  LOD-specific; localized to the final merge or the following projection
  update; the Solstice scheduling, GPU merge and tile clone changes ruled
  out ([build-format-test.md](pitfalls/build-format-test.md), Tests, Open).
  The root cause is not found; the investigation stopped here.
- Lesson: summed or per-process rates mislead; an in-process repetition
  loop with background CPU load turned a 1-in-30 process failure into a
  measurable rate, and toggling suspects by environment variables in one
  build compared them quickly.

## 2026-10-12 ingest: remaining blend modes (phase 4.107)

- RGBA16F brushes use the GPU in every GPU blend mode; the HSI/HSL/HSV
  families and HSY Tint and increase/decrease modes run on the GPU for
  layers and brushes ([gpu-engine.md](../gpu-engine.md), "Remaining blend
  modes (phase 4.107)";
  [history](history/gpu-phases-4.93-.md#remaining-blend-modes-phase-4107)).
- Lessons ([cpu-gpu-bit-parity.md](concepts/cpu-gpu-bit-parity.md),
  techniques 9-11): the composite type of float is float; the NVIDIA driver
  fuses multiply-adds despite `precise`; per-dab debugging with ULP-scaled
  GPU intermediates finds a 1-ULP drift quickly. Modes already enabled
  without tests had drifted; add a test row whenever a mode is enabled.

## 2026-10-13 ingest: all other blend modes (phase 4.108)

- Nearly every blend mode runs on the GPU
  ([gpu-engine.md](../gpu-engine.md), "All other blend modes (phase 4.108)";
  [history](history/gpu-phases-4.93-.md#all-other-blend-modes-phase-4108)).
- Lessons ([cpu-gpu-bit-parity.md](concepts/cpu-gpu-bit-parity.md),
  techniques 12-14): check `KoColorSpaceMaths` specializations before
  reproducing a helper (`clamp<float>` and `clamp<half>` do nothing);
  GLSL transcendental functions need accurate replacements; compare NaN as
  equal and print inputs and outputs of the worst pixel in failure messages.

## 2026-10-13 ingest: precise contraction audit (phase 4.109)

- The driver keeps `precise` in small shaders but fused it in the real blend
  code ([gpu-engine.md](../gpu-engine.md), "Precise contraction audit
  (phase 4.109)"; [history](history/gpu-phases-4.93-.md#precise-contraction-audit-phase-4109));
  `testBlendProbe` is the reproduction, `roundedProduct()` the remedy, now
  also in the F16 lerps, F16 Alpha Darken and Liquify.
- Lesson ([cpu-gpu-bit-parity.md](concepts/cpu-gpu-bit-parity.md),
  technique 10): a minimal reproduction can hide a context-dependent driver
  behavior; keep the reproduction that calls the real code. Updated the
  phase numbering page ([gpu-phase-numbering.md](decisions/gpu-phase-numbering.md)).

## 2026-10-13 ingest: pre-release review

- Three parallel read-only audits (public docs, interface text, visual
  assets) found stale GPU statements, development-diary text and internal
  links in the user guides, leftover "Krita" text, Krita's bug tracker in
  Help > Report Bug, Krita shortcuts in the ZIP, a gray-icon trap for stable
  builds and old Krita document icons inside the application; fixed and
  recorded in [the branding checklist](../solstice-visual-branding-todo.md),
  "Pre-release review (2026-10-13)", with the open decisions.
- Lesson: user guides written phase by phase accumulate history; rewrite
  them for the reader before a release and archive the history
  ([archive](history/gpu-user-guide-archive.md)). Lesson: a bash heredoc
  with Python string literals turns a backslash followed by r in Windows paths into a carriage
  return; write such scripts to a file.

## 2026-10-13 ingest: pre-release decisions

- The user decided the open review items: the installer and MSIX are
  rebranded (a new Solstice NSIS script that never touches Krita), the
  welcome page loses the upstream support and manual links, saved files keep
  naming Krita, the About data gets a description and one copyright text,
  names stay, and the Vision ML guide is expanded
  ([branding checklist](../solstice-visual-branding-todo.md), "Pre-release
  review (2026-10-13)").
- Lesson: an inherited installer is not just strings; Krita's also removed
  Krita installations and took machine-wide integration, so a rebrand meant
  a smaller script of Solstice's own.

## 2026-10-13 ingest: brush stroke layer selection moves and record check

- A user's layer enlarged blurry: Move Tool selection moves had cut and
  pasted pixels without moving the record, so the redraw check failed and
  the layer was scaled as pixels, noticed only after scaling. Selection
  moves now move the strokes wholly inside the selection (last in the
  order, as the pasted pixels lie on top), and a background check after
  non-brush edits warns with a canvas message and a layer icon badge
  ([plan](history/brush-stroke-layer-development.md), "選択範囲の移動と記録の照合").
- Diagnosis method: render each recorded stroke alone and measure the share
  of its own opaque pixels that differ; whole strokes at 100% point to an
  edit that moved pixels, not to brush nondeterminism.
- Pitfalls: a queued Qt event that owns a KisNodeSP deadlocks at exit (the
  event is destroyed under Qt's posted-event lock, and the node's QObject
  destructor takes it again); and changing a kritaui class layout requires
  reinstalling the plugins, because tests load `_install` plugins built
  against the old size (heap corruption in an unrelated place).
- Follow-up the same day: a layer still enlarged blurry after the warning
  and an undo. Its brush had a texture, and the layer had been moved; the
  texture is anchored to the canvas (`X - OffsetX`), so drawing a moved
  stroke again picked another part of the pattern (small alpha
  differences on every line). Redraws now move the texture offset with
  the stroke. Lesson: a redraw must reproduce every canvas-anchored input
  of a dab (texture, and anything else keyed to image coordinates), not
  only the dab positions.

## 2026-10-13 ingest: brush stroke layer partial redraw

- Scaling a brush stroke layer now draws the strokes again where the layer
  still matches its record and transforms only the mismatching part as
  pixels (grown 2 px, feathered, blended with `COMPOSITE_COPY` through the
  mask); most of the layer mismatching still falls back to pixels. The
  callers pass their own pixel transform so the fallback part looks as it
  did ([plan](history/brush-stroke-layer-development.md), "一致しない部分だけ画素で拡大する").
- Lesson: when a fallback replaces a whole result, ask whether the check
  that triggers it can say where it failed; the mismatch mask was already
  computed and only needed to be kept.

## 2026-10-13 ingest: 0.2.0-beta, main branch, package root launcher

- The version label is `beta` (0.2.0-beta, [versioning](../versioning.md)).
  The GitHub Actions package name and BUILD-INFO carry the version.
- `main` is the primary and default branch, created from `krita-sol-gpu`,
  which is retained and no longer updated (AGENTS.md branch policy).
- The ZIP root has `Solstice.exe`, a small launcher for `bin\solstice.exe`
  ([executable name](../executable-name.md)). Lesson: a helper executable
  placed outside `bin` must not import the toolchain's runtime DLLs
  (llvm-mingw's `libc++.dll`); link it statically and check its imports with
  `llvm-objdump -p`. A stand-in target that records its command line tests a
  launcher without starting the application.

## 2026-10-13 ingest: Windows CI dependency lock refresh

- Every locked KDE registry package had been removed (HTTP 404), and only
  the branch-scoped cache on `krita-sol-gpu` kept builds working. The lock
  is regenerated by `build-tools/github-actions/refresh-dependency-lock.py`
  ([GitHub Actions](../github-actions.md), "Lock refresh"). It is a
  dependency upgrade (Boost 1.90, Eigen 5, exiv2 0.28, FFmpeg 8, Imath
  split out), not just a rebuild.
- Lesson: the KDE registry keeps only recent dependency builds, and Actions
  caches are per branch, so a new default branch, or a cache unused for a
  week, exposes a stale lock at once. Refresh the lock before a release,
  and read the dependency recipe diff between the old and new source
  revisions to know what is really changing.

## 2026-10-13 ingest: welcome page Solstice releases

- The welcome page News is split: Krita news above, the Solstice
  repository's GitHub releases (Atom feed) below, both behind the existing
  opt-in news switch ([welcome news](../welcome-news.md)). KisRssReader
  reads Atom entries now.

## 2026-10-13 ingest: Windows dependency mirror and weekly check

- Permanent fix for the dependency registry removals: a public mirror
  repository, `udtren/solstice-deps`, holds each locked set as release
  assets; the bootstrap prefers it, and a weekly workflow checks the mirror
  and the registry ([GitHub Actions](../github-actions.md), "Dependency
  mirror"). Lesson: a mirror repository's own workflow can upload to its
  own releases with the built-in token, so no cross-repository secret is
  needed.

## 2026-10-14 ingest: 0.2.0-beta release, launch, Python plugins, beta.2

- 0.2.0-beta was published as a GitHub prerelease and the website went live
  at solstice.tidalboxstudio.com (Cloudflare Workers, `develop` merged into
  `main`). The user then found no Python Plugin Manager: the CI build had
  always configured without the Python plugins
  ([GitHub Actions](../github-actions.md), "Python plugins"). 0.2.0-beta.2
  is the corrected build.
- Lesson: an optional feature that configure drops silently must be checked
  in CI like the GPU engine is, or a release can ship without it unnoticed;
  check what the published package contains against a local build, not only
  that the build succeeds.

## 2026-10-14 docs: original plugin repositories

- The user guides of the features ported from Python plugins (Quick Access,
  Lazy Tools, Rest Note, Asset Library, Vision ML) now point to the original
  plugin repositories for details (user request); the guides cover the
  native versions and where they differ.

## 2026-10-14 docs: Krita User Manual and the website

- README, the guide index and the website point to the Krita User Manual
  (`https://docs.krita.org/en/user_manual.html`) for the basics of using
  the application (user request); Solstice's guides cover what it adds.
- README's Project page links the live website instead of calling it
  planned; AGENTS.md's README rule names the website.

## 2026-10-14 release: 0.2.0-beta.2

- GitHub Release `v0.2.0-beta.2` (prerelease, commit `9224a5d118`) was
  published; the user confirmed the Python Plugin Manager in its package
  ([GitHub Actions](../github-actions.md), "Python plugins"). The
  `v0.2.0-beta` notes now say it is superseded, and the merged
  `ci/python-plugins` branch was deleted.

## 2026-10-14 ingest: upstream selective intake

- New decision page [upstream-selective-intake](decisions/upstream-selective-intake.md):
  weekly upstream review by Hermes Agent, user decision, cherry-picks by
  Claude; `AGENTS.md` Branch policy and index, new
  [upstream-intake.md](../upstream-intake.md) (procedure and ledger),
  `development-workflow.md` upstream section marked historical.
- Validation run 2026-W42 on the DGX share: 70 commits, 32 reviewed in 16
  items; series dry runs and an isolated object directory were needed.

## 2026-10-14 intake: upstream 2026-W42

- Adopted items U-2026W42-01 to 06 (nine upstream commits: thumbnail cache
  assert, animation curves scrolling, AssistantsToolOptions.ui class name,
  perspective transform conditions, second-wheel horizontal scrolling, PSD
  group collapse and layer locks); every other W42 candidate rejected by
  the user. Ledger in [upstream-intake.md](../upstream-intake.md).
- Applying from the shallow share mirror: `git fetch` would make this
  repository shallow, so commits are applied with the share's
  `scripts/intake.sh` (`git am -3 --keep-non-patch`; plain `git am` drops
  `[psd]`); recorded in
  [upstream-selective-intake](decisions/upstream-selective-intake.md).
- `kis_psd_test` loads the PSD import/export plugins from the install tree:
  the new round-trip tests failed until `libs/psd` and `plugins/impex/psd`
  were installed.

## 2026-10-14 release: 0.2.0-beta.3

- GitHub Release `v0.2.0-beta.3` (prerelease, commit `c89bc02475`, Actions
  run 38503291774): the 2026-W42 upstream intake. Package checked: checksums,
  5,300 ZIP entries intact, Python plugins and PSD plugins present.
- Website `solstice/src/site.ts` updated and `develop` merged into `main`;
  the live Download page showed `0.2.0-beta.3` a few minutes later. New rule
  (user decision): every release is followed by the website update
  ([versioning](../versioning.md), "Releasing").
- `SHA256SUMS.txt` has CRLF line endings: check it with
  `tr -d '\r' < SHA256SUMS.txt | sha256sum -c -`.

## 2026-10-14 docs: other GPUs are untested, not CPU-only

- The README, the GPU engine guide and the website had implied that other
  GPUs are rejected and Solstice falls back to the CPU. The code only
  requires Vulkan 1.3 features (`KisGpuContext.cpp`); `isBlackwell()` just
  logs a warning. The wording is now "the only tested GPU; other GPUs are
  untested" (user decision; no call for reports). Check the device selection
  code before describing hardware support.

## 2026-10-14 fix: Python bindings return bool from Node.save

- `Node.sip`, `Filter.sip` and `Scratchpad.sip` declared `void` for
  `Node.save()`, `Filter.apply()` and `Scratchpad.setScale()`, which return
  `bool` in libkis (upstream Krita bug). Python got `None`, and the external
  `krita-spine-export` plugin reported "Could not write PNG" after every
  successful export. A scan of all `.sip` files against `libs/libkis/*.h`
  found no other return-type mismatch. New pitfall
  [sip-return-types](pitfalls/sip-return-types.md): `.sip` edits do not
  trigger regeneration, and kritarunner cannot save files.

## 2026-10-14 fix: crash in enableDirtyRequests during repeated Node.save

- Solstice 0.2.0-beta.3 crashed (access violation reading
  `FFFFFFFFFFFFFFF0` in `KisImage::removeProjectionUpdatesFilter`, from
  `UpdateCommand::partB`) during repeated Python `Node.save()` on an RGBA
  float document. Cause: `KisImage::projectionUpdatesFilters` was
  unsynchronized, and the GUI thread's non-const iteration in
  `refreshGraphAsync()` (from `initialRefreshGraph()` right after the
  asynchronous `cropImage()` in `Node::save()`) detached the never-allocated
  vector while a stroke worker appended the drop-all filter. Not
  GPU-specific. The vector is now guarded by a mutex, and readers iterate
  a snapshot. New test `KisImageProjectionUpdatesFilterTest` (crashed
  before the fix), new pitfall
  [qt6-detach-on-iteration](pitfalls/qt6-detach-on-iteration.md).
- Checked in the dev build: 560 `Node.save()` calls (29 layers, 20 rounds)
  on an F32 document converted on open, 0 failures, no crash.

## 2026-10-15 release: 0.2.0-beta.4

- GitHub Release `v0.2.0-beta.4` (prerelease, commit `3575bbb59c`, Actions
  run 38584970834): the `Node.save()` crash fix and the Python `bool` return
  types. Package checked: checksums, 5,300 ZIP entries intact, Python
  plugins present. Website `site.ts` updated and published the same day.

## 2026-10-15 maintenance and beta.5 preparation

- Recorded the findings and regression coverage from the brush-layer, Quick
  Access, GPU conversion, and document-switch work in
  [maintenance-review-20261015](queries/maintenance-review-20261015.md).
- Prepared version `0.2.0-beta.5` for the user-requested push, Windows build,
  release distribution and corresponding website update. Build/publication
  outcomes will be recorded after verification.

## 2026-10-15 fix: Puppet Warp audit items (manually checked)

- Fixed the read-only audit's Puppet Warp items (stale Fast preview after
  Order, unlock keeping rotations, uncommitted click deletions, pixels deleted
  by the solid-triangle cell filter, damaged mesh strings, Accurate preview of
  transform masks, rotation wrap, and smaller items); details in
  [puppet-warp.md](../puppet-warp.md).
- New pitfall [transform-tool-input](pitfalls/transform-tool-input.md).

## 2026-10-15 fix: brush stroke layer audit items (manually checked)

- Fixed the read-only audit's brush stroke layer items: Multibrush records
  rejected on load (limit 64, now 4096), canvas mirror recorded (format
  version 3), Instant Preview selection moves, cancelled continued
  transforms, the record check skipping unrecorded edits (recorded-change
  scopes), LOD state leaking into the check, the Gradient tool, stalled
  checks of removed layers, and smaller items; details in
  [brush-stroke-layer-plan.md](history/brush-stroke-layer-development.md) ("監査の修正").

## 2026-10-15 release: 0.2.0-beta.5

- GitHub Release `v0.2.0-beta.5` (prerelease, commit `dd9e48c152`, Actions
  run 38669973004): RGBA16F conversion, per-canvas tool state, Quick Access
  hold ownership, and the Brush Stroke Layer and Puppet Warp audit fixes.
  Package checked: checksums, 5,300 ZIP entries intact, Python plugins
  present. Website `site.ts` updated and published the same day.

## 2026-10-16 release: 0.2.0-beta.6

- GitHub Release `v0.2.0-beta.6` (prerelease, commit `b7ee343e81fe`, Actions
  run 38833086246): Effects & Color, Color Reference with image drag and
  drop, Layer Comps and the CPU G'MIC runtime. Relevant local effects,
  composition, Lens Blur and G'MIC tests passed, including the real G'MIC
  runtime integration checks.
- The first cloud G'MIC build and package succeeded. Both ZIP checksums and
  CRCs passed; the application has 5,313 entries, including Python plugins,
  the new native plugins and the exact G'MIC source/patches/data. The packaged
  version command passed with a scratch profile. Details and SHA-256 are in
  [GitHub Actions](../github-actions.md#effects-layer-comps-and-gmic-package-check-2026-10-16).
- Recorded the user's documentation policy in
  [release-feature-documentation](decisions/release-feature-documentation.md):
  keep README's latest-release summary short, use `In development` before
  publication, and retain stable feature guides under `docs/`.
- Website published the same day: `tidalboxstudio-web` develop `792e60c`,
  production merge `a3c7383`. Cloudflare's production check succeeded.
  Verified beta.6 on the live home/download pages, the GitHub Release link,
  both new feature pages with their images, and their sitemap entries in a
  normal browser. README and the guide index now identify the published beta.

## 2026-10-16 tooling: translation catalogs (Simplified Chinese)

- Recorded the user's translation policy in
  [translation-policy](decisions/translation-policy.md) and
  `docs/agent/translations.md`.
- Added `dev-tools/translations/update_translations.py` (Windows replacement
  for `Messages.sh`) and merged `po/zh_CN/krita.po`: 9,664 translated (all
  previous translations unchanged), 577 fuzzy, 179 untranslated, 158
  obsolete. `msgfmt --check` and `ninja pofiles` passed; not installed.
- Pitfalls in [translation-extraction](pitfalls/translation-extraction.md).

## 2026-10-16 fix: strings outside the translation catalog (F1-F5)

- Audited Solstice C++ code for text that bypasses `i18n()`; results and the
  F1-F5 list in `docs/agent/translations.md` ("Strings outside the catalog").
  No Qt `tr()`; the standard action texts are covered by `.action` files.
- The Krita-profile import texts in `krita/main.cc` and the
  `KisSolsticeProfile` errors are built in for English, Japanese and
  Simplified Chinese (`builtInText()`, `textLanguageFor()`, tested in
  `KisSolsticeProfileTest`); Quick Access and Vision ML error details use
  `i18n()`. Catalog re-merged: 12 new strings, no translation changed.
- Built, `KisSolsticeProfileTest` and `QuickAccessCoreTest` passed;
  kritaglobal, krita, Quick Access and Vision ML installed. The dialogs were
  not checked interactively.
- Updated [translation-extraction](pitfalls/translation-extraction.md).

## 2026-10-16 translation: zh_CN batches 1-2 (shared strings, Quick Access)

- Translated 89 Krita -> Solstice renames (old translation with the same
  rename), 173 shared strings and 220 Quick Access strings in
  `po/zh_CN/krita.po`: 10,146 translated, 183 fuzzy, 103 untranslated; no
  previous translation changed. `msgfmt --check` and `ninja pofiles`
  passed; `krita.mo` installed. Not checked in the interface yet.
- Terminology choices (配置方案, 文字标签, 标签页, English feature and docker
  names) are in `docs/agent/translations.md` ("Rules for translating").
- Left: Solstice filters, Rest Note, Asset Library, Vision ML, GPU, Layer
  Comps, Transform Tool (286 strings).

## 2026-10-16 translation: zh_CN batches 3-9 (filters, Rest Note, Asset Library, Vision ML, GPU, Layer Comps, Transform Tool)

- Translated the Solstice filters (127), Rest Note (38), Asset Library (34)
  and Vision ML (34) strings: catalog 10,379 translated, 30 fuzzy, 23
  untranslated; no previous translation changed. `msgfmt --check` and
  `ninja pofiles` passed; `krita.mo` installed. Not checked in the
  interface yet.
- Filter names Solstice added stay in English (including Background
  Removal); the list is in `docs/agent/translations.md`.
- Then batches 7-9: GPU settings and messages (24), Layer Comps (15),
  Transform Tool / Puppet Warp (14). The `zh_CN` catalog is complete:
  10,432 translated, 0 fuzzy, 0 untranslated; `msgfmt --check` and
  `ninja pofiles` passed; `krita.mo` installed.

## 2026-10-16 docs: interface languages (A5)

- Added hand-off checklist step 7 (update and translate the catalog) to
  `development-workflow.md`, the user guide `docs/languages.md` (guide
  index), a README "In development" entry and a "Supported environment"
  language line, and the translation touch points in `feature-inventory.md`.
- At the next release: move the README entry into the release summary and
  mention the machine-translated Simplified Chinese interface in the GitHub
  Release notes.

## 2026-10-16 translation: Layer Comps terminology in zh_CN

- Changed the 14 inherited "composition" entries of the Layer Comps docker
  (context menu, export, `krita.action`, the export warning) from 图层显示方案
  to Layer Comp, matching the docker's newer strings. Catalog still 10,432
  translated; `krita.mo` rebuilt and installed.

## 2026-10-16 release: 0.2.0-beta.7

- Released GitHub Release `v0.2.0-beta.7` (prerelease, commit `53685b8aae`,
  run 38851516022): the complete Simplified Chinese interface. Package
  checks in [GitHub Actions](../github-actions.md#simplified-chinese-package-check-2026-10-16);
  the packaged `zh_CN` catalog is identical to the checked one.
- README now summarizes beta.7; the guide index marks Interface languages as
  added in 0.2.0-beta.7.
- Website: `site.ts` points to beta.7 and the Simplified Chinese pages are
  published with it (see the website repository's `docs/solstice-site.md`).

## 2026-10-16 maintenance and feature-branch policy

- Consolidated the user-verified native-tool refactors: Asset Library shared
  services; Quick Access settings/profiles/item editing, Grid Edit, Quick Adjust,
  palette and gesture responsibilities; Rest Note, Lazy Tools and Brush Stroke
  Layer. Feature documents record build, install, regression and manual results.
- Captured [native refactor boundaries](concepts/native-refactor-boundaries.md):
  preserve compatibility adapters and QObject/deferred-callback ownership.
- Recorded the user's [feature-branch policy](decisions/feature-branch-workflow.md):
  push a feature branch and merge into main through a PR, including releases and
  documentation. Preparing 0.2.0-beta.8 through this workflow.

## 2026-10-16 release: 0.2.0-beta.8

- Published [v0.2.0-beta.8](https://github.com/udtren/Solstice/releases/tag/v0.2.0-beta.8)
  from PR #3's merge commit `4e92a41ee78c`; CI run 38872500400 passed.
  Package and local regression evidence is recorded in
  [GitHub Actions](../github-actions.md#native-tools-maintenance-package-check-2026-10-16).
- README and the guide index now link to the published maintenance release.
  Post-release documentation also uses a feature-branch PR, preserving the
  [branch policy](decisions/feature-branch-workflow.md).
- The release tag stays at the tested build commit; subsequent documentation
  commits do not replace or move it. Website publication follows the release
  using `develop` and a pull request into the website's `main`.

## 2026-10-16 documentation ownership and role cleanup

- Recorded [repository-owned documentation](decisions/documentation-source-of-truth.md):
  originals stay with the code. Added the [maintenance rules](../documentation.md)
  and [agent guide](../README.md), distinguishing current references, mixed
  plan/references, superseded proposals and history.
- Replaced AGENTS' duplicated feature progress with stable links. Corrected
  active configuration/resource guidance, translation-catalog ownership,
  menu-editing guidance, the process check and inherited upstream-merge advice;
  aligned both README download links with beta.8.
- Preserved the expired GPU handoff and accumulated per-phase source inventory
  as indexed archives. Extracted the Brush Stroke Preview maintenance contract
  from its original design, corrected its cache path against
  `KisBrushStrokePreviewCache.cpp`, and retained dated verification evidence.
- Validation: local document/image targets and heading references checked;
  wiki metadata and index coverage checked; all three archived bodies compared
  against the source revision (only relocation links adjusted). `git diff
  --check` passed. Documentation only: no new native build or interactive
  verification is claimed. No site generator or publication was introduced.

## 2026-10-16 documentation: Brush Stroke Layer reference and history

- Extracted the [maintenance reference](../brush-stroke-layer.md) after checking
  recording, replay, transforms, serialization and monitor lifetime against the
  current code. Preserved the complete 767-line original in
  [development history](history/brush-stroke-layer-development.md); the old plan
  path remains a pointer. Indexes and current source/document references now
  use the maintained owner; historical log citations reach the archive.
- Updated [the documentation decision](decisions/documentation-source-of-truth.md)
  with the extraction lesson: later corrections and implementation take precedence
  over early proposals, while pending/manual verification retains its original
  scope. Corrected obsolete brush-tip/ABR embedding comments in the source.
- Validation: archived body exactly matches the source revision; local links,
  heading targets and wiki metadata/index coverage pass. Changed C++ files
  differ only in comments, with changed-line clang-format checks passing;
  `git diff --check` passes. No native behavior, format or user workflow changed,
  and no new native build, installation or interactive verification is claimed.

## 2026-10-16 documentation: ABR import reference and history

- Extracted [ABR import](../abr-import.md) from the mixed plan after checking
  parser, collection, converter and storage behavior. Preserved the full
  286-line original in [development history](history/abr-import-development.md),
  adjusting only two relocation links; the old plan path remains a pointer.
- Current references now own final texture/color/scatter mapping, resource
  identity and reload, folder tags, test scope and pending real-file checks.
  Aligned the user guide with the implemented 16-bit precision, naming,
  computed-only-file limitation and non-exhaustive diagnostics. No behavior
  changed. Source comments now link to the current reference and accurately
  scope the parser's limits.
- Extended [the documentation decision](decisions/documentation-source-of-truth.md):
  separate version support from content coverage, approximations from omissions,
  and synthetic tests from real-file verification.
- Validation: original archive body matches after the two link relocations;
  local links/heading references and wiki metadata/index coverage pass. The
  seven changed C++ files differ only in comments; changed-line clang-format
  and `git diff --check` pass. No native build, installation or interactive
  verification was run for this documentation-only extraction.

## 2026-10-16 documentation: Brush option shared model reference and history

- Extracted [the maintained reference](../brush-option-shared-model.md) after
  comparing the complete plan and phase 0 investigation with current code.
- Preserved both original bodies in history and retained their old paths as
  pointers; updated indexes, source comments and stale Tool Options checks.
- Lesson: derive current contracts from final implementation and later fixes,
  including dependent reads after external changes, not only the original
  write design. See [the documentation decision](decisions/documentation-source-of-truth.md).
- Validation: both archived bodies match their source revision (935 and 370
  lines); 112 Markdown files, 635 local references and 153 heading references
  pass checks, as do wiki metadata/index coverage and `git diff --check`.
  All 46 changed C++ files and 12 CMake files differ only in comments;
  changed-line clang-format checks pass. Historical build/manual results remain
  dated evidence; no new native or interactive verification is claimed.

## 2026-10-16 documentation: Solstice interface reference and history

- Extracted [the interface maintenance reference](../ui-modernization.md)
  and preserved the complete original plan in
  [development history](history/ui-modernization-development.md), adjusting
  only relative links. The old path remains a pointer.
- Resolved default-off proposals against embedded defaults and current read
  fallbacks; distinguished explicit imported settings from absent keys,
  style geometry from window layout, and current behavior from unverified
  display scales. Corrected the user guide's radio-button description.
- Extended [the documentation decision](decisions/documentation-source-of-truth.md)
  with the lesson to trace defaults through both initialization and read paths.
- Validation: archival body preservation, local links/headings, wiki metadata
  and index coverage, comment-only C++ changes, changed-line clang-format
  and `git diff --check`. No native build, installation or new interactive
  verification is claimed.

## 2026-10-16 documentation: settings/profile reference and history

- Rewrote [Settings location](../settings-location.md) as the maintained
  reference at its existing path and preserved the original investigation,
  proposals and phase results in
  [development history](history/settings-location-development.md).
- Checked startup ordering, KConfig default mounting, import/cleanup callers,
  shared custom resources, different-drive fallback and reset boundaries.
  Updated the user guide and obsolete phase references accordingly.
- Recorded the lesson in [the documentation decision](decisions/documentation-source-of-truth.md):
  broad migration guarantees need the actual caller and fallback behavior.
- Validated all 629 original lines, 715 local references (including 156
  heading references) across 115 Markdown files, wiki metadata/index coverage,
  comment-only C++/CMake edits, changed-line clang-format and `git diff --check`.
  No runtime changes or fresh native/interactive verification are claimed.

## 2026-10-16 documentation: CPU effects and G'MIC reference split

- Extracted [native CPU effects](../cpu-effects.md) and [G'MIC integration](../gmic.md)
  as separate maintenance owners. Preserved the complete 1,296-line plan,
  phase results and timing tables in [development history](history/cpu-effects-and-gmic-development.md);
  the original plan path remains a pointer.
- Retained persisted IDs/settings, pixel/alpha/halo and transaction contracts,
  native regression checks, runtime ABI/ownership, source pinning and packaging.
  Updated feature indexes, build notes and user-guide links.
- Reconciled the old cloud-unverified wording with the later beta.6 build and
  package record; rechecked run 38833086246 as successful at its recorded commit.
  Cloud GUI/runtime tests remain disabled. Clarified G'MIC's visible/all-layer
  scope and profile paths, including independence from custom Resource Folder.
- Recorded the lesson in [the documentation decision](decisions/documentation-source-of-truth.md):
  component boundaries and later release evidence matter more than plan layout.
- Validation: exact archival-body preservation, Markdown local links/headings,
  wiki metadata/index coverage and `git diff --check`. Changes are documentation
  only; no fresh native build, installation or interactive verification is claimed.

## 2026-10-16 documentation: illustration filters reference and history

- Extracted [Illustration filters](../illustration-filters.md) as the current
  owner for the five native filters, shared scalar CPU helpers, configuration
  compatibility, alpha/LOD contracts, UI lifetime and regression checks.
- Preserved all 1,095 original lines in [development history](history/illustration-filters-development.md),
  adjusting only relocated links. The old plan path remains a pointer.
  Repaired user-guide, inventory and cross-feature references; removed the
  now-empty list of mixed references from the agent guide.
- Kept Line Art's interactive/real-artwork verification pending and Drop
  Shadow's optional on-canvas handle deferred. Earlier Glow LOD and blur
  proposals do not override full-resolution processing or the large-radius
  three-box path.
- Added the extraction lesson to [the documentation decision](decisions/documentation-source-of-truth.md):
  numerical halos and per-feature alpha/UI boundaries must survive summaries.
- Validation: archival-body comparison, local Markdown links and headings,
  wiki metadata/index coverage and `git diff --check`. Documentation only;
  no native build, installation or new interactive verification is claimed.

## 2026-10-16 documentation: cross-document consistency audit

- Audited current documentation navigation and searched the user/agent guides
  for obsolete branch/profile paths, release/version claims, build limitations,
  pending checks and expired handoff wording. Compared identified discrepancies
  with maintained references, release records and the relevant source paths.
- Corrected [trial-build instructions](../../test-builds.md): Python support
  is included, artifact names carry a run number, and published downloads are
  separate from expiring Actions artifacts. Checked the workflow and build
  script against the existing package verification records.
- Reconciled [Color Reference](../color-reference.md)'s synchronous processing
  description with its widget-bound deferred import callback; replaced the
  expired commit/ingest handoff with release provenance. Interactive acceptance
  remains pending. The source still uses `QTimer::singleShot(0, this, ...)`.
- Scoped [Qt observations](concepts/qt-numeric-and-geometry.md) to the recorded
  local version; corrected wiki index descriptions and removed the duplicate
  current-release number from [Versions](../../versioning.md).
- Added the review lesson to [the documentation decision](decisions/documentation-source-of-truth.md)
  and the [maintenance checklist](../documentation.md). Historical bodies and
  dated test/benchmark results are unchanged. This pass is not an exhaustive
  source-level audit of every feature or new native/interactive verification.
- Validation: 121 Markdown files, 833 local references (including 167 heading
  references), wiki metadata/index coverage and `git diff --check` passed.
  All files under `wiki/history/` remain unchanged.

## 2026-10-16 documentation: repository checker and PR workflow

- Added the [repository checker](../../../dev-tools/docs/check_docs.py),
  pinned parser dependencies and regression fixtures; local usage, scope and
  limitations are in [Documentation maintenance](../documentation.md#automated-checks).
- Added a lightweight [PR workflow](../../../.github/workflows/docs-check.yml)
  for every PR targeting main, plus manual dispatch. It checks links, images,
  heading anchors, wiki schema/index coverage and changed whitespace without
  building or launching the application.
- Fixed seven metadata errors exposed by YAML parsing and related-page
  resolution: three source strings need quoting and four related paths need
  page-relative resolution. Historical bodies and technical claims are unchanged.
- The regression fixtures exercise malformed YAML, missing links/anchors/images,
  case mismatches, missing index entries, duplicate/Unicode headings, escaped
  paths, reference links, code examples and committed whitespace failures.
- Local validation: 15 regression tests passed; 121 Markdown files and 870
  local references (173 heading references) passed. Workflow YAML parsed and
  `git diff --check` passed. The PR template now names main as its target.

## 2026-10-16 documentation: GPU maintenance and work-order separation

- Extracted the current [GPU maintenance contract](../gpu-engine.md) from the
  mixed architecture/phase diary. Corrected obsolete opt-in, float-depth,
  channel-mask, scratch-projection and shared-image canvas claims against the
  committed implementation. Retained settings, tile ownership, synchronization,
  failure/save boundaries, test targets and manual checks.
- Replaced [GPU priorities](../gpu-work-priorities.md)'s accumulated diary with
  delivered scope, unfinished candidates and explicit user deferrals. The
  post-4.100 CPU latency remains unmeasured; failure/save UI acceptance remains
  pending. No new implementation or manual acceptance is implied.
- Preserved both complete originals in frozen snapshots:
  [engine development](history/gpu-engine-development.md) and
  [work-order development](history/gpu-work-priorities-development.md).
  Only actual Markdown link destinations were relocated. Existing phase history
  bodies remain unchanged.
- Updated navigation, the documentation lesson and phase-number allocation:
  check current records and concurrent reservations instead of copying a
  supposedly next-free number into a second document.
- Scope is main at `cc2445cc4a521d58ae0904079ee67354667b3eb6` (through 4.109);
  unmerged work in other checkouts is not imported. Source checks covered gates,
  projection, canvas transfer, dab layout, update scheduling and worker fallbacks.
  This is documentation-only, with no native build or installation.
- Validation: the repository checker passed for 123 Markdown files and 1024
  local references (281 heading references); staged whitespace checks passed.
  Compared both archive bodies with their base-commit originals after link
  relocation and confirmed the pre-existing history files were unchanged.

## 2026-10-16 documentation: GitHub Actions operations and history

- Separated [current build operations](../github-actions.md) from the complete
  [trial/package history](history/github-actions-development.md), preserving
  the original body with relocated Markdown links.
- Current guidance retains manual dispatch, pinned dependency/cache rules,
  mirror refresh, Python/GPU/G'MIC requirements and package verification.
  It distinguishes PR documentation checks, Windows compilation/packaging,
  local regression coverage and interactive acceptance.
- Updated release-evidence links and retained the three historical check
  anchors used by older log entries. No workflow or build behavior changed.

## 2026-10-16 Clone Layer measurements and GPU copy (phase 4.110)

- Added [Clone Layer updates](benchmarks/clone-layer-updates.md): a source
  stroke costs a clone a second walker; offset clones downloaded the GPU
  brush's source tiles; masks and nesting measured.
- Phase 4.110: offset clones without masks copy on the GPU
  (`docs/agent/clone-layer.md`); the next phase number is 4.111.
- Recorded the partial-tile projection clear in GPU merges as an open
  candidate.
- Manual check found a Move Tool "Internal Error" when editing the X/Y
  boxes: an upstream bug (`commitChanges()` without a stroke), visible
  because Solstice shows safe asserts that release Krita hides. Fixed in
  `kis_tool_move.cc` (feature inventory, "Undocumented custom changes").

## 2026-10-16 Partial-tile projection clears on the GPU (phase 4.111)

- Phase 4.111: `KisAsyncMerger::setupProjection()` hands unaligned clears of
  GPU-resident projections to `KisGpuMergeBatch::deferClear()`; the first
  GPU composite fills the rect with the default pixel
  (`docs/agent/gpu-engine.md`). The next phase number is 4.112.
- [Clone Layer updates](benchmarks/clone-layer-updates.md): new "after
  phase 4.111" tables; unaligned dabs without masks download nothing and
  take about 0.2-0.4 ms per dab.
- Recorded in the history page: an adjustment layer as the first leaf reads
  the projection on the CPU right after `setupProjection()` (the merger's
  flush for it comes before), so that case keeps the CPU clear.
- `clone-layer.md` remaining candidate 1 marked done.

## 2026-10-16 duplicated layer thumbnails

- Measured why a duplicated layer's Layers docker thumbnail appeared late:
  the node juggler keeps its stroke open for about 4.5 s and idle tasks wait
  for it; generating the thumbnail takes about 2 ms. Recorded as
  [idle tasks wait for every open stroke](pitfalls/idle-tasks-wait-for-open-strokes.md).
- `KisLayerThumbnailCache` now draws a provisional thumbnail for added nodes
  on a worker thread (about 30 ms after a duplicate); the idle task still
  regenerates it. Test `testDuplicateGetsThumbnailWhileJugglerStrokeIsOpen`;
  opt-in `benchmarkDuplicateThumbnail` (`KRITA_THUMBNAIL_BENCHMARK=1`).

## 2026-10-16 release: 0.3.0 preparation

- Integrated PRs #5, #7 and #8 in dependency order, preserving current
  documentation ownership and both sides of appended history. Each updated
  PR passed the documentation workflow; native changes retained their existing
  regression evidence. PR #8's real-app acceptance is still unrecorded.
- Set the Solstice version to 0.3.0 with an empty prerelease label and build
  digit 0, at the user's request. The Krita compatibility version is unchanged.
- Updated the [version policy](../versioning.md) and user explanation: a
  regular release may have a 0.x version. README keeps 0.3.0 marked in development
  until the verified package is actually published.
- Build/package validation and release publication are pending; this entry
  does not claim new interactive verification.

## 2026-10-17 implementation: Reference Image Filters

- Added ordered native filter stacks, source preservation, KRA/KRF metadata and
  undoable editing. Preview overrides remain outside serialized state because
  autosave can run inside a modal event loop.
- Recorded [cache ownership](concepts/reference-image-filter-cache.md).
- Native builds and the dedicated regression test pass; translations and
  documentation validation pass. Interactive acceptance remains pending.

## 2026-10-17 release: 0.3.0 publication

- Published regular release `v0.3.0` at the exact verified build commit
  `f947bc31421b1b819336c2ad53489179a86999b1`, with all five assets and
  prerelease disabled. GitHub identifies it as latest.
- Recorded build identity, ZIP checks, packaged version smoke check,
  uploaded-asset hash/size comparison and verification limits in
  [0.3.0 release verification](history/release-0.3.0.md).
- Updated README, the user-guide index and version history after publication.
  Kept the Actions snapshot frozen and linked the new evidence separately.
- Existing feature regression/manual records remain distinct from CI package
  verification; #8's real-app acceptance is still unrecorded.

## 2026-10-17 implementation: reusable reference filter templates

- Added profile-wide named stack snapshots for reuse across images/documents.
  Hashed filenames keep labels separate from paths; atomic writes and explicit
  overwrite protect saved templates. Applied images own independent copies.
- Saving a template in the editor is a profile operation, separate from image
  Cancel/Undo. Recorded this boundary in the [cache concept](concepts/reference-image-filter-cache.md).
- Native builds, expanded regression test, documentation checker and translated
  catalog validation passed. Test installation updated; interactive acceptance
  remains pending in the [feature reference](../reference-image-filters.md).

## 2026-10-17 release preparation: 0.4.0

- User accepted the Reference Tool enhancement and requested it as website
  Highlight 09, with an illustrated overview in the repository and website.
- Bumped only the Solstice version to 0.4.0; release packaging/publication
  follows the [version procedure](../versioning.md). No new runtime claims.

## 2026-10-17 release: 0.4.0

- Published the regular latest release at the exact successful CI build commit.
  Recorded ZIP integrity, five uploaded asset digests, isolated version output
  and the distinction from existing feature regression/acceptance evidence in
  [release verification](history/release-0.4.0.md).
- Dependency-lock comparison must account for Windows checkout CRLF versus
  Git-blob LF while still rejecting any other content change.
- Replaced README's previous release summary with Reference Tool 0.4.0;
  the illustrated overview remains the ninth Highlights entry. Website
  publication follows the release, using the repository guides as the source.

## 2026-10-17 README: illustrated Highlights

- Moved Project page to the top, added a Contents list above What's new and
  replaced the Highlights bullet list with two-column image tables (New
  Features, Ported Plugins) plus Other Major Changes; removed Screenshots.
- `docs/images/Solstice_images.png` shows Puppet Warp despite its name; it
  replaces `docs/images/puppet-warp.webp` in `docs/puppet-warp.md`, and the
  `.webp` was removed.
- The effects guide shows each filter on one example portrait
  (`docs/images/20161016/`; `Normal.jpg` is the unfiltered original).

## 2026-10-17 docs: Puppet Warp image name

- Renamed `docs/images/Solstice_images.png` to `docs/images/puppet_warp.png`;
  README and `docs/puppet-warp.md` use the new name.

## 2026-10-19 upstream intake: Solstice-authored commits

- GitHub's Contributors list showed four Krita developers because the
  2026-W42 intake kept the upstream commit authors (`git am`). From now on
  each adopted commit is amended to the local author with an
  `Upstream-Author:` trailer (`docs/agent/upstream-intake.md`, step 4;
  [upstream-selective-intake](decisions/upstream-selective-intake.md)).
- At the user's request `main` was rewritten and force-pushed so that the
  nine W42 commits carry the local author too (`4e849ed12c` to
  `6dec99bbce`, identical trees). Release tags stay on the pre-rewrite
  commits; the old-to-new SHA table is on the decision page, and the
  ledger lists the new SHAs.
- New rule in `AGENTS.md` ("Commit authorship and Contributors"): the
  user `udtren` is the only human contributor; LLM agents may be listed.

## 2026-10-19 README: GPU and CPU coverage table

- The README's GPU engine section now opens with a table of what runs on the
  GPU and what stays CPU only, per area (color spaces, compositing, canvas,
  blend modes, brushes, tips, textures, masking brush, Transform Tool,
  filters, Clone Layers). Sources: `docs/gpu-engine.md`,
  `docs/agent/gpu-engine.md` ("Projection", "Pixel brush integration",
  "Transforms and Gaussian convolution").
- Two CPU-only cases the user guide did not list were added to it: layers
  moved by a distance that is not a multiple of 64 pixels
  (`KisGpuMergeBatch::tryAdd()`), and brush engines other than the pixel
  brush (only `plugins/paintops/defaultpaintops/brush` uses the GPU brush).

## 2026-10-19 Quick Access guide and Layer Comps image

- `docs/quick-access.md` is now a full user guide built from the Python
  plugin's README (`krita-plugin/quick_access_manager/README.md`), with its
  screenshots and GIFs copied to `docs/images/quick_access_manager/`. The
  text was checked against the native sources: menu and dialog labels come
  from `QuickAccessDock.cpp`, `QuickAccessGestureDialog.cpp` and
  `QuickAccessSettingsDialog.cpp`.
- Where the native version differs from the plugin README: the gesture key
  is typed into a field (no "Config Key" button); gesture, temporary-key and
  temporary-brush settings apply on Save without a restart (the key
  controller reads KConfig on every key event); only the HueSVC and Quick
  Brush Adjustments enable switches need a restart (read in
  `QuickAccessDockerPlugin.cpp`); brush rotation exists only in the HueSVC
  popup; the selection button of Quick Brush Adjustments only shows whether
  the selection outline is displayed.
- The README's Quick Access highlight uses `quick_access_manager/1_overview.png`;
  `ported_plugin/quick_access_manager.png` was removed. `docs/images/Layer_Comps.png`
  was replaced by the user. The website copies both images.

## 2026-10-21 feature: Remove Dust

- New Remove Dust Tool and Remove Dust filter (requested from CLIP STUDIO
  PAINT's Remove dust, Fill leftover and Select dust). The detector
  (`libs/image/KisDustDetector`) is shared; the tool lives in the Enclose and
  Fill plugin to reuse its enclosing subtools. Contract, source map and
  manual checks: [Remove Dust](../remove-dust.md); user guide
  `docs/remove-dust.md`.
- Tiling: connected-component analysis stays tile-invariant with a window
  margin of `dustSize + 1` and components that reach the window edge treated
  as too large; recorded as an invariant in the feature document.
- Pitfalls added to [Build, format and test pitfalls](pitfalls/build-format-test.md):
  defaulted smart-pointer parameters, const lambda captures, the filter
  registry in plugin tests, and per-case test output.

## 2026-10-21 release preparation: 0.5.0

- The user requested a release with Remove Dust as a new feature, illustrated
  with `docs/images/remove_dust.png` in README and on the website, and chose
  version 0.5.0.
- Bumped only the Solstice version; README marks 0.5.0 as in development until
  the GitHub Release exists ([version procedure](../versioning.md#releasing)).

## 2026-10-21 release: 0.5.0

- Published GitHub Release `v0.5.0` at `49697aadce6c` (run 39566003093) and
  the website's Remove Dust pages and version 0.5.0. Evidence:
  [0.5.0 release verification](history/release-0.5.0.md); ledger in
  [Versioning](../versioning.md#history).
- Lesson: `solstice.com --version` waits on the Krita-profile import prompt
  unless the legacy configuration and resource inputs are also isolated.

## 2026-10-24 fix: Quick Access icon items take one cell

- Adding an icon to an existing 2x1 Action or Docker item kept it 2x1, and
  Grid Edit cannot resize icon items. `normalizeIconItemSpans()`
  (`QuickAccessItemPresentation`) now shrinks them after property and
  Resources-dialog alias edits and when a profile loads. Contract and tests:
  [Quick Access](../quick-access.md); user guide `docs/quick-access.md`.
- Pitfall added to [Build, format and test pitfalls](pitfalls/build-format-test.md):
  `_build` compiles only the main checkout, so a worktree change was built
  out of tree from `_build`'s own ninja commands.

## 2026-10-24 feature: Lazy Tools export, presets and guide

- Fast Image Export gained registered folders (General > Custom; Browse…
  starts there, the chosen folder becomes the export folder), an optional
  export Name with Clear, an Export Region button and the Export icon.
  General > Custom gained the Rename Layer Presets editor (the Python Name
  List tab) and now scrolls. Contracts, keys and checks:
  [Lazy Tools](../lazy-tools.md); user guide `docs/lazy-tools.md`, rewritten
  with the user's screenshots and a list of plugin features not ported.
- Decisions (user): registered folders are only a starting point, never an
  export destination; the Browse button sits beside Registered folder and
  replaced a first "Open in Explorer" button; the guide and website present
  Fast Image Export, Rename Layer, color labels, selection masks, then the
  rest.
- Pitfalls added to [Build, format and test pitfalls](pitfalls/build-format-test.md):
  temporary export documents must copy the source's batch mode, accepting a
  `QFileDialog` from a test, and `clang-format-diff.py` needing
  `PYTHONUTF8=1`.

## 2026-10-24 release preparation: 0.5.1

- The user asked to release the Lazy Tools work as 0.5.1 and to include the
  Quick Access icon-span fix (PR #32, merged first). Only the Solstice
  version's `PATCH` changes; README marks 0.5.1 as in development until the
  GitHub Release exists ([version procedure](../versioning.md#releasing)).

## 2026-10-24 release: 0.5.1

- Published GitHub Release `v0.5.1` at `00153a635789` (run 40076396030), and
  the website's Lazy Tools pages and version 0.5.1. Evidence:
  [0.5.1 release verification](history/release-0.5.1.md); ledger in
  [Versioning](../versioning.md#history).
- Lesson: run `update_translations.py` before the release build. It found
  24 untranslated Lazy Tools strings and an indented `.ui` placeholder line;
  `&#10;` does not work as a `.ui` newline because the extractor keeps it.

## 2026-10-24 fix: Fast Image Export keeps format options

- The user found that **Force convert to sRGB** reset to off in every Fast
  Image Export PNG options dialog. Fast Image Export passed a configuration
  with only its own keys, and an explicit configuration replaces the
  remembered one. It now starts from `ExportConfiguration-<mime>` and applies
  its own values on top ([Lazy Tools](../lazy-tools.md)).
- Lesson: when calling `exportDocumentSync()` with options, start from
  `KisConfig::exportConfigurationXML(mime)` unless the export must ignore the
  user's last choices.

## 2026-10-24 release preparation: 0.5.2

- The user confirmed the format-options fix in the application and asked to
  release it as 0.5.2 with the website update. Only `PATCH` changes; README
  marks 0.5.2 as in development until the GitHub Release exists.

## 2026-10-24 release: 0.5.2

- Published GitHub Release `v0.5.2` at `e352f1c51963` (run 40091292555) and
  the website's version 0.5.2. Evidence:
  [0.5.2 release verification](history/release-0.5.2.md); ledger in
  [Versioning](../versioning.md#history).

## 2026-10-25 fix: HueSVC color space

- The user found HueSVC showing a single lightness bar instead of the hue bar
  and S/V square until they switched documents. HueSVC set its selector's
  color space only in `setCanvas()`, from the current node; a filter or fill
  layer or a mask reports GrayA. It now follows node and image color space
  changes and falls back to the image's RGB color space or `rgb8`
  ([Quick Access](../quick-access.md)). The user confirmed the fix in the
  application.
- Lesson: [A node's color space can be GrayA](pitfalls/node-color-space-of-selections.md).

## 2026-10-25 release preparation: 0.5.3

- The user asked to merge the HueSVC fix (PR #37) and release it as 0.5.3
  with the website update. Only `PATCH` changes; README marks 0.5.3 as in
  development until the GitHub Release exists.

## 2026-10-26 intake: upstream 2026-W44

- The user accepted the evaluation of the Hermes report 2026-W44 and asked
  to adopt U-2026W44-03 to -09 (11 upstream commits: palette crashes, PSP
  palette bounds check, color conversion cache, shape copy-paste, clone-paste
  of frames, text properties padding, font dropdown autocomplete). Ledger in
  [upstream-intake.md](../upstream-intake.md); -01, -02 and -11 are held
  (-01 and -02 for their own PR), the rest rejected.
- `e09952bcad` needed an adaptation (`reset()` instead of `clear()`); a
  `krita/6.0` backport carried a `Co-authored-by:` line that was removed.
  The share was not reachable, so the commits came from the GitHub mirror;
  the manual procedure is now in step 2 of the intake document.
- Lessons: master-only prerequisites can be rejected commits, and a clean
  3-way apply is not proof of compilation
  ([decision page](decisions/upstream-selective-intake.md)).

## 2026-10-26 fix: follow-ups to the 2026-W44 intake

- A review of the adopted upstream code found two older palette docker bugs
  and one regression from U-2026W44-06; the user asked to fix them
  (rows after "Small behavior fixes" in the
  [feature inventory](../feature-inventory.md)). Undo/redo button
  connections no longer pile up on palette switches; a palette switch no
  longer writes the lock state to the previous palette; a vector layer
  passes resolution changes to its shapes again.
- Not built or run in this session (cloud container without Qt); the
  manual checks were handed to the user.
- Lesson: a `setChecked()` on a button whose `toggled()` drives a slot that
  reads "current" state must run after that state is updated, or under a
  `QSignalBlocker`.

## 2026-10-26 decision: staged upstream adoption

- The user asked whether to adopt upstream changes in one batch after each
  upstream release instead of weekly. Decision: keep the weekly review and
  adopt by type (now / soak / release), with follow-up detection in the
  Hermes report. Recorded in
  [the decision page](decisions/upstream-selective-intake.md) and
  [upstream-intake.md](../upstream-intake.md), "Timing". The addendum for
  `hermes/SKILL.md` was handed to the user, since the share is not
  reachable from the cloud session.

## 2026-10-26 plan: UI test automation through MCP

- The user asked whether interactive tests (brush strokes, selection tools)
  could be automated by an LLM through a new MCP server, then asked for a
  plan. Written as [UI test automation plan](../ui-automation-plan.md):
  a native, environment-gated test control plugin injects Qt input into the
  canvas and answers state/pixel queries; an external MCP bridge and a
  scenario runner use the same local protocol. Six decisions await the user.
- Not built or run (cloud container without Qt).
- Lessons for the harness: libkis painting (`Node::paintLine()`) bypasses
  the input manager and tools, so it cannot test tool behavior; the
  single-instance check in `krita/main.cc` ignores `SOLSTICE_PROFILE_ROOT`,
  so a test launch while Solstice runs hands its arguments to that instance.

## 2026-10-26 feature: test control phase 0

- The user decided the plan's open questions (ship the plugin, MCP inside
  Solstice, hashes and state in the repository, scenarios next to feature
  tests, no second instance by operation) and authorized phase 0 only.
  Recorded in the [plan](../ui-automation-plan.md).
- Implemented `plugins/extensions/testcontrol/`: an application plugin that
  serves MCP Streamable HTTP on `127.0.0.1` with a mandatory token when
  `SOLSTICE_TEST_CONTROL_PORT`/`_TOKEN` are set, eight tools and synthetic
  mouse/tablet strokes through the canvas input manager. Maintained in
  [Test control](../test-control.md); user guide `docs/test-control.md`.
- Verified only in the cloud container: protocol and injector tests against
  Qt 6.4.2 outside Krita's build, a syntax check of all sources against the
  repository headers with stubs, and the MCP TypeScript SDK client against a
  standalone server. Not built in `_build` or run in Solstice; the phase 0
  manual checks were handed to the user.
- Lessons: [synthetic canvas input](pitfalls/synthetic-canvas-input.md)
  (receiver, eaten pen press, keyboard-resynced modifiers); two C++ pitfalls
  added to [build pitfalls](pitfalls/build-format-test.md) (SP/WSP
  conditional, `QObject::property` hiding a helper).

## 2026-10-26 intake: KisClipboard rewrite (U-2026W44-01/02) and Linux build

- Adopted the twelve held KisClipboard commits on
  `claude/clipboard-intake-cross-platform` (the clipboard commits of
  `claude/wizardly-mendel-g5fwrz`). That branch did not compile:
  `0a84c23fe5` passes `KisProcessingApplicator::READ_ONLY_ACTION`, which
  comes from upstream `a46d36546f`. Adopted only the applicator part of
  `a46d36546f`, placed before `0a84c23fe5` so every commit builds; ledger
  rows updated in [upstream intake](../upstream-intake.md).
- First Linux build and test run (Ubuntu 26.04, GCC 15, Qt 6.10). Fixed
  two GCC-only build errors (`quint64` vs `uint64_t` in `KisGpuContext`,
  `-Wno-missing-prototypes` on visioncpp C++). `kis_clipboard_test`
  passes all 37 cases under Xvfb; the full suite shows no failure that
  `main` does not have too.
- Windows (clang, Qt 6.8): full build passes. `kis_clipboard_test`
  passes every case, but only when failing rows are rerun alone: the
  clipboard service makes `OleSetClipboard` fail on random rows in a full
  run. The user checked copy/paste in the app (before the fix below;
  closing the app after the fix is not rechecked yet).
- Fixed on Windows: closing Solstice after a copy showed the
  `!sanityCheckPointer.isValid()` assert (`KisDocument.cpp`) and kept the
  clipboard locked. The rewrite's `application/zip` path in
  `KisMimeData::retrieveData()` deleted its temporary document while
  `serializeToNativeByteArray()` still held the image in `savingImage`;
  restored the `setCurrentImage(nullptr)` that `main` had. Windows requests
  that format after every copy and at exit (`OleFlushClipboard`), Linux
  without a clipboard manager does not, so the Linux run missed it.
- Lessons: [build pitfalls](pitfalls/build-format-test.md), "Linux (GCC)
  builds" (LP64 types, clang-only flags, install paths, Xvfb, test memory
  cap, stale `~/.qttest` data) and "Tests" (assert dialogs, Windows
  clipboard contention).

## 2026-10-26 fix: test control on Windows

- First Windows build of `plugins/extensions/testcontrol` (merged with
  `main` after #40). Two test-only fixes: `TestControlInputInjectorTest`
  kept Qt 6 `quint64` timestamps in `ulong` (32-bit on Windows, narrowing
  error), and `TestControlProtocolTest::testHttpEndToEnd` blocked in
  `waitForReadyRead()` while its server needed the same thread's event
  loop. Both tests pass through `ctest` (3 repeats). Plugin not installed
  or run in Solstice; verification status in [Test control](../test-control.md).
- Lessons: [build pitfalls](pitfalls/build-format-test.md), "Tests"
  (same-thread server, 32-bit `ulong`).

## 2026-10-26 fix: test control phase 0 checks on Linux

- Ran the twelve phase 0 manual checks against a running Solstice on Linux
  (Xvfb, Qt 6.10) through the MCP endpoint; results recorded in
  [Test control](../test-control.md).
- Fixed three input defects the checks found: events are now spontaneous
  (rectangle selection cancelled on release), tablet strokes start with a
  hover move and a 50 ms settle (the first pen stroke on a new canvas
  painted a dot), and `wait_idle` needs three idle polls (a selection read
  was empty in 1 of 25 runs). Unit tests updated; both pass.
- Still open: strokes do not paint while another application holds focus
  (step 10); Windows has not been checked interactively.
- Lessons: [synthetic canvas input](pitfalls/synthetic-canvas-input.md)
  (spontaneous releases and `sendEvent()`, stylus device switch, settling
  idle, platform-dependent accepted flags).

## 2026-10-27 fix: test control phase 0 checks on Windows

- Ran the twelve phase 0 manual checks against a running Solstice on
  Windows 11 (Qt 6.8, `_install`, scratch profile) through the MCP endpoint;
  results recorded in [Test control](../test-control.md). All pass; step 10
  (strokes while another window has focus) works on Windows, unlike Linux.
- Fixed `wait_idle`: the selection tools default to vector selections, whose
  pixels Krita renders from update compressors that `KisImage::isIdle()` does
  not report. A read right after the three-poll settle was empty or busy in
  most back-to-back runs (1 of 25 correct). `wait_idle` now needs 600 ms of
  idle polls while the current selection has a shape selection: 75 of 75
  correct. The first guess (the projection compressor alone, 400 ms) left a
  late job at about 430 ms that made 1 of 50 reads busy.
- A scratch profile root on Windows needs the `ready` marker in
  `config/SOLSTICE_PROFILE` (and `klanguageoverridesrc`) when a Krita
  profile exists, or the first start stops at the native import question.
- Lessons: [synthetic canvas input](pitfalls/synthetic-canvas-input.md)
  ("A vector selection renders its pixels later").

## 2026-10-27 check: test control re-run on Linux

- Re-ran the phase 0 checks on Linux with the Windows `wait_idle` fix
  (`b8d202ba6b`): same results as on 2026-10-26 (step 10 still fails on
  Linux only); 75 of 75 back-to-back selection reads correct. Recorded in
  [Test control](../test-control.md).
- New: with a vector selection active, `wait_idle` slows as documents
  accumulate (0.6 s with a few, 1.2 s with 12, past the 10 s timeout with
  about 75). Cause not investigated; listed as a limitation.

## 2026-10-27 fix: test control step 10 on Linux

- Investigated why synthetic strokes did not paint on Linux while another
  window had focus: matcher traces (`DEBUG_MATCHER`, local only) showed the
  press starting no action because the canvas had received a `Leave` when a
  window covered the pointer; focus was incidental. The injector now sends a
  spontaneous `Enter` before each stroke. Step 10 passes on Linux and the
  rest of the checklist passes again; Windows not re-run.
- Lesson added to [synthetic canvas input](pitfalls/synthetic-canvas-input.md).

## 2026-10-27 fix: test control wait_idle slowdown

- Investigated the `wait_idle` slowdown with many open documents: the image
  never went busy; the 20 ms polls were stretched by GUI-thread load. Stack
  samples showed constant canvas repaints, from the marching-ants timer that
  every view with a selection runs, also in covered background tabs.
- `wait_idle` now measures its 600 ms vector-selection settle with a clock
  plus twice the longest poll gap, and reports `maxPollGapMs`. 40 of 40 reads
  correct while documents with selections accumulated; 1.2 s with 40 instead
  of over 10 s with about 75.
- Not changed: `KisSelectionDecoration` keeps animating covered views
  (skipping them cut 5.4 s to 0.9 s in a temporary test); left for the user
  to decide because it changes core behavior.
- Lesson: [synthetic canvas input](pitfalls/synthetic-canvas-input.md),
  "Count settle time with a clock, not in polls".

## 2026-10-27 decision: test control phase 1

- Phase 0 recorded as done in the [UI test automation plan](../ui-automation-plan.md)
  (checks pass on Windows and Linux); the user authorized phase 1, whose
  planned scope is listed there.

## 2026-10-27 feature: test control phase 1

- Added eight tools: `document_open` and `document_export_png` (confined to
  `SOLSTICE_TEST_CONTROL_ROOT`), `document_close`, `view_reset`,
  `brush_set`, `input_click`, `input_drag`, `query_pixels`; and `modifiers`
  on the input tools (key events plus canvas focus). Shared helpers moved to
  `TestControlSupport`, path checks to `TestControlPaths` (unit tested).
- Defined scenario format version 1 and added three scenarios next to the
  covered features' tests (brush stroke and undo, rectangular selection and
  fill, eraser stroke); they pass on Linux in four runs each, and a copy with
  wrong expectations fails where expected. Recorded in
  [Test control](../test-control.md); plan status in the
  [UI test automation plan](../ui-automation-plan.md). Windows not checked.
- Lesson: modifier key events carry the state before the key
  ([synthetic canvas input](pitfalls/synthetic-canvas-input.md)).

## 2026-10-27 feature: test control phase 2

- The user said to keep going after phase 1; recorded as the phase 2
  go-ahead in the [UI test automation plan](../ui-automation-plan.md).
- Added the scenario runner `dev-tools/test-control/run_scenarios.py`
  (Python standard library; replays scenarios, JSON report, `--launch`,
  `--record`, `--strict`), per-platform recorded values and reference images
  next to scenarios, and reference comparison with tolerance in
  `query_pixels` (`TestControlImageCompare`). Unit tests for both; a fourth
  scenario (`pressure-stroke`) uses a recorded reference and hash.
- Verified on Linux: record, three strict runs, a failing negative copy, and
  a `--launch` run that started and quit Solstice cleanly. Windows not
  checked. Details in [Test control](../test-control.md).

## 2026-10-27 feature: test control phase 3 (selection and Transform Tool)

- The user authorized phase 3 starting with the selection tools, selection
  modifiers and the state-checkable Transform Tool / Puppet Warp items;
  recorded in the [UI test automation plan](../ui-automation-plan.md).
- Added `input_key`, `ui_query`, `ui_click`, `ui_set`, `docker_show`, a
  double click, a hover move before mouse presses, a restart in
  `tool_activate`, and `uiLanguages` in `session_info`; the runner sets an
  English UI for `--launch` (`--language`). Object names for the Puppet Warp
  click-action combo and order buttons.
- Eleven scenarios: five for selections, six for the Transform Tool and
  Puppet Warp; the covered checklist items are listed in
  [Puppet Warp](../puppet-warp.md#manual-regression-checklist). All 15
  scenarios pass on Linux with `--launch --strict`; a wrong expectation in
  each new scenario fails on that step. Windows not checked.
- Lessons: [clicks need a hover move, and a key sent with XSendEvent can
  stay held](pitfalls/synthetic-canvas-input.md); [the transform holds the
  image busy, the center grabs the pivot, mode and lock carry
  over](pitfalls/transform-tool-input.md). Details in
  [Test control](../test-control.md).

## 2026-10-27 feature: test control phase 3 (Lazy Tools and Quick Access)

- The user authorized the rest of phase 3; recorded in the
  [UI test automation plan](../ui-automation-plan.md).
- Added `query_layers`, `layer_activate` and `ui_close`; widget selectors
  by tool tip, class and window title; `async` for modal dialogs; combo
  `activated` in `ui_set`; more brush and document state.
- Eight scenarios: Lazy Tools color labels, saved selections, foreground
  slots, Rename Alternative, Fast Image Export; Quick Access brush item,
  Quick Brush Adjustments, HueSVC. Covered checks are listed in
  [Lazy Tools](../lazy-tools.md#manual-checks) and
  [Quick Access](../quick-access.md#automated-scenarios-2026-10-27). All 23
  scenarios pass on Linux with `--launch --strict`; a wrong expectation in
  each new one fails on that step. Windows not checked.
- The rename scenario found a `KisNodeModel` crash (queued updates after the
  document closed); fixed in `setDummiesFacade()`
  ([pitfall](pitfalls/compressed-updates-outlive-their-model.md),
  [feature inventory](../feature-inventory.md)).
- Lessons: [modal dialogs and combo `activated`](pitfalls/synthetic-canvas-input.md).

## 2026-10-27 fix: KisLazyToolsTest on Linux

- `explorerFolders` gave the folder list a Windows path with backslashes,
  which only Windows normalizes, so the case failed on Linux. The input now
  uses native separators, plus a non-Windows check that backslashes stay
  and case matters ([Lazy Tools](../lazy-tools.md)). The code was right.
- `git clang-format` re-sorted the include block next to the new include
  again ([build pitfalls](pitfalls/build-format-test.md)); restored.

## 2026-10-27 feature: test control phase 4 (paint trace series)

- The user said "go phase 4"; recorded in the
  [UI test automation plan](../ui-automation-plan.md).
- Added `dev-tools/test-control/paint_trace_series.py` (with tests): the
  paint trace protocol's 16 strokes as identical synthetic tablet strokes in
  fresh processes per mode, checked and aggregated like the hand baseline.
  `brush_set` gained `paintMode`, `session_info` `gpuEngine`.
- First synthetic series on Linux (nine processes, all accepted), recorded
  as a separate series in the
  [run sheet](../paint-trace-baseline-runs.md#synthetic-series-linux-2026-10-27-test-control).
- Lessons: on Xvfb the canvas draws with software OpenGL, so the time after
  the last upload is the same in every mode and dominates the absolute
  numbers; compare modes by the part before the upload. The shared-buffer
  canvas path needs a hardware-GL display, so Xvfb never exercises it.
  Identical input shrank the process ranges to about 1 ms at 64 px.
  Vision ML's startup warning appears in every fresh scratch process on a
  machine where it cannot initialize; a multi-process tool needs someone
  (or a helper) to close it each time.
- The regression run after the series found `puppet-show-mesh` failing at
  random: a release right after the last move dropped the compressed move
  ([pitfall](pitfalls/synthetic-canvas-input.md)). The injector now waits
  50 ms before releasing a stroke with moves; the core behavior is recorded
  in [Test control](../test-control.md#known-limitations-and-pending-work)
  for the user to decide.

## 2026-10-27 fix and correction: releases, compressed moves, Transform Apply

- The user asked to fix `KisInputManager` so a release delivers the move
  its compression holds. Done (`Private::flushCompressedMove()` before mouse
  and tablet releases); verified on Linux with back-to-back free transform
  drags (at least 29 of 30 short per device without it, 30 of 30 exact with
  it) and the new scenario `transform-quick-release`
  ([feature inventory](../feature-inventory.md)).
- Correction of the previous entry: the random `puppet-show-mesh` failures
  were not the release. A gdb trace showed every move reaching the Puppet
  strategy; the cause is the Transform Tool's 40 ms recalculation compressor
  with Apply right after the drag ([Test control](../test-control.md#known-limitations-and-pending-work),
  [Puppet Warp](../puppet-warp.md)). Not fixed; the injector's pause before
  the release keeps the scenarios stable.
- Found while checking: a crash when Puppet pins placed with the mouse are
  dragged with the pen (separate tool instances, stale pin index), and a
  Layer Comps crash on a null document after fast document switching. Not
  fixed.
- Lesson: [one step short has more than one cause](pitfalls/synthetic-canvas-input.md).

## 2026-10-27 fix: Transform Tool Apply flushes the pending recalculation

- The user asked to fix the stale Apply found while checking the release
  fix. `KisToolTransform::endStroke()` now runs the strategy's pending
  recalculation first (`flushPendingRecalculation()`; Warp, Cage, Puppet,
  Mesh). Verified on Linux with Puppet Warp drags released at once (60 of 60
  and 30 of 30 correct per device, against 5 of 60 and 4 of 30 wrong before)
  and the new scenario `puppet-quick-apply` (9 of 10 runs failed without
  the fix, 10 of 10 pass); all 25 scenarios pass twice
  ([Puppet Warp](../puppet-warp.md#interaction-invariants),
  [feature inventory](../feature-inventory.md)).
- Test control gained `keyAfterRelease` so a key can follow the release in
  the same call; an HTTP round trip in between hid the race
  ([pitfall](pitfalls/synthetic-canvas-input.md)).
- `kis_cage_transform_worker_test` fails its two unity cases on Linux (image
  comparison); it does not load the tool plugin and its code is unchanged
  since the initial snapshot.

## 2026-10-27 fix: Puppet stale pin index and Layer Comps null document

- Puppet Warp: after pins placed with the mouse were dragged with the pen,
  the next transform's first mouse press came without a hover and used the
  old pin index; `beginPrimaryAction()` now decides again for the press
  position. Traced with gdb, verified with a mixed-device diagnostic and the
  new scenario `puppet-mixed-devices` (a build without the guard crashed in
  it) ([Puppet Warp](../puppet-warp.md#interaction-invariants),
  [pitfall](pitfalls/transform-tool-input.md)).
- Layer Comps: a delayed refresh reached `CompositionDocumentState::get()`
  with a null document; guarded in `updateModel()`, `setCanvas()` and
  `get()`. Not reproduced again, so verified only by the backtrace, the
  workflow test and the scenario suite ([Layer Comps](../layer-comps.md)).
- All 26 scenarios pass twice on Linux; Windows not checked.

## 2026-10-27 fix: selection outline animation in covered views

- `KisSelectionDecoration::antsAttackEvent()` now skips views whose
  `visibleRegion()` is empty. Background tabs of the tabbed document area
  are covered, not hidden, so every document with a selection repainted its
  canvas every 150 ms. Found while investigating the test control
  `wait_idle` slowdown (branch `claude/test-control-linux`).
- Verified on Linux (Xvfb, software OpenGL) with this commit on top of the
  test control branch (local build only): with 40 documents that have
  selections, the longest GUI poll gap stayed at 73 ms instead of 296 ms,
  `wait_idle` after a selection at 751 ms instead of 1246 ms, and after a
  brush stroke at 40 ms instead of 552 ms. The visible view animates; a
  covered view resumes when brought to the front (`windows_next`, about 710
  changed pixels per 0.3 s, none for a document without a selection); in
  subwindow mode two tiled views animate at once. Selection and decoration
  tests (8) pass.
- Verified on Windows 11 (hardware OpenGL) the same way, comparing the test
  control branch with and without this commit (local build only). With 40
  documents that have selections, GUI timers were not delayed even without
  the fix (longest poll gap 23 ms; `wait_idle` 739 ms after a selection and
  40 ms after a brush stroke, both builds; 40 of 40 selection reads
  correct). The cost on Windows was CPU: idle, Solstice used 331 ms of CPU
  per second without the fix and 24 ms with it. Screen captures 0.3 s apart:
  the visible view, covered views brought to the front (`windows_next`) and
  the view after minimize and restore animate (510-770 changed pixels, all
  on the outline); a document without a selection does not change; in
  subwindow mode two tiled views animate at once. Selection and decoration
  tests (7 on Windows) pass.
- Test control's `canvas_capture` (`QWidget::grab()` of the canvas) shows no
  selection outline on Windows, so it cannot judge the animation there; the
  Windows check compared screen captures of the main window instead.
- Touch point recorded in [feature inventory](../feature-inventory.md).

## 2026-10-27 feature: duplicate selection in the same layer

- Move Tool Ctrl-drag with a selection on a paint layer duplicates the
  selected pixels (`MoveSelectionStrokeStrategy` duplicate mode: no cut,
  "Duplicate Selection" undo entry); on a Brush Stroke Layer the strokes
  inside the selection are copied (`createSelectionDuplicateCommand()`).
  Maintained reference [Duplicate selection](../duplicate-selection-in-layer.md),
  user guide [Move Tool](../../move-tool.md); the plan is archived in
  [history](history/duplicate-selection-in-layer-plan.md).
- Tests: two new `MoveSelectionStrokeTest` cases and one each in
  `KisBrushStrokeLayerTest` and `KisBrushStrokeLayerTransformTest`; built
  out of tree and run on Windows. No manual check yet.
- Lessons: [build pitfalls](pitfalls/build-format-test.md), "Worktrees"
  (`_build` and `_install` changing under an out-of-tree build, launching
  the commands).

## 2026-10-27 ingest: Clipping Mask spike

- Investigation of the forum requests for Clipping Mask and for duplicating
  a selection on the same layer; the user chose option A for both. Plans:
  [Clipping Mask](../clipping-mask-plan.md),
  [duplicate selection](../duplicate-selection-in-layer-plan.md).
- Clipping spike in `libs/image`: a `KisClipStackLayer` outside the node
  graph replaces a clip base and its run in the projection leaf tree, so
  the walkers and `KisAsyncMerger` composite it like a group. Pixel-identical
  to an inherit-alpha group in `KisClippingTest`; GPU, LoD and UI unverified.
- Lesson: a projection leaf may belong to a node that is not in the graph,
  but `isRoot()` must then not rely on `node->parent()`, and overrides
  queried from the stack must read the base node, not the base leaf.

## 2026-10-27 ingest: Clipping Mask property, Merge Down, .kra, docker

- Clipping became an undoable layer property, exclusive with inherit alpha;
  Merge Down bakes a clipped layer into its base; `.kra` stores
  `clipped="1"`; the Layers docker shows it in the inherit alpha slot
  (Alt+click) with a row bar; two actions. Details and test results:
  [Clipping Mask plan](../clipping-mask-plan.md); user guide
  [Clipping Mask](../../clipping-mask.md). The user tested it, saving
  included, and reported it OK.
- Lesson: [no strong image reference during a copy](pitfalls/strong-image-ref-during-copy.md)
  (the save crash). Also: `KisAsynchronouslyMergeableCommandInterface` is
  not exported, so tests outside `kritaimage` cannot construct
  `KisNodePropertyListCommand`; use `setNodePropertyAutoUndo()` with a
  `KisSurrogateUndoStore` (`kritacommand`).

## 2026-10-27 ingest: Clipping Mask PSD, Merge Selected, assert fix

- PSD import/export of clipping, Merge Selected/Flatten with clip stacks,
  `zh_CN` strings, the README New Features card (#04, before Puppet Warp)
  and the guide image. Row indents were added and removed again at the
  user's request. Details: [Clipping Mask plan](../clipping-mask-plan.md).
- Lessons: the PSD test loads the *installed* PSD plugins, so install them
  before trusting a round-trip result. A property that switches another
  property off must record that in `KisNodePropertyListCommand`'s new list,
  or `mergeWith()` asserts on the next merged command;
  `QTest::failOnWarning("SAFE ASSERT")` turns such asserts into test failures.

## 2026-10-27 fix: Windows test run of main and the clipboard exit crash

- Merged today's branches into `main` (PRs #42-#45: test control phases
  1-4, covered-view selection animation, duplicate selection in the same
  layer, Clipping Mask) and built and tested `main` (`24d3dde29f`) on
  Windows: full build clean, 14 of 399 tests failed, none caused by the
  merges. The list and how each was judged are in
  [build pitfalls](pitfalls/build-format-test.md), "Full suite on Windows".
- Fixed: `kis_clipboard_test` passed every row and then crashed at exit.
  The `multi-mask` row leaves two masks on the clipboard;
  `clipImageFromKritaLayers()` gives no image for them by design, and
  `KisMimeData::retrieveData()` dereferenced that null image for
  `application/x-qt-image` (and would have saved a null image for
  `application/zip`) when `OleFlushClipboard` asked for every format at
  exit. Both formats now return an empty `QVariant`. The new check in
  `testCopyLayerWithMask` crashed without the fix and passes with it; the
  test program now exits 0 (3 runs, 37 of 37 rows). Came with the
  U-2026W44-01 `KisClipboard` rewrite; touch point in
  [feature inventory](../feature-inventory.md). Not checked in the app.
- Found, not fixed: `TestControlPathsTest::testSymbolicLinkOutOfRoot`
  fails on Windows because `QFile::link()` writes a `.lnk` shortcut; left
  for the remaining test control work.
- Lessons: [build pitfalls](pitfalls/build-format-test.md), "Tests"
  (clipboard formats at exit, `QFile::link()` on Windows).

## 2026-10-27 docs: Duplicate on the same layer card

- README New Features: a "Duplicate on the same layer" card with the user's
  image `docs/images/duplicate_in_same_layer.png` as #08 (after Effects &
  Color), linking to [Move Tool](../../move-tool.md#duplicating-a-selection);
  Layer Comps moved from #09 to the last place (#11). The image is also in
  the Move Tool guide. The website got the same card and order on branch
  `claude/duplicate-selection-page` of the website repository (not
  published; publish after the release).

## 2026-10-27 release prep: Solstice 0.6.0

- User request: release 0.6.0 (`MINOR`, new features) instead of the
  unreleased 0.5.3. Version bumped in `CMakeLists.txt`; README "In
  development: 0.6.0" lists Clipping Mask, duplicating a selection on the
  same layer, the HueSVC fix, the clipboard rewrite and fixes, the
  2026-W44 intake, the fixes found by test control and test control
  itself; [versioning](../versioning.md) records the preparation.
- Next: the Windows x64 trial build on `main`, GitHub Release `v0.6.0`, the
  post-release README and guide index, and the website (same day).

## 2026-10-27 release: Solstice 0.6.0

- Released `v0.6.0` at `87fb3e39798c` (run 40491540847) and published the
  website the same day (tidalboxstudio-web #17, #18), including the new
  `/features/` page and a top page reduced to six highlights. Evidence:
  [0.6.0 release verification](history/release-0.6.0.md).
- README, the guide index and the Clipping Mask guide and plan now describe
  0.6.0 as released.
- Lesson: `SHA256SUMS.txt` from the Windows build has CRLF line endings;
  `sha256sum -c` in Git Bash needs them stripped (`tr -d '\r'`).

## 2026-10-28 fix: middle-click closed more than one document tab

- User report: middle-clicking the leftmost document tab also closed the
  other documents. `QMdiArea` closes a tab on middle press, so the next
  tab slides under the cursor and a repeated press from the same click
  closes it. `MdiTabMiddleClickCloser` in `libs/ui/KisMainWindow.cpp` now
  closes on release over the same tab and ignores repeated presses;
  confirmed by the user in Solstice. Recorded in the
  [feature inventory](../feature-inventory.md) and the new pitfall
  [Closing on press moves the next tab under the cursor](pitfalls/close-on-press-shifts-tabs.md).

## 2026-10-29 fix: group thumbnail kept pixels outside the canvas

- User report: a group copied from one document and pasted into another,
  partly outside the canvas, kept showing the old out-of-canvas image in
  its layer thumbnail and tooltip after Ctrl+T. The copy constructor
  copies the group projection, and projection updates are cropped to the
  image bounds, so those pixels were never refreshed.
  `KisGroupLayer::setImage()` now crops the group's own projection to the
  image bounds; `KisGroupLayerTest::testCopyToSmallerImageCropsProjection`
  fails without it. Confirmed by the user in Solstice. Recorded in
  the [feature inventory](../feature-inventory.md) and the new pitfall
  [Group projections stop at the canvas](pitfalls/group-projection-outside-canvas.md).

## 2026-10-29 fix: filter layer thumbnail kept pixels outside the canvas

- Follow-up to the group fix. A temporary test measured both
  `KisSelectionBasedLayer` kinds copied from a 200x200 into a 100x100
  image: the filter layer's original stayed 200x200 even after a full
  refresh; the fill layer's became 100x100 on its first update.
  `KisAdjustmentLayer::setImage()` now crops the filtered original to the
  image bounds; `KisAdjustmentLayerTest::testCopyToSmallerImageCropsOriginal`
  fails without it. Fill layers were left unchanged; the reason is in
  [Group projections stop at the canvas](pitfalls/group-projection-outside-canvas.md).
