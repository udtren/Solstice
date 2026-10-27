# Brush Stroke Layer - maintenance reference

This is the current technical contract for Brush Stroke Layer, checked against
the implementation on 2026-10-16. User workflows and visible limitations are in
[the user guide](../brush-stroke-layer.md). The
[development history](wiki/history/brush-stroke-layer-development.md) preserves
the original proposal, investigations, measurements and dated verification.

## Scope and architecture

`KisBrushStrokeLayer` derives from `KisPaintLayer` and implements
`KisRedrawableLayerInterface`. It retains normal paint-layer pixels and ordered
brush records, including erasing strokes. The native freehand, Dynamic Brush
and Multibrush paths record strokes; other pixel-painting tools are refused
through `nodePaintAbility()` / `supportsBrushStrokeLayer()` (the Gradient Tool
has its own guard). Filters and other non-brush edits can change pixels without
changing the record. Duplication copies the records.

Replay on image/layer scaling or Free Transform can replace resampling with a
fresh drawing at the new size. A record is not proof that the pixels still
match: compare them before replay, preserve non-brush edits, and use the
ordinary pixel transform when replay is unsuitable.

The initial external-layer design, blanket CPU-only/brush-engine restrictions
and proposed per-stroke vector editing in the archive are not the implemented
contract. There is no individual stroke/control-point editor or intersection
eraser. Do not implement archived future stages as part of routine maintenance.
Animation layers with a keyframe channel do not record, check or redraw strokes.

## Source ownership

Paths in this table are relative to the repository root. The
[feature inventory](feature-inventory.md) lists shared-file touch points.

| Source | Responsibility |
| --- | --- |
| [KisBrushStrokeLayer.h](../../libs/ui/KisBrushStrokeLayer.h) | Public layer, record structures, undo commands and compatibility-sensitive object layout |
| [KisBrushStrokeLayer.cpp](../../libs/ui/KisBrushStrokeLayer.cpp) | Layer construction/cloning, mutex-protected record list, change tracking and recorded-change scopes |
| [KisBrushStrokeLayerMonitor.cpp](../../libs/ui/brushstrokelayer/KisBrushStrokeLayerMonitor.cpp) | Idle checks, generations, worker lifetime, shutdown and canvas messages |
| [KisBrushStrokeLayerReplay.cpp](../../libs/ui/brushstrokelayer/KisBrushStrokeLayerReplay.cpp) | Native freehand replay; position, size, texture and mirror mapping |
| [KisBrushStrokeLayerTransform.cpp](../../libs/ui/brushstrokelayer/KisBrushStrokeLayerTransform.cpp) | Pixel comparison, selection coverage/moves and full/partial redraw commands |
| [KisBrushStrokeLayerPrivate.h](../../libs/ui/brushstrokelayer/KisBrushStrokeLayerPrivate.h) | Internal length-scale and queued-message helpers; no exported API |
| [KisBrushStrokeLayerIO.cpp](../../libs/ui/KisBrushStrokeLayerIO.cpp) | Serialization, embedded resources and record validation |
| [freehand_stroke.cpp](../../libs/ui/tool/strokes/freehand_stroke.cpp), [kis_painter_based_stroke_strategy.cpp](../../libs/ui/tool/strokes/kis_painter_based_stroke_strategy.cpp) | Recording, sequential dabs and attaching record/scope commands to pixel undo |
| [kis_resources_snapshot.h](../../libs/ui/tool/kis_resources_snapshot.h) | Stroke resources and mirror state |
| [KisRedrawableLayerInterface.h](../../libs/image/KisRedrawableLayerInterface.h), [kis_transform_processing_visitor.cpp](../../libs/image/processing/kis_transform_processing_visitor.cpp) | Image-library interface and image/layer scale hook, without an image-to-UI dependency |
| [inplace_transform_stroke_strategy.cpp](../../plugins/tools/tool_transform2/strokes/inplace_transform_stroke_strategy.cpp), [transform_stroke_strategy.cpp](../../plugins/tools/tool_transform2/strokes/transform_stroke_strategy.cpp) | Final Transform Tool redraw and pixel-transform fallback |
| [move_selection_stroke_strategy.cpp](../../plugins/tools/basictools/strokes/move_selection_stroke_strategy.cpp) | Undoable full-resolution selection move and duplicate ([Duplicate selection](duplicate-selection-in-layer.md)) |
| [libkra](../../plugins/impex/libkra) | Layer attribute, record sidecar and preservation of saved pixels on load failure |

