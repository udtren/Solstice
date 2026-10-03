# GitHub Actions Windows trial build

## Scope

`.github/workflows/windows-build.yml` is manual-only (`workflow_dispatch`),
Windows x64, Release, unsigned ZIP plus logs. It uses GitHub-hosted
`windows-2022`, two build workers, a 330-minute timeout, `contents: read`, and
SHA-pinned official actions. There are no push/PR triggers, publishing jobs,
signing keys, self-hosted runners, or inherited KDE CI credentials.

The user authorized creation, push and a first trial run on 2026-10-03.
User instructions: `docs/test-builds.md`.

## Dependency and build files

- `build-tools/github-actions/windows-dependencies.lock.json`: 86 public
  KDE Windows package versions, download URLs, sizes, SHA-256 checksums,
  source recipe revisions and dependency relationships. The package set was
  seeded from the local Qt6 dependency inventory. Some older local versions
  were already absent from the registry, so available versions on the same
  Qt6 dependency branch were pinned for CI. Do not claim binary identity with
  the local development installation. The resolved Qt version is in logs.
- `bootstrap-windows.py`: checksum verification, constrained archive
  extraction, omission of dependency debug symbols, LLVM-MinGW 20251118
  and Vulkan SDK 1.4.357.0 installation. SDK installation uses `copy_only=1`.
- `build-windows.ps1`: configure, build/install, and package stages; all paths
  are under the runner's temporary `solstice` directory. It never uses the
  developer's `env.bat`, `_build`, `_install`, or active `kritarc`.

Dependencies are downloaded sequentially and each verified tar is deleted
after extraction, reducing peak disk usage. An exact cache key includes the
lock and bootstrap script hashes. The cache is saved only after a complete
bootstrap. No partial-key cache fallback is allowed. An incomplete cache
fails rather than mixing dependency sets. If the public registry prunes a
locked package, deliberately refresh the lock and verify the new version;
never bypass a checksum or silently fall back to a floating version.

CMake uses Qt6, Next branding, Python 3.13, a separate dependency prefix and
install tree. `BUILD_TESTING=OFF` keeps GUI tests out of cloud jobs. Configure
must record `HAVE_KRITA_GPU_ENGINE=ON`; otherwise the workflow fails instead
of passing with an accidentally disabled GPU engine. Vision ML is built by
the regular application build, with Vulkan SDK available.

Packaging reuses `packaging/windows/package-complete.py`, with noninteractive
arguments and LLVM-MinGW runtime DLLs. It uploads the ZIP and dependency/build
metadata as an artifact, not a GitHub Release. Build logs and CMake diagnostics
are uploaded even if a later stage fails. Retention is seven days.

## Verification and operation

Before pushing changes, parse the Python/PowerShell/YAML and inspect archive
handling and checksum rejection. Dispatch the workflow on `krita-sol-gpu`,
record the exact commit and run URL, and follow its result. Inspect configure
output, GPU enablement, compiler failures, package output and artifacts. A
run is only successful when build, installation, packaging and artifact
upload complete. Do not report a skipped GPU test as passed.

No broad UI tests or `kis_kra_saver_test` run here. Interactive painting,
Undo/Redo, save/reload and Vulkan/OpenGL sharing remain manual real-hardware
checks. No local application restart is required for workflow-only changes.
