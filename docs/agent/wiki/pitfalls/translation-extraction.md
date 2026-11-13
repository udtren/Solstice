---
type: pitfall
updated: 2026-11-02
sources:
  - dev-tools/translations/update_translations.py
  - dev-tools/translations/perl/Archive/Zip.pm
  - action_i18n.pl
  - extracti18n.pl
  - Messages.sh
  - libs/global/KisSolsticeProfile.cpp
  - docs/agent/translations.md
  - "libs/widgetutils/kis_action_registry.cpp (quietlyTranslate())"
related:
  - ../decisions/translation-policy.md
---

# Translation extraction on Windows and Linux

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
- **Linux gettext wraps differently** (found 2026-11-02, U-2026W45-05).
  `update_translations.py` runs on Linux with the system tools, but its
  `msgmerge` breaks long msgids at other points (`...bundle:brushes/` and
  `3_flat-brush.png` on separate lines), so `po/zh_CN` got about 1,300
  unrelated changed lines and `po/ja`, never merged by the tool, was
  reordered completely (59,000 lines). Use the run only to see which
  entries are new: take them from the template or the fuzzy entries of the
  merged copy and add them to the committed catalog by hand, or run the
  tool on Windows. The Linux extraction also kept a tab in the
  `swatche.gpl` palette name ("Swatches\t") that the Windows run drops.
- **Do not merge `po/ja` with the tool, on Windows either** (2026-11-13,
  Show in File Explorer). `--lang zh_CN ja` on Windows also reordered the
  whole `po/ja/krita.po` (about 59,000 changed lines): it was never merged
  by the tool, so its entry order is not the tool's. Merge `zh_CN` only,
  restore `po/ja` from Git, and add its new entries by hand next to a
  related entry, with the references the `zh_CN` merge produced.
- **Moving a text into an `.action` file keeps its translation.**
  `quietlyTranslate()` in `kis_action_registry.cpp` looks the text up with
  the `action` context first and falls back to `i18n()` without context, so
  tooltips that upstream moved from code into `krita.action` (U-2026W45-05)
  still show the old translation. Only texts that are new in the `.action`
  file need entries; the template still lists every moved text with the
  `action` context, and the next Windows run turns them into fuzzy entries,
  so add them with the reused translation.
