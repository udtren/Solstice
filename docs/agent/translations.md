# Translations (Simplified Chinese first)

Status: policy decided 2026-10-16; extraction and merge tooling done
2026-10-16 (`zh_CN` catalog merged); audit and translation not started.

## Decisions (user, 2026-10-16)

| Topic | Decision |
| --- | --- |
| Catalog | Solstice maintains `po/<lang>/krita.po` directly. No separate `solstice` domain; every Solstice string keeps going through the `krita` domain. |
| Translator and review | AI translation only. Release notes state that the Simplified Chinese interface is machine-translated and that corrections are welcome. |
| Feature names | Product and feature names stay in English (Solstice, Quick Access, Rest Note, Asset Library, Vision ML, Puppet Warp, Layer Comps, Color Reference, Brush Stroke Layer, filter names added by Solstice). Descriptions, labels, buttons and messages are translated. |
| Upstream translation updates | Not taken. The current `krita.po` is the starting point and Solstice maintains it alone; upstream intake does not consider `po/` changes. Strings arriving with cherry-picked upstream code are translated here. |

The first target is `zh_CN`. `zh_TW` and other languages are not scheduled.

## Starting point (Codex count, 2026-10-16)

Against the strings extracted from `main`, `zh_CN` needs about 750 entries
(749 missing from the catalog, 1 empty; about 5,100 English words): Quick
Access 211, Solstice filters 127, Rest Note 38, Asset Library 34, Vision ML
31, GPU settings and status 24, Layer Comps 16, Transform Tool and Puppet
Warp 15, and the rest in settings, menus, brushes and strings renamed from
"Krita" to "Solstice". Not counted: G'MIC itself, manuals, strings that do
not go through `i18n()`.

## Updating the catalogs

`dev-tools/translations/update_translations.py` replaces `Messages.sh` and
the KDE extraction infrastructure it expects. Run it after source strings
change:

```bat
python dev-tools\translations\update_translations.py --gettext-dir <krita-dev-root>\_install\bin
```

Options: `--lang zh_CN ja` (catalogs under `po/`, default `zh_CN`),
`--pot <path>` (keep the template somewhere specific; by default it stays
in a `solstice-i18n-*` temporary directory), `--pot-only` (do not touch
`po/`), `--perl <exe>`. `SOLSTICE_GETTEXT_DIR` can replace
`--gettext-dir`. Perl defaults to `perl` on `PATH`, then Git for Windows'
`usr\bin\perl.exe`. A run takes about 10 seconds.

What it does, in `Messages.sh` order:

1. Sources are the **tracked** files (`git ls-files`); `git add` a new
   file before running, or its strings are missed. Exclusions follow
   `Messages.sh` (`/tests/`, `sdk/templates/`, `krita/crashreporter/`, the
   Sketch and Gemini xmlgui files).
2. Writes a temporary `rc.cpp` in the repository root (refuses to run if
   one exists) from `.ui` and `.xmlgui` files, `.action` files
   (`action_i18n.pl --context=action`) and resource names
   (`extracti18n.pl`), with carriage returns removed (CRLF checkouts).
3. `xgettext` with the KDE keywords (`i18n*`, `ki18n*`, `xi18n*`,
   `kli18n*`, `I18N_NOOP*`, `tr2i18n`), a second pass for `kundo2_i18n*`
   whose entries get the `(qtundo-format)` context, merged with `msgcat`
   (as `kundo2_aware_xgettext.sh`), then the Python files (`i18n*`).
4. Turns `i18n: file: x:N` comments into `#:` references and drops `rc.cpp`
   references, then orders entries like the KDE catalogs (unreferenced
   resource names first in extraction order, then by first reference).
5. `msgmerge --previous` into `po/<lang>/krita.po`, then `msgfmt --check
   --statistics`; the catalog is replaced only if both succeed.

Implementation notes:

- `action_i18n.pl` only treats `context=` as the gettext context. For `.ui`
  and `.xmlgui` the script writes a copy that also uses `comment=` (Qt's
  disambiguation), which the KDE catalogs rely on (`msgctxt "EXIF"`); it
  stops if the patched line in `action_i18n.pl` changes.
- Git for Windows' Perl lacks `Archive::Zip`, which `extracti18n.pl` uses
  for `.bundle` files. `dev-tools/translations/perl/` holds a minimal
  stand-in (central-directory reader with `Compress::Raw::Zlib`; stored and
  deflated members), added with `-I` only when the real module is missing.
  `IO::Uncompress::Unzip` cannot read the SeExpr bundle (data descriptors).
- `xgettext` warns about unterminated strings in
  `plugins/dockers/textproperties/qml/TextPropertyBase.qml:66,72`; harmless.
- The upstream entry order cannot be reproduced exactly, so the first run
  reordered `po/zh_CN/krita.po` (about 30,000 changed lines, mostly
  movement and reference line numbers). Later runs change only
  `POT-Creation-Date` when sources are unchanged.
- The header's `Report-Msgid-Bugs-To` now names the Solstice issues page.

First merge (2026-10-16, `zh_CN`): template 10,420 entries; 9,664
translated (every previous translation kept unchanged), 577 fuzzy, 179
untranslated, 158 obsolete (`#~`). The 276 strings that left the template
belong to removed features (Resource Manager, S Pen settings, Android menus,
brush HUD, supporter links) or to text renamed from "Krita" to "Solstice";
118 of them became the previous text (`#|`) of fuzzy entries. Fuzzy entries
are not used at runtime, and many of the fuzzy matches are wrong (for
example "Digital painting application" got the translation of "Quit
application"), so translate every fuzzy entry; do not just remove the flag.
`ninja pofiles` builds the catalog.

## Rules for translating

- Follow the terms already used in `po/zh_CN/krita.po` (for example 图层,
  笔刷预设, 滤镜).
- Keep `kde-format` placeholders (`%1`), accelerators (`&`), rich-text markup
  and `msgctxt` intact; `Plural-Forms` is `nplurals=1`.
- Check the catalog with `msgfmt --check` (`_install/bin/msgmerge.exe` and
  the gettext tools ship with the dependencies).

## Remaining work

1. Tooling: done (see "Updating the catalogs").
2. Audit: `libs/widgetutils/config/kstandardaction_p.h` marks the standard
   action texts (including "Configure/About Solstice") with
   `QT_TRANSLATE_NOOP`, which the KDE extraction does not read, so they are
   not in `krita.po`; check where they are translated. Solstice strings that bypass `i18n()` (literals, Qt `tr()`); the
   built-in English/Japanese startup dialog in `krita/main.cc`
   (`solsticeUseJapanese()`) needs a Simplified Chinese branch; decide on
   G'MIC filter names, Python plugin `Manual.html`, the NSIS installer and
   MSIX texts.
3. Translate in batches (shared menus and messages, Quick Access, filters,
   Rest Note/Asset Library/Vision ML, GPU/Layer Comps/Transform), building,
   installing and checking each batch in the interface.
4. Add "update the template and translate" to the hand-off checklist in
   `development-workflow.md`; list the language in `README.md` and the
   release notes.
