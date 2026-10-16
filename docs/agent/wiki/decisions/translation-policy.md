---
type: decision
updated: 2026-10-16
sources:
  - docs/agent/translations.md
  - dev-tools/translations/update_translations.py
related:
  - pitfalls/translation-extraction.md
  - decisions/upstream-selective-intake.md
---

# Translations: Solstice owns `krita.po`

Decided by the user on 2026-10-16, with Simplified Chinese (`zh_CN`) first.
Details and the work list: [translations.md](../../translations.md).

- **Solstice edits `po/<lang>/krita.po` directly.** Every Solstice string
  already goes through the `krita` domain; a separate `solstice` catalog
  would mean switching every Solstice `i18n()` call to another domain.
  `po/` is no longer "generated upstream, never edit".
- **AI translation only, stated in the release notes.** It is faster than
  waiting for a native reviewer; the note invites corrections.
- **Product and feature names stay in English.** They then match the website,
  the GitHub documents and screenshots.
- **No upstream translation updates.** Merging KDE's catalogs would conflict
  with Solstice's own entries and needs a review every time; strings that come
  with cherry-picked upstream code are translated here.
