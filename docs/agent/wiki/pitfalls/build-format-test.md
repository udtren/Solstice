---
type: pitfall
updated: 2026-10-08
sources:
  - docs/agent/coding-rules.md ("Formatting and diffs")
  - docs/agent/development-workflow.md (build, test, install)
  - docs/agent/gpu-engine.md ("Build, test, and install")
  - experience from the 2026-10 sessions (Quick Access, docker locks, GPU Liquify)
related:
  - ../concepts/cpu-gpu-bit-parity.md
---

# Build, format and test pitfalls

The commands themselves are in `development-workflow.md` and in
`gpu-engine.md` "Build, test, and install". This page collects what went
wrong anyway.

## Formatting

- **`clang-format --lines` still re-sorts an include block** when one of
  its lines changed. Formatting only the changed lines of an upstream file
  can reorder that file's includes. Compare with `git diff` after
  formatting. Restore the original order and keep the new include at the
  end of the block, matching the file's style.
- **Shells drop empty arguments.** A helper called as
  `script "" "a.cpp,b.cpp"` from PowerShell lost the empty first argument,
  and whole upstream files were formatted. Call such helpers from bash, or
  avoid positional empty arguments.
- **Check list separators.** A helper expecting comma-separated files
  received space-separated ones and reported "formatted 0 ranges".
- **Mixed line endings.** Many files are CRLF in the working tree while
  edits arrive as LF. Edit helpers must match either. Run `git diff --check`
  before committing.

## Shaders and GPU structs

- **GLSL reserved words.** A function named `sample` failed to compile
  ("unexpected SAMPLE"). Other reserved names include `input`, `output`,
  `filter`, `common`, `partition` and `active`.
- **std430 layout.** `static_assert` the C++ mirror of every buffer
  struct, and compute the size by hand:
  - `ivec4` + 2 x (`ivec2` + 2 `int`) + 4 `int` + `vec4` = 80 bytes, not 64;
  - a struct containing `dvec4` has 32-byte alignment, so its array stride
    is a multiple of 32.
- **Push constants** carry buffer device addresses only. Every buffer is
  passed by address (`GL_EXT_buffer_reference`), in the same order as the
  C++ struct.

## KConfig paths

- `KConfig::setMainConfigName()` must not be absolute: KConfig 6.7 appends
  the main config name to `GenericConfigLocation`, and looks up defaults at
  `":/kconfig/" + name`. Relative names with `..` work, and the defaults path
  is cleaned by Qt (`docs/agent/settings-location.md`).
- The main config is first opened inside the temporary `QCoreApplication` in
  `krita/main.cc`, by a static startup function (`KisAnimAutoKey`). Anything
  that changes it must run before that point.

## C++ access

- `KisPaintDevice::fastBitBltPossible()` is protected. Compare `x()`, `y()`
  and color spaces instead.

## Tests

- Run GPU tests through the validation wrapper (`KRITA_GPU_VALIDATION=1`,
  `VK_LAYER_PATH` pointing at the Vulkan SDK `Bin`).
- Opt-in benchmarks: `KRITA_GPU_BENCHMARK_FILTERS=1` for
  `benchmarkFiltersAndTransforms`. `KRITA_GPU_TRANSFORM_DEBUG=1` prints
  stage times of the GPU transform paths.
- Offscreen UI tests need `QT_QPA_PLATFORM=offscreen` **and**
  `QT_QPA_FONTDIR=C:\Windows\Fonts`. Without the fonts, layout tests fail;
  this is environmental, not a regression.
- Do not run the `libs-ui-*` suite through ctest or `kis_kra_saver_test`
  on the desktop session: they open blocking dialogs.
