---
type: history
status: released and verified 2026-11-11 JST
sources:
  - eb169f073da7c349cbe48ea6b510de022afe6fa0
  - https://github.com/udtren/Solstice/actions/runs/43022003263
  - https://github.com/udtren/Solstice/releases/tag/v0.9.0
  - https://github.com/udtren/Solstice/pull/72
  - https://github.com/udtren/Solstice/pull/74
  - https://github.com/udtren/tidalboxstudio-web/pull/33
  - https://github.com/udtren/tidalboxstudio-web/pull/34
---

# Solstice 0.9.0 release verification

Published 2026-11-11 at 14:18:22 UTC (23:18:22 JST) as a regular release,
marked latest. The user asked for the At least 1 pixel brush option, then
asked to merge every open pull request and release as 0.9.0 (`MINOR`).

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Contents

Changes since 0.8.1:

- **At least 1 pixel** (PR #72): a Freehand Brush Tool Options check box,
  on by default, that draws dabs smaller than 1 pixel at 1 pixel
  ([agent document](../../brush-minimum-one-pixel.md)).
- The Asset Library docker and welcome page settings stay in sync (PR #68).
- Test Control `gpu_fault` (PR #70), the GPU projection stress test
  (PR #71) and the GPU failure UI acceptance record (PR #73).
- The 2026-W46 upstream decisions (PR #69, documents only) and the version
  preparation (PR #74).

Before the release, #73 failed the documentation check (`related` named
feature documents instead of wiki pages) and, like #71 before it,
conflicted with `main` only in `wiki/log.md`. Both entries were kept, and
the feature documents moved to `sources`.

## Build and artifacts

- **Tag and build commit:** `eb169f073da7c349cbe48ea6b510de022afe6fa0`, the
  `main` merge of PR #74.
- **Run 43022003263** (number 34, about 1 h 36 min): configuration,
  compilation, installation, the CPU G'MIC runtime build, packaging and
  artifact upload all passed. `BUILD-INFO.txt`: version 0.9.0, the commit
  above, GPU engine compiled, G'MIC CPU runtime 3.7.4.1.
- **ZIP integrity:** both downloaded ZIPs matched `SHA256SUMS.txt` (CRLF
  stripped) and passed full CRC checks: 5,323 application entries and 221
  debug entries.
- **Files present:** `Solstice.exe`, `bin/solstice.exe`, `bin/solstice.com`,
  `libkritaui.dll`, `libkritaimage.dll`, `libkritagpu.dll`,
  `libkritalibpaintop.dll`, `kritatestcontrol.dll`, `kritadefaulttools.dll`,
  `kritadefaultpaintops.dll`, the zh_CN and ja catalogs, `kritamenu.action`
  and `shellex/register-thumbnails.cmd`.
- **The new option in the package:** `kritadefaulttools.dll` contains
  "At least 1 pixel" and `BrushMinimumOnePixel`; `libkritaui.dll` contains
  `MinimumOnePixelDab`; the zh_CN and ja catalogs have the check box text.
- **Dependency lock:** the packaged lock (CRLF) equals the Git blob (LF)
  after line-ending normalization.
- **Windows metadata:** ProductName `Solstice`, file and product version
  `0.9.0 (git eb169f0)`, numeric version `0.9.0.0`.
- **Version call:** `solstice.com --version`, run with empty `APPDATA`,
  `LOCALAPPDATA` and `USERPROFILE` folders, exited 0 and printed
  `krita 0.9.0 (git eb169f0)` with empty stderr. The interactive application
  was not launched from this package.
- **Publication:** the release was created as a draft. All five uploaded
  assets matched the verified files' SHA-256 and size before it was
  published. The tag resolves to the built commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-0.9.0-windows-x64-eb169f073da7.zip` | 418624522 | `4d46972e2640a396d40827244272d8cc08cfaa7439a7037f24409a0c411c7c09` |
| `Solstice-0.9.0-windows-x64-eb169f073da7-dbg.zip` | 12441823 | `5c1a970c3cf8dd090042415da9299811f49e0f5e92977d985b99427ba871badb` |

## Website

- **tidalboxstudio-web #33**, merged into `develop` after the release
  existed: the version and release link set to 0.9.0 in
  `solstice/src/site.ts`. No feature page changed; the site has no page for
  At least 1 pixel. `npm run check` (0 errors) and `npm run build`
  (40 pages) passed.
- **#34** published `develop` through `main` at about 14:20:33 UTC; the live
  site showed 0.9.0 by 14:21:40 UTC.
- **Live check:** the home and Download pages in both languages and
  `/features/` return 200 and show 0.9.0; `/go/release` redirects to v0.9.0.
  No page was viewed in a browser after publication.

## Verification boundary

- **Local tests (PR #72):** `KisBrushMinimumOnePixelTest` and the related
  brush, GPU brush, Brush Stroke Layer and engine parity tests passed on
  Windows after installation. The full suite was not run.
- **In the application:** At least 1 pixel was not tried by hand with a
  tablet before the release (manual checks in the
  [agent document](../../brush-minimum-one-pixel.md)). The Asset Library
  sync fix (#68) was not checked interactively either.
- **Cloud release run:** it did not execute native regression suites,
  interactive painting or GPU runtime tests.
