---
type: concept
updated: 2026-11-01
sources:
  - libs/ui/tool/strokes/kis_painter_based_stroke_strategy.cpp (updateStrokeNodes())
  - libs/image/kis_image.cc (KisImage::refreshGraphAsync())
  - libs/image/kis_refresh_subtree_walker.h
related:
  - ../pitfalls/paint-ops-read-temporary-target.md
---

# Updating many layers at once

`KisNode::setDirty()` starts one merge walker from that node. The walker
composites the whole stack around it, so calling it for each of N layers in a
stack composites about N * N layers for one update. This has not been measured;
it follows from how the walkers visit the stack.

`KisImage::refreshGraphAsync()` on the root visits every node once and treats
all of them as changed, so the cost is one pass over the visible layers. It also
reruns filter layers that did not need it, which is why a single layer keeps
`setDirty()`. A refresh does not call the layers' `setDirty()`, so a layer that
overrides it does not hear about the change: `KisBrushStrokeLayer` needs
`markChangeUnrecorded()`.

The [Through-Layer Eraser](../../through-layer-eraser.md) uses a refresh for
every update of a stroke with two or more layers.
