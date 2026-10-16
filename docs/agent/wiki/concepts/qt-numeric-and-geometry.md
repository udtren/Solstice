---
type: concept
updated: 2026-10-16
sources:
  - <krita-dev-root>/_install/include/QtCore/qnumeric.h (Qt 6.8.0)
  - https://github.com/qt/qtbase/blob/v6.8.0/src/gui/painting/qpolygon.cpp
  - docs/agent/github-actions.md (Dependency and build files; package checks)
related:
  - cpu-gpu-bit-parity.md
  - krita-copy-semantics.md
---

# Qt numeric and geometry semantics

These observations use the local development installation's Qt 6.8.0 headers
and sources, as inspected on 2026-10-07 (`QT_VERSION_STR` in `qconfig.h`).
They are not a version guarantee for every build: the
[CI package checks](../../github-actions.md) record a different Qt version.
For a new parity investigation, identify the target build's Qt version and
inspect its headers/source revision before applying these observations. The
original local headers are under `_install/include`; the source tag used here
is `v6.8.0`.

## `qRound(double)`

- The variant depends on the platform (`qnumeric.h`):
  - ARM64 with GCC: `int(__builtin_round(d))`;
  - x86-64 with SSE2 and clang/GCC (this build):
    `int(d + __builtin_copysign(0.5, d))`;
  - other platforms: `d >= 0 ? int(d + 0.5) : int(d - 0.5)`.
- For non-negative values, the x86-64 variant is `int(d + 0.5)` with a
  rounded double addition. This differs from round-half-away-from-zero at
  0.49999999999999994. Reproduce the addition, not `round()`.

## `QPolygonF::containsPoint(pt, Qt::OddEvenFill)`

- The function walks the edges `(p[i-1], p[i])`. It adds the closing edge
  `(last, first)` only when `last != first`; `QPointF::operator==` is fuzzy.
- For each edge, `qt_polygon_isect_line()`:
  - ignores the edge when `qFuzzyCompare(y1, y2)`;
  - orders the end points by y, and the winding direction is -1 when they
    were swapped;
  - counts the edge when `y1 <= pt.y < y2` and
    `x1 + ((x2 - x1) / (y2 - y1)) * (pt.y - y1) <= pt.x`.
- The point is inside when the winding number is odd.
- Everything except the last test is independent of the pixel and can be
  precomputed on the CPU (as `KisGpuGridWarpWorker::Recorder` does). On the
  GPU, keep `x1 + slope * (y - y1)` as a `precise` multiply then add.

## `QPolygonF::boundingRect()` and `QRectF::toAlignedRect()`

- `boundingRect()` is the min/max over the points: `QRectF(minx, miny,
  maxx - minx, maxy - miny)`.
- `toAlignedRect()` is `floor(x)` to `ceil(x + w)`. `x + w` is computed again
  and can differ from `maxx` by rounding.
- Compute bound rects on the CPU with Qt itself rather than in a shader.

## Fuzzy comparisons

- `qFuzzyCompare(a, b)` for doubles is relative:
  `qAbs(a - b) * 1e12 <= qMin(qAbs(a), qAbs(b))`. Near 0 it only accepts
  equality; `qFuzzyIsNull()` is the absolute test.
- `KisAlgebra2D::fuzzyPointCompare(p1, p2, delta)` is absolute per
  coordinate, and its overload for polygons compares point by point.
