---
type: history
status: released and verified 2026-10-17 JST
sources:
  - f947bc31421b1b819336c2ad53489179a86999b1
  - https://github.com/udtren/Solstice/actions/runs/38915897544
  - https://github.com/udtren/Solstice/releases/tag/v0.3.0
  - https://github.com/udtren/Solstice/pull/5
  - https://github.com/udtren/Solstice/pull/7
  - https://github.com/udtren/Solstice/pull/8
---

# Solstice 0.3.0 release verification

Published 2026-10-17 JST (2026-10-16 16:10 UTC) as a regular release, with
no Beta/prerelease label and marked latest. The user requested this version
and integration of PRs #5, #7 and #8 on 2026-10-16.

Current procedures: [Actions operations](../../github-actions.md) and
[Releasing](../../versioning.md#releasing). This record is dated evidence,
not verification of later builds.

## Build and artifacts

- Exact tag/build commit: `f947bc31421b1b819336c2ad53489179a86999b1`,
  the main merge of version-preparation PR #20 after #5, #7 and #8.
- [Run 38915897544](https://github.com/udtren/Solstice/actions/runs/38915897544)
  (run number 23) passed configuration, compilation, installation, the CPU
  G'MIC build, packaging and artifact upload in 1 h 37 min 47 s.
- Configuration: Solstice 0.3.0, Qt/PyQt 6.11.0, Python build interpreter
  3.13.16, `HAVE_KRITA_GPU_ENGINE=ON` and `BUILD_TESTING=OFF`.
- Both downloaded ZIPs matched `SHA256SUMS.txt` and passed full CRC checks:
  5,313 application entries and 220 debug entries.
- Required executables, GPU library, Python/PyQt, Vision ML, QML, Chinese
  catalog, G'MIC runtime/interface/data/source and Explorer thumbnail
  registration script were present. The packaged dependency lock matched
  the repository lock at the built revision byte for byte.
- Windows executable metadata: ProductName `Solstice`, file/product version
  `0.3.0 (git f947bc3)`, numeric file version `0.3.0.0`.
  `solstice.com --version` returned `krita 0.3.0 (git f947bc3)` with exit
  code 0 and empty stderr, using separate scratch profile and legacy-input
  directories. It did not launch the interactive application.
- All five uploaded assets matched the verified local SHA-256 and size
  before publication. The published tag resolves to the exact built commit.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Solstice-0.3.0-windows-x64-f947bc31421b.zip` | 418179274 | `0bbe8755c560a347f0bc647832e83ac6bb734bd42270b5e9680ae59bdf8b3ee6` |
| `Solstice-0.3.0-windows-x64-f947bc31421b-dbg.zip` | 12362144 | `6ed3dbe191254cce148f4fa3d18728d76f80814516907a47faa887b585c8876a` |

## Verification boundary

Existing PR records cover GPU/CPU parity, zero-download updates and the
thumbnail worker/idle replacement path. The user manually confirmed #5
and #7 locally on 2026-10-16. PR #8's real-application acceptance is still
unrecorded. The #5 image-suite record has a known cage-transform test failure;
it is not an all-tests-passed claim.

This release run did not execute native regression suites, interactive
painting or GPU runtime tests. Packaging, checksum and version checks do
not replace those tests. Feature-specific limitations remain in the
maintained guides; removing the Beta label does not change them.