- **Test programs embed the MIME database** (since 2026-10-07). Qt 6 in
  this build has no built-in MIME database; the application embeds
  `krita/data/mime-database/freedesktop.org.xml` at
  `:/qt-project.org/qmime/packages`.
  - Before, tests got empty MIME types, so resource loaders were not found
    ("Could not create loader for symbols test.svg ''") and resource tests
    failed.
  - `sdk/tests/CMakeLists.txt` now compiles the resource once into the
    OBJECT library `kritatestmimedatabase`, and adds its object to
    `kritatestsdk` (an INTERFACE library), so every test registers it at
    startup.
  - An empty storage location is no longer detected as a folder: Qt 6 treats
    `QFileInfo("")` as the current directory (`autoDetectStorageType()` in
    `KisResourceStorage.cpp`). `TestResourceStorage` now expects the
    designed result, an invalid memory storage.
- **Known failing tests (2026-10-07)**, from `ctest -j 12 -E
  "libs-ui|kis_kra_saver_test|KisGpuSaveTest"` (291 tests, 13 failed). All of
  these also fail without the MIME change:
  - `TestSvgParser`, `TestSvgParserCloned`, `TestSvgParserRoundTrip`: mesh
    gradient renders differ from the reference images (5 each);
  - `KisBrushModelTest` (initTestCase);
  - `psd_cos_parser_test` (map and string round trips);
  - `kis_cage_transform_worker_test` (2 image comparisons);
  - `kis_transform_mask_test` (two rects one pixel off);
  - `StoryboardModelTest` (7);
  - `kis_tiff_test` (`testFiles`);
  - `kis_jpegxl_test` (CMYK with layers, multipage).
  - `TestFallBackColorTransformation` and `TestKisSwatchGroup` pass alone
    but fail in the parallel run (120-150 s each).

  With the MIME database, `TestSvgParserRoundTrip` went from 8 failures to 5
  and `kis_jpegxl_test` from 3 to 2. Fontconfig warns "Cannot load default
  config file" in some tests; this was not investigated.
- **A new virtual function in a plugin base class breaks the installed
  plugins** (2026-10-07). Tests load filter plugins from the install prefix.
  After `KisFilter::prefersSingleCall()` was added, `kis_filter_test`,
  `kis_filter_mask_test` and `KisGpuPaintDeviceTest` crashed with
  0xc0000005 in old plugins until `cmake --install` of the whole build.
- **Removing a platform's conditionals: resolve them, do not hand-edit.**
  The Android cleanup (2026-10-08) resolved 267 `Q_OS_ANDROID` lines in 71
  files with a small unifdef-like script that treats the macro as undefined
  and simplifies mixed expressions (`defined(Q_OS_LINUX) &&
  !defined(Q_OS_ANDROID)` -> `defined(Q_OS_LINUX)`). Check afterwards for
  blank lines left at the end of files (`git diff --check`), and that no new
  unused-variable warnings appear.
- **`.ui` edits regenerate tracked `*_ui.py` files.** The user's editor runs
  pyuic6 when a `.ui` file changes, including the tracked
  `libs/ui/forms/*_ui.py`. A broken intermediate `.ui` produced a broken
  `.py`; validate `.ui` XML (`xml.dom.minidom.parse`) before saving, and
  commit the regenerated files with their `.ui`.
- **Git Bash `sed -i` rewrites CRLF files with LF.** Running `sed -i` on a
  CRLF source file converted the whole file, so `git diff` showed every
  line. Edit CRLF files with the edit helper (or Python in binary mode) and
  check `file <path>` afterwards.
- **`Select-Object -First N` stops the producer.** Piping
  `cmake --build` into `... | Select-Object -First 30` in PowerShell ended
  the build early once 30 lines matched. Redirect the build to a log file and
  filter the file instead.
- Test executables are named after their source files
  (`kis_liquify_transform_worker_test`, `KisGpuPaintDeviceTest`). Check
  `_build/bin` when a CMake target name is not found.

## Installation

- A running Krita locks plugin and library DLLs. Check that no `krita`
  process runs before installing. Never stop it without the user's approval.
- After installing, compare file hashes of the build and install copies.
- Changes under `libs/*` need that library's `cmake_install.cmake`, not only
  the plugin's.

## Measuring

- To find where time goes, add temporary `QElapsedTimer` prints. Back up the
  file, or remove the lines by marker afterwards. Then rebuild, and confirm
  `git diff` shows no leftovers before formatting and committing.
