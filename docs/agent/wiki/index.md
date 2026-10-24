---
type: index
updated: 2026-10-24
---

# Agent wiki

A persistent knowledge base maintained by coding agents, following the LLM
wiki pattern (sources -> wiki pages -> schema). It holds what the feature
documents do not: cross-cutting technical knowledge, the reasons behind
decisions, pitfalls found while working, benchmarks and archived phase
records.

Read this index before starting work; open the pages whose one-line summary
matches the task.

Document ownership and lifecycle are defined in
[Documentation maintenance](../documentation.md); the
[agent guide](../README.md) identifies maintained references, plans and archived records.

## Conventions (schema)

- **Single source of truth.** Feature behavior, invariants, build/test
  commands and manual checks live in the feature documents under
  `docs/agent/` (index in `AGENTS.md`). Wiki pages link to them instead of
  copying them. A wiki page may summarize, but the feature document wins in
  a conflict; fix the wiki page.
- **Page types and folders:**
  - `concepts/`: technical knowledge that spans features (how Krita/Qt
    behave, techniques);
  - `decisions/`: one decision per page, with the reason and date;
  - `pitfalls/`: things that went wrong and how to avoid them;
  - `benchmarks/`: measured numbers with date, hardware, workload and source;
  - `history/`: archived phase logs, superseded designs and expired handoffs,
    with bodies preserved. Add provenance/archival notices and repair relative
    links when relocating them; append new phase results only to the newest
    phase page, not to frozen snapshots;
  - `queries/`: saved answers to questions that are likely to come up again.
- **Frontmatter** on every page: `type`, `updated` (YYYY-MM-DD), `sources`
  (commits, `path:line` or symbols, documents, external URLs) and `related`
  (paths to other wiki pages, relative to the containing page). Quote source
  strings containing `: ` so YAML does not interpret them as mappings.
  History pages carry `status` instead of `updated`,
  and no `related`. All history pages are listed here; the GPU phase section
  index also lives in `docs/agent/gpu-engine.md`.
- **Names:** lowercase-hyphenated file names; one topic per page; update a
  page instead of adding a near-duplicate.
- **Evidence:** name the symbol or file a claim depends on, so a lint pass can
  check it still exists. Mark unverified statements as such.
- **Language:** English, like the other agent documents.

## Workflows

- **Ingest** (after each committed task): add or update the pages the work
  taught something about: a new concept, a decision with its reason, a
  pitfall, a benchmark. Append an entry to [log.md](log.md). Phase records
  of the GPU engine go to the newest `history/gpu-phases-*.md` page.
- **Query** (before a task): read this index, then the relevant pages and
  the feature documents they link to. Save a reusable answer in `queries/`.
