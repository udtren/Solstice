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
- [Brush Stroke Layer](brush-stroke-layer.md) owns recording, replay, persistence,
  lifecycle and regression checks; its original plan is archived below.
- [Photoshop brush import](abr-import.md) owns parsing, conversion, resource
  compatibility and verification scope; its development history is archived below.
- [Brush option shared model](brush-option-shared-model.md) owns state,
  serialization compatibility and dependencies across all brush engines.
- [Solstice interface](ui-modernization.md) owns theme/style boundaries,
  defaults, scale/font configuration and visual regression scope.
- [Settings location and profile import](settings-location.md) owns path
  resolution, startup/copy behavior, defaults and reset boundaries.
- [Native CPU effects](cpu-effects.md) owns the six filters' pixel, configuration
  and regression contracts; [G'MIC](gmic.md) owns its runtime, bridge and packaging.
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
| [Illustration filters](illustration-filters-plan.md) | Decisions and Stage S/C/G/D1/L implementation records, including per-filter limits and pending interactive checks | Initial gap analysis, proposed algorithms and work order |

These references remain authoritative until their complete current contracts
have been extracted. Avoid appending unrelated work diaries to them. New
maintenance updates should revise the relevant current behavior and link
dated validation evidence, rather than adding another conflicting proposal.

## Superseded plans and historical evidence

| Record | Role / maintained successor |
| --- | --- |
| [CPU effects and G'MIC development](wiki/history/cpu-effects-and-gmic-development.md) | Complete plan, phase results and measurements; [CPU effects](cpu-effects.md) and [G'MIC](gmic.md) own maintenance; [old plan path](cpu-effects-and-gmic-plan.md) remains a pointer |
| [Brush Stroke Layer development](wiki/history/brush-stroke-layer-development.md) | Complete original plan and dated results; [Brush Stroke Layer](brush-stroke-layer.md) owns the current contract; [old plan path](brush-stroke-layer-plan.md) remains a pointer |
| [ABR import development](wiki/history/abr-import-development.md) | Original investigation, staged implementation and dated results; [ABR import](abr-import.md) owns the current contract; [old plan path](abr-import-plan.md) remains a pointer |
| [UI modernization development](wiki/history/ui-modernization-development.md) | Complete proposal and dated results; [Solstice interface](ui-modernization.md) owns current behavior; [old plan path](ui-modernization-plan.md) remains a pointer |
| [Settings/profile development](wiki/history/settings-location-development.md) | Complete pre-migration inventory and phase results; [Settings location](settings-location.md) remains the maintained reference at its stable path |
| [Color Reference plan](color-reference-plan.md) | Archived proposal; [Color Reference](color-reference.md) supersedes it |
| [Layer Comps plan](layer-comps-plan.md) | Original plan summary and optional LC5 scope; [Layer Comps](layer-comps.md) owns implementation |
| [Brush model development](wiki/history/brush-option-shared-model-development.md) | Complete plan and implementation record; [shared model](brush-option-shared-model.md) owns current behavior; [old plan path](brush-option-shared-model-plan.md) remains a pointer |
| [Brush model phase 0](wiki/history/brush-option-shared-model-phase0.md) | Original source investigation; [old investigation path](brush-option-shared-model-phase0.md) remains a pointer |
| [Paint trace baseline runs](paint-trace-baseline-runs.md) | Recorded workloads and run IDs; not the current work queue |
| [GPU handoff archive](wiki/history/gpu-engine-handoff.md) | Expired session instructions, PIDs and measurements; use GPU architecture/priorities for new work |
| [Feature touch-point record](wiki/history/feature-touchpoints-20261016.md) | Historical per-phase hook detail; use the maintained inventory to update current source ownership |
| [Brush Stroke Preview design](wiki/history/brush-stroke-preview-design.md) | Superseded design and dated test results; [Brush Stroke Preview](brush-stroke-preview.md) owns the current contract |

The [wiki index](wiki/index.md) also lists archived GPU phase records,
benchmarks, decisions and reusable pitfalls. Historical instructions are
evidence of earlier work, not authorization or current operational policy.