Keep the redraw interface destructor out of line for shared-library RTTI.
The 2026-10-16 responsibility split kept class layout, exported API and behavior;
it introduced no settings or file migration.

## Recording, replay and undo

Record post-smoothing `FreehandStrokeStrategy::Data` jobs (POINT, LINE and
CURVE with control points), not raw tablet input. Each record retains:

- A cloned preset with its stroke-resource snapshot; foreground/background
  colors, effective composite operation (including erase), opacity, zoom,
  pattern and gradient.
- Random seed, painter/copy count, each job's painter index, paint information
  (including canvas rotation/mirroring), and the device offset.
- Each copy's starting dab state: last position/direction if present, spacing
  and timing update intervals, and dab sequence number.
- Canvas mirror flags and the mirror-axis center in image coordinates.

Read the device offset in `initStrokeCallback()`, after earlier queued layer
moves. LOD preview clones do not record. Unsupported drawing-job types abandon
that stroke's record and mark its pixels as an unrecorded change.

Live recorded strokes and replay both enable sequential dab rendering.
Scheduling parallel dab jobs differently can change random image tips and
other brush results even with the same seed. `SequentialRunnableJobs` must
preserve `FreehandStrokeRunnableJobDataWithUpdate` so canvas updates still
arrive. Ordinary paint layers retain their existing parallel behavior.

`renderStrokes()` draws the ordered records through `FreehandStrokeStrategy`
into a temporary native paint layer, flushes asynchronous dabs and waits for
completion. It restores starting spacing, resources, mirror axes and seed.
Replay must not depend on GUI-thread resource-database lookup from processing
threads. Do not replace this path with a simplified point renderer.

`transformedStrokes()` clones records/presets. It maps positions, control
points, starting position/direction and mirror axes using the current device
offset, then the transform. Brush size and texture scale use
`sqrt(abs(determinant))`; pure translation must not reset the brush size
(rounding it can change replay). Texture offsets follow the moved/scaled
strokes in both replay and record mapping. Per-stroke random texture offsets
cannot reliably follow; mismatch detection and fallback still apply.

A completed stroke adds `KisRecordBrushStrokeCommand` to the same undo
transaction as its pixels. `KisRecordedChangeScopeCommand` Opening/Closing
commands bracket both, including indirect-painting merge jobs. Undo runs the
brackets in reverse; first redo accounts for the already-open live scope.
Cancellation, unsupported jobs, empty completion and destruction must close
any remaining scope. Transform redraw similarly brackets record replacement
and the pixel transaction. Preserve pixels and records together on undo/redo.

`setDirty()` outside a recorded scope marks an unrecorded change. LOD-only
dirty notifications do not count. A recorded change must never hide a nearby
filter/edit: the monitor may skip a known state only when a recorded change
occurred with no unrecorded change since the last check.

## Transform and comparison contract

### Eligibility and integration

`createTransformRedrawCommand()` requires records, a device, original pixels,
no keyframe channel, and translation/positive scale only
(`QTransform::type() <= TxScale`, both diagonal scales positive). Rotation,
reflection, shear and perspective fall back to the normal pixel operation.
A null command tells the caller to use that fallback.

Image/layer scaling calls the interface only without a selection. Transform
Tool integration additionally requires Free Transform, no selection and no
external-source transform. In-place dragging remains a pixel preview; replay
runs once at final application against the cached pre-transform pixels.
Read the final transform arguments inside the sequential job, after throttled
updates. Cancelling a continued transform must redraw with
`initialTransformArgs` when reapplying its overridden predecessor.
The overlay-preview strategy also supplies cached original pixels.

