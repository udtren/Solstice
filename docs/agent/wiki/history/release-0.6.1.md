---
type: history
status: released and verified 2026-10-30 JST
sources:
  - 88fecab57e3146e574312e4180b8ca94aaf1d8e2
  - https://github.com/udtren/Solstice/actions/runs/40882283677
  - https://github.com/udtren/Solstice/releases/tag/v0.6.1
  - https://github.com/udtren/Solstice/pull/52
  - https://github.com/udtren/tidalboxstudio-web/pull/19
  - https://github.com/udtren/tidalboxstudio-web/pull/20
---

# Solstice 0.6.1 release verification

Published 2026-10-29 at 15:11:39 UTC (2026-10-30 00:11:39 JST) as a regular
release, marked latest. The user confirmed the group and filter layer
thumbnail fixes in the application and asked to release them as 0.6.1
(`PATCH`: fixes only).

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Contents

Changes since 0.6.0:

- Group and filter layer thumbnails no longer keep pixels outside the canvas
  after a paste from another document (PR #52;
  [pitfall](../pitfalls/group-projection-outside-canvas.md))
- Middle-clicking a document tab closes only that tab (PR #51)
- The version preparation (PR #52)

## Build and artifacts

- **Tag and build commit:** `88fecab57e3146e574312e4180b8ca94aaf1d8e2`, the
  `main` merge of PR #52.
- **Run 40882283677** (number 30): configuration, compilation, installation,
  the CPU G'MIC runtime build, packaging and artifact upload all passed.
  `BUILD-INFO.txt`: version 0.6.1, the commit above, GPU engine compiled,
  G'MIC CPU runtime 3.7.4.1.
- **ZIP integrity:** both downloaded ZIPs matched `SHA256SUMS.txt` (CRLF
  stripped) and passed full CRC checks: 5,315 application entries and 221
  debug entries.
- **Files present:** `Solstice.exe`, `bin/solstice.exe`, `bin/solstice.com`,
  `libkritaui.dll`, `libkritaimage.dll`, `libkritagpu.dll`,
  `kritatestcontrol.dll`, Python and PyQt6, QML, the G'MIC files, the zh_CN
  catalog, `kritamenu.action` and `shellex/register-thumbnails.cmd`.
- **Dependency lock:** the packaged lock (CRLF) equals the Git blob (LF)
  after line-ending normalization.
- **Windows metadata:** ProductName `Solstice`, file and product version
  `0.6.1 (git 88fecab)`, numeric version `0.6.1.0`.
- **Version call:** `solstice.com --version`, run with empty profile and
  legacy input folders, exited 0 and printed `krita 0.6.1 (git 88fecab)`
  with empty stderr. The interactive application was not launched.
- **Publication:** the release was created as a draft. All five uploaded
  assets matched the verified files' SHA-256 and size before it was
  published. The tag resolves to the built commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-0.6.1-windows-x64-88fecab57e31.zip` | 418545758 | `05b4c5a44b1230b864de007ffbfb819d23938c5a03cc60544d3792198ff27e13` |
| `Solstice-0.6.1-windows-x64-88fecab57e31-dbg.zip` | 12433057 | `1013237e14a8ac79464c1ff6f3fed7299e98a329e9865793f4720d7f1e4a32f2` |

## Website

- **tidalboxstudio-web #19**, merged into `develop` after the release
  existed: the version set to 0.6.1 (`solstice/src/site.ts` only).
  `npm run check` and `npm run build` passed before the merge.
- **#20** published `develop` through `main` (`aa527dc`). The Cloudflare
  production build succeeded; the live site showed 0.6.1 about five
  minutes after the merge.
- **Live check:** the home and Download pages in both languages return 200,
  show 0.6.1 and link to v0.6.1. The remaining "0.6.0" on the home page is
  the Clipping Mask card's "New in 0.6.0", which is correct.

## Verification boundary

- **Local tests:** the two new regression tests fail without their fixes
  and pass with them; 13 related `libs/image` tests and 3 `.kra` load and
  save tests passed on Windows. The full suite was not run.
- **In the application:** the user checked the group and filter layer
  thumbnail fixes and the middle-click tab fix.
- **Cloud release run:** it did not execute native regression suites,
  interactive painting or GPU runtime tests.
