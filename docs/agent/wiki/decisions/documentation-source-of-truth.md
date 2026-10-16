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
