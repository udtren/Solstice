---
type: pitfall
updated: 2026-11-01
sources:
  - plugins/paintops/libpaintop/KisOpacityOption.cpp (m_indirectPaintingActive)
  - plugins/paintops/libpaintop/KisFlowOpacityOption.cpp (m_indirectPaintingActive)
  - libs/ui/tool/strokes/kis_painter_based_stroke_strategy.cpp (initStrokeCallback(), attachWhenPaintOpsExist)
  - plugins/paintops/defaultpaintops/brush/tests/KisThroughLayerEraserTest.cpp (testErase)
related:
  - ../concepts/updating-many-layers.md
---

# Paint ops read the temporary target when created

`KisOpacityOption` and `KisFlowOpacityOption` check
`KisIndirectPaintingSupport::hasTemporaryTarget()` on the node when the paint
op is constructed. With a temporary target they keep the opacity sensors out
of the dab's opacity, because a Wash stroke applies its opacity at the merge.

So painting a Build-up preset through a temporary target changes the stroke
unless the target is set afterwards. The Through-Layer Eraser did this at
first: a Build-up stroke erased far more than an ordinary one at the same
settings (alpha 0.23 against 0.37 in `testErase`).

**Avoid it:** create the paint ops with `initPainters()` first, then attach the
device. Wash strokes keep the old order, which is what the option expects.
See [Through-Layer Eraser](../../through-layer-eraser.md).
