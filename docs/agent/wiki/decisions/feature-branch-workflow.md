---
type: decision
updated: 2026-10-16
sources:
  - AGENTS.md (Branch policy)
  - docs/agent/development-workflow.md
  - docs/agent/versioning.md (Releasing)
related:
  - release-feature-documentation.md
---

# Feature branches before main

The user changed Solstice's branch policy on 2026-10-16: work on a feature
branch, push it, then merge into main. Direct pushes to main are no longer
allowed. Codex uses `codex/<topic>` branches and pull requests, making the
proposed change and its validation reviewable before integration.

Main remains the default integration and release branch. Release preparation
and post-release documentation use the same workflow. Build the merged release
commit and publish its verified artifacts. The website keeps its existing
develop-to-main publishing workflow; see the release procedure for ordering.

The native-tools maintenance release is the first task using this policy.
Historical Krita branches and the discontinued upstream synchronization policy
are unaffected.