- **Lint** (periodically, or when asked): check that referenced files and
  symbols still exist, benchmarks are not stale, pages do not contradict
  `AGENTS.md` or the feature documents, and every page is listed here. Log
  the pass. Run the [automated navigation/schema checks](../documentation.md#automated-checks)
  locally; the same checks run on PRs. Source accuracy and dated evidence
  still need review.

## Pages

### Concepts

- [Reference image filter cache](concepts/reference-image-filter-cache.md): shape
  effects, preview isolation and asynchronous result ownership.

- [Native refactor boundaries](concepts/native-refactor-boundaries.md): sharing
  services without changing compatibility or QObject/deferred-work ownership.

- [CPU/GPU bit-identical parity](concepts/cpu-gpu-bit-parity.md): which
  parity rule applies (bit-identical transforms, tolerance for the FFT-based
  Gaussian blur); how the GPU transform paths reproduce CPU results bit for bit (planning with CPU
  classes, `precise` doubles, correctly rounded division and square root,
  rounding to float and half, ordering, fallback), and the test pattern.
- [Krita copy and sampling semantics](concepts/krita-copy-semantics.md):
  `copyAreaOptimized()`, `bitBlt()`'s `fastBitBlt()` path, the Copy
  composite op clearing transparent pixels and clamping HDR colors in SIMD
  batches,
  `KisRandomSubAccessor` weights.
- [Qt numeric and geometry semantics](concepts/qt-numeric-and-geometry.md):
  `qRound()`, `QPolygonF::containsPoint()`, `boundingRect()` /
  `toAlignedRect()`, fuzzy comparisons in Qt 6.8.

### Decisions

- [Repository-owned documentation](decisions/documentation-source-of-truth.md):
  canonical Markdown stays with the code; document roles, stable indexes and
  explicit archival prevent conflicting copies.

- [Feature branches before main](decisions/feature-branch-workflow.md): push
  feature branches and merge PRs; no direct main pushes, including releases.

- [Release and feature documentation](decisions/release-feature-documentation.md):
  a single latest-release summary in README, stable guides, illustrated website
  pages, and publication ordering.
- [GPU engine phase numbering](decisions/gpu-phase-numbering.md): how phase
  numbers are reserved and checked before reuse.
- [`krita5.xmlgui` stays unchanged](decisions/xmlgui-unchanged.md): why menu
  changes avoid the main XMLGUI file.
- [Solstice version vs. Krita compatibility version](decisions/versioning.md):
  why the public product version is separate from the internal compatibility version.
- [Upstream: weekly review, selective cherry-picks](decisions/upstream-selective-intake.md):
  Hermes reports, the user decides, Claude cherry-picks; dry-run lessons.
- [Translations: Solstice owns `krita.po`](decisions/translation-policy.md):
  direct catalog edits, AI translation stated in release notes, English
  feature names, no upstream translation updates.

### Pitfalls

- [Build, format and test pitfalls](pitfalls/build-format-test.md):
  building a worktree out of tree (`_build` compiles the main checkout),
  clang-format include sorting, shells, shader and struct layout mistakes,
  test environment (filter registry in plugin tests, per-case output),
  installation (including plugins after a vtable change), smart-pointer
  default arguments and lambda captures.
- [Weak pointers as ordered map keys](pitfalls/weak-pointer-map-keys.md):
  a `KisWeakSharedPtr` key turns into `nullptr` when its object dies and
  corrupts the map (missing layer docker thumbnails).
- [Python bindings declare their own return types](pitfalls/sip-return-types.md):
  a `.sip` file that says `void` gives Python `None` for a C++ `bool`
  (`Node.save()`); `.sip` edits do not trigger a rebuild.
- [Iterating a Qt 6 container is a write](pitfalls/qt6-detach-on-iteration.md):
  a non-const `begin()`/`rbegin()` detaches even a never-allocated list;
  a GUI-thread loop over `KisImage`'s filter stack lost a worker's filter
  (crash in `enableDirtyRequests()` during repeated `Node.save()`).
- [Transform Tool input](pitfalls/transform-tool-input.md): click-only
  changes need `acceptsClicks()` to be committed; the strategies' "Alt" flag
  is the SampleFgNode action (Ctrl+Alt by default); cache keys must hold every
  input.

- [Idle tasks wait for every open stroke](pitfalls/idle-tasks-wait-for-open-strokes.md):
  thumbnails, Overview and Histogram idle tasks wait until no stroke is open;
  Duplicate Layer's node juggler kept the new layer's thumbnail empty for
  about 4.5 s (now a provisional worker-thread thumbnail).
- [Translation extraction on Windows](pitfalls/translation-extraction.md):
  `.ui` `comment=` contexts, CRLF resource names, missing `Archive::Zip`,
  reference fix-up order, `QT_TRANSLATE_NOOP` not extracted.

### Benchmarks

- [Transform and filter costs](benchmarks/transform-and-filter-costs.md):
  CPU baseline and GPU results for affine transforms, Liquify, Puppet Warp,
  Gaussian blur and other filters on a 2480x3508 RGBA32F layer.
- [Clone Layer updates](benchmarks/clone-layer-updates.md): update time and
  GPU downloads per dab for clone offsets, masks, RGBA8/F32 and nesting,
  before and after phases 4.110 and 4.111.

### History

- [Illustration filters development](history/illustration-filters-development.md):
  original designs, stage S/C/G/D1/L results and timings; the maintained
  reference retains the current contracts and pending Line Art verification.

- [CPU effects and G'MIC development](history/cpu-effects-and-gmic-development.md):
  original plan, phase results and measurements; native effects and the
  separately built G'MIC runtime have their own maintained references.

- [Settings location and profile migration](history/settings-location-development.md):
  pre-migration inventory, provisional design and dated implementation checks;
  the stable settings reference owns current paths and startup behavior.

- [UI modernization development](history/ui-modernization-development.md):
  original proposal, visual baseline and dated implementation results;
  the feature reference owns current defaults and maintenance checks.

- [Brush option shared model development](history/brush-option-shared-model-development.md)
  and [phase 0 investigation](history/brush-option-shared-model-phase0.md):
  complete proposals and dated migration results; the feature reference owns
  current synchronization, compatibility and regression checks.

- [ABR import development](history/abr-import-development.md): original
  investigation, interim conversion rules and dated results; the feature
  reference owns current support, compatibility and remaining verification.

- [Brush Stroke Layer development](history/brush-stroke-layer-development.md):
  complete original plan, investigations, measurements and dated verification;
  current recording/replay/persistence rules live in the feature reference.

- [GPU engine development snapshot](history/gpu-engine-development.md):
  complete mixed reference, initial architecture, phase plan and measurements;
  current rules remain in the maintained GPU reference.
- [GPU work-order development snapshot](history/gpu-work-priorities-development.md):
  original priority decisions, progress and benchmark diary; the maintained
  work-order page owns unfinished work and user deferrals.

- [Expired GPU handoff](history/gpu-engine-handoff.md): former session context,
  measurements and instructions; current work uses GPU architecture/priorities.
- [Feature touch-point record](history/feature-touchpoints-20261016.md):
  accumulated per-phase source changes removed from the maintained inventory.
- [Brush Stroke Preview design](history/brush-stroke-preview-design.md):
  original design, assessment and dated verification, superseded by the
  maintained feature contract.

- [GitHub Actions development](history/github-actions-development.md): original
  trial failures, dependency refreshes and package verification records; use
  the maintained Actions guide for current operation.
- [0.3.0 release verification](history/release-0.3.0.md): exact build identity,
  package checks, publication and the boundary of existing feature-test evidence.
- [0.4.0 release verification](history/release-0.4.0.md): Reference Tool release,
  package integrity, isolated version checks and dependency-lock line endings.
- [0.5.0 release verification](history/release-0.5.0.md): Remove Dust release,
  package integrity, the legacy-profile prompt in version checks, website publication.

GPU engine phase records moved from `docs/agent/gpu-engine.md` (section index
there, under "Phase history"):

- [Reviews and memory budgets](history/gpu-reviews-and-memory.md)
- [Pixel brush and Wash (phases 4.1-4.16)](history/gpu-phases-4.01-4.16.md)
- [Blend modes, RGBA16F and transfers (phases 4.17-4.57)](history/gpu-phases-4.17-4.57.md)
- [Paint trace and latency analysis (phases 4.58-4.81)](history/gpu-phases-4.58-4.81.md)
- [Generated dabs and brush latency (phases 4.82-4.92)](history/gpu-phases-4.82-4.92.md)
- [Filters and transforms (phases 4.93-)](history/gpu-phases-4.93-.md) (newest)
- [GPU Engine user guide, development notes](history/gpu-user-guide-archive.md):
  the status and measurement sections removed from `docs/gpu-engine.md`
  before the first public release (2026-10-13)

### Queries

- [Maintenance review, 2026-10-15](queries/maintenance-review-20261015.md):
  document/device tool state, float redraw comparison, record validation,
  key-hold ownership, and conversion-policy compatibility.
