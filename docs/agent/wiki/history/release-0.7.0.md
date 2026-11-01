---
type: history
status: released and verified 2026-11-01 JST
sources:
  - 51d2d0c6f9d791f638a9ffdadb3740819d1512eb
  - https://github.com/udtren/Solstice/actions/runs/41300851395
  - https://github.com/udtren/Solstice/releases/tag/v0.7.0
  - https://github.com/udtren/Solstice/pull/56
  - https://github.com/udtren/tidalboxstudio-web/pull/27
  - https://github.com/udtren/tidalboxstudio-web/pull/28
---

# Solstice 0.7.0 release verification

Published 2026-11-01 at 12:54:07 UTC (21:54:07 JST) as a regular release,
marked latest. The user tried the Through-Layer Eraser Tool in the
application and asked to release it as 0.7.0 (`MINOR`).

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Contents

Changes since 0.6.1:

- The [Through-Layer Eraser Tool](../../through-layer-eraser.md) (PR #56)
- The PNG options dialog ticks Force convert to sRGB when the setting is
  missing (PR #55;
  [pitfall](../pitfalls/script-export-replaces-saved-options.md))
- The test control paint trace overhead series (PR #50)
- The version preparation (PR #56)

## Build and artifacts

- **Tag and build commit:** `51d2d0c6f9d791f638a9ffdadb3740819d1512eb`, the
  `main` merge of PR #56.
- **Run 41300851395** (number 31): configuration, compilation, installation,
  the CPU G'MIC runtime build, packaging and artifact upload all passed.
  `BUILD-INFO.txt`: version 0.7.0, the commit above, GPU engine compiled,
  G'MIC CPU runtime 3.7.4.1.
- **ZIP integrity:** both downloaded ZIPs matched `SHA256SUMS.txt` (CRLF
  stripped) and passed full CRC checks: 5,315 application entries and 221
  debug entries.
- **Files present:** `Solstice.exe`, `bin/solstice.exe`, `bin/solstice.com`,
  `libkritaui.dll`, `libkritaimage.dll`, `libkritagpu.dll`,
  `kritatestcontrol.dll`, Python and PyQt6, QML, the G'MIC files, the zh_CN
  catalog, `kritamenu.action` and `shellex/register-thumbnails.cmd`.
- **The new tool in the package:** `KisToolThroughLayerEraser` is in
  `kritadefaulttools.dll`, its action is in `tools.action`, and the zh_CN
  catalog has its texts.
- **Dependency lock:** the packaged lock (CRLF) equals the Git blob (LF)
  after line-ending normalization.
- **Windows metadata:** ProductName `Solstice`, file and product version
  `0.7.0 (git 51d2d0c)`, numeric version `0.7.0.0`.
- **Version call:** `solstice.com --version`, run with empty profile and
  legacy input folders, exited 0 and printed `krita 0.7.0 (git 51d2d0c)`
  with empty stderr. The interactive application was not launched from this
  package.
- **Publication:** the release was created as a draft. All five uploaded
  assets matched the verified files' SHA-256 and size before it was
  published. The tag resolves to the built commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-0.7.0-windows-x64-51d2d0c6f9d7.zip` | 418567018 | `2295cf2457ca64b907a05262e04a222b11615e41b65c33dff32efc3be47567e8` |
| `Solstice-0.7.0-windows-x64-51d2d0c6f9d7-dbg.zip` | 12436847 | `7b7faecf2e0d7b778cbaf8e1b6fa1b5fba011db70d3b79ccacbaaff888135a07` |

## Website

- **tidalboxstudio-web #27**, merged into `develop` after the release
  existed:
  - The Through-Layer Eraser pages in both languages, and a `/features/`
    card third in Painting and brushes, with Tool Options fifth (the user's
    order; it differs from the Highlights order in README).
  - The animated GIF on the card and the pages, with a still image for
    visitors who prefer reduced motion.
  - The version set to 0.7.0.
  - `npm run check` and `npm run build` passed before the merge.
- **#28** published `develop` through `main` (`2a40d1e`), with the
  documentation commits of #25 and #26. The Cloudflare production build
  succeeded; the live site showed 0.7.0 about two minutes after the merge.
- **Live check:**
  - The home, Download, Features and Through-Layer Eraser pages in both
    languages return 200, show 0.7.0, link to v0.7.0 and no longer mention
    0.6.1.
  - `/features/` lists Brush Presets, Through-Layer Eraser, Brush Stroke
    Layer and Tool Options in that order, and the GIF is served as a GIF.
  - `/go/release` redirects to v0.7.0, and the sitemap lists 38 URLs.
  - The reduced-motion still image is declared on the live page and returns
    200 as WebP. No browser ran with reduced motion on.
  - The 375 px layout was checked on the local preview, not on the live site.

## Verification boundary

- **Local tests:** `KisThroughLayerEraserTest`, `KisGpuBrushJobsTest`,
  `KisBrushStrokeLayerTest` and `KisBrushStrokeReplayTest` passed on Windows.
  The UI scenarios `through-layer-eraser`, `eraser-stroke` and
  `brush-stroke-undo` passed in the application. The two new PNG tests
  passed (PR #55). The full suite was not run.
- **Partly run:** `KisGpuStrokeTest` exceeded the 1500 s ctest limit. Run
  directly, `testHalfBlendModes` was stopped after 553 rows without a
  failure; `testTexturedMaskedStroke`, `testHalfTextureModesExact` and
  `testMaskingCompositeMatchesCpu` passed.
- **In the application:** the user tried the Through-Layer Eraser Tool.
- **Not checked in the application:** the PNG options fix.
- **Not checked on Windows:** the paint trace overhead series (Linux only).
- **Cloud release run:** it did not execute native regression suites,
  interactive painting or GPU runtime tests.
