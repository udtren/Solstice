---
type: query
updated: 2026-11-13
sources:
  - libs/image/kis_paint_device.cc (calculateExactBounds)
  - libs/image/layerstyles/kis_layer_style_projection_plane.cpp (changeRect)
  - libs/ui/KisMainWindow.cpp (constructor, createDockWidget)
  - libs/ui/KisApplication.cpp (start, loadPlugins, registerResources)
  - plugins/dockers/textproperties/TextPropertiesDock.cpp
  - libs/ui/opengl/kis_opengl.cpp (selectSurfaceConfig)
  - libs/global/KisScopedPerformanceLogger.h
  - build-tools/paint-trace/run.cmd (how to start the development build)
related:
  - ../benchmarks/exact-bounds-scan.md
  - ../pitfalls/exact-bounds-after-move.md
---

# Where does the CPU time go, from start to everyday use? (2026-11-13)

Asked by the user: are there places to improve in the parts the CPU
handles, seen from the architecture? Answered in two steps: a static read
of the start, painting and UI paths, then measurements. The measurements
changed the order, so they come first.

## Measured

Machine and build as in
[exact bounds scan](../benchmarks/exact-bounds-scan.md). Documents: scratch
copies of the user's files.

- **Painting is not the bottleneck.** A dab-sized update (`setDirty()` of
  64 or 256 px on a layer) took 0.1-1.5 ms in RGBA8 paintings with up to
  112 visible paint layers, and 1-2 ms in the RGBA F32 assets with the GPU
  engine. A full refresh: about 97 ms for 5319x4705 and 3899x4204 RGBA8;
  46-81 ms (GPU) or 291-989 ms (CPU) for the F32 assets.
- **Exact bounds were the largest cost**: up to 430 ms per layer, 3.5 s
  for all layers of one painting. Fixed the same day (tile-wise scan).
- **Start: about 6.6 s** to the end of `KisApplication::start()`, warm
  disk cache, development build. From the `krita.performance` log:

  | Stage | Time |
  | --- | --- |
  | Before `start()` (five temporary applications for the OpenGL probe) | about 0.5 s |
  | `loadPlugins` (Vision ML's Vulkan backends included, under 0.1 s) | 0.26 s |
  | `registerResources` | 0.47 s |
  | Main window constructor before the dockers (18 view plugins, Python) | 2.9 s, not split |
  | Text Properties docker (QML) | 1.04 s |
  | Log Viewer docker | 0.20 s (first of the loop; cause not looked at) |
  | The other dockers together | about 0.2 s |

  Importing PyQt6 alone takes 50-200 ms, so the 2.9 s are the Python
  plugins' own start or the view plugins. Scopes around the two
  `KoPluginLoader::load()` calls in the constructor would split it.

## What the static read got wrong

- It ranked "layer styles above the painted layer grow every update rect"
  (`KisLayerStyleProjectionPlane::changeRect()`) first. The mechanism is
  real, but in the four documents measured the growth was 0-3 px per side
  (most styled layers are hidden) and made no measurable difference. Not
  worth changing without a document that shows it.
- It ranked the Asset Library docker (thumbnails in its constructor) and
  Vision ML (Vulkan at plugin load) first for the start. Measured: about
  40 ms of file reads for 101 documents, and under 0.1 s.
- A hypothesis that columns after the thumbnail blob make queries on the
  resource cache slow was wrong: a full scan takes 5 ms.

## Candidates left, by measured or expected gain

1. Text Properties docker: load its QML on first show (1.0 s per start).
2. Split the 2.9 s of the main window constructor, then decide.
3. Cache the OpenGL probe result (0.5 s per start; needs invalidation on
   driver or GPU changes).
4. Not measured, small and low risk: the Quick Adjust docker's 200 ms
   timer (runs hidden, rebuilds four icons per tick); the preset editor
   builds a `KisResourceModel` and scans all presets on every setting
   change, also when hidden (`slotUpdatePresetSettings()`); Undo dirties
   the bounding box of the changed tiles (`KisTransactionData::startUpdates()`).
5. The user's resource cache is 478 MB: 187 MB of it are preset XML
   embedded in old preset thumbnails (629 rows, stored before the loader
   stripped the text), 166 MB are pattern thumbnails at full size. A
   one-time rewrite would shrink it; the cost today is decode time on
   first display, memory and disk, not query time.

## How to measure again

- Start: `QT_LOGGING_RULES=krita.performance.debug=true`,
  `QT_FORCE_STDERR_LOGGING=1`,
  `QT_MESSAGE_PATTERN="%{time process} %{category}: %{message}"`, and
  start `solstice.com` from `_install\bin` after `env.bat` and the Python
  environment, as `build-tools/paint-trace/run.cmd` does. Started without
  them, the development build exits at once.
- Layer costs: a test program on a scratch copy of a document, like
  `KisMoveDocumentBenchmark`. Documents with file layers block on a
  missing-file dialog when copied alone.
