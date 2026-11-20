---
type: pitfall
updated: 2026-11-18
sources:
  - libs/flake/KoShape.cpp (KoShape::KoShape(const KoShape &), setSize(), setSizeImpl(), setTransformation())
  - libs/ui/flake/KisBrushStrokeShape.cpp (KisBrushStrokeShape::createPiece())
  - plugins/paintops/defaultpaintops/brush/tests/KisVectorBrushStrokeTest.cpp (testEraserWholeLineAndCancel)
related:
  - ../concepts/qt-numeric-and-geometry.md
---

# Resizing a copied shape resizes its source

`KoShape`'s copy constructor shares the source's data (`s(rhs.s)`). Most
setters detach it first, but `setSize()` goes through `setSizeImpl()`, a
const function that writes the size without detaching. Calling `setSize()`
first on a fresh copy therefore changes the size of the shape it was copied
from as well.

Seen with the vector eraser on 2026-11-18: the first piece cut from a
stroke shape resized the original, and the second piece, computed from the
original's size, came out with the wrong size and position, so a later
eraser pass missed it.

**Avoid it:** on a copy, call a setter that detaches before `setSize()`.
`setTransformation()` does, even when the matrix does not change (it reads
the matrix through the non-const pointer).