### Matching and partial redraw

`sameContent()` compares the union of original and replayed `exactBounds()`,
including erased edges outside the edited device's smaller bounds. Compare
alpha and premultiplied color with tolerance 3/255; invisible color in fully
transparent pixels does not cause a mismatch.

Alpha-bearing F16/F32/F64 color spaces use native channels, preserving HDR and
negative values (including non-RGBA spaces). Other spaces use ARGB32 conversion
in bands of approximately four million pixels. Pixel counts are 64-bit.
A failed image conversion yields a mismatch, never an assumed match.

For a complete match, redraw transformed records and replace pixels and
records in one undoable operation. Otherwise:

1. Build a mismatch selection and count differing pixels versus pixels covered
   by either image (alpha greater than 3/255).
2. Fall back for the entire layer if the original bounds are empty, the caller
   supplies no `PixelTransform`, or `differing * 2 > covered`. Retain the
   untransformed records when the caller falls back.
3. Otherwise redraw the transformed records. Grow the mismatch mask by two
   pixels; transform it and the original pixels using the caller's actual
   pixel-transform algorithm/filter. Feather by
   `max(1, round(sqrt(abs(determinant))))`.
4. Blend those transformed original pixels over the redraw through the mask.
   Floating-point spaces use `KoMixColorsOp` with native channels (direct copy
   for full selection), avoiding HDR clipping in masked COPY's SIMD path.
   Other spaces use masked `COMPOSITE_COPY`.
5. Store the transformed records and pixels together. Log/show the approximate
   pixel-fallback percentage. The mismatching area remains mismatching and is
   handled again on the next scale.

A later brush stroke crossing that mask is also resampled inside the mask.
The record does not contain interleaved raster edits. Do not promise arbitrary
post-edit vector reconstruction or sharpness beyond a bitmap brush tip's
source detail.

### Selection moves

`createSelectionMoveCommand(mask, offset)` classifies each complete stroke
(including mirror copies) as inside, outside or crossing. A quick expanded
bounds test is followed, near a boundary, by replay of that stroke alone.
For alpha `a` and selection `s` on a 0-255 scale, inside requires
`a * (255 - s) <= 3 * 255`; outside requires `a * s <= 3 * 255`.

Move fully selected strokes to the end of the record, matching the pixel
paste order. If any stroke crosses the selection, leave the record unchanged;
later comparison detects the mismatch. Selecting only a mirror copy counts
as cutting its stroke. Erase/non-over compositing can still mismatch after
reordering, so selection moves request verification.

`createSelectionDuplicateCommand(mask, offset)` (Move Tool Ctrl-drag,
[Duplicate selection](duplicate-selection-in-layer.md)) uses the same
classification. Inside strokes stay, and moved clones of them are appended
after every stroke, matching the copy pasted over the layer. A crossing
stroke leaves the record unchanged, as for a move.

The Move Tool applies the record command only at LOD 0, with a copy of the
selection moved back to its original position. Do not update records again
from the Instant Preview clone. Selection transforms/scales remain pixel
operations.

## Background checks and lifetime

`KisBrushStrokeLayerMonitor` lives on the GUI thread. Requests carry no owning
node references. A shared scheduling flag coalesces requests; a 1.5-second
timer waits for edits to settle. Scan layers of open documents and wait for an
idle image; busy images retry after one second. Layers outside open documents
are not checked until a later request/pass finds them again.

Only one worker runs in a dedicated `QThreadPool`. Before copying pixels,
capture the generation; give the copy independent `KisDefaultBounds` so image
LOD state and a closed document cannot leak into it. The monitor's
`m_running` owns the node while the worker compares records with pixels.
Queued completion carries plain values/raw identity, not an owning node.

