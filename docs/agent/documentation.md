# Documentation maintenance

## Canonical sources

Solstice's documentation source stays in this repository (user decision,
2026-10-16). Review implementation and its documentation in the same feature
branch and pull request. A future documentation site may render these sources;
it must not introduce another hand-maintained copy of the same guide.

| Document | Owns | Does not own |
| --- | --- | --- |
| `AGENTS.md` | Global policy and a stable technical-document index | Feature progress, phase logs, test-result copies |
| `README.md` | Project overview, latest published release, representative benchmarks and links | Detailed feature contracts or a release archive |
| `docs/README.md` and `docs/*.md` | User navigation, workflows, supported scope and visible limitations | Implementation instructions and development logs |
| `docs/agent/<feature>.md` | Current architecture, compatibility, lifecycle, source map, checks and verification status | Unmarked obsolete proposals |
| Feature plans | Proposed scope, unresolved decisions and work still to do | Authority over an implemented contract |
| `feature-inventory.md` | Custom source touch points and shared-file hazards | A second feature-status tracker |
| `wiki/concepts`, `wiki/pitfalls` | Reusable knowledge spanning features | Copies of feature specifications |
| `wiki/decisions` | A dated decision and its reasons, linked to the rule that implements it | A conflicting second set of operational instructions |
| `wiki/benchmarks` | Measurements with workload, environment, date and evidence | Undated general performance promises |
| `wiki/history`, archived plans and handoffs | What was proposed, observed or verified at a particular time | Current instructions or new work authorization |
| `wiki/log.md` | Chronological summaries and links to changed knowledge | Full copies of results or the current backlog |
| `todo.md` | User-registered work not yet started | Completed work or inferred new tasks |

`AGENTS.md` owns global policy. The feature's maintained document owns its
implementation contract. User guides describe that contract for users; they
do not independently define different behavior. Wiki summaries and indexes
link to the owner. If a source and a summary disagree, check the code and the
recorded user decision, correct the stale text, and retain historical evidence
as history. A recent edit date alone does not settle a conflict.

## Reading and document lifecycle

Start at [the technical index](../../AGENTS.md#agent-technical-document-index)
and [the wiki index](wiki/index.md). The [agent guide](README.md) identifies
maintained references, plans and archived records.

- A new plan states its scope and approval/implementation status. Separate user
  decisions from implementation choices; a proposal does not authorize work.
- As work lands, maintain the actual contract and its tests. Record automated
  validation and interactive confirmation separately, with dates and evidence.
  Do not turn "built" into "user-verified", or infer release inclusion from
  a feature branch. Link the release/commit when that distinction matters.
- When a plan is superseded, name and link its maintained successor at the top.
  Preserve useful design rationale and measurements. Retain an old-path pointer
  when moving a document, and repair incoming links and heading references.
- When a feature reference still mixes current rules with development history,
  its dated investigation/proposal sections are historical; explicit implementation
  results and later corrections establish what was delivered. Before changing
  such a feature, read the complete reference, not just its opening proposal.
- When extracting a maintained reference from a mixed document, carry over
  configuration and serialization contracts, ownership/threading/undo rules,
  fallbacks, targets, tests, manual checks and outstanding verification. Mark
  the old document historical only after that extraction is complete. Do not
  mark the entire mixed document obsolete while it still owns current rules.
- Archive a handoff after its task context expires. Branches, PIDs, temporary
  files, "next steps" and old permissions in an archive are historical data.

## Granularity and navigation

Split documents by responsibility and reader task, not by an arbitrary line
limit. Keep a feature's current contract together when it is normally changed
and reviewed together. Put detailed measurement runs and completed phase logs
in history; put reusable lessons in the wiki and link their evidence.

The technical index uses feature names, stable descriptions and links. It must
not repeat completion dates, active work branches or per-phase progress. The
feature document holds verification status; GPU work order belongs in
`gpu-work-priorities.md`. The inventory lists source locations rather than
repeating these statuses. History may retain dated snapshots of old indexes.

Keep ordinary Markdown links usable in the repository. Do not add theme-specific
components, a site generator, or a second documentation repository as part of
routine document maintenance. The product website owns introductions and
downloads and links to the guides; its release procedure is in
[Versioning](versioning.md#releasing).

## Change and review checklist

1. Determine the owner of each changed fact. Update it and repair conflicting
   summaries; prefer replacing repeated facts with links.
2. Keep behavior, user guidance and implementation notes in the same PR when
   behavior changes. A documentation-only correction does not imply a new
   build, installation or interactive verification.
3. Check relative links, images and heading fragments, plus indexes and wiki
   metadata. Keep original archival bodies intact when adding provenance or
   relocation notices. Do not silently rewrite historical measurements.
4. Run `git diff --check` on the changed files and inspect the rendered Markdown
   where layout or navigation changed. Repository-wide symbol/behavior checks
   require code review; a successful link check does not prove accuracy.
5. Record the resulting decision or reusable lesson in the wiki and append a
   concise entry to `wiki/log.md`, linking the authoritative document.

For releases, update all download links in README together and follow
[the release procedure](versioning.md#releasing). Keep the latest-release
summary short; older release notes remain in GitHub Releases and the version
ledger. A future published guide must distinguish released and development
behavior instead of presenting every merge as already shipped.
