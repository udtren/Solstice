---
type: decision
updated: 2026-10-16
sources:
  - docs/agent/versioning.md
  - User decision 2026-10-16 (release 0.3.0 without Beta)
  - plugins/impex/libkra/kra_converter.cpp ("kritaVersion" written and read)
  - libs/resources/KisResourceLocator.cpp (KRITA_RESOURCE_VERSION comparison)
related:
  - gpu-phase-numbering.md
---

# Solstice version separate from the Krita compatibility version

**Decision (user, 2026-10-07).**

- Solstice shows its own version: pre-1.0 semantic versioning with a stage
  label when designated as a prerelease, starting at `0.1.0-alpha`.
- On 2026-10-16 the user selected `0.3.0` with an empty label for a regular
  GitHub Release, superseding the earlier expectation that unlabeled releases
  would begin at 1.0.0. This does not expand hardware support or remove
  feature-specific experimental limitations.
- The upstream-derived `KRITA_VERSION_STRING` (`6.0.5-prealpha`) stays as an
  internal compatibility version.

**Why not simply replace the number.**

- The Krita version is also data:
  - `.kra` files store it, and loaders choose compatibility behavior from
    it;
  - the resource locator reinstalls bundled resources only when the version
    increases;
  - Python plugins compare `Krita.version()`.
- Replacing it with 0.1.0 would:
  - mark new files as coming from an ancient Krita;
  - stop resource updates until Solstice passed 6.0.5;
  - break plugins that check the major version.

**Alternatives considered.**

- Calendar versions (`2026.10`): they do not show the test stage.
- A Krita-derived scheme (`6.0.5-sol.1`): it contradicts the decision that
  Solstice is an independent application.

**How to apply.**

- Show `KritaVersionWrapper::solsticeVersionString()` wherever users see a
  version.
- Keep `versionString()` for formats, resources and scripting.
- Bump only the `SOLSTICE_VERSION_*` values (`docs/agent/versioning.md`).
