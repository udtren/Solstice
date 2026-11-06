---
type: decision
updated: 2026-11-07
sources:
  - docs/agent/through-layer-eraser.md (alpha locked layers)
  - libs/ui/tool/KisThroughLayerEraserTargets.cpp
  - libs/ui/tool/strokes/kis_painter_based_stroke_strategy.cpp (attachThroughLayerTarget)
related: []
---

# Through-Layer Eraser: alpha locked layers are erased by default

**Decision (user, 2026-11-07).** The Through-Layer Eraser Tool erases layers
with Alpha Lock on, in spite of the lock, while the Tool Options check box
**Erase alpha locked layers** is checked. The box is checked by default
(kritarc `eraseAlphaLocked`, default true). The first release (0.7.0) left
alpha locked layers out with no way to include them.

**Why.** The user found that the tool did nothing to alpha locked layers
and asked for that to change; offered an option, they chose it and asked
for it to be on by default. Alpha Lock keeps transparency, and erasing
changes nothing but transparency, so a strict reading makes the two
exclusive; the option keeps the old behavior for a lock that protects a
shape. The ordinary eraser most likely erases an alpha locked layer
already: the Erase blend mode ignores channel flags on the CPU and the GPU
(read from the code and seen in the tests; not tried with the ordinary
eraser in the application).

**How to apply.**
- Locked layers (the padlock) stay out whatever the option says.
- The stroke turns only the alpha bit back on for its temporary channel
  flags; it never changes the layer's lock.
- If the Erase blend mode is ever made to honor channel flags, the
  through-layer stroke keeps working because of those explicit flags, but
  the ordinary eraser on an alpha locked layer would change behavior.
