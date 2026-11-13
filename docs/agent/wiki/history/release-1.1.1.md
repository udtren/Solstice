---
type: history
status: released and verified 2026-11-13 JST
sources:
  - ac468d986539e40ac390fe7008073aef6c1da483
  - https://github.com/udtren/Solstice/actions/runs/43391326431
  - https://github.com/udtren/Solstice/releases/tag/v1.1.1
  - https://github.com/udtren/Solstice/pull/94
  - https://github.com/udtren/Solstice/pull/95
  - https://github.com/udtren/Solstice/pull/96
  - https://github.com/udtren/Solstice/pull/97
  - https://github.com/udtren/Solstice/pull/98
  - https://github.com/udtren/Solstice/pull/99
  - https://github.com/udtren/Solstice/pull/102
  - https://github.com/udtren/tidalboxstudio-web/pull/41
  - https://github.com/udtren/tidalboxstudio-web/pull/42
  - https://github.com/udtren/tidalboxstudio-web/pull/43
  - docs/agent/versioning.md (Releasing, History)
---

# Solstice 1.1.1 release verification

Published 2026-11-13 at 10:45:00 UTC (19:45:00 JST) as a regular release,
marked latest. The user asked for the day's CPU performance work to be
released as 1.1.1 and the website to be published afterwards (`PATCH`: no
new features).

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Contents

Changes since 1.1.0:

- **Exact bounds scan** (PR #94): tile-wise instead of pixel sweeps
  ([benchmark](../benchmarks/exact-bounds-scan.md)).
- **Text Properties docker** (PR #95): QML loaded on first show.
- **Quick Adjust docker** (PR #96): its timer runs only while shown.
- **Closed Brush Editor** (PR #97): brush setting changes no longer search
  all presets; the resource model looks rows up by id through a hash.
- **Resource cache database** (PR #98): preset text removed from
  thumbnails at startup, then `VACUUM` above a threshold.
- **Reload Original Preset** (PR #99): the other stroke previews are kept
  ([Brush Stroke Preview](../../brush-stroke-preview.md)).
- README corrections and restructuring with new benchmark rows (PRs #100,
  #101), and the version preparation (PR #102).

The audit that led to these changes is in
[CPU performance audit](../queries/cpu-performance-audit-20261113.md).

## Build and artifacts

- **Tag and build commit:** `ac468d986539e40ac390fe7008073aef6c1da483`, the
  `main` merge of PR #102.
- **Run 43391326431** (number 38, 08:11 to 09:48 UTC): configuration,
  compilation, installation, packaging and artifact upload all passed.
  `BUILD-INFO.txt`: version 1.1.1, the commit above, GPU engine compiled,
  G'MIC CPU runtime 3.7.4.1.
- **ZIP integrity:** both ZIPs matched `SHA256SUMS.txt` (CRLF stripped
  first) and passed full CRC checks: 5,323 application entries and 221
  debug entries, the same counts as 1.1.0.
- **Files present:** `Solstice.exe`, `bin/solstice.exe`,
  `bin/solstice.com`, `shellex/register-thumbnails.cmd`, and zh_CN
  catalogs, G'MIC, Python and Vulkan files by name.
- **Dependency lock:** the packaged lock equals
  `build-tools/github-actions/windows-dependencies.lock.json` at the build
  commit after line-ending normalization.
- **Windows metadata:** both executables report ProductName `Solstice` and
  product version `1.1.1 (git ac468d9)`. The numeric version was not read.
- **Publication:** created directly (not as a draft) from the verified
  files; the digests GitHub reports for the uploaded ZIPs equal the
  verified SHA-256 values, and the release targets the build commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-1.1.1-windows-x64-ac468d986539.zip` | 418792745 | `e0a44b007e79babe0e6333d0421504dbe0487d7244c4f3ddfd065499a7fcc551` |
| `Solstice-1.1.1-windows-x64-ac468d986539-dbg.zip` | 12450233 | `665d5178945bd2e545ad8fdad822a84493af7c2bc757858da6e69cb1bafd25a5` |

## Website

- **tidalboxstudio-web #42**, merged into `develop` at 08:13:21 UTC: version
  and release link 1.1.1 in `solstice/src/site.ts`. #41 (08:06:13 UTC) had
  added the layer stack chart to the GPU engine page in both languages.
  No feature page got a "New in 1.1.1" note: the release has no new
  feature.
- **#43** published `develop` through `main` at 10:45:48 UTC, after the
  release existed; the Workers build of the preview had passed.
- **Live check** (a few minutes later, by HTTP): the home page, the
  download page and the zh-CN home page show 1.1.1 and no 1.1.0;
  `/go/release` redirects to v1.1.1; the GPU engine page has the layer
  stack section. No page was viewed in a browser.

## Verification before the release (local, Windows 11)

- Each change has a regression test that passed locally (offscreen):
  `kis_paint_device_test` and `KisGpuPaintDeviceTest` (#94),
  `QuickAdjustDockTest` (#96), `TestResourceModel` (#97),
  `TestResourceCacheDb` (#98), `KisBrushStrokePreviewTest` (#99). #95 has
  no test; it was measured (1038 ms to 3 ms at start).
- The user ran the manual checks of all six changes in the development
  build and reported no problems. On the user's profile the first start
  took the resource database from 500.7 MB to 222.7 MB.

## Not verified

- The release package was not started. The cloud build uses
  `BUILD_TESTING=OFF`.
- The database clean-up was seen on one real profile only.

The release notes state the first gap.
