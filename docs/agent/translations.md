# Translations (Simplified Chinese first)

Status: policy decided 2026-10-16; extraction and merge tooling done
2026-10-16 (`zh_CN` catalog merged); audit and fixes F1-F5 done
2026-10-16; translation not started.

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

## Strings outside the catalog (audit 2026-10-16)

Scope: C++ files added or changed relative to the upstream snapshot
(`git diff krita/6.0 main`, 265 added and 479 modified files, vendored
`plugins/visionml/thirdparty/` excluded; only the changed lines of modified
files). A statement-level scan removed `i18n*()` calls and listed the
sentence-like literals that were left (356), then each was classified by
where it ends up. Heuristic: literals built in unusual ways can be missed.

**Qt `tr()` / `QT_TRANSLATE_NOOP`:** no `tr()` or
`QCoreApplication::translate` in Solstice code. The three Solstice
`QT_TRANSLATE_NOOP` texts in `libs/widgetutils/config/kstandardaction_p.h`
("&Configure Solstice...", "Upstream Krita &Handbook", "&About Solstice")
are fine: `kstandardaction.cpp` translates labels with `i18nc("action", ...)`
and the strings are extracted from `krita/kritamenu.action`.

**User-visible, fixed 2026-10-16** (how: last column):

| # | Where | What | Fix |
| --- | --- | --- | --- |
| F1 | `krita/main.cc` `askImportKritaProfile()`, `prepareSolsticeProfile()` (before `QApplication`) | Krita-profile import prompt and two error boxes (`MessageBoxW`), built in as English/Japanese; `solsticeUseJapanese()` picks the language | Built in: `KisSolsticeProfile::builtInText()` picks English, Japanese or Simplified Chinese (`textLanguageFor()`: Krita override `ja`/`zh_CN`/`zh_SG`, else the Windows UI language); `solsticeUseJapanese()` removed |
| F2 | `krita/main.cc` `finishSolsticeImport()` (after `setApplicationDomain`) | progress dialog text, "import stopped" message (English/Japanese built in) | Same built-in texts as F1 (one language for the whole import, and the F3 details it shows are built in) |
| F3 | `libs/global/KisSolsticeProfile.cpp` `setError()` | 5 distinct English details (11 calls: "Could not create/copy/clear %1", "Could not write the profile marker", "Not enough free space...") shown inside F1/F2 messages | `builtInText()` (they can appear before `QApplication`); "Cancelled" too |
| F4 | `plugins/dockers/quickaccess/QuickAccessModel.cpp` (6), `QuickAccessProfileRepository.cpp:54` (1), `QuickAccessLegacyImporter.cpp:76,83` (2) | English details passed as `%1` into translated warnings | `i18n()`; `kritaquickaccesscore` links `KF6::I18n` |
| F5 | `plugins/visionml/src/VisionML.cpp:308,410`, `inpaint/InpaintTool.cpp:107` | `std::runtime_error` texts appended to the translated "Error during image processing:" box (`VisionMLErrorReporter::showError`); ONNX Runtime errors stay English | `i18n()` at the throw (`e.what()` is UTF-8; `QString(const char *)` decodes it) |

**User-visible, English upstream too (low priority):** the root launcher's
start failure (`krita/windows_root_launcher.cpp:99`, before any Qt), the
animation render timeout (`libs/ui/animation/KisAnimationRender.cpp:196`;
upstream had the same literal, Solstice only renamed Krita), the assert box
(`libs/global/kis_assert.cpp:43`), the usage-log warning
(`plugins/extensions/buginfo/DlgKritaLog.cpp:30`).

**Not user-visible (no action):** usage-log and system-information lines
(`KisBrushStrokeLayer.cpp`, `KisUsageLogger.cpp`, `main.cc` sysinfo), style
sheets, SQL, debug output, GPU engine internal errors (logged; the GPU
settings and failure dialogs in `libs/ui/KisGpuEngineUi.cpp` use `i18n()`),
ABR parser warnings (`warnKrita` in `kis_abr_brush_collection.cpp`), Quick
Access layout issues and legacy-import warnings (never displayed),
`std::length_error` in `KisCpuPlaneBlur.cpp` and `kis_qmic_interface.h`
(caught and logged), internal object names.

**Outside `krita.po`:**

- G'MIC-Qt translates its own interface: `krita_gmic_qt.dll` embeds
  `zh.qm` (and `ja.qm`, `zh_tw.qm`) from G'MIC-Qt's `translations.qrc`. Filter
  name translations (`translations/filters/zh.qm`) are in G'MIC-Qt's
  `wip_translations.qrc`; check in the running application whether they are
  built in and enabled.
- Qt's own texts (standard buttons, file dialogs) come from
  `_install/translations/qtbase_zh_CN.qm`, installed by `installQtTranslations()`
  in `krita/main.cc`.
- The NSIS installer is English only (`MUI_LANGUAGE "English"` in
  `packaging/windows/installer/installer_solstice.nsi`); the MSIX manifest
  texts are English. The three Python plugin `Manual.html` files are
  English only, as upstream.
- 26 tracked `*_ui.py` files (pyuic output from `.ui` forms, for example
  `libs/ui/forms/wdggeneralsettings_ui.py`) use Qt's `_translate()`, which
  the extraction does not read. They are generated companions of the forms
  and are not loaded by the application (`docs/agent/puppet-warp.md` keeps
  `wdg_tool_transform_ui.py` synchronized on purpose); the strings come from
  the `.ui` files.

## Rules for translating

- Follow the terms already used in `po/zh_CN/krita.po` (for example 图层,
  笔刷预设, 滤镜).
- Keep `kde-format` placeholders (`%1`), accelerators (`&`), rich-text markup
  and `msgctxt` intact; `Plural-Forms` is `nplurals=1`.
- Check the catalog with `msgfmt --check` (`_install/bin/msgmerge.exe` and
  the gettext tools ship with the dependencies).

## Remaining work

1. Tooling: done (see "Updating the catalogs").
2. Audit and fixes F1-F5: done (see "Strings outside the catalog").
3. Translate in batches (shared menus and messages, Quick Access, filters,
   Rest Note/Asset Library/Vision ML, GPU/Layer Comps/Transform), building,
   installing and checking each batch in the interface.
4. Add "update the template and translate" to the hand-off checklist in
   `development-workflow.md`; list the language in `README.md` and the
   release notes.
