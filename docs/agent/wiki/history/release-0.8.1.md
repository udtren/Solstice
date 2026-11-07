---
type: history
status: released and verified 2026-11-07 JST
sources:
  - 8ec846b94750da356af39bf871e7f2dc8e8b7ebf
  - https://github.com/udtren/Solstice/actions/runs/42314221895
  - https://github.com/udtren/Solstice/releases/tag/v0.8.1
  - https://github.com/udtren/Solstice/pull/66
  - https://github.com/udtren/tidalboxstudio-web/pull/31
  - https://github.com/udtren/tidalboxstudio-web/pull/32
---

# Solstice 0.8.1 release verification

Published 2026-11-07 at 01:29:03 UTC (10:29:03 JST) as a regular release,
marked latest. The user asked for the Through-Layer Eraser to erase alpha
locked layers, tried the new option in the application, and asked to
release as 0.8.1 (`PATCH`).

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Contents

Changes since 0.8.0:

- The Through-Layer Eraser erases alpha locked layers while the new Tool
  Options check box **Erase alpha locked layers** is checked (on by
  default), and the version preparation (PR #66;
  [decision](../decisions/through-layer-eraser-alpha-locked.md))

## Build and artifacts

- **Tag and build commit:** `8ec846b94750da356af39bf871e7f2dc8e8b7ebf`, the
  `main` merge of PR #66.
- **Run 42314221895** (number 33): configuration, compilation, installation,
  the CPU G'MIC runtime build, packaging and artifact upload all passed.
  `BUILD-INFO.txt`: version 0.8.1, the commit above, GPU engine compiled,
  G'MIC CPU runtime 3.7.4.1.
- **ZIP integrity:** both downloaded ZIPs matched `SHA256SUMS.txt` (CRLF
  stripped) and passed full CRC checks: 5,323 application entries and 221
  debug entries.
- **Files present:** `Solstice.exe`, `bin/solstice.exe`, `bin/solstice.com`,
  `libkritaui.dll`, `libkritaimage.dll`, `libkritagpu.dll`,
  `kritatestcontrol.dll`, `kritadefaulttools.dll`, the zh_CN catalog,
  `kritamenu.action` and `shellex/register-thumbnails.cmd`.
- **The new option in the package:** `kritadefaulttools.dll` contains
  `chkEraseAlphaLocked`, `eraseAlphaLocked` and "Erase alpha locked layers";
  the zh_CN catalog has the check box text and its tooltip.
- **Dependency lock:** the packaged lock (CRLF) equals the Git blob (LF)
  after line-ending normalization.
- **Windows metadata:** ProductName `Solstice`, file and product version
  `0.8.1 (git 8ec846b)`, numeric version `0.8.1.0`.
- **Version call:** `solstice.com --version`, run with empty `APPDATA`,
  `LOCALAPPDATA` and `USERPROFILE` folders, exited 0 and printed
  `krita 0.8.1 (git 8ec846b)` with empty stderr. The interactive application
  was not launched from this package.
- **Publication:** the release was created as a draft. All five uploaded
  assets matched the verified files' SHA-256 and size before it was
  published. The tag resolves to the built commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-0.8.1-windows-x64-8ec846b94750.zip` | 418613515 | `9ea56cd729d1415069db3cd150359429d9f8df1abd10e60dcab1d0a4895f83d6` |
| `Solstice-0.8.1-windows-x64-8ec846b94750-dbg.zip` | 12440907 | `62a1cf3d2e50564a7a54647f63d045e8512ecbb48229cf4beabc77436dc3c2bc` |

## Website

- **tidalboxstudio-web #31**, merged into `develop` after the release
  existed:
  - The Through-Layer Eraser pages in both languages no longer list alpha
    locked layers as left alone, and describe the option (New in 0.8.1).
  - The version set to 0.8.1.
  - `npm run check` (0 errors) and `npm run build` (40 pages) passed.
- **#32** published `develop` through `main` (`c29eaa3`) at about
  01:29:40 UTC; the live site showed 0.8.1 by 01:30:44 UTC.
- **Live check:**
  - The home, Download and Through-Layer Eraser pages in both languages and
    `/features/` return 200 and show 0.8.1; the only 0.8.0 left on
    `/features/` is the Lazy Tools card's "Saved exports are new in 0.8.0".
  - `/go/release` redirects to v0.8.1, and the sitemap lists 38 URLs.
  - No page was viewed in a browser after publication.

## Verification boundary

- **Local tests (PR #66):** `KisThroughLayerEraserTest` 16/16 (alpha locked
  rows on the CPU and the GPU) and the scenarios `through-layer-eraser`,
  `eraser-stroke` and `brush-stroke-undo` with `--strict --launch`. The full
  suite was not run.
- **In the application:** the user tried the option in a local build before
  the release, not from this package.
- **Cloud release run:** it did not execute native regression suites,
  interactive painting or GPU runtime tests.
