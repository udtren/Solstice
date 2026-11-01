# Through-Layer Eraser — agent development notes

## Status and scope

A freehand tool that erases several paint layers with one brush stroke
(user request, 2026-11-01; idea from CLIP STUDIO PAINT). User guide:
[Through-Layer Eraser](../through-layer-eraser.md).

Verification (2026-11-01, Windows 11, `_install`):

- `KisThroughLayerEraserTest` passes on the CPU and the GPU, as do
  `KisGpuBrushJobsTest`, `KisBrushStrokeLayerTest` and `KisBrushStrokeReplayTest`.
- `KisGpuStrokeTest` exceeded 1500 s under ctest. Run directly and stopped
  during `testHalfBlendModes` (553 rows passed there), it had no failure; the
  rest of that function was not run, and the three functions after it pass.
- Scenarios `through-layer-eraser`, `eraser-stroke` and `brush-stroke-undo` pass
  through test control (key `windows-cpu`).
- The user tried the tool in the application and approved its release; the
  guide's animation is from that session. Pressure with a tablet has not been
  checked separately.

## Source locations

- Tool and option widget:
  `plugins/tools/basictools/KisToolThroughLayerEraser.*`, registered in
  `default_tools.cc`.
- Target rules: `libs/ui/tool/KisThroughLayerEraserTargets.*`.
- Stroke: `KisResourcesSnapshot::setThroughLayerTargets()`,
  `KisToolFreehandHelper::setThroughLayerTargets()`,
  `KisPainterBasedStrokeStrategy` (`strokeNodes()`,
  `attachThroughLayerTarget()`, `updateStrokeNodes()`), `FreehandStrokeStrategy`.
- Action and icons: `plugins/tools/tools.action`,
  `krita/pics/tools/SVG/16/{dark,light}_krita_tool_through_layer_eraser.svg`.
- Test: `plugins/paintops/defaultpaintops/brush/tests/KisThroughLayerEraserTest.cpp`
  compares each target with an ordinary erase stroke, on the CPU and the GPU,
  in Wash and Build-up.
- Scenario: `plugins/tools/basictools/tests/scenarios/through-layer-eraser.json`
  drives the real tool through [test control](test-control.md).

## Build, test, and install

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritadefaulttools KisThroughLayerEraserTest -j 8"
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && set KRITA_NO_ASSERT_MSG=1&& ctest --test-dir <krita-dev-root>\_build -R KisThroughLayerEraserTest --output-on-failure"
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\libs\ui\cmake_install.cmake
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\tools\basictools\cmake_install.cmake
```

The icon is compiled into `kritaui`'s resources, and `tools.action` is
installed with `plugins/tools`.

## Configuration and compatibility

`Selected layers only` is stored as `selectedLayersOnly` in the kritarc group
named after the tool id, `KritaShape/KisToolThroughLayerEraser`. A saved
document holds nothing of the tool. The shape of a stroke's undo command is
the usual one, with one transaction per layer.

## Architecture and lifecycle

**One device, many layers.** The stroke paints its dabs once, on one temporary
target created from the first layer's device. Every target layer gets that
same device through `KisIndirectPaintingSupport` with composite op Erase, so
the cost of the brush does not grow with the layer count. The alternative,
one painter per layer, renders every dab once per layer. A device shared this
way is only read after the painters stop, so the layers' merges do not
interfere.

- The tool collects the targets on the GUI thread when the stroke starts and
  hands them to the helper, which passes them to the snapshot. The first
  target becomes the current node, so the tool works whatever node is active;
  it does its own `beginPrimaryAction()` because the base checks the active
  node.
- `KisResourcesSnapshot::setThroughLayerTargets()` forces `COMPOSITE_ERASE`,
  clears the global alpha lock and turns level of detail off.
  `FreehandStrokeStrategy::createLodClone()` then returns null, and the
  strokes queue does not suspend such a stroke.
- Each layer keeps its own selection (`KisLayer::selection()`, which includes
  a local selection mask) and channel flags, applied when it composites the
  device. The first layer's `activeSelection()` is only what the assertions in
  `initStrokeCallback()` see.
- **Build-up presets** would normally skip indirect painting. The stroke forces
  it, and paints with Over at the stroke's opacity into the shared device, with
  the temporary opacity at 1. Erasing with the accumulated alpha gives the same
  result as erasing dab by dab, up to rounding:
  `(1 - a)(1 - b) = 1 - (a + b - ab)`. The layers get the device only after
  `initPainters()`: `KisOpacityOption` and `KisFlowOpacityOption` look for a
  temporary target when the paint op is created and then move the opacity
  sensors away from the dab, which loses pressure and opacity in Build-up.
- **Updates.** `updateStrokeNodes()` calls `refreshGraphAsync()` on the root
  once per update. `setDirty()` per layer starts one merge walker per target,
  each compositing the whole stack, which is quadratic in the layer count. With
  one target it is a plain `setDirty()`.
- **Final merge.** `finishStrokeCallback()` adds `mergeToLayerThreaded()` jobs
  for each layer to one parent command, so the stroke is one undo step. Each
  merge starts with its own barrier and takes that layer's temporary target
  lock, as before. No update follows: the preview already shows the result.
- **Cancel** only drops the temporary targets. Nothing touches a layer's
  paint device before the final merge.
- **GPU.** `KisGpuBrushPainter` composites the shared device per layer. A layer
  whose color space differs from the shared device's, or whose tiles are not
  aligned with it, is refused by `KisGpuProjectionCompositor::composite()` and
  falls back to the CPU path in `KisPaintLayer`.
- **Animation.** Each layer gets its own auto-keyframe command, collected into
  `m_autokeyCommand`.
- **Brush Stroke Layer.** The stroke is not recorded
  (`FreehandStrokeStrategy::initRecording()`); the layer is told about the
  change with `markChangeUnrecorded()`, because `refreshGraphAsync()` never
  reaches its `setDirty()` override.
- **Color Smudge** and other presets with interstroke data lose it: that data
  belongs to a single paint device and cannot work with indirect painting.

## Invariants

- Unchecked means every visible paint layer; checked means the selected layers
  and the layers in selected groups. Hidden layers are never targets, and a
  layer inside a hidden group is hidden (user request, 2026-11-01).
- Locked and alpha locked layers are not targets. A layer inside a locked group
  is locked.
- Targets are `KisPaintLayer`s only, listed from the top of the stack.
- In isolated mode the walk starts at the isolation root.
- The tool never changes Eraser Mode or the preset.

## Manual regression checklist

1. Paint on several layers, some in groups. Erase with the option unchecked:
   all visible layers lose pixels live during the stroke, and hidden ones do not.
2. Check **Selected layers only**, select two layers and a group: only those
   change. Select nothing erasable: the floating message appears.
3. Undo once: every layer is restored. Redo.
4. Erase with a selection active, with Wash and with Build-up presets, with a
   masked brush, and with a GPU and a CPU color space.
5. Press Esc mid-stroke: nothing is left behind.
6. Isolate a layer and erase: other layers stay.
7. Erase on an animated layer with auto-keyframe on: a key appears on each
   animated target, and undo removes them.
8. Erase with a layer selected that is hidden, locked, vector or a group: the
   tool still erases the others.

## Known limitations and pending work

- Instant Preview is off for these strokes.
- Every update refreshes the rect in all visible layers of the image, including
  filter layers below the targets, even when only a few layers are selected.
