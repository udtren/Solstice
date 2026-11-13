---
type: pitfall
updated: 2026-11-13
sources:
  - libs/image/kis_paint_device.cc (KisPaintDevice::moveTo, calculateExactBounds, Impl::calculateExactBoundsTileWise)
  - libs/image/kis_paint_device_cache.h (validBounds, setTranslatedBounds)
  - libs/ui/tool/strokes/move_stroke_strategy.cpp (MoveNodeStrategyBase::moveNodeCommon)
  - plugins/impex/libkra/tests/KisMoveDocumentBenchmark.cpp
related:
  - ../benchmarks/move-layer-updates.md
  - ../benchmarks/exact-bounds-scan.md
  - gpu-tiles-read-back-after-strokes.md
  - extent-smaller-than-tiles.md
---

# Exact bounds after a move

**What happened.** After the GPU engine learned to composite moved layers
(phase 4.112), moving a group in a user's document still took about 0.4 s
per update. A benchmark on that document showed 330 of 364 ms per Move
Tool step in `tightUserVisibleBounds()`, with the GPU engine on or off.

**Why.**

- `KisPaintDevice::exactBounds()` is cached, but
  `calculateExactBounds()` scans the device's tile extent until it meets
  content. A layer whose tiles cover much more than its content (erased
  areas, pasted or converted layers) is scanned almost completely: 35 ms
  for a layer with 133x171 pixels of content in a 1754x2480 RGBA F16
  document.
- `KisPaintDevice::moveTo()` invalidated the cache, and the Move Tool asks
  for the tight bounds of every moved node at every pointer position
  (`MoveNodeStrategyBase::moveNodeCommon()`), hidden layers included.
  17 layers made 330 ms.
- A group's tight bounds read its projection, which the GPU engine holds
  after every update (a readback per step; the Move Tool uses the loose
  bounds for groups now).

**Fix.** `moveTo()` keeps the cached exact bounds and non-default pixel
area and translates them. It still drops them for a device with a visible
default pixel (its bounds include the image bounds, which do not move) and
in wrap-around mode (the move rewrites pixels).

**How to avoid it.**

- Do not call `exactBounds()` / `tightUserVisibleBounds()` in a loop that
  also changes the device; a write still drops the cache. Use `extent()` /
  `looseUserVisibleBounds()` or `exactBoundsAmortized()` where a larger
  rect is acceptable.
- A synthetic benchmark whose layers are filled rect by rect has tiles
  that match the content, and showed 0.6 ms where the document needed
  330 ms. Measure a real document
  (`KisMoveDocumentBenchmark`, `docs/agent/gpu-engine.md`, "Build, test,
  and install") before concluding.
- The first request after the pixels changed still scans. The Move Tool
  pays it once when a stroke starts (the outline rect).

**The scan itself (2026-11-13).** The cache only helps the second request.
In the user's paintings (RGBA8, layers whose tiles reach far outside the
canvas) one uncached scan took up to 430 ms and all layers of one document
3.5 s. `calculateExactBounds()` now scans tile by tile: 9 ms and 72 ms
([measurements](../benchmarks/exact-bounds-scan.md),
`docs/agent/gpu-engine.md`, "Moved layers and groups"). It still reads
every tile outside the content once, so the advice above stands for code
that asks in a loop.
