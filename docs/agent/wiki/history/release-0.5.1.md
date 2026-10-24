---
type: history
status: released and verified 2026-10-24 JST
sources:
  - 00153a6357891087d89b42b4ff9d3560e84de29c
  - https://github.com/udtren/Solstice/actions/runs/40076396030
  - https://github.com/udtren/Solstice/releases/tag/v0.5.1
  - https://github.com/udtren/Solstice/pull/32
  - https://github.com/udtren/Solstice/pull/33
  - https://github.com/udtren/tidalboxstudio-web/pull/13
  - https://github.com/udtren/tidalboxstudio-web/pull/14
---

# Solstice 0.5.1 release verification

Published 2026-10-24 at 09:17:01 UTC (18:17:01 JST) as a regular release,
marked latest. The user requested the release as 0.5.1 and chose to include
the Quick Access icon-span fix (PR #32). It extends Lazy Tools: Fast Image
Export registered folders, an export Name, Export Region, the Export icon on
the action, and the Rename Layer Presets editor in General > Custom, with an
illustrated guide and Simplified Chinese strings. The feature references
([Lazy Tools](../../lazy-tools.md), [Quick Access](../../quick-access.md))
own the implementation and regression checklists.

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Build and artifacts

- Exact tag/build commit: `00153a6357891087d89b42b4ff9d3560e84de29c`, the
  main merge of PR #33 (Lazy Tools, translations and version preparation),
  after the merge of PR #32.
- Run 40076396030 (number 26) passed configuration, compilation,
  installation, CPU G'MIC runtime build, packaging and artifact upload in
  1 h 9 min 29 s. `BUILD-INFO.txt`: version 0.5.1, the commit above, GPU
  engine compiled, G'MIC CPU runtime 3.7.4.1.
- Both downloaded ZIPs matched `SHA256SUMS.txt` and passed full CRC checks:
  5,314 application entries and 220 debug entries.
- Present: `Solstice.exe`, `bin/solstice.exe`, `bin/solstice.com`,
  `libkritaui.dll`, `libkritaimage.dll`, `libkritagpu.dll`, Python/PyQt, QML,
  G'MIC files and `shellex/register-thumbnails.cmd`. The packaged
  `kritamenu.action` gives `fast_image_export` the `document-export` icon,
  and the zh_CN catalog (`bin/locale/zh_CN/LC_MESSAGES/krita.mo`) contains
  the new Lazy Tools strings.
- The packaged dependency lock has CRLF; the Git blob uses LF. Content matches
  the built revision after line-ending normalization, including parsed JSON
  equality.
- Windows metadata: ProductName `Solstice`, file/product version
  `0.5.1 (git 00153a6)`, numeric version `0.5.1.0`.
  `solstice.com --version` with empty `SOLSTICE_PROFILE_ROOT`,
  `SOLSTICE_LEGACY_CONFIG_DIR` and `SOLSTICE_LEGACY_RESOURCE_DIR` folders
  printed `krita 0.5.1 (git 00153a6)` with empty stderr. The interactive
  application was not launched.
- The release was created as a draft; all five uploaded assets matched the
  verified files' SHA-256 and size before it was published. The published
  tag resolves to the exact built commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-0.5.1-windows-x64-00153a635789.zip` | 418297304 | `5299ddaf57118c3108a867b27db2a7681b33fb8d942abc6eb78416beb83f92e0` |
| `Solstice-0.5.1-windows-x64-00153a635789-dbg.zip` | 12378510 | `2e672da5d860c1d754b10e9e6d3567fe2d5b5d61939f913efaba988a8e65f2fe` |

## Translations

`update_translations.py` found 24 new or changed strings (14 fuzzy, 10
untranslated), all from the Lazy Tools work; all were translated, and the
catalog has 10,536 translated entries with no fuzzy or untranslated ones.
The run exposed an indented second line in the Rename Layer Presets
placeholder (an editing slip in the `.ui` file), fixed before the build.
`&#10;` is not a usable newline in `.ui` strings: the extractor keeps it
literally, so the catalog key would not match `uic`'s decoded text.

## Website

The Lazy Tools pages (`/lazy-tools/`, `/zh-cn/lazy-tools/`), the home cards'
new cover and link, and version 0.5.1 were merged into `develop`
(tidalboxstudio-web #13) after the release existed, and published through
`main` (#14, `ca1197a`); the Cloudflare production build succeeded. The live
home, Download and Lazy Tools pages return 200 in both languages and link to
v0.5.1, and the sitemap lists the new pages.

## Verification boundary

Local tests passed: `KisLazyToolsTest` (20 cases, including real PNG/JPEG and
Export Region exports) and the Quick Access suites of PR #32 (seven). The
user used the new export dialog in the application and made the guide's
screenshots with Export Region. The Quick Access fix and the remaining Lazy
Tools manual checks (9-12 in the feature reference) are not yet confirmed in
the running application. The cloud release run did not execute native
regression suites, interactive painting or GPU runtime tests.
