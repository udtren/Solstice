---
type: pitfall
updated: 2026-10-16
sources:
  - dev-tools/translations/update_translations.py
  - dev-tools/translations/perl/Archive/Zip.pm
  - action_i18n.pl
  - extracti18n.pl
  - Messages.sh
  - libs/global/KisSolsticeProfile.cpp
  - docs/agent/translations.md
related:
  - ../decisions/translation-policy.md
---

# Translation extraction on Windows

Found while writing `update_translations.py`
([translations.md](../../translations.md)). Measure a new extraction by
comparing its template with the existing catalog: with the right setup, only
real new and removed strings differ (755 new, 276 gone on 2026-10-16).

- **`action_i18n.pl` is not plain extractrc.** It only takes `context=` as the
  gettext context. KDE's catalogs use the `.ui` `comment=` attribute as the
  context too (`msgctxt "EXIF"`); without it about 100 `.ui` strings looked
  new and lost their translations.
- **CRLF checkouts break resource names.** `extracti18n.pl` copied the `\r`
  of `.ggr`/`.gpl` lines into the strings ("unterminated string" warnings,
  wrong msgids). Strip `\r` from `rc.cpp`.
- **Git for Windows' Perl lacks `Archive::Zip`**, and the core
  `IO::Uncompress::Unzip` cannot read members with data descriptors (the
  SeExpr bundle: 46 names silently missing). The stand-in reads the central
  directory instead.
- **KDE fixes up references after xgettext**: `// i18n: file: x.ui:12` becomes
  `#: x.ui:12` and `rc.cpp` references are dropped. Sorting the template
  before that fix-up reordered the whole catalog.
- **`QT_TRANSLATE_NOOP` is not extracted** (`kstandardaction_p.h`), but the
  standard action labels are still translated: `kstandardaction.cpp` looks
  them up with `i18nc("action", ...)` and the same texts are extracted from
  `krita/kritamenu.action`. Change both when renaming one.
- **Texts shown before `QApplication` cannot use `i18n()`** (the Krita-profile
  import in `krita/main.cc`, and the `KisSolsticeProfile` errors it shows).
  They are built in per language with `KisSolsticeProfile::builtInText()`;
  adding a language means adding it there (audit 2026-10-16,
  [translations.md](../../translations.md)).
- **Error details are easy to miss:** a translated dialog that shows `%1`
  from a model or an exception (`std::runtime_error`) stays partly English.
  Wrap the detail with `i18n()` where it is made; `e.what()` carries UTF-8
  (`QString(const char *)` decodes it).
- **`msgfmt -o nul` fails on Windows** ("Invalid argument"); write the check
  output to a temporary `.mo`.
