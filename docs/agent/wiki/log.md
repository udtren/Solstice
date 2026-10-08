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

- No new wiki page: the decisions and findings are in `docs/agent/settings-location.md`.
  These include the relative KConfig names, the mounted kritarc defaults, the import rules,
  the Solstice defaults and the verification.
- Pitfall recorded in `docs/agent/settings-location.md` (phase 5): starting `krita.exe`
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
