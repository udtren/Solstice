---
type: decision
updated: 2026-10-07
sources:
  - docs/agent/docker-locks.md ("krita5.xmlgui is deliberately unchanged")
  - docs/agent/extension-points.md (menu placement options)
related: []
---

# `krita5.xmlgui` stays unchanged

**Decision.** New menu entries and settings are added without editing
`krita/krita5.xmlgui`. Options include a settings check box, a plugin
`.xmlgui` installed to `kritaplugins`, or a menu built in code.

**Why.** The user keeps a locally customized `krita5.xmlgui` in their
configuration. KXMLGUI prefers the local copy until the shipped file's
`version` attribute increases. Shipping a change therefore requires a version
bump, and that bump can discard the user's customizations. Recorded with the
docker locks feature (2026-10).

**How to apply.** If a feature seems to need a main-menu entry, use one of
the alternatives in `docs/agent/extension-points.md`, or ask the user before
touching `krita5.xmlgui`. Existing historical edits listed in
`feature-inventory.md` (Export Region, desktop-only cleanup) predate this
decision.
