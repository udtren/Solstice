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
