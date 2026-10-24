---
type: history
status: released and verified 2026-10-24 JST
sources:
  - e352f1c51963df372a5344f2fc28a6cb96759d91
  - https://github.com/udtren/Solstice/actions/runs/40091292555
  - https://github.com/udtren/Solstice/releases/tag/v0.5.2
  - https://github.com/udtren/Solstice/pull/35
  - https://github.com/udtren/tidalboxstudio-web/pull/15
  - https://github.com/udtren/tidalboxstudio-web/pull/16
---

# Solstice 0.5.2 release verification

Published 2026-10-24 at 13:28:58 UTC (22:28:58 JST) as a regular release,
marked latest. The user found that Fast Image Export's PNG options dialog
reset **Force convert to sRGB** every time, confirmed the fix in the
application and asked to release it as 0.5.2 with the website update. Fast
Image Export now starts from the format's remembered export configuration;
the [feature reference](../../lazy-tools.md) owns the contract and checks.

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Build and artifacts

- Exact tag/build commit: `e352f1c51963df372a5344f2fc28a6cb96759d91`, the
  main merge of PR #35 (the fix and version preparation).
- Run 40091292555 (number 27) passed configuration, compilation,
  installation, CPU G'MIC runtime build, packaging and artifact upload in
  1 h 35 min 29 s. `BUILD-INFO.txt`: version 0.5.2, the commit above, GPU
  engine compiled, G'MIC CPU runtime 3.7.4.1.
- Both downloaded ZIPs matched `SHA256SUMS.txt` and passed full CRC checks:
  5,314 application entries and 220 debug entries.
- Present: `Solstice.exe`, `bin/solstice.exe`, `bin/solstice.com`,
  `libkritaui.dll`, `libkritaimage.dll`, `libkritagpu.dll`, Python/PyQt, QML,
  G'MIC files, the zh_CN catalog with the 0.5.1 Lazy Tools strings, the
  Fast Image Export icon in `kritamenu.action` and
  `shellex/register-thumbnails.cmd`.
- The packaged dependency lock has CRLF; the Git blob uses LF. Content matches
  the built revision after line-ending normalization, including parsed JSON
  equality.
- Windows metadata: ProductName `Solstice`, file/product version
  `0.5.2 (git e352f1c)`, numeric version `0.5.2.0`.
  `solstice.com --version` with empty profile and legacy input folders
  printed `krita 0.5.2 (git e352f1c)` with empty stderr. The interactive
  application was not launched.
- The release was created as a draft; all five uploaded assets matched the
  verified files' SHA-256 and size before it was published. The published
  tag resolves to the exact built commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-0.5.2-windows-x64-e352f1c51963.zip` | 418297592 | `5627607ae13f99fd11d8bf7a8f2a9baf43217c480f8325d0cab1669ac815e4e3` |
| `Solstice-0.5.2-windows-x64-e352f1c51963-dbg.zip` | 12377702 | `a319d597cb664766ed68825e7e2039ceef51cc5c2eae4311e7fac6f2baa6033b` |

## Website

Version 0.5.2 was merged into `develop` (tidalboxstudio-web #15) after the
release existed and published through `main` (#16, `7673077`); page content
is unchanged. The Cloudflare production build succeeded, and the live home,
Download (both languages) and Lazy Tools pages return 200 and link to v0.5.2.

## Verification boundary

`KisLazyToolsTest` passed locally (21 cases, including remembered PNG and
JPEG options), and the user confirmed the fix in the application. No source
strings changed, so the translation catalog was not updated. The cloud
release run did not execute native regression suites, interactive painting
or GPU runtime tests.
