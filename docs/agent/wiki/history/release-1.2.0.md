---
type: history
status: released and verified 2026-11-20 JST
sources:
  - f4041d71cfc1503ecc489756aff512abcb1cf23a
  - https://github.com/udtren/Solstice/actions/runs/44678851678
  - https://github.com/udtren/Solstice/releases/tag/v1.2.0
  - https://github.com/udtren/Solstice/pull/104
  - https://github.com/udtren/Solstice/pull/105
  - https://github.com/udtren/Solstice/pull/106
  - https://github.com/udtren/Solstice/pull/107
  - https://github.com/udtren/Solstice/pull/108
  - https://github.com/udtren/Solstice/pull/109
  - https://github.com/udtren/Solstice/pull/110
  - https://github.com/udtren/tidalboxstudio-web/pull/44
  - https://github.com/udtren/tidalboxstudio-web/pull/45
  - https://github.com/udtren/tidalboxstudio-web/pull/46
  - docs/agent/versioning.md (Releasing, History)
---

# Solstice 1.2.0 release verification

Published 2026-11-20 at 06:23:19 UTC (15:23:19 JST) as a regular release,
marked latest. The user asked for Vector Brush Strokes to be released as
1.2.0 (`MINOR`) and presented at the same rank as the GPU engine, in the
README and on the website.

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Contents

Changes since 1.1.1:

- **Vector Brush Strokes** (PR #106): the brush tools paint on vector
  layers, each stroke a shape; the vector eraser; the Edit Brush Strokes
  Tool ([development notes](../../vector-brush-strokes.md)).
- **Fixes and additions from the user's manual test** (PR #107): the
  rectangle of an edited canvas-mirror line, `KoShape::setSize()` and the
  painting copies, the Multibrush's vector eraser, the Apply Background
  Color message, assistants from vector shapes.
- **Upstream intake 2026-W47** (PR #104) and the Test Control busy-read
  fix (PR #105).
- **README and guide** (PRs #108 to #110): Vector Brush Strokes as a second
  headline feature, the user's animations (41 MB compressed to 14 MB, the
  cover replaced once), and the statement that the functions were checked
  by hand with a pen tablet. #108 also holds the version preparation.

## Build and artifacts

- **Tag and build commit:** `f4041d71cfc1503ecc489756aff512abcb1cf23a`, the
  `main` merge of PR #108. PRs #109 and #110 (documents and the cover
  animation only) were merged after it and are not in the tag.
- **Run 44678851678** (number 39, 05:07 to 06:16 UTC): configuration,
  compilation, installation, packaging and artifact upload all passed.
  `BUILD-INFO.txt`: version 1.2.0, the commit above, GPU engine compiled,
  G'MIC CPU runtime 3.7.4.1.
- **ZIP integrity:** both ZIPs matched `SHA256SUMS.txt` (CRLF stripped
  first) and passed full CRC checks: 5,323 application entries and 221
  debug entries, the same counts as 1.1.1.
- **Files present:** `Solstice.exe`, `bin/solstice.exe`,
  `bin/solstice.com`, `shellex/register-thumbnails.cmd`,
  `kritadefaulttools.dll`, and zh_CN catalogs, G'MIC, Python and Vulkan
  files by name.
- **Dependency lock:** the artifact's lock equals
  `build-tools/github-actions/windows-dependencies.lock.json` at the build
  commit after line-ending normalization.
- **Windows metadata:** both executables report ProductName `Solstice` and
  product version `1.2.0 (git f4041d7)`.
- **Publication:** created directly (not as a draft) from the verified
  files; the digests GitHub reports for the uploaded ZIPs equal the
  verified SHA-256 values, and the release targets the build commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-1.2.0-windows-x64-f4041d71cfc1.zip` | 419041553 | `f16f161d2827ef69d7bd4473b6e3bc50bbd92ab88cfc0d344e55208057611245` |
| `Solstice-1.2.0-windows-x64-f4041d71cfc1-dbg.zip` | 12479878 | `d34d5f0101b2f787f3e4cb9bdaffeda57326497a068226e502eeeb97978169f2` |

## Website

- **tidalboxstudio-web #44**, merged into `develop`: version and release
  link 1.2.0, a "Vector brush strokes" section on the top page before the
  GPU engine section, the feature page `/vector-brush-strokes/` in English
  and Simplified Chinese with seven recordings, the first cards of
  `/features/`, two FAQ entries, the differences page and the sitemap.
  **#45** replaced the cover recording.
- **#46** published `develop` through `main` at 06:24:08 UTC, after the
  release existed; the Workers build of the preview had passed.
- **Live check** (a few minutes later): by HTTP, the home page, the
  download page and the zh-CN home page show 1.2.0 and no 1.1.1,
  `/go/release` redirects to v1.2.0, and both feature pages answer 200. In
  a browser, the top page's section shows the new cover recording.
- Before publication: `npm run check` and `npm run build` passed; the top
  page section and the head of the English feature page were looked at in
  a browser at desktop width, the top page at mobile width (no horizontal
  overflow).

## Verification before the release (local, Windows 11)

- `KisVectorBrushStrokeTest` 36/36, `KisBrushStrokeLayerTest` 43/43 and
  `kis_shape_commands_test` 10/10 (offscreen).
- Test control scenarios in the application: `vector-brush-stroke`,
  `vector-eraser-intersection`, `vector-eraser-multibrush`,
  `vector-join-lines`, `vector-shape-assistants` and
  `edit-brush-strokes-tool`.
- The user ran the manual checks of the feature in the development build,
  found three problems (fixed in PR #107) and reported the rest and the
  fixes as working.

## Not verified

- The release package was not started. The cloud build uses
  `BUILD_TESTING=OFF`.
- The leftover-ink race fixed in PR #107 was never reproduced in a test;
  it rests on the user's check.
- On the website, the lower part of the feature page and its Chinese
  version were not looked at in a browser.

The release notes state the first gap.
