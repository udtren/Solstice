---
type: history
status: released and verified 2026-11-13 JST
sources:
  - 0bb81c7190dd144b3b7d8c5fb9956c7961076db5
  - https://github.com/udtren/Solstice/actions/runs/43246462782
  - https://github.com/udtren/Solstice/releases/tag/v1.0.0
  - https://github.com/udtren/Solstice/pull/79
  - https://github.com/udtren/Solstice/pull/80
  - https://github.com/udtren/Solstice/pull/81
  - https://github.com/udtren/Solstice/pull/82
  - https://github.com/udtren/Solstice/pull/83
  - https://github.com/udtren/Solstice/pull/84
  - https://github.com/udtren/Solstice/pull/85
  - https://github.com/udtren/Solstice/pull/86
  - https://github.com/udtren/Solstice/pull/87
  - https://github.com/udtren/tidalboxstudio-web/pull/37
  - https://github.com/udtren/tidalboxstudio-web/pull/38
  - docs/agent/versioning.md (Releasing, History)
  - docs/agent/gpu-engine.md (manual regression checklist, items 19 to 24)
---

# Solstice 1.0.0 release verification

Published 2026-11-12 at 23:47:29 UTC (2026-11-13 08:47:29 JST) as a regular
release, marked latest. The user asked on 2026-11-13 for the release after
0.9.1 to be 1.0.0 (`MAJOR`: the first version outside the 0.x series; no
other meaning was stated, so the documents claim none).

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Contents

Changes since 0.9.1, all in the GPU engine (RGBA 16-bit and 32-bit float
documents):

