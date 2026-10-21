# GitHub Actions — Windows build operations

This document owns workflow operation, dependency maintenance, build/package
requirements and verification. User download instructions are in
[Windows test builds](../test-builds.md); version bumps and publication are in
[Releasing](versioning.md#releasing).

Dated trial failures, dependency upgrades and release package checks are preserved
in the [build history](wiki/history/github-actions-development.md). A successful
past run does not verify a new artifact. Record new build evidence in history,
with the exact commit and run URL.

## Scope

`.github/workflows/windows-build.yml` is manual-only (`workflow_dispatch`),
Windows x64, Release, unsigned ZIP plus logs. It uses GitHub-hosted
`windows-2022`, four build workers, a 330-minute timeout, `contents: read`, and
SHA-pinned official actions. There are no push/PR triggers, publishing jobs,
signing keys, self-hosted runners, or inherited KDE CI credentials.

`.github/workflows/dependency-check.yml` runs weekly (Monday
03:17 UTC) and on demand, with `contents: read` and `issues: write`; see
"Dependency mirror" below.

## Dependency and build files

- `build-tools/github-actions/windows-dependencies.lock.json`: 87 public
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
- After installing the host, the build stage invokes
  `build-tools/gmic/build-runtime.py` against that job's installed Qt6/QMic
  ABI. `build-tools/gmic/runtime.lock.json` pins G'MIC 3.7.4.1 and its source
  SHA-256 separately from the prebuilt dependency mirror. The archive already
  includes CImg and the community standard library. The runtime builds on the
  CPU with OpenMP/FFTW; logs go to `gmic-build.log`. Packaging requires the
  runtime DLL, host interface DLL, four data packs and exact source/patches;
  `BUILD-INFO.txt` records the runtime version. See the
  [G'MIC build and distribution reference](gmic.md#build-install-and-distribution).

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

`KDE_INSTALL_USE_QT_SYS_PATHS=ON` and `KDE_INSTALL_QMLDIR=qml` are required and
checked during configuration.
With separate dependency and application prefixes, ECM otherwise defaults it
to OFF and installs QML modules under `lib/qml`. The Windows packager requires
the Qt layout (`qml`) and correctly rejects that incompatible installation.
The explicit relative QML directory also prevents ECM from selecting an absolute
directory in the dependency prefix instead of the application's install tree.
The packager passes the application install tree's `qml` directory through
`windeployqt --qmlimport` so application modules are discovered even when Qt
is in the separate dependency prefix. Only an existing import directory is
passed: Qt 6.11's scanner returns no JSON for a nonexistent directory.
Debug splitting, like copying, skips optional CLI tools when they are absent
(notably `kritarunner` when Python support is disabled).

Packaging reuses `packaging/windows/package-complete.py`, with noninteractive
arguments and LLVM-MinGW runtime DLLs. Its `--pre-zip-hook` runs
`build-tools/windows-shellex/add-shellex.py`, which downloads the pinned Krita
Shell Extension, verifies its checksums and adds the `shellex` folder
(`windows-shell-thumbnails.md`); a checksum mismatch fails the package stage. It uploads the ZIP and dependency/build
metadata as an artifact, not a GitHub Release. Build logs and CMake diagnostics
are uploaded even if a later stage fails. Retention is seven days.

## Verification and operation

1. Merge the intended feature/release PRs into `main` and record the desired
   commit. Check version values in CMake and the dependency lock. If modifying
   build tooling, validate Python/PowerShell/YAML and review archive extraction
   and checksum rejection before dispatch.
2. Dispatch the Windows workflow and identify its run by ref and commit:

   ```console
   gh workflow run windows-build.yml --repo udtren/Solstice --ref main
   gh run list --repo udtren/Solstice --workflow windows-build.yml --limit 5
   gh run view <run-id> --repo udtren/Solstice
   ```

   Confirm the run's `headSha` is the intended merged revision; a moving branch
   name alone does not prove which commit was built. Record the run URL.
3. Follow configuration, compilation, installation, G'MIC runtime build,
   packaging and artifact upload. Success requires all required stages.
   Configure must confirm GPU and Python support. Download diagnostic logs
   on failure and fix the owning input through a feature PR; do not publish a
   partial build or bypass integrity checks.
4. Download `Solstice-windows-x64-<run-number>` to an isolated directory.
   Verify every ZIP against `SHA256SUMS.txt`, run ZIP CRC checks, and inspect
   `BUILD-INFO.txt` for the built commit/version. Check required executable,
   GPU, Python/PyQt, QML, translation, G'MIC runtime/data/source and shell
   thumbnail files. Check executable version metadata and, where supported,
   run `solstice.com --version` with empty scratch folders for
   `SOLSTICE_PROFILE_ROOT`, `SOLSTICE_LEGACY_CONFIG_DIR` and
   `SOLSTICE_LEGACY_RESOURCE_DIR`; otherwise a real Krita profile triggers the
   native import prompt and the command waits.
5. For a release, follow [Releasing](versioning.md#releasing): tag the built
   commit and publish the verified application/debug ZIPs, checksums, build
   information and dependency lock. Compare uploaded assets with the verified
   files. Update release documentation and the website only after the release
   exists.

The Windows workflow has no publishing step; the ZIP is unsigned. Artifact
retention is seven days, so an expired artifact needs a new run. Logs use
`Windows-build-logs-<run-number>-<attempt>`.

CI uses `BUILD_TESTING=OFF`. No broad UI tests or `kis_kra_saver_test` run
here. A build/package pass, checksum match or `--version` smoke check does not
establish interactive painting, Undo/Redo, save/reload or Vulkan/OpenGL runtime
behavior. Preserve local regression and user acceptance evidence separately;
never report skipped GPU tests as passed. Workflow-only changes need no local
application restart.

## Dependency mirror

KDE's registry removes older builds. The pinned mirror keeps the locked set
available independently of registry retention; the weekly check detects missing
mirror assets. The lock is the authority for the active set and checksums.

- **Mirror:** public repository `udtren/solstice-deps` (created 2026-10-13).
  One GitHub Release per locked set, tag `deps-<newest build date>-<source
  revision>` (now `deps-20261009-4aff6eb`), assets `<package>-<version>.tar`
  byte-identical to the registry's `archive.tar` (largest: `ext_qt`, 813 MiB;
  the asset limit is 2 GiB). Its README covers provenance and licenses. Its
  workflow "Mirror Solstice dependencies" (`mirror.yml`, `contents: write`,
  its own `GITHUB_TOKEN`, no secrets) takes a Solstice ref, reads that lock,
  downloads each missing package from KDE, checks size and SHA-256 against
  the lock and uploads it, then verifies that every asset is present.
- **Lock:** `mirror` (`repository`, `tag`) and a `mirrorUrl` per package,
  written by `refresh-dependency-lock.py` (a full refresh, or
  `--mirror-only` for the current set). The cache key includes the lock, so
  changing these fields starts a new cache.
- **Bootstrap:** `download()` tries `mirrorUrl`, then the KDE `url`; both
  must match the locked SHA-256. Toolchain and Vulkan SDK still come from
  their original URLs.
- **Weekly check:** `check-dependency-lock.py` HEAD-checks every mirror URL
  (status and size; required) and KDE URL (informational), writes the job
  summary, and the workflow opens or comments on the issue "Windows build
  dependencies: mirror files missing" when the mirror is incomplete.

Refresh procedure: run `refresh-dependency-lock.py` on a feature branch, review
package/recipe/version changes and commit/push. Run the mirror workflow with
that exact ref promptly, before KDE prunes the downloads. Verify all mirrored
assets, merge the Solstice PR, then run a Windows build on main. A refresh may
upgrade libraries; review ABI/API consequences and do not describe it as only
a URL repair. `--mirror-only` adds mirror fields without changing the set.

## Python plugin requirements

- `build-windows.ps1` sets `PYTHONPATH` to `$Deps/lib/site-packages` (the
  dependency packages install SIP, PyQt6, pyqt6-sip and pyqt-builder there,
  built for Python 3.13 like setup-python's).
- The workflow's "Prepare Python build modules" step installs `packaging`
  25.0 and `setuptools` 80.9.0 (the local versions; SIP requires setuptools
  69.5 or newer) from `python-build-requirements.txt`, hash-pinned,
  wheels only. The KDE packages stopped shipping such build-only modules
  (upstream moved them to build dependencies). The `run:` line is quoted:
  `--only-binary :all:` contains `: `, which YAML reads as a mapping.
- Configure fails when `plugins/extensions/pykrita/plugin` is not in the
  build tree (pykrita adds `sip`, `plugin` and `kritarunner` only with
  Python, SIP and PyQt found), so the plugins cannot be lost silently again.
- The packager already copies the dependencies' `lib\site-packages` and
  Krita's `lib\krita-python-libs`.

## Documentation checks

[Documentation checks](documentation.md#automated-checks) is a separate,
lightweight workflow on PRs targeting main and manual dispatch. It checks
Markdown navigation, wiki schema and whitespace, and runs its own regression
fixtures. It does not start the Windows build or validate application behavior.

## Historical evidence

[0.3.0 package verification](wiki/history/release-0.3.0.md) records the first
release after the documentation split. Subsequent build evidence belongs in
separate dated history records; the pre-separation snapshot stays frozen.

<a id="native-tools-maintenance-package-check-2026-10-16"></a>
<a id="simplified-chinese-package-check-2026-10-16"></a>
<a id="effects-layer-comps-and-gmic-package-check-2026-10-16"></a>

Older links resolve here. The dated checks are now in history:
[beta.8 native tools](wiki/history/github-actions-development.md#native-tools-maintenance-package-check-2026-10-16),
[beta.7 Chinese interface](wiki/history/github-actions-development.md#simplified-chinese-package-check-2026-10-16),
and [beta.6 effects/Layer Comps/G'MIC](wiki/history/github-actions-development.md#effects-layer-comps-and-gmic-package-check-2026-10-16).

The [complete pre-separation snapshot](wiki/history/github-actions-development.md)
contains the first trial and packaging fixes, dependency refresh rationale,
Python plugin restoration and beta.6/beta.7/beta.8 package verification.
Use the run/commit recorded in each entry when interpreting its results;
temporary artifact availability and old package versions are not current
operating instructions.
