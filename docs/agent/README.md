# Solstice agent documentation

The [technical index in AGENTS.md](../../AGENTS.md#agent-technical-document-index)
is the feature entry point. Read [the wiki index](wiki/index.md) before work,
then the relevant complete feature reference. User workflows are in
[the user guide index](../README.md).

Documentation source stays in this repository. See
[Documentation maintenance](documentation.md) for ownership, precedence,
granularity, archival and the review checklist.

## Maintained references

- [Codebase map](codebase-map.md), [extension points](extension-points.md),
  [coding rules](coding-rules.md) and [development workflow](development-workflow.md)
  cover shared implementation rules.
- [Feature inventory](feature-inventory.md) maps custom source locations.
  It is not a progress tracker.
- Feature references linked from AGENTS own current behavior, compatibility,
  architecture and checks. [GPU architecture](gpu-engine.md) owns the engine
  contract; [GPU priorities](gpu-work-priorities.md) owns work order.
- [Versioning](versioning.md) owns release procedures and the version ledger;
  [GitHub Actions](github-actions.md) owns build/package verification.
- [Upstream intake](upstream-intake.md) owns selective patch decisions;
  [Todo](todo.md) holds user-registered work not yet started.

## References that still contain design history

The following files evolved from plans into maintenance references. Their
names alone do not make them unimplemented plans or disposable archives.
Do not infer current behavior from an initial proposal. Read the implementation
records and subsequent corrections, and preserve their compatibility and
validation details when extracting a standalone current reference.

| Reference | Maintained information to find | Historical context to distinguish |
| --- | --- | --- |
| [CPU effects and G'MIC](cpu-effects-and-gmic-plan.md) | Status/decisions and F1-F6/G'MIC implementation records, including CPU bridge refinements | Initial investigation, proposed IDs/architecture and original completion gates |
| [Illustration filters](illustration-filters-plan.md) | Decisions and Stage S/C/G/D1/L implementation records, including per-filter limits and pending interactive checks | Initial gap analysis, proposed algorithms and work order |
| [Brush option shared model](brush-option-shared-model-plan.md) | Implementation results through phase 5, ownership/compatibility rules and manual checks | Original data flow, provisional class names, early-phase "next" steps |
| [Brush Stroke Layer](brush-stroke-layer-plan.md) | Current implementation layout, recording/replay contracts, later transform/move/partial-redraw behavior and audit corrections | Initial external-layer proposal and superseded early serialization/comparison behavior |
| [Photoshop brush import](abr-import-plan.md) | Implemented parser, tips, patterns, presets, tags and finishing corrections | Initial parser limitations and staged proposal |
| [UI modernization](ui-modernization-plan.md) | Implemented phase results and remaining scope | Early UI proposals and historical branch instructions; current branch policy is in AGENTS |
| [Settings location](settings-location.md) | Final profile paths, path helpers, import/default behavior and later fixes | Pre-migration file inventory and interim migration stages |

These references remain authoritative until their complete current contracts
have been extracted. Avoid appending unrelated work diaries to them. New
maintenance updates should revise the relevant current behavior and link
dated validation evidence, rather than adding another conflicting proposal.

## Superseded plans and historical evidence

| Record | Role / maintained successor |
| --- | --- |
| [Color Reference plan](color-reference-plan.md) | Archived proposal; [Color Reference](color-reference.md) supersedes it |
| [Layer Comps plan](layer-comps-plan.md) | Original plan summary and optional LC5 scope; [Layer Comps](layer-comps.md) owns implementation |
| [Brush model phase 0](brush-option-shared-model-phase0.md) | Dated source investigation; the shared-model reference owns subsequent implementation |
| [Paint trace baseline runs](paint-trace-baseline-runs.md) | Recorded workloads and run IDs; not the current work queue |
| [GPU handoff archive](wiki/history/gpu-engine-handoff.md) | Expired session instructions, PIDs and measurements; use GPU architecture/priorities for new work |
| [Feature touch-point record](wiki/history/feature-touchpoints-20261016.md) | Historical per-phase hook detail; use the maintained inventory to update current source ownership |
| [Brush Stroke Preview design](wiki/history/brush-stroke-preview-design.md) | Superseded design and dated test results; [Brush Stroke Preview](brush-stroke-preview.md) owns the current contract |

The [wiki index](wiki/index.md) also lists archived GPU phase records,
benchmarks, decisions and reusable pitfalls. Historical instructions are
evidence of earlier work, not authorization or current operational policy.
