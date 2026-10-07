---
type: pitfall
updated: 2026-10-07
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
