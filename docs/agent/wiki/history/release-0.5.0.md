---
type: history
status: released and verified 2026-10-21 JST
sources:
  - 49697aadce6cce8c4b826c372045f8bf066a63fc
  - https://github.com/udtren/Solstice/actions/runs/39566003093
  - https://github.com/udtren/Solstice/releases/tag/v0.5.0
  - https://github.com/udtren/Solstice/pull/30
  - https://github.com/udtren/tidalboxstudio-web/pull/10
  - https://github.com/udtren/tidalboxstudio-web/pull/11
---

# Solstice 0.5.0 release verification

Published 2026-10-21 at 09:31:16 UTC (18:31:16 JST) as a regular release,
marked latest. The user requested the release, chose version 0.5.0 and
approved merging PR #30 without review. It adds the Remove Dust tool and
filter; the [feature reference](../../remove-dust.md) owns the implementation
and regression checklist.

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Build and artifacts

- Exact tag/build commit: `49697aadce6cce8c4b826c372045f8bf066a63fc`, the
  main merge of PR #30 (Remove Dust and version preparation).
- Run 39566003093 (number 25) passed configuration, compilation,
  installation, CPU G'MIC runtime build, packaging and artifact upload in
  1 h 23 min 51 s. `BUILD-INFO.txt`: version 0.5.0, the commit above, GPU
  engine compiled, G'MIC CPU runtime 3.7.4.1.
- Both downloaded ZIPs matched `SHA256SUMS.txt` and passed full CRC checks:
  5,314 application entries and 220 debug entries.
- Present: `Solstice.exe`, `bin/solstice.exe`, `bin/solstice.com`,
  `libkritaimage.dll`, `libkritagpu.dll`, the `kritatoolencloseandfill` and
  `kritasolsticeeffectsfilter` plugins, `KisToolRemoveDust.action`, Python/PyQt,
  QML, G'MIC files and `shellex/register-thumbnails.cmd`. The zh_CN catalog
  (`bin/locale/zh_CN/LC_MESSAGES/krita.mo`) contains the new Remove Dust strings.
- The packaged dependency lock has CRLF; the Git blob uses LF. Content matches
  the built revision after line-ending normalization, including parsed JSON
  equality (as in 0.4.0).
- Windows metadata: ProductName `Solstice`, file/product version
  `0.5.0 (git 49697aa)`, numeric version `0.5.0.0`.
  `solstice.com --version` printed `krita 0.5.0 (git 49697aa)` with empty
  stderr. The interactive application was not launched.
- All five uploaded assets matched local SHA-256 and size before publishing.
  The published tag resolves to the exact built commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-0.5.0-windows-x64-49697aadce6c.zip` | 418272638 | `1e018e59e6cf81b86f40062e66ad7c4712b075b05bfe94452319235dab0fca53` |
| `Solstice-0.5.0-windows-x64-49697aadce6c-dbg.zip` | 12374017 | `e48c47a967b84450d9058300d1c43a253d6645c6dbebf6a45b57b8262bd7f35f` |

## Pitfall: the version check needs every profile input isolated

The first `solstice.com --version` attempt set only `SOLSTICE_PROFILE_ROOT`.
Startup found the real Krita profile and waited on the native import prompt,
so the check never finished. The scratch process was stopped by its package
path. Set `SOLSTICE_PROFILE_ROOT`, `SOLSTICE_LEGACY_CONFIG_DIR` and
`SOLSTICE_LEGACY_RESOURCE_DIR` to empty scratch folders before running it
([Settings location](../../settings-location.md)).

## Website

The Remove Dust pages (`/remove-dust/`, `/zh-cn/remove-dust/`), home cards
and version 0.5.0 were merged into `develop` (tidalboxstudio-web #10) and
published through `main` (#11, `98bc29c`). The live Download pages link to
v0.5.0, and the new pages return 200 in both languages.

## Verification boundary

Local tests passed: `KisDustDetectorTest` (14 cases) and
`KisRemoveDustFilterTest` (7). The user confirmed removing specks with the
tool's rectangle, and supplied the illustration. The filter, Select and the
rest of the manual checklist are not yet confirmed. The cloud release run did
not execute native regression suites, interactive painting or GPU runtime tests.