Accept results only for the same running layer and generation. Discarded
results request another unrecorded check, even if the intervening change was
a brush stroke. The monitor is intentionally never destroyed;
`aboutToQuit` waits for its pool, then releases `m_running`. Owning nodes in
queued events can deadlock during Qt's event teardown; preserve this policy.

`NotMatching` replaces the layer's brush icon with a warning and emits a
seven-second canvas message/log. A return from `NotMatching` to `Matching`
notifies recovery; the first successful check is quiet. The warning currently
uses general pixel-scaling wording; the transform's independent comparison
still permits partial redraw. The icon/state is not the eligibility gate.

## Persistence and compatibility

A `.kra` stores normal paint-layer pixels with `nodetype="paintlayer"` and
`solstice-brushstrokelayer="1"`. Nonempty records are stored alongside pixels
as `layers/<layer>.brushstrokes`; a write failure fails the save. Upstream
Krita reads the saved pixels as a paint layer; saving there drops the Solstice
record/attribute. Other image formats retain pixels only.

The sidecar uses magic `0x5342534c` (SBSL), `QDataStream::Qt_5_15`,
little-endian byte order and double-precision floating-point encoding.
The writer emits version 3; existing versions 1, 2 and 3 remain readable.
Version 2 added per-copy starting dab state; version 3 added canvas mirror
flags/axes. Version 1 has no starts; versions 1/2 default to no canvas mirror.
Earlier applications unable to read a newer record keep the saved pixels.

Preset XML and a deduplicated resource table precede indexed stroke records.
Colors retain model, depth, profile name and raw bytes. Serialize the preset
settings without calling `KisPaintOpPreset::toXML()`: its global-resource
lookup is GUI-thread-only, while saving can run on another thread.
Use the recorded `KisLocalStrokeResources` snapshot for linked resources.

### Embedded resources

Embed brush tips, mask tips, textures, patterns and gradients when their data
can be saved. ABR tips lack standalone serializable files, so `writeResource()`
stores their image as PNG; `readResource()` reconstructs a `KisAbrBrush`
without its original collection and restores name/md5. Saved resources take
precedence; missing/unreadable entries can fall back to installed signature
matches. Rebuild each loaded preset's resource snapshot using its recorded
canvas resources; presets needing no canvas resources can share a snapshot.

### Invalid records and older recordings

Validate before replay: painter/copy count 1-4096, start count no greater than
that count, job indices in range and job type POINT/LINE/CURVE. Positions
(input/control points, device offset, mirror axes and present starting
positions) must be finite with absolute coordinates below 1e7; opacity and
effective zoom must be finite. These are the implemented checks, not a claim
that every numeric field or allocation has a bound.

Invalid/truncated data or a newer version fails loading without publishing a
partial result. The KRA visitor warns and keeps the saved pixels. A missing
brush engine skips affected strokes; the subsequent pixel comparison controls
fallback. Pre-sequential-dab or pre-start-state recordings can remain readable
yet fail to reproduce their saved pixels. Do not silently rewrite those pixels
or treat successful parsing as a match.

## Build, tests and installation

Follow [Development workflow](development-workflow.md) for environment setup
and the source/build/install separation. For implementation changes, build
`kritaui` and affected plugin/dependency targets plus the relevant tests:

| Test target | Coverage to preserve |
| --- | --- |
| `KisBrushStrokeLayerTest` | Record/replay pixels, scales, undo, partial/full fallback, erased edges, HDR/GrayA, serialization/KRA, embedded tips/ABR, starting state, mirrors, many copies, invalid records, texture/selection moves and duplicates, background edit/recovery and device-offset timing |
| `KisBrushStrokeLayerTransformTest` | Actual in-place scale/rotation, cancelled continued transforms and selection moves and duplicates with Instant Preview |
| `KisBrushStrokeReplayTest` | Deterministic native replay and scale/edge-quality comparisons |
| `MoveSelectionStrokeTest`, `kis_processings_test` | Shared Move Tool and image-processing regressions |
| `KisBrushStrokePreviewTest` | Adjacent native replay path when shared recording/rendering code changes |
| `kis_kra_loader_test`, `KisGpuSaveTest` | Additional format/save integration when changing KRA persistence |

