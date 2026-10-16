# G'MIC integration: maintenance reference

This document owns the native host bridge, pinned CPU runtime, build/package
route and regression checks. User workflow and limitations are in
[G'MIC-Qt filters](../gmic.md). The original investigation, implementation
record and measurements remain in [development history](wiki/history/cpu-effects-and-gmic-development.md).
[Native CPU effects](cpu-effects.md) work independently of this runtime.

The user's CPU-only decision applies to G'MIC/CImg/FFTW computation. Existing
GPU display/compositing and tile synchronization remain permitted. No GPU
backend, GPU-only external helper or native filter-mask adaptation of the
catalog is part of the implemented contract. Optional downloaded filters and
external commands are outside the verified bundled baseline.

## Source map and pinned runtime

| Responsibility | Sources |
| --- | --- |
| Launch/actions/profile setup | [QMic.cpp](../../plugins/extensions/qmic/QMic.cpp) |
| ABI, allocations, captured image and output | [kis_qmic_interface.h](../../plugins/extensions/qmic/kis_qmic_interface.h), [implementation](../../plugins/extensions/qmic/kis_qmic_interface.cpp), [plugin interface](../../plugins/extensions/qmic/kis_qmic_plugin_interface.h) |
| Conversion | [KisQmicRgbConverter.h](../../plugins/extensions/qmic/KisQmicRgbConverter.h), [kis_qmic_simple_convertor.cpp](../../plugins/extensions/qmic/kis_qmic_simple_convertor.cpp) |
| Input modes, selection and metadata | [kis_qmic_import_tools.cpp](../../plugins/extensions/qmic/kis_qmic_import_tools.cpp) |
| Undoable layer synchronization | [kis_qmic_synchronize_layers_command.cpp](../../plugins/extensions/qmic/kis_qmic_synchronize_layers_command.cpp) |
| Source pin and exact runtime modifications | [runtime.lock.json](../../build-tools/gmic/runtime.lock.json), [runtime-patches.json](../../build-tools/gmic/runtime-patches.json) |
| Builder/provenance | [build-runtime.py](../../build-tools/gmic/build-runtime.py), [build notes](../../build-tools/gmic/README.md) |
| CI/package integration | [build-windows.ps1](../../build-tools/github-actions/build-windows.ps1), [package-complete.py](../../packaging/windows/package-complete.py), [Windows workflow guide](github-actions.md) |
| Tests | [KisQmicIntegrationTest.cpp](../../plugins/extensions/qmic/tests/KisQmicIntegrationTest.cpp), [kis_qmic_tests.cpp](../../plugins/extensions/qmic/tests/kis_qmic_tests.cpp) |

The lock pins the Krita-native `vanyossi/gmic` 3.7.4.1 source archive,
`gmic-3.7.4.1.tar.gz`, SHA-256
`15674b544264fbc3f020be6fae27477aee6ac49316416f538e578a18ddb23879`.
Use the lock's URL; an arbitrary standalone/GIMP build is not interchangeable.
The archive supplies CImg and `gmic_stdlib_community.h` without floating
compile-time downloads. Build after the installed host interface, with the
same Qt6/KF6, compatible compiler ABI and C++ runtime.

Keep interface version `0x05010001`, IID
`org.kde.krita.KritaGmicPluginInterface` and service `Krita/GMic` compatible.
Host targets `kritaqmic`/`kritaqmicinterface` are distinct from the runtime
`krita_gmic_qt.dll`. A menu action or bridge DLL does not prove runtime loading.

The builder uses `GMIC_QT_HOST=krita-plugin`, `BUILD_WITH_QT6=ON`, Release,
`ENABLE_SYSTEM_GMIC=OFF`, `ENABLE_DYNAMIC_LINKING=OFF`, `ENABLE_CURL=OFF`,
`ENABLE_FFTW3=ON`, `ENABLE_LTO=OFF`. It requires OpenMP in the CMake cache;
the recorded local LLVM-MinGW build used Clang 21.1.6 and `-fopenmp=libomp`.
Disabling CImg curl does not disable G'MIC-Qt's optional definition updates.

`prepare()` verifies the archive checksum, rejects links/devices/out-of-root
paths when extracting, and applies exact replacement patches. Already-applied
patches are accepted. Existing extracted trees are reused, not independently
rehashed against the archive; use a fresh work directory for a clean-source
reproduction. `--archive` must point to an existing verified file for offline
work: if the supplied path does not exist the script attempts a download.

## Launch, settings and lifetime

`QMic::slotQMic()` uses a process-wide launch guard and scope-guard action
restoration. The launching window's actions stay disabled during execution;
another window cannot start a nested runtime. Reapply remains disabled until
that window accepts output, including after action-manager refreshes. Merely
opening/cancelling a dialog does not enable it. Preserve early-exit restoration
for path creation, plugin lookup/type checks and unsuccessful launch.

Set paths before constructing the runtime, because it caches them:

- `GMIC_PATH = KisSolsticePaths::defaultResourceDir() + "/gmic"`.
  It follows the Solstice profile root, not a custom `ResourceDirectory` or
  command-line resource location. Windows data packs are read from installed
  `share/gmic` and copied into the profile when absent.
- `SOLSTICE_GMIC_SETTINGS = configFilePath("gmic-qt.ini")`; patched QSettings
  macros use this INI file. It follows the different-drive configuration
  fallback described in [Settings location](settings-location.md), independently
  of G'MIC data. Stock Krita settings are not reused.

The runtime patches set the displayed host name, release shared buffers/image
interface on early returns and report output-transfer exceptions. The native
interface captures the launching image, active node and selected nodes; the
view manager is weak. Output checks that the launching image is still active
and the view exists before starting the applicator. Worker commands use
captured state rather than consulting `KisPart` or the current main window.

## Input/output and conversion contract

The pinned runtime exposes Active, All, ActiveAndBelow and ActiveAndAbove.
Active requires a visible paint layer; adjacent modes use only the immediate
visible paint-layer sibling. All walks paint layers recursively, including
hidden ones. Groups/vectors are not flattened. Host predicates for AllVisible
and AllInvisible exist, but those modes remain disabled in the pinned UI.

Only InPlace output is enabled. Reject other destinations rather than coercing
them. Empty output is a no-op, not deletion of all inputs. Reject locked input
layers, invalid output buffers/dimensions/channel counts and changed targets
before document application. Off-canvas size queries/crop export include the
union with canvas bounds; selections use their exact bounds.

`KisQMicImage::checkedElementCount()` requires positive width/height, 1-4
channels and float payload bytes fitting signed int. This prevents overflow,
not out-of-memory below that bound. Input allocation failure returns no images;
runtime output-transfer failures are reported before starting host application.

RGB U8/U16/F16/F32 transfers planar floats at G'MIC unit value 255. Direct
native dispatch walks tile rows using their actual stride; output alpha
presence is dispatched once. Preserve `KoColorSpaceMaths` operation order,
LUT scaling, clipping and rounding. Native F32 negative/HDR values survive
the bridge, but individual filters may clip or assume a particular range.
Non-RGB retains the color-managed RGB fallback; clear/reset output devices
there as on the fast path so a smaller result cannot leave stale pixels.

Runtime output references each CImg result without an owning intermediate.
Depth-one results make one payload copy into `KisQMicImage` rather than three.
Volumes copy the first z slice of each channel using `width*height*depth`
source-channel stride. Keep the final owning copy: the asynchronous applicator
can outlive the CImg image list. Input ownership/copies were not changed by
this optimization. Do not infer new thread-count, OpenMP or LTO policies.

## Application and Undo invariants

`KisProcessingApplicator` submits a sequential/exclusive layer synchronization
command. The composite transaction owns pixels, layer count, metadata and node
selection. Initial execution consumes first-redo wrappers once; Undo/Redo
replays stored commands instead of rebuilding them. Pixel transactions precede
metadata changes so offsets do not translate pixels twice.

Selections use the existing Copy composite with coverage, retaining unselected
pixels. Without a selection, returned names can encode layer position; position
metadata is not applied over a selection. Output may grow/remove layers and
change dimensions, names, opacity, blend modes and offsets within the same
undoable operation. After accepted output the interface follows the first
retained input layer so a second Apply cannot read a layer just removed by
the previous operation. Closing without acceptance leaves the document alone;
closing after Apply does not undo already accepted results.

## Build, install and distribution

Use [Development workflow](development-workflow.md) for environment setup.
The script requires Python 3.13+, CMake, Ninja and LLVM-MinGW. Local application
and dependencies can share `_install`; CI prefixes stay separate. Build and
install the bridge/public headers first, then the runtime:

```bat
call <krita-dev-root>\env.bat
cmake --build <krita-dev-root>\_build --target kritaqmic kritaqmicinterface KisQmicIntegrationTest kis_qmic_tests -j 2
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\extensions\qmic\cmake_install.cmake
python build-tools\gmic\build-runtime.py --work <krita-dev-root>\gmic --install <krita-dev-root>\_install --deps <dependency-prefix> --compiler-bin <toolchain-root>\bin --jobs 2
```

Use `--archive <existing-local-tar.gz>` for offline source builds. The script
checks installation of `lib/kritaplugins/krita_gmic_qt.dll` and four assets in
`share/gmic`: `gmic_cluts.gmz`, `gmic_denoise_cnn.gmz`, `gmic_lightleaks.gmz`,
`gmic_scale2x_cnn.gmz`. It copies the exact archive, lock, patches, script and
README to `share/gmic/source`. Preserve G'MIC-Qt GPL-3.0-or-later and bundled
G'MIC/CImg CeCILL/CeCILL-C/third-party notices along with the host dependency
notices. These source inputs are part of redistributed packages.

The Windows workflow builds this runtime after host installation; packaging
asserts the runtime, interface, assets and source provenance in both installation
and final staging. A pin update also needs the hard-coded archive/version
references in packaging/build metadata updated. File assertions do not replace
a real load/filter test with recursively satisfied DLL dependencies.

Install all affected shared libraries and fully restart after DLL changes.
Do not terminate a user's running Solstice to unlock files. For a distribution
check, extract to a fresh staging directory, use fresh profile/cache data,
restrict PATH to packaged binaries and OS dependencies, then run an offline
filter/Undo round trip. Release publication follows [Versioning](versioning.md).

## Automated and manual checks

```bat
ctest --test-dir <krita-dev-root>\_build -R "(KisQmicIntegrationTest|kis_qmic_tests)$" --output-on-failure
```

Ordinary tests cover allocation limits, native/Gray conversion, metadata,
layer growth/removal, feathered selection and repeated Undo/Redo. For conversion
changes retain `testPlanarReference`: independently assembled pixels, all four
RGB depths, 1-4 channels, unit values 1/255, negative/HDR samples, partial tiles,
negative origins and moved devices. Integer/F16 comparisons are byte-exact;
F32 round trips allow 2e-7 error from scaling by 255.

The actual installed-runtime test is opt-in and must not be silently skipped
when validating a runtime/bridge change:

```bat
set QT_QPA_PLATFORM=offscreen
set QT_QPA_FONTDIR=C:\Windows\Fonts
set KRITA_NO_ASSERT_MSG=1
set SOLSTICE_TEST_GMIC_RUNTIME=1
set GMIC_SYSTEM_PATH=<krita-dev-root>\_install\share\gmic
<krita-dev-root>\_build\bin\KisQmicIntegrationTest.exe -o <scratch>\gmic-integration.txt,txt
```

The fixture isolates its profile and plugin directory (LCMS, paint ops/tools/
shapes and G'MIC modules), constructs a real application/view and avoids unrelated
Python/ML dock initialization. `SOLSTICE_GMIC_TEST_PLUGIN_DIR` overrides its
plugin source. It checks actual `add 10`, Undo/Redo, 1-4 channel depth-1/depth-3
first-slice output, unsupported/empty results, offline dialog/menu cancellation
and action restoration. `SOLSTICE_GMIC_ARTIFACT_DIR` enables a screenshot.
Clear stored `Arguments` when changing a synthetic headless command; remove
the synthetic command before catalog launch. Never force-close a parent while
a nested warning dialog is active.

Manual regressions: restart, open artwork, compare Preview/Apply/OK, repeated
Apply, Re-apply, Close and Undo/Redo. Check soft selections, multiple layers,
alpha, non-RGB and off-canvas input; cancel slow work and reopen. Exercise
multiple windows/target changes, missing runtime/dependencies and repair/retry,
and optional online-update failure without losing the offline baseline.

## Verification and performance boundaries

The 2026-10-15 record reports six native-effect plus two G'MIC CTest suites
passing. After bridge optimization the actual-runtime run reported 12 passes,
zero failures and one skipped opt-in transfer benchmark; the separate
conversion/reference timing run passed. The user confirmed runtime operation
and both CPU bridge optimizations in the local Windows build. This is not
individual acceptance of every catalog filter or manual scenario.

The historical fresh offline catalog contained 691 filters. A separate staging
directory with 113 dependency-checked binaries, isolated PATH/settings/cache
passed the 11 pre-optimization integration checks. This establishes that
tested runtime dependency closure, not validation of every release ZIP.

The later [beta.6 package record](wiki/history/github-actions-development.md#effects-layer-comps-and-gmic-package-check-2026-10-16)
reports successful cloud build/install/package/upload in run 38833086246 on
2026-10-16, ZIP checksum/CRC and required-file/source-hash checks, plus a local
packaged `--version` startup check. The run's success and source commit
`b7ee343e81fe79b6fff257f6ae8b34af8ece7363` were rechecked during extraction.
This supersedes the original cloud-unverified statement; cloud GUI/runtime
tests were disabled. Linux/macOS runtime packaging and exhaustive catalog,
optional-update/failure-path checks remain unverified. Release inclusion is
recorded in [Versioning](versioning.md), separately from runtime test scope.

`SOLSTICE_BENCHMARK_GMIC_TRANSFER=1` enables `benchmarkTransfer`; it measures
native/planar conversion, not filter execution or runtime output-copy timing.
`SOLSTICE_BENCHMARK_GMIC=1` enables a Windows CPU-time observation in the runtime
test. [History](wiki/history/cpu-effects-and-gmic-development.md) retains the
workloads, hardware and before/after values. Do not claim universal speedup:
some 2048 integer output conversions were slower. Removing two 256 MiB payloads
for a 4096-square RGBA float result is allocation accounting, not a measured
process peak-memory reduction. Parallel CPU work on one blur workload does
not establish end-to-end latency or concurrency for all catalog filters.

This reference was extracted on 2026-10-16 against
`39b556e1e807cca9deb01f08d236142e53fe9292`. This documentation task does not
claim a new native build, runtime test, installation or interactive check.
