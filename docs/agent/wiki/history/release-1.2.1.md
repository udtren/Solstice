---
type: history
status: released and verified 2026-11-21 JST
sources:
  - 948ac276816b253b412681a66a677a569ff874af
  - https://github.com/udtren/Solstice/actions/runs/44880267082
  - https://github.com/udtren/Solstice/releases/tag/v1.2.1
  - https://github.com/udtren/Solstice/pull/112
  - https://github.com/udtren/Solstice/pull/113
  - https://github.com/udtren/tidalboxstudio-web/pull/47
  - https://github.com/udtren/tidalboxstudio-web/pull/48
  - docs/agent/versioning.md (Releasing, History)
---

# Solstice 1.2.1 release verification

Published 2026-11-21 at 10:03:03 UTC (19:03:03 JST) as a regular release,
marked latest. The user asked for the Docker Tray to be released as 1.2.1
(`PATCH` by the user's choice, although it adds a feature) and added a
recording, `docs/images/docker_tray.gif`, for README's "Other Major
Changes" and the website.

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Contents

Changes since 1.2.0:

- **Docker Tray** (PR #113): right-click with a painting tool or the
  **Show Docker Tray** shortcut opens the chosen dockers, hidden ones
  included, in columns with their own sizes; the settings dialog and drag
  resizing; Layers, Quick Brush Adjustments, Quick Access and Rest Note
  follow their content instead of their docker
  ([maintenance reference](../../docker-tray.md)).
- **HueSVC popup under a hovering pen** (PR #112, merged into the Docker
  Tray branch and reaching `main` through #113): the popup closes only
  when the pointer is really outside it
  ([pitfall](../pitfalls/pen-hover-false-leave.md)). PR #112 itself stayed
  open on GitHub.
- **README, guide and version** (in #113): the recording in the guide and
  under "Other Major Changes", and `SOLSTICE_VERSION_PATCH 1`.

## Build and artifacts

- **Tag and build commit:** `948ac276816b253b412681a66a677a569ff874af`,
  the `main` merge of PR #113.
- **Run 44880267082** (number 40):
  - Attempt 1 (07:26 to 09:05 UTC) compiled and installed, then failed in
    "Create unsigned ZIP": `build-tools/windows-shellex/add-shellex.py`
    downloads `krita-nsis-v1.2.4d.zip` from files.kde.org, which redirects
    to a mirror chosen per request, and Python rejected that mirror's
    certificate chain ("self-signed certificate in certificate chain").
    The download is pinned by SHA-256, so a different mirror cannot change
    the package.
  - Attempt 2 (re-run of the failed job, 09:06 to 10:00 UTC) passed every
    stage. `BUILD-INFO.txt`: version 1.2.1, the commit above, GPU engine
    compiled, G'MIC CPU runtime 3.7.4.1, Krita Shell Extension 1.2.4d.
- **ZIP integrity:** both ZIPs matched `SHA256SUMS.txt` and passed full CRC
  checks: 5,323 application entries and 221 debug entries, the same counts
  as 1.2.0.
- **Files present:** `Solstice.exe`, `bin/solstice.exe`,
  `bin/kritarunner.exe`, `bin/Qt6Core.dll`, `bin/libkritagpu.dll`,
  `bin/python313.dll` and `lib/site-packages/PyQt6`, `bin/qml` (1,415
  files), `bin/locale/zh_CN/LC_MESSAGES/krita.mo`,
  `lib/kritaplugins/krita_gmic_qt.dll`, `share/gmic` data and
  `share/gmic/source`, and `shellex/register-thumbnails.cmd` with
  `kritashellex64.dll`.
- **Dependency lock:** the artifact's lock equals
  `build-tools/github-actions/windows-dependencies.lock.json` at the build
  commit after line-ending normalization.
- **Windows metadata:** both executables report ProductName `Solstice` and
  product version `1.2.1 (git 948ac27)`.
- **Publication:** created directly (not as a draft) from the verified
  files; the digests GitHub reports for the uploaded ZIPs equal the
  verified SHA-256 values, and the tag points at the build commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-1.2.1-windows-x64-948ac276816b.zip` | 419101714 | `6a868d67388f97d613b0d0f1bd3bb151e75aded2263582d837c9d5efbc34617a` |
| `Solstice-1.2.1-windows-x64-948ac276816b-dbg.zip` | 12486242 | `bdfd6500dc6a2b8ccd984acc1076572cc31d2060351a537154734c892f0c126e` |

## Website

- **tidalboxstudio-web #47**, merged into `develop`: version and release
  link 1.2.1, the Docker Tray in "Other major changes" on `/features/` in
  English and Simplified Chinese with a 640 px copy of the recording
  (0.9 MB) and a still frame for reduced motion, and the Popup Palette
  entries on the features and differences pages.
- **#48** published `develop` through `main` at 10:03:27 UTC, after the
  release existed.
- Before publication: `npm run check` and `npm run build` passed and the
  built pages showed 1.2.1. The browser pane could not open the local
  preview, so the pages were not looked at in a browser.
- **Live check** (after the Workers build finished at 10:04:35 UTC): by
  HTTP, the home, download and zh-CN home pages show 1.2.1 (the only
  remaining 1.2.0 is the "New in 1.2.0" label of Vector brush strokes),
  `/features/` and `/zh-cn/features/` name the Docker Tray and serve its
  recording and still frame, the differences page names it, and
  `/go/release` redirects to v1.2.1.

## Verification before the release (local, Windows 11)

- `KisDockerBorrowRegistryTest` 11/11, `KisDockerTrayTest` 22/22,
  `QuickAdjustDockTest` 16/16, `QuickAccessInteractionTest` 22/22 and the
  other Quick Access, Rest Note, `TestNodeView` and `KisInputManagerTest`
  suites.
- The user checked every Docker Tray phase in the development build
  (opening and closing, hidden dockers, the settings dialog, the dockers
  that follow their content, drag resizing) and the HueSVC popup with a
  pen.

## Not verified

- The release package was not started. The cloud build uses
  `BUILD_TESTING=OFF`.
- The website's new section was not looked at in a browser.

The release notes state the first gap and the failed first attempt.
