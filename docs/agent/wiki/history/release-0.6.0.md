---
type: history
status: released and verified 2026-10-27 JST
sources:
  - 87fb3e39798c83904ff6d72b87b8b5e8c173b628
  - https://github.com/udtren/Solstice/actions/runs/40491540847
  - https://github.com/udtren/Solstice/releases/tag/v0.6.0
  - https://github.com/udtren/Solstice/pull/48
  - https://github.com/udtren/tidalboxstudio-web/pull/17
  - https://github.com/udtren/tidalboxstudio-web/pull/18
---

# Solstice 0.6.0 release verification

Published 2026-10-27 at 12:19:14 UTC (21:19:14 JST) as a regular release,
marked latest. The user asked to release the day's work as 0.6.0 (`MINOR`:
Clipping Mask and duplicating a selection on the same layer). It replaces
0.5.3, which was prepared on 2026-10-25 with the HueSVC fix and never
released.

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Contents

Changes since 0.5.2:

- Clipping Mask (PR #45)
- Duplicating a selection on the same layer with Ctrl-drag (PR #42)
- The HueSVC color space fix (PR #37)
- The upstream `KisClipboard` rewrite, with the exit assert fix (PR #40) and
  the mask-only copy crash fix (PR #46)
- The 2026-W44 upstream intake (PR #39)
- Test control phases 0-4 and the fixes their scenarios found (PRs #41,
  #43)
- The covered-view selection animation fix (PR #44)
- The README card (PR #47) and the version preparation (PR #48)

## Build and artifacts

- **Tag and build commit:** `87fb3e39798c83904ff6d72b87b8b5e8c173b628`, the
  `main` merge of PR #48.
- **Run 40491540847** (number 29): configuration, compilation, installation,
  the CPU G'MIC runtime build, packaging and artifact upload all passed, in
  1 h 22 min. `BUILD-INFO.txt`: version 0.6.0, the commit above, GPU engine
  compiled, G'MIC CPU runtime 3.7.4.1.
- **ZIP integrity:** both downloaded ZIPs matched `SHA256SUMS.txt` (whose
  CRLF line endings need stripping before `sha256sum -c`) and passed full CRC
  checks: 5,315 application entries and 221 debug entries.
- **Files present:** `Solstice.exe`, `bin/solstice.exe`, `bin/solstice.com`,
  `libkritaui.dll`, `libkritaimage.dll`, `libkritagpu.dll`,
  `kritatestcontrol.dll`, Python/PyQt, QML, the G'MIC files, the zh_CN
  catalog, `kritamenu.action` and `shellex/register-thumbnails.cmd`.
- **Dependency lock:** the packaged lock has CRLF and the Git blob LF. They
  are equal after line-ending normalization, and the parsed JSON is equal.
- **Windows metadata:** ProductName `Solstice`, file and product version
  `0.6.0 (git 87fb3e3)`, numeric version `0.6.0.0`.
- **Version call:** `solstice.com --version`, run with empty profile and
  legacy input folders, printed `krita 0.6.0 (git 87fb3e3)` with empty
  stderr. The interactive application was not launched.
- **Publication:** the release was created as a draft. All five uploaded
  assets matched the verified files' SHA-256 and size before it was
  published. The tag resolves to the built commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-0.6.0-windows-x64-87fb3e39798c.zip` | 418543696 | `5d6419b9f81b8240799e4b89b52b2cef92e408539eb68cfcbed02ca7ca37baf8` |
| `Solstice-0.6.0-windows-x64-87fb3e39798c-dbg.zip` | 12432845 | `b2ada21f4244c59686f9d09af25ab7f021b889868854149d2c0517e5c068b920` |

## Website

- **tidalboxstudio-web #17**, merged into `develop` after the release
  existed:
  - Clipping Mask and Duplicate on the same layer pages.
  - The top page reduced to six highlights.
  - The new `/features/` page with all 16 features by group; the Features
    navigation link now opens it.
  - The version set to 0.6.0.
- **#18** published `develop` through `main` (`f5ddf63`). The Cloudflare
  production build succeeded.
- **Live check:** the home, Download, Features, Clipping Mask and Duplicate
  pages, in both languages where checked, return 200 and show 0.6.0. The
  Download page links to v0.6.0, and the sitemap lists 36 URLs.

## Verification boundary

- **Local tests:** the full suite ran on Windows the same day (385 of 399
  passed; failures judged in
  [build pitfalls](../pitfalls/build-format-test.md), "Full suite on
  Windows"). The Clipping Mask, clipboard and test control suites passed.
- **In the application:** the user checked Clipping Mask.
- **Not checked in the application:** duplicating a selection, and copying
  only masks.
- **Cloud release run:** it did not execute native regression suites,
  interactive painting or GPU runtime tests.
