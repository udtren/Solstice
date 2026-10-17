---
type: history
status: released and verified 2026-10-17 JST
sources:
  - 88386550c1dd51975ee47ea33be9beb254b9e7ad
  - https://github.com/udtren/Solstice/actions/runs/38998728479
  - https://github.com/udtren/Solstice/releases/tag/v0.4.0
  - https://github.com/udtren/Solstice/pull/22
  - https://github.com/udtren/Solstice/pull/23
  - https://github.com/udtren/Solstice/pull/24
---

# Solstice 0.4.0 release verification

Published 2026-10-17 at 04:37:35 UTC (13:37:35 JST) as a regular release,
marked latest. The user explicitly authorized the release and production
website publication. It adds Reference Tool native filter stacks and reusable
templates; the [feature reference](../../reference-image-filters.md) owns the
implementation and regression checklist.

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Build and artifacts

- Exact tag/build commit: `88386550c1dd51975ee47ea33be9beb254b9e7ad`,
  the main merge of version-preparation PR #24 after #22 and #23.
- Run 38998728479 (number 24) passed configuration, compilation, installation,
  CPU G'MIC runtime build, packaging and artifact upload in 1 h 7 min 51 s.
- Configuration: Solstice 0.4.0, Qt/PyQt 6.11.0, Python build interpreter
  3.13.16, GPU engine enabled and `BUILD_TESTING=OFF`.
- Both downloaded ZIPs matched `SHA256SUMS.txt` and passed full CRC checks:
  5,313 application entries and 220 debug entries.
- Required executables, GPU library, Python/PyQt, Vision ML, QML, Chinese
  catalog, G'MIC runtime/interface/data/source and Explorer thumbnail
  registration script were present.
- The packaged dependency lock has CRLF from Windows checkout; the Git blob
  uses LF. Content matches the built revision after line-ending normalization,
  including parsed JSON equality. Do not interpret that byte difference as
  changed dependency pins, or silently ignore other content differences.
- Windows metadata: ProductName `Solstice`, file/product version
  `0.4.0 (git 8838655)`, numeric version `0.4.0.0`.
  `solstice.com --version` returned `krita 0.4.0 (git 8838655)` with exit 0
  and empty stderr, using scratch profile and legacy-input directories.
  The interactive application was not launched.
- All five uploaded assets matched local SHA-256 and size before publishing.
  The published tag resolves to the exact built commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-0.4.0-windows-x64-88386550c1dd.zip` | 418218440 | `fd755db574c4a98aa6fc0b9e6c82273d256cd467e1371e9d8044399984921cfc` |
| `Solstice-0.4.0-windows-x64-88386550c1dd-dbg.zip` | 12365702 | `3250d526178a2833a884d77551d14dd250d6fd70fdf6f70d2bbf575b81b06446` |

## Verification boundary

The local feature regression suite passed effect ordering, original-image
preservation, XML/legacy/clone isolation, asynchronous invalidation/destruction,
and template persistence/reuse/overwrite/deletion/invalid-file handling.
The user accepted the demonstrated filter/template workflow and supplied the
illustration for [Reference Tool](../../../reference-tool.md). This does not
record completion of every item in the manual checklist.

The cloud release run did not execute native regression suites, interactive
painting or GPU runtime tests. Packaging and version checks do not replace
those tests. G'MIC remains outside the reference-filter feature even though
its existing standalone runtime is included in the application package.
