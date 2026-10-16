---
type: decision
updated: 2026-10-16
sources:
  - README.md (What's new and Highlights)
  - docs/README.md (Effects and color)
  - docs/agent/versioning.md (Releasing)
related:
  - versioning.md
---

# Keep release news short and feature guides stable

The user approved a short latest-release section in README on 2026-10-16.
Use `In development` before publication, then replace it with the actual
release version and link after the release exists. Keep only the newest
release summary in README; older version history belongs in GitHub Releases.

Highlights is a lasting feature index. Group the illustration filters under
Effects & Color, with Layer Comps and G'MIC as separate entries. User workflows
and limits live in `docs/`; the website provides illustrated summaries and
links to those guides rather than becoming another specification.

Release source, package and website changes in order: build the pushed source,
verify the resulting ZIPs and checksums, publish the GitHub Release (prerelease
only when the version has a stage label), then
update README's release wording and publish the website's matching download
link. See the release procedure in `docs/agent/versioning.md`.