The brush suites and their static Transform/Move Tool dependencies are declared
in [brush tests CMakeLists.txt](../../plugins/paintops/defaultpaintops/brush/tests/CMakeLists.txt).
Run matching CTest entries from the configured development environment, for
example (Windows `cmd.exe`; substitute local paths):

```bat
call <krita-dev-root>\env.bat
set QT_QPA_PLATFORM=offscreen
set QT_QPA_FONTDIR=C:\Windows\Fonts
ctest --test-dir <krita-dev-root>\_build -R "KisBrushStrokeLayerTest|KisBrushStrokeLayerTransformTest|KisBrushStrokeReplayTest|MoveSelectionStrokeTest|kis_processings_test" --output-on-failure
```

Use the suites' isolated profiles and bundled presets. For replay image
inspection, `SOLSTICE_REPLAY_DUMP=<folder>` enables the replay test's dumps.
Do not call `image->waitForDone()` while a transform stroke is still open;
the integration tests submit a forced asynchronous update before finishing.

Install rebuilt shared libraries and plugins, including dependencies. For UI:

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\libs\ui\cmake_install.cmake
```

After class-layout changes, rebuild/install dependent KRA plugins before tests
load them from `_install`; stale allocation sizes previously corrupted the
heap. Restart Solstice after installation. Never terminate the user's running
application to unlock DLLs.

## Manual regression checks

Use these for relevant behavior changes; automated tests do not replace them.

1. Draw freehand, Dynamic Brush and Multibrush; include erase, textured/random
   image tips and horizontal/vertical/both canvas mirrors. Scale, save/reopen
   and scale again; confirm expected sharp replay and no unexpected warning.
2. Move a whole layer, then fully selected strokes; check texture alignment,
   ordering and undo/redo. Repeat selection moves with Instant Preview.
   A selection cutting a stroke or only one mirror copy must be detected.
   Duplicate fully selected strokes with Ctrl-drag; the originals stay and
   the copies redraw on scaling.
3. Apply Free Transform scale, undo/redo, then reopen a continued transform
   and cancel. Records and pixels must retain the preceding committed scale.
   Check both preview strategies; rotation and selected transforms use pixels.
4. Alter only part of a layer (filter, cut/move or erased edge), then scale by
   image scaling and Free Transform. Edits survive, matching areas redraw,
   feathered boundaries align, and a majority mismatch falls back entirely.
   Repeat with HDR/negative RGBA floating-point data and GrayA F32.
5. Apply a non-brush edit and immediately draw another stroke. After idle,
   confirm warning; undo the edit and confirm recovery. Check close/reopen,
   deleted-layer restoration, and no stalls during large-brush painting.
6. Confirm Gradient/other unsupported tools refuse painting. Check ordinary
   paint layers remain unaffected and animated layers do not record/replay.
7. Open/save ordinary and stroke-layer KRA files; try embedded image/ABR tips
   without the originals installed. Confirm damaged/newer records keep pixels
   and warn. Recheck autosave followed by continued drawing.

## Verification status and history

The archived 2026-10-16 maintenance entry records successful native builds,
six CTest suites, identical moved implementation bodies/exports, installation
and user confirmation after the responsibility split. The 2026-10-15 audit
and earlier feature stages have their own dated results in
[the development history](wiki/history/brush-stroke-layer-development.md).
Keep those dates and scopes attached to that evidence; this documentation
extraction does not claim a new native build or interactive test.

The original generic cross-machine image-tip check was left unconfirmed;
embedded-tip/ABR automated tests and the ABR plan's dated manual result are
more specific evidence, not a blanket portability guarantee. LOD leakage,
Gradient refusal and canvas-update timing were manual checks in the audit,
not dedicated automated regressions. Preserve these distinctions in future
handoffs. Full measurements, temporary log names and superseded investigations
remain in history; update this reference when the maintained contract changes.