- **Moved layers and groups** (PR #79, phase 4.112): composited on the GPU
  at any offset, not only at multiples of 64 pixels; the Move Tool no
  longer reads the image back while dragging, and device bounds survive a
  move. [Measurements](../benchmarks/move-layer-updates.md).
- **Transparency masks** (PR #80, phase 4.113) applied on the GPU.
- **The Stroke layer style** (PR #81, phase 4.114): outside, solid color,
  up to 16 pixels.
- **Filter layers and the Copy blend mode** (PR #82, phase 4.115):
  Desaturate in both depths; Posterize and Color Adjustment curves in
  RGBA 16-bit float; Copy at 100 % opacity.
- **Filter masks and Color Reference** (PR #83, phase 4.116): filter masks
  with those filters, and Color Reference filter layers and masks in RGBA
  16-bit float.
- **Crash fix** (PR #85): adding a mask to a layer that is off the 64 pixel
  tile grid could crash; a GPU reader used tile data another thread had
  freed ([pitfall](../pitfalls/gpu-readers-need-a-tile-data-reference.md)).
  Found by the user's manual check.
- **Verified GPUs** (PR #86): the engine starts by itself only on NVIDIA
  Blackwell. On other GPUs it stays off until "Also use it with graphics
  cards it was not verified with" is checked
  ([decision](../decisions/gpu-verified-devices.md)). This changes
  behavior for anyone who ran the engine on another GPU; the release notes
  say so.
- Documentation (PR #84) and the version preparation (PR #87).

Exact scope and limits: `docs/gpu-engine.md` ("What runs on the GPU", "What
stays on the CPU", "Requirements").

## Build and artifacts

- **Tag and build commit:** `0bb81c7190dd144b3b7d8c5fb9956c7961076db5`, the
  `main` merge of PR #87.
- **Run 43246462782** (number 36, 15:19 to 16:45 UTC on 2026-11-12):
  configuration, compilation, installation, packaging and artifact upload
  all passed. `BUILD-INFO.txt`: version 1.0.0, the commit above, GPU engine
  compiled, G'MIC CPU runtime 3.7.4.1.
- **ZIP integrity:** both ZIPs matched `SHA256SUMS.txt` and passed full CRC
  checks: 5,323 application entries and 221 debug entries. The packaged
  `SHA256SUMS.txt` has CRLF line endings; `sha256sum -c` on Git Bash reads
  the `\r` as part of the file name, so strip it first
  (`tr -d '\r' < SHA256SUMS.txt | sha256sum -c -`).
- **Files present:** `Solstice.exe`, `bin/solstice.exe`,
  `bin/libkritagpu.dll`, `bin/python3.dll`, `lib/site-packages/PyQt6`,
  466 QML files, `lib/kritaplugins/krita_gmic_qt.dll` with the G'MIC data
  and `share/gmic/source`, the ja and zh_CN `krita.mo` under `bin/locale`,
  and `shellex/register-thumbnails.cmd`. `libkritaui.dll` contains the new
  option text ("verified with").
- **Dependency lock:** the packaged lock (CRLF) equals
  `build-tools/github-actions/windows-dependencies.lock.json` at the build
  commit after line-ending normalization.
- **Windows metadata:** ProductName `Solstice`, file and product version
  `1.0.0 (git 0bb81c7)`, numeric version `1.0.0.0`.
- **Version call:** not run. `solstice.com --version` started without a
  console shows a dialog instead of printing (see below), and the cloud
  session that first checked the package had no Windows.
- **Publication:** created as a draft from the local session; all five
  assets downloaded again from the draft were byte-identical to the
  verified files before it was published. The tag resolves to the built
  commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-1.0.0-windows-x64-0bb81c7190dd.zip` | 418756064 | `515ae1546fcba8900b15a244f0ebd0124abed7fde367f31cb06ccbd161baf3ab` |
| `Solstice-1.0.0-windows-x64-0bb81c7190dd-dbg.zip` | 12447991 | `0f97617afa89b3a37e3ccbf123f25e974da0a8c1b165e681a271eb55688405be` |

## Website

- **tidalboxstudio-web #37**, merged into `develop` at 23:48:09 UTC, after
  the release existed: version and release link 1.0.0 in
  `solstice/src/site.ts`, the current GPU Engine settings image, and the
  GPU engine wording ("on by default" replaced: it starts by itself only
  on NVIDIA Blackwell) on the GPU engine, Download, home, FAQ and
  Differences pages in both languages, recorded in `docs/solstice-site.md`.
  Prepared by the cloud session; `npm run check` (0 errors) and
  `npm run build` (40 pages) passed again locally before the merge.
- **#38** published `develop` through `main` at 23:48:23 UTC; the live site
  showed 1.0.0 by 23:49:30 UTC.
- **Live check:** home, Download and GPU engine pages in both languages and
  `/features/` return 200 and show 1.0.0; `/go/release` from all four
  buttons redirects to v1.0.0. No page was viewed in a browser.

## Verification before the release (local, Windows 11, RTX PRO 6000 Blackwell)

- **Manual checks by the user:** checklist items 19 to 23 on 2026-11-12
  and item 24 on 2026-11-13, in development builds, all reported OK. The
  crash of PR #85 was found during item 23 and did not occur again with
  the fix.
- **Automated, at the commit of PR #86 (2026-11-12):** the regression set
  (`libs-image`, `libs-gpu`, filters, GPU stroke, save, canvas upload and
  settings tests) passed 158 of 159; the failure,
  `libs-image-kis_cage_transform_worker_test`, also fails without these
  changes. The GPU-forced subset passed 44 of 44.
- **After the version change (2026-11-13):** full local rebuild and
  install. CMake reports `Solstice version: 1.0.0`; `kritaversion.h` has
  `SOLSTICE_VERSION_STRING "1.0.0"` and `KRITA_VERSION_STRING
  "6.0.5-prealpha"`; the local `solstice.exe` reports file and product
  version `1.0.0 (git a0df294)`. `KisGpuEngineTest` (19),
  `KisGpuEngineUiTest` (14) and `KisGpuProjectionTest` (930) passed. The
  full regression set was not run again: the change is the version number.
- **`solstice.com --version` on the desktop:** started from a process
  without a console of its own, it shows the version in a dialog and waits
  for a click instead of printing; with output redirected nothing is
  written. The package check of earlier releases printed `krita <version>`;
  if a release session sees no output, it is this dialog, not a failure.

## Not verified

- No GPU other than the RTX PRO 6000 Blackwell ran any of this. The
  refusal on other GPUs is only simulated
  (`KRITA_GPU_TREAT_DEVICE_AS_UNVERIFIED=1`).
- The rare white rectangle during painting
  ([GPU engine risks](../../gpu-engine.md#risks-and-open-questions)) is
  still open and unreproduced.
- The concurrent crash of PR #85 was not reproduced by a stress test; a
  single-threaded replay fails without the fix.
- The release package was not started: neither the interactive
  application nor `--version`. The cloud build uses `BUILD_TESTING=OFF`.

## Process notes

- The user had the build and release done by a cloud session. It built
  and checked the package, prepared the website branch, and wrote the
  notes, but `gh release create` returned HTTP 403 ("not permitted for
  this session type"): a cloud session cannot create a GitHub Release.
  The local session downloaded the artifact again, repeated the package
  checks, created the draft, published it and merged the website PRs.
- A cloud session's notes file stays in its sandbox; the local session
  rebuilt the notes from the draft on this page and the build values.
