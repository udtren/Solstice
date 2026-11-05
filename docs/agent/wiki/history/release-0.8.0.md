---
type: history
status: released and verified 2026-11-05 JST
sources:
  - 4b8b580a02a35060692a9ae6e314578994a02631
  - https://github.com/udtren/Solstice/actions/runs/41942109402
  - https://github.com/udtren/Solstice/releases/tag/v0.8.0
  - https://github.com/udtren/Solstice/pull/63
  - https://github.com/udtren/Solstice/pull/64
  - https://github.com/udtren/tidalboxstudio-web/pull/29
  - https://github.com/udtren/tidalboxstudio-web/pull/30
---

# Solstice 0.8.0 release verification

Published 2026-11-05 at 02:16:20 UTC (11:16:20 JST) as a regular release,
marked latest. The user replaced the Lazy Tools screenshots, added an
animated GIF for duplicating a selection on the same layer, and asked to
release as 0.8.0 (`MINOR`).

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Contents

Changes since 0.7.0:

- Fast Image Export saved exports per document, stored in the `.kra`, with
  the Run All Saved Exports and Run Checked Saved Exports actions (PR #63;
  [decision](../decisions/fast-export-saved-exports-in-kra.md))
- The version preparation, the duplicate GIF in README and the Move Tool
  guide, and the new Lazy Tools screenshots and descriptions (PR #64)

## Build and artifacts

- **Tag and build commit:** `4b8b580a02a35060692a9ae6e314578994a02631`, the
  `main` merge of PR #64.
- **Run 41942109402** (number 32): configuration, compilation, installation,
  the CPU G'MIC runtime build, packaging and artifact upload all passed.
  `BUILD-INFO.txt`: version 0.8.0, the commit above, GPU engine compiled,
  G'MIC CPU runtime 3.7.4.1.
- **ZIP integrity:** both downloaded ZIPs matched `SHA256SUMS.txt` (CRLF
  stripped) and passed full CRC checks: 5,323 application entries and 221
  debug entries.
- **Files present:** `Solstice.exe`, `bin/solstice.exe`, `bin/solstice.com`,
  `libkritaui.dll`, `libkritaimage.dll`, `libkritagpu.dll`,
  `kritatestcontrol.dll`, the zh_CN catalog, `kritamenu.action` and
  `shellex/register-thumbnails.cmd`.
- **The new feature in the package:** `kritamenu.action` has
  `fast_image_export_run_all` and `fast_image_export_run_checked`;
  `libkritaui.dll` and `libkritalibkra.dll` contain the `fastexport` entry
  name; the zh_CN catalog has the new action texts.
- **Dependency lock:** the packaged lock (CRLF) equals the Git blob (LF)
  after line-ending normalization.
- **Windows metadata:** ProductName `Solstice`, file and product version
  `0.8.0 (git 4b8b580)`, numeric version `0.8.0.0`.
- **Version call:** `solstice.com --version`, run with empty `APPDATA`,
  `LOCALAPPDATA` and `USERPROFILE` folders, exited 0 and printed
  `krita 0.8.0 (git 4b8b580)` with empty stderr. The interactive application
  was not launched from this package.
- **Publication:** the release was created as a draft. All five uploaded
  assets matched the verified files' SHA-256 and size before it was
  published. The tag resolves to the built commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-0.8.0-windows-x64-4b8b580a02a3.zip` | 418611957 | `e223ea05fe3364c5191d7ae5af07828e04c10b6af51b585728eadeb88d9258cd` |
| `Solstice-0.8.0-windows-x64-4b8b580a02a3-dbg.zip` | 12440708 | `c888847b7a3a0716765c6f1eb885943bcf5515198518339b095bfdd4718fb448` |

## Website

- **tidalboxstudio-web #29**, merged into `develop` after the release
  existed:
  - The duplicate pages in both languages and the `/features/` card show the
    animated GIF through `FeatureAnimation`; the reduced-motion still image
    is its last frame. The old PNG was removed.
  - The new Lazy Tools cover and Fast Image Export screenshots, with their
    transparent margins cropped, and a Saved exports section (New in 0.8.0)
    in both languages.
  - The version set to 0.8.0.
  - `npm run check` and `npm run build` passed; the changed pages were
    viewed on a local preview at desktop width and at 375 px.
- **#30** published `develop` through `main` (`154f56d`).
  The Cloudflare production build succeeded at 02:20:08 UTC; the live site
  showed 0.8.0 about three minutes after the merge.
- **Live check:**
  - The home, Download, Features, duplicate and Lazy Tools pages in both
    languages return 200 and show 0.8.0; the only 0.7.0 left on
    `/features/` is the Through-Layer Eraser card's "New in 0.7.0".
  - `/go/release` redirects to v0.8.0, and the sitemap lists 38 URLs.
  - The duplicate pages and the `/features/` card serve the GIF as
    `image/gif` (428,917 bytes), and the reduced-motion still returns 200 as
    WebP. No browser ran with reduced motion on.
  - Both Lazy Tools pages have the Saved exports section.
  - The 375 px layout was checked on the local preview, not on the live site.

## Verification boundary

- **Local tests (PR #63):** `KisLazyToolsTest` 27/27 offscreen and the UI
  scenario `lazy-fast-export-presets` with `--launch`. The full suite was not
  run.
- **Not checked in the application from this package:** saved exports and
  every other interactive behavior.
- **Cloud release run:** it did not execute native regression suites,
  interactive painting or GPU runtime tests.
