---
type: history
status: released and verified 2026-11-12 JST
sources:
  - 02090017a7f77de795461a4798d5d97a3fbea0d1
  - https://github.com/udtren/Solstice/actions/runs/43134758192
  - https://github.com/udtren/Solstice/releases/tag/v0.9.1
  - https://github.com/udtren/Solstice/pull/76
  - https://github.com/udtren/Solstice/pull/77
  - https://github.com/udtren/tidalboxstudio-web/pull/35
  - https://github.com/udtren/tidalboxstudio-web/pull/36
---

# Solstice 0.9.1 release verification

Published 2026-11-12 at 02:22:46 UTC (11:22:46 JST) as a regular release,
marked latest. The user asked for the device extent fix and then to release
it as 0.9.1 (`PATCH`: a fix only).

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Contents

Changes since 0.9.0:

- **Device extent fix** (PR #76): `KisTiledExtentManager` could keep an
  extent smaller than the device's tiles after concurrent tile additions;
  compositing skips a layer outside its extent
  ([pitfall](../pitfalls/extent-smaller-than-tiles.md)).
- The version preparation (PR #77).

The fix was found while looking for the cause of a rare white rectangle
during painting ([GPU engine risks](../../gpu-engine.md#risks-and-open-questions)).
It is not shown to be that cause, and the release notes say so.

## Build and artifacts

- **Tag and build commit:** `02090017a7f77de795461a4798d5d97a3fbea0d1`, the
  `main` merge of PR #77 (PR #76 merged before it).
- **Run 43134758192** (number 35, about 49 min): configuration,
  compilation, installation, packaging and artifact upload all passed.
  `BUILD-INFO.txt`: version 0.9.1, the commit above, GPU engine compiled,
  G'MIC CPU runtime 3.7.4.1.
- **ZIP integrity:** both downloaded ZIPs matched `SHA256SUMS.txt` and
  passed full CRC checks: 5,323 application entries and 221 debug entries.
- **Files present:** `Solstice.exe`, `bin/solstice.exe`, `bin/solstice.com`,
  `libkritaui.dll`, `libkritaimage.dll`, `libkritagpu.dll`,
  `libkritalibpaintop.dll`, `kritatestcontrol.dll`, `kritadefaulttools.dll`,
  `kritadefaultpaintops.dll`, the zh_CN and ja catalogs, `kritamenu.action`,
  `shellex/register-thumbnails.cmd`, and the G'MIC data and source folder.
- **The fix in the package:** not checked in the binary; it adds no string.
  The package is built from the commit that contains it.
- **Dependency lock:** the packaged lock (CRLF) equals the Git blob (LF)
  after line-ending normalization.
- **Windows metadata:** ProductName `Solstice`, file and product version
  `0.9.1 (git 0209001)`, numeric version `0.9.1.0`.
- **Version call:** `solstice.com --version`, run with empty
  `SOLSTICE_PROFILE_ROOT`, `SOLSTICE_LEGACY_CONFIG_DIR` and
  `SOLSTICE_LEGACY_RESOURCE_DIR` folders, exited 0 and printed
  `krita 0.9.1 (git 0209001)` with empty stderr. The interactive application
  was not launched from this package.
- **Publication:** the release was created as a draft. All five uploaded
  assets matched the verified files' SHA-256 and size before it was
  published. The tag resolves to the built commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-0.9.1-windows-x64-02090017a7f7.zip` | 418623953 | `44c9eaf699076021136b97588ddf0b26ad2edc62579a9980ccbabfef89257e31` |
| `Solstice-0.9.1-windows-x64-02090017a7f7-dbg.zip` | 12441759 | `b7962bdba3eb1c80c8f7884760d8bf3d1c2ac921578f7fca4aef23ca9ed18eba` |

## Website

- **tidalboxstudio-web #35**, merged into `develop` at 02:33:51 UTC, after
  the release existed: the version and release link set to 0.9.1 in
  `solstice/src/site.ts`. No feature page changed. `npm run check`
  (0 errors) and `npm run build` (40 pages) passed.
- **#36** published `develop` through `main` at 02:51:35 UTC; the live site
  showed 0.9.1 by 02:52:36 UTC.
- **Live check:** the home and Download pages in both languages and
  `/features/` return 200 and show 0.9.1; `/go/release` redirects to v0.9.1.
  No page was viewed in a browser after publication.

## Verification boundary

- **Local tests (PR #76):** `stressTestExtentAfterConcurrentAdds` fails
  without the fix (261 of 50,000 rounds) and passes with it. 21 related
  tile, paint device, transaction, iterator, merge and GPU tests passed on
  Windows after a rebuild. The full suite was not run.
- **In the application:** a local build with the fix was started through
  Test Control and painted 20 strokes without differences. The user did not
  try the fix by hand before the release, and the white rectangle has not
  been reproduced, so the release does not show that it is gone.
- **Cloud release run:** it did not execute native regression suites,
  interactive painting or GPU runtime tests.

## Process notes

- This session could not merge pull requests (the permission mode refused
  merges without review), so the user merged PRs #76 and #77 and the
  website PRs #35 and #36. A reply of "merged" once referred to #35 while
  #36 was still open; checking the PR state before continuing caught it.
