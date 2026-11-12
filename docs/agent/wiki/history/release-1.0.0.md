---
type: history
status: prepared 2026-11-13 JST; not built, not released
sources:
  - https://github.com/udtren/Solstice/pull/79
  - https://github.com/udtren/Solstice/pull/80
  - https://github.com/udtren/Solstice/pull/81
  - https://github.com/udtren/Solstice/pull/82
  - https://github.com/udtren/Solstice/pull/83
  - https://github.com/udtren/Solstice/pull/84
  - https://github.com/udtren/Solstice/pull/85
  - https://github.com/udtren/Solstice/pull/86
  - docs/agent/versioning.md (Releasing, History)
  - docs/agent/gpu-engine.md (manual regression checklist, items 19 to 24)
---

# Solstice 1.0.0 release: preparation and handoff

The user asked on 2026-11-13 for the release after 0.9.1 to be 1.0.0, and
to have the build, the GitHub Release and the website update done by a
cloud session. This page is what the local session prepared and what is
left. When the release exists, turn it into the verification record, like
[0.9.1](release-0.9.1.md): replace the status line, add the build,
artifact, website and verification sections, and keep "Contents".

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing).

## Done in the preparation pull request

- `SOLSTICE_VERSION_*` set to 1.0.0 (empty label, build 0); the version
  history row, the user guide "Versions" and the versioning decision
  updated.
- Manual check 24 recorded as reported OK by the user.
- Not changed, on purpose: README's "What's new in 0.9.1", the guide index
  `docs/README.md` and the website. They describe the latest release that
  exists and change after the GitHub Release is published (step 3 of
  "Releasing").

## Left to do

1. Merge the preparation pull request into `main` (the user decides).
2. Run the "Windows x64 trial build" workflow on `main` and wait for it:
   `gh workflow run windows-build.yml --ref main`.
3. Create the GitHub Release `v1.0.0` at the built commit as a regular
   release (no label), as a draft first. Attach the application ZIP,
   `-dbg.zip`, `SHA256SUMS.txt`, `BUILD-INFO.txt` and
   `windows-dependencies.lock.json`; verify the downloaded ZIPs and
   checksums; publish. Draft notes are below.
4. In a feature-branch pull request: README "What's new in 1.0.0" (and its
   Contents entry), `docs/README.md`, the History row of
   `docs/agent/versioning.md` (commit, pull request, run), this page, the
   wiki index and log.
5. Website the same day, after the release exists: `solstice/src/site.ts`
   on `develop` of `tidalboxstudio-web`, check, build, merge to `main`,
   check the live Download page.

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
  behavior for anyone who ran the engine on another GPU.
- Documentation (PR #84) and this preparation.

Exact scope and limits: `docs/gpu-engine.md` ("What runs on the GPU", "What
stays on the CPU", "Requirements").

## Draft release notes

Follow the structure of the 0.9.1 and 0.9.0 notes. Fill in the file name,
commit, run and checksums from the build; do not publish the measured
values below without the conditions next to them.

```markdown
Solstice 1.0.0 moves more of the layer stack to the GPU: moved layers and
groups, transparency masks, the Stroke layer style, and filter layers and
filter masks with the most common color filters.

## What's new since 0.9.1

All of the following applies to RGBA 16-bit and 32-bit float documents
with the GPU Engine on.

- **Moved layers and groups (#79):** layers and groups are composited on
  the GPU wherever they are. Up to 0.9.1 a layer moved by a distance that
  was not a multiple of 64 pixels was composited on the CPU, also after
  the move. The Move Tool follows the pointer without reading the image
  back.
- **Transparency masks (#80)** are applied on the GPU.
- **The Stroke layer style (#81)**, outside the layer with a solid color
  and up to 16 pixels wide, is drawn on the GPU with its layer or group.
- **Filter layers and filter masks (#82, #83):** Desaturate in both
  depths; Posterize, Color Adjustment curves and Color Reference in RGBA
  16-bit float. The Copy blend mode at 100 % opacity is composited on the
  GPU.
- **Crash fix (#85):** adding a filter or transparency mask to a layer
  whose position is not a multiple of 64 pixels could crash Solstice.
- **The GPU Engine starts by itself only on NVIDIA Blackwell (#86).** On
  other graphics cards it now stays off, and Solstice works on the CPU,
  until you check **Also use it with graphics cards it was not verified
  with** under Settings → Configure Solstice → Performance → General and
  restart. If you used the engine on another graphics card, check that
  option after updating. Other settings are kept.

1.0.0 includes everything in 0.9.1.
```

For "Scope and supported environment", replace the 0.9.x sentence about
other GPUs: the engine remains experimental, is developed and tested on an
RTX PRO 6000 Blackwell, and is not used on other GPUs unless the option is
checked.

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
  "6.0.5-prealpha"`; `solstice.exe` reports ProductName `Solstice`, file
  and product version `1.0.0 (git a0df294)`, numeric `1.0.0.0`; the usage
  log of a start says "Solstice Version: 1.0.0 (git a0df294), based on
  Krita: 6.0.5-prealpha". `KisGpuEngineTest` (19), `KisGpuEngineUiTest`
  (14) and `KisGpuProjectionTest` (930) passed. The full regression set
  was not run again: the change is the version number.
- **`solstice.com --version` on the desktop:** started from a process
  without a console of its own, it shows the version in a dialog and waits
  for a click instead of printing; with output redirected nothing is
  written. The local session took the wait for a hang and stopped three
  such processes before the user said that they had clicked the first
  dialog. The package check of earlier releases printed `krita <version>`;
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
- The release package: nothing here tests it. The cloud build uses
  `BUILD_TESTING=OFF`.
