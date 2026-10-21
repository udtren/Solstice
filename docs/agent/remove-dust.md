# Remove Dust — agent development notes

## Status and scope

- A Remove Dust Tool and a Remove Dust filter that remove or select small
  specks and fill small holes, inspired by CLIP STUDIO PAINT's Remove dust,
  Fill leftover and Select dust tools. User guide: [Remove Dust](../remove-dust.md).
- Requested by the user on 2026-10-21 (phase 1: detector core, tests and
  filter; phase 2: the tool). Both phases were implemented on branch
  `claude/remove-dust`.
- Verification, 2026-10-21: `libs-image-KisDustDetectorTest` (13 cases)
  and `plugins-filters-solsticeeffects-KisRemoveDustFilterTest` (7 cases)
  pass; the rest of the `solsticeeffects` tests pass. The tool has no
  automated test.
- Interactive, 2026-10-21 (user): the tool's Remove with a rectangle, Dust
  on transparency, removed isolated specks on a sketch layer. A dark speck
  stayed at Tolerance 0% and was removed at 2–5%: nearly invisible pixels
  (eraser leftovers, soft brush edges) joined it to a line, so the joined
  component was larger than Dust size. The rest of the manual checklist,
  the filter and the Select action are not yet confirmed.
- On the user's request (2026-10-21) the tool and the filter start at
  Tolerance 2% (`KisDustDetector::defaultTolerancePercent`); covered by
  `testInvisibleBridge` and `KisRemoveDustFilterTest::testProperties`.
  `KisDustDetector::Options::tolerance` itself still defaults to 0.

## Source locations

- Detector: `libs/image/KisDustDetector.{h,cpp}` (`kritaimage`, exported).
  Test: `libs/image/tests/KisDustDetectorTest.cpp`.
- Filter: `plugins/filters/solsticeeffects/KisRemoveDustFilter.*`,
  `KisRemoveDustConfigWidget.*`, `tests/KisRemoveDustFilterTest.cpp`.
- Tool: `plugins/tools/tool_enclose_and_fill/KisToolRemoveDust.*`,
  `KisToolRemoveDustFactory.h`, `KisToolRemoveDust.action`. It lives in the
  Enclose and Fill plugin module (`kritatoolencloseandfill`) so that it
  shares the enclosing subtools in `subtools/` without copying them.
- Icons: `krita/pics/tools/SVG/16/{dark,light}_krita_tool_remove_dust.svg`
  (Solstice-made).
- Hooks in upstream files: `libs/image/CMakeLists.txt`,
  `libs/image/tests/CMakeLists.txt`,
  `plugins/tools/tool_enclose_and_fill/CMakeLists.txt` (source and action
  install), `KisToolEncloseAndFillPlugin.cpp` (registers the second
  factory), `krita/pics/tools/SVG/16/tools-svg-16-icons.qrc`,
  `plugins/dockers/recorder/recorder_writer.cpp` (`KisToolRemoveDust` in
  `forceBlacklistedTools`, like Enclose and Fill). Solstice files:
  `SolsticeEffectsPlugin.cpp`, `solsticeeffects/CMakeLists.txt`,
  `solsticeeffects/tests/CMakeLists.txt`.

## Build, test, and install

| Part | Target | Install from |
| --- | --- | --- |
| Detector | `kritaimage` | `_build\libs\image\cmake_install.cmake` |
| Filter | `kritasolsticeeffectsfilter` | `_build\plugins\filters\solsticeeffects\cmake_install.cmake` |
| Tool and action file | `kritatoolencloseandfill` | `_build\plugins\tools\tool_enclose_and_fill\cmake_install.cmake` |
| Recorder blacklist | `kritarecorderdocker` | `_build\plugins\dockers\recorder\cmake_install.cmake` |
| Tool icon (qrc) | `krita` | `_build\krita\cmake_install.cmake` |

Tests: `KisDustDetectorTest`, `KisRemoveDustFilterTest`
(`ctest -R "DustDetector|RemoveDust"`). The filter test registers the
filter itself, so it does not need the installed plugin.

## Configuration and compatibility

- Filter id `solstice_remove_dust`, configuration version 1, stored in
  `.kra` filter layers and masks. Properties: `mode` (`transparency`,
  `white`, `gaps_surrounding`, `gaps_color`; unknown values read as
  `transparency`), `dustSize` (int, clamped to 1–1000), `tolerance`
  (percent, clamped to 0–100; 2 when missing), `color` (`KoColor`, used
  by `gaps_color`). Keep these names and values stable.
- Tool id `KisToolRemoveDust` (also the action name and shortcut id).
  Settings group `[KisToolRemoveDust]` in `kritarc`: `action`
  (`remove`/`select`), `enclosingMethod` (`rectangle`, `ellipse`, `path`,
  `lasso`, `brush`), `mode` (`transparency`, `white`, `gaps`), `gapFill`
  (`surrounding`/`foreground`), `dustSize`, `tolerance` (percent, default 2),
  `selectionAction` (`replace`, `intersect`, `add`, `subtract`,
  `symmetricDifference`),
  `reference` (`currentLayer`, `allLayers`, `colorLabeledLayers`),
  `colorLabels` (comma-separated), `useActiveLayer`. Unknown strings fall
  back to the first value.

