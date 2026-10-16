---
type: decision
updated: 2026-10-16
sources:
  - User decision 2026-10-16 (keep documentation originals in the current repository)
  - AGENTS.md (Documentation organization)
  - docs/agent/documentation.md
related:
  - release-feature-documentation.md
  - feature-branch-workflow.md
---

# Keep documentation originals with the code

The user chose to resolve existing contradictions and document roles before
building a documentation site, and to keep the originals in this repository.
The operational rule is in [Documentation maintenance](../../documentation.md).

Keeping implementation and documentation in one pull request makes their
relationship reviewable. A future website can render these Markdown sources;
it should not require another hand-edited version of the same guide.

The review found stale configuration paths, release links and branch/menu
instructions, plus progress copied into multiple indexes. Stable indexes now
point to the owning feature reference. Expired handoffs and implementation
diaries are explicitly historical; a dated archive must not authorize new work.

Long mixed plan/reference files still contain compatibility and validation
contracts. Identify their role and extract those contracts before archiving
them. The Brush Stroke Preview split demonstrates this without treating its
old docker-only proposal or legacy cache path as current behavior.

The [Brush Stroke Layer extraction](../../brush-stroke-layer.md) adds a second
example: compare every later correction with the implementation before promoting
it into the maintained reference. An early proposal can disagree with both the
final architecture and persistence format. Preserve the complete
[dated evidence](../history/brush-stroke-layer-development.md), including pending
checks, and distinguish past verification from the checks run during extraction.
A source comment can lag the implementation too; inspect the serializer/replay
path before carrying its wording forward.

The [ABR import extraction](../../abr-import.md) also distinguishes format-version
support from the content the loader actually converts. Keep unsupported,
approximated and unverified behavior separate: synthetic coverage does not
establish real-file compatibility. State where a parser limit applies instead
of promoting it to a total memory or safety guarantee. The
[original results](../history/abr-import-development.md) retain their dates and
sample scope.

The [shared brush model extraction](../../brush-option-shared-model.md) shows
why an early architecture proposal must be checked against both write and
external-update paths. Dependency propagation is a current invariant; rejected
transactional state and removed UI checks belong to the
[historical record](../history/brush-option-shared-model-development.md).
Preserve early investigations as evidence, not as a second current reference.

The [interface extraction](../../ui-modernization.md) distinguishes an initial
opt-in rollout from later application defaults. Check embedded defaults,
read fallbacks, import behavior and explicit saved values together; reading
only the original feature plan can give the wrong default. Keep a widget
style's geometry guarantee separate from an independent option that changes
layout, and retain the actual display-scale scope of visual verification.

The [profile extraction](../../settings-location.md) keeps a stable reference
path while moving the entire mixed source into history. Migration guarantees
must be traced through callers as well as low-level helpers: cleanup scope,
custom shared resources, platform-specific prompts and different-drive path
fallbacks qualify broad claims such as "one folder" or "everything copied is
removed." Preserve these boundaries instead of turning implementation intent
into an unconditional guarantee.

The [CPU effects](../../cpu-effects.md) and [G'MIC](../../gmic.md) extraction
separates components by maintenance responsibility, even when they shared one
implementation plan. Check release/build records as well as that plan: the
later cloud-package check supersedes an earlier "unverified" statement without
establishing GUI runtime coverage. Keep pixel/ABI compatibility in current
references and dated timing tables in history; buffer accounting and converter
microbenchmarks are not end-to-end speed or process-memory guarantees.

The [illustration-filter extraction](../../illustration-filters.md) retains
shared-helper dependency rules as well as visible controls: numerical rebasing
can require a larger halo than the visual kernel. Avoid simplifying that away
when summarizing algorithms. Preserve different alpha/LOD rules per feature,
and distinguish queued widget callbacks from worker-thread execution. Archiving
a delivered implementation's plan does not complete its pending artwork/UI
verification or authorize an optional control from the proposal.

The subsequent consistency audit found stale claims outside the extracted
plans: the [trial-build guide](../../../test-builds.md) still excluded Python
plugins, and [Color Reference](../../color-reference.md) described no callbacks
despite its later deferred file-drop import. Follow cross-document consumers
and later implementation changes, not only the original plan. Scope
[dependency observations](../concepts/qt-numeric-and-geometry.md) to the build
and date they describe; local measurements do not identify CI dependencies.

The [automated checks](../../documentation.md#automated-checks) make navigation
and wiki schema validation reproducible in the repository and on PRs. Parsing
Markdown/YAML found malformed source strings and inconsistent related-page
paths that the earlier regular-expression checks missed. Run on every PR so
deleting a referenced source or image is covered even without a Markdown edit.
Fixtures must exercise both deliberate failures and valid code examples;
machine validation still cannot establish feature behavior or manual acceptance.
