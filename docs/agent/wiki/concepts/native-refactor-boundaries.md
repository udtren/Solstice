---
type: concept
updated: 2026-10-16
sources:
  - docs/agent/asset-library.md
  - docs/agent/quick-access.md
  - docs/agent/rest-note.md
  - docs/agent/lazy-tools.md
  - docs/agent/brush-stroke-layer-plan.md
related:
  - ../pitfalls/build-format-test.md
---

# Native refactor boundaries

The 2026-10-16 maintenance work separates responsibilities while retaining
the native architecture and legacy configuration contracts. Feature documents
are the authoritative source for ownership and tests.

- Extract shared services when callers really use the same behavior (Asset
  Library settings/browser/operations). Preserve caller-specific presentation
  and legacy fallback differences, as in palette versus Grid Edit aliases.
- Keep mutation, saving and rebuilding with the coordinator; input controllers
  own temporary interaction state. Synchronous callbacks can obtain the current
  grid or canvas without retaining pointers into a mutable document.
- Delete controllers with callbacks into owner members before those members
  are destroyed. Bind deferred work to a QObject context so destruction cancels
  it; rebuild after mouse release returns, not inside delivery to a widget
  that rebuilding will delete.
- Cohesive implementation files can retain the same class and ownership when
  introducing another owner would add lifecycle risk (brush stroke replay,
  Quick Adjust history, palette rendering and popup construction).
- Compare moved method bodies and literal tokens, then test configuration round
  trips, cancel paths and destruction. Build/install checks complement actual
  user verification of native input, focus, rendering and window behavior.

All affected features were built, tested, installed and confirmed by the user
before the maintenance release was requested. Logs and individual test scope
are recorded in their feature documents.