## Architecture and lifecycle

**Detector.** `KisDustDetector::detect(reference, rect, options[, enclosure,
containment], progress)` classifies each pixel of
`neededRect(rect, dustSize)` (`rect` grown by `dustSize + 1`) as candidate
or background. The rule depends on the mode: opacity above the tolerance;
color difference from white above the tolerance, ignoring alpha; or opacity
below `1 - tolerance`. It then flood-fills each 8-connected candidate
component that has a pixel in `rect`, visiting every pixel once. A component
stops collecting pixels once its bounding box exceeds `dustSize` or it
reaches the window edge, but the flood continues so that large components
are not fed again. For gaps it collects the 8-neighbor background pixels,
groups them by `differenceA() <= 12` and averages the largest group
(`mixColorsOp`), so a hole on a two-color border takes one color. `apply()`
writes raw pixels (erase / white / surrounding / fixed color) inside an
optional clip rect; `select()` writes `MAX_SELECTED` into a pixel selection.
With the GPU engine both call `KisGpuTileAccess::syncToCpu()` first (batched
downloads; the data is correct without it).

**Filter.** `processImpl()` detects over `rect` and applies inside `rect`.
`neededRect()`/`changedRect()` are `rect` grown by `dustSize + 1`. Level of
detail is off (the size is in pixels). `needsTransparentPixels()` is true
only for the gap modes. Channel flags are ignored.

**Tool.** `KisToolRemoveDust` follows `KisToolEncloseAndFill`: a
`KisDynamicDelegatedTool<KisToolShape>` whose delegate is one of the
enclosing producers; `enclosingMaskProduced` starts the work. The work runs in
a `KisProcessingApplicator` (one undo step). A color-labeled reference is
merged first with `KisMergeLabeledLayersCommand`, as in Enclose and Fill.

- Remove: a `KisCommandUtils::LambdaCommand` (SEQUENTIAL, EXCLUSIVE)
  intersects the enclosing mask with the active selection, detects, applies
  inside a `KisTransaction` on the active layer's paint device, calls
  `setDirty()` on the components' bounds and returns the transaction's
  command. Nothing found returns no command.
- Select: a BARRIER lambda fills a new pixel selection, then
  `KisSelectionToolHelper::selectPixelSelection(applicator, ...)` applies it
  with the chosen selection action.
- Containment: `Touching` for the brush, `FullyInside` for the shapes.
- Reference: active layer `paintDevice()` (Remove) or `projection()`
  (Select), the image projection, or the merged color-labeled layers.
- The found count is shown with `showFloatingMessage()`, posted to the GUI
  thread through `QMetaObject::invokeMethod(qApp, ..., Qt::QueuedConnection)`
  with a `QPointer` to the canvas.
- While a subtool interaction runs, alternate actions are passed to the
  delegate's primary action (copied from Enclose and Fill).

## Invariants

- Tiling invariance: analysing tiles through `neededRect()` gives the same
  pixels as analysing the whole area (`testTilesMatchWholeArea`). Keep the
  window margin at `dustSize + 1` and keep treating components that reach
  the window edge as non-dust.
- The filter id and property names above are a `.kra` compatibility contract.
- Remove changes only the active layer, whatever the reference.

## Manual regression checklist

1. Filter > Enhance > Remove Dust on transparent line art with specks of 1–6
   px at Dust size 4: specks up to 4 px disappear in the preview and the
   result; lines stay. Undo restores them.
2. White background mode on an opaque scan with gray specks; raise
   Tolerance to keep off-white paper unchanged.
3. Fill transparent gaps with surrounding color on flat color with
   pinholes, including a hole on the border between two colors.
4. Filter mask and filter layer with each mode; save, reopen, compare.
5. Tool, Remove, rectangle: only specks entirely inside are removed; the
   message reports the count; one undo step.
6. Tool, brush with Fill transparent gaps: holes touched by the stroke are
   filled with the surrounding color, then with Foreground color.
7. Tool, Select with each selection mode; with an existing selection.
8. Tool, Remove with an active selection: only selected pixels change.
9. Reference All layers and Color labeled layers (with and without Use
   active layer); confirm that only the active layer changes.
10. Bezier curve enclosing: finish the path with a double click and with
    the alternate action.
11. Tool on a vector or group layer in Remove: the refusal message appears.
12. Options persist after restarting; Reset restores the defaults.
13. Check the tool icon in the toolbox in light and dark themes, and the
    Simplified Chinese strings.

## Known limitations and pending work

- Remove writes raw pixels: it ignores alpha lock, the brush's opacity and
  blending mode, and wrap-around mode.
- The filter cannot enclose: spots crossing a selection edge are cut by the
  selection mask. The tool is the exact-area path.
- Dust size is limited to 1000 px; detection memory is one byte per pixel
  of the enclosed area's bounding box plus the margin, and up to two bytes
  per pixel when an enclosure is used.
- Not implemented from CLIP STUDIO PAINT: the shape operation/correction
  categories of the advanced tool settings, and the double-valued dust size.
