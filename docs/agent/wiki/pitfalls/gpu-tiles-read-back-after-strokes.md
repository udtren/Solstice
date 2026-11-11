---
type: pitfall
updated: 2026-11-11
sources:
  - libs/image/gpu/KisGpuTileBackend.cpp (downloadTiles, downloadBatch, injectDownloadFailuresForTesting)
  - libs/ui/KisGpuEngineUi.cpp (showFailureMessage, confirmSave, mayFinishExport)
  - libs/ui/KisImportExportManager.cpp (mayFinishExport before the target is replaced)
  - plugins/extensions/testcontrol/TestControlCanvasTools.cpp (gpu_fault)
related:
  - ../../gpu-engine.md
  - ../../test-control.md
---

# GPU tiles are read back soon after every stroke

A GPU brush stroke leaves its tiles valid only on the GPU. They do not stay
that way for long in a normal window. Measured on 2026-11-11 with Test
Control (`gpu_fault` reports `downloadedTiles`), RGBA F32 1200x900:

- The stroke itself reads back GPU-only tiles it touches (48 tiles for one
  stroke over earlier GPU strokes).
- The Layers docker's thumbnail and the Overview docker read the result
  back within about three seconds (164 more tiles). With both dockers
  hidden, nothing was read back after the stroke.
- A PNG export reads only the projection, which those reads already brought
  to the CPU, so it performs no read-back at all. A `.kra` or PSD save reads
  the layers.

Consequences:

- **Testing a loss during a save.** To make a save or export lose data,
  GPU-only tiles must still exist when it runs. Hide the Layers and Overview
  dockers, paint, inject the failure, then save or export to a layer format
  (`.kra`, PSD) without painting again. With the dockers shown, or with PNG,
  the export simply succeeds: nothing is lost, so nothing is refused. A
  stroke after the injection loses data immediately, before the save.
- **Real device loss.** Unread GPU-only data is normally only a few
  seconds of work, which bounds what a lost device can take. The save
  guards (`confirmSave()`, `mayFinishExport()`) matter most when a save
  starts right after painting.
- **`gpu_fault reset`** lets read-backs resume, but strokes painted after it
  were not left on the GPU in the same process (cause not investigated).
  Use a fresh process for each attempt that needs GPU-only tiles.

The checklist and the recorded acceptance are in
[GPU Engine item 16](../../gpu-engine.md#manual-regression-checklist).
