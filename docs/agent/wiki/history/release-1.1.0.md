---
type: history
status: released and verified 2026-11-13 JST
sources:
  - 9c6299a73fe581ae2fe1533192a6bf3833c4c507
  - https://github.com/udtren/Solstice/actions/runs/43359604919
  - https://github.com/udtren/Solstice/releases/tag/v1.1.0
  - https://github.com/udtren/Solstice/pull/89
  - https://github.com/udtren/Solstice/pull/90
  - https://github.com/udtren/Solstice/pull/91
  - https://github.com/udtren/tidalboxstudio-web/pull/39
  - https://github.com/udtren/tidalboxstudio-web/pull/40
  - docs/agent/versioning.md (Releasing, History)
  - docs/agent/lazy-tools.md (manual check 15)
---

# Solstice 1.1.0 release verification

Published 2026-11-13 at 05:11:39 UTC (14:11:39 JST) as a regular release,
marked latest. The user asked for all open pull requests to be merged and
released as 1.1.0 (`MINOR`: new features, no compatibility change).

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Contents

Changes since 1.0.0:

- **Layer Comp saved exports** (PR #90): a Fast Image Export saved export
  can export one Layer Comp, applied to a copy of the image, so the canvas,
  the docker and the undo history do not change. Layer Comps got a
  persistent id in the `.kra`; older versions drop it on save, and a saved
  export then finds its comp by name if only one comp has that name
  ([decision](../decisions/fast-export-saved-exports-in-kra.md#layer-comps-2026-11-13)).
  Layer Comp names rename in place by double-click or F2; the configured
  export folder is the default Destination and the choice is remembered.
- **Show in File Explorer** (PR #89) in the Recent Images and Asset Library
  (welcome page and docker) menus.
- zh_CN translations for both; ja entries for #89 were added by hand
  ([pitfall](../pitfalls/translation-extraction.md)).
- The version preparation (PR #91).

Both feature PRs were open when the release was requested. #89 merged
first; #90 then conflicted only in `wiki/log.md` (both entries kept) and its
eight new zh_CN entries were filled before it merged.

## Build and artifacts

- **Tag and build commit:** `9c6299a73fe581ae2fe1533192a6bf3833c4c507`, the
  `main` merge of PR #91.
- **Run 43359604919** (number 37, 03:32 to 05:07 UTC): configuration,
  compilation, installation, packaging and artifact upload all passed.
  `BUILD-INFO.txt`: version 1.1.0, the commit above, GPU engine compiled,
  G'MIC CPU runtime 3.7.4.1.
- **ZIP integrity:** both ZIPs matched `SHA256SUMS.txt` (CRLF stripped
  first) and passed full CRC checks: 5,323 application entries and 221
  debug entries, the same counts as 1.0.0.
- **Files present:** `Solstice.exe`, `bin/solstice.exe`,
  `bin/libkritaui.dll` (contains "Show in File Explorer"), and the ja and
  zh_CN `krita.mo` under `bin/locale`.
- **Dependency lock:** the packaged lock equals
  `build-tools/github-actions/windows-dependencies.lock.json` at the build
  commit after line-ending normalization.
- **Windows metadata:** both executables report ProductName `Solstice`,
  file and product version `1.1.0 (git 9c6299a)`, numeric version
  `1.1.0.0`.
- **Publication:** created directly (not as a draft) from the verified
  files; the digests GitHub reports for the uploaded ZIPs equal the
  verified SHA-256 values, and the tag resolves to the build commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-1.1.0-windows-x64-9c6299a73fe5.zip` | 418775427 | `d1e7a9e31b5ddd1746101421c8b8386ec1024c7430f180a511b84c422782aacd` |
| `Solstice-1.1.0-windows-x64-9c6299a73fe5-dbg.zip` | 12449500 | `08ef4c3c557f84d485ba2d5b7c3efc07d316c4728576583aa00b83d96371fa28` |

## Website

- **tidalboxstudio-web #39**, merged into `develop` at 03:33:55 UTC: version
  and release link 1.1.0 in `solstice/src/site.ts`, "New in 1.1.0" notes on
  the features, Layer Comps and Lazy Tools pages in both languages, and
  three new screenshots. `npm run check` and `npm run build` passed.
- **#40** published `develop` through `main` at 05:12:12 UTC, after the
  release existed; the live home page showed 1.1.0 about a minute later.
- **Live check:** the home page and the zh-CN Layer Comps page show 1.1.0;
  `/go/release` redirects to v1.1.0. No page was viewed in a browser.

## Verification before the release (local, Windows 11)

- PR #90: `KisLazyToolsTest` 30/30, `KisLayerCompositionTest` 5/5 and
  `CompositionWorkflowTest` 5/5 (offscreen).
- PR #89: `kritaui` and the Asset Library docker built and installed; the
  catalogs pass `msgfmt --check`.

## Not verified

- Manual check 15 of [Lazy Tools](../../lazy-tools.md) (saved exports with
  Layer Comps in the app) had not been run.
- Show in File Explorer was not clicked in the app.
- The release package was not started. The cloud build uses
  `BUILD_TESTING=OFF`.

The release notes state both gaps.
