---
type: query
updated: 2026-10-15
sources:
  - libs/flake/KoToolManager.cpp
  - libs/ui/KisBrushStrokeLayer.cpp
  - libs/ui/KisBrushStrokeLayerIO.cpp
  - plugins/dockers/quickaccess/QuickAdjustKeyController.cpp
related:
  - ../concepts/krita-copy-semantics.md
---

# Maintenance review: 2026-10-15

- A canvas switch can change an application's input-device identity without
  a physical input event. Falling back to the mouse state resurrected an old
  transform tool after a pen-selected brush. Preserve the device and create
  a destination state without activating the stale state first; canvas tool
  lookup must also honor its argument. See
  [document tool state](../../document-tool-state.md) and its regression tests.
- Compare redraws across both devices' bounds: erased edge pixels are outside
  the edited device's bounds. RGBA float comparisons and masked replacement
  must preserve HDR/negative values. Validate stored painter indices before
  background replay. See [Brush Stroke Layer](../../brush-stroke-layer.md).
- A temporary key action must restore the view manager that owned its key
  press, even after focus changes. Weak references and focus-loss cleanup
  prevent state sticking or being restored in another window. See
  [Quick Access](../../quick-access.md).
- Persisted enum values are a compatibility contract: the RGBA16F conversion
  option appends value 3 while existing Ask/Convert32/Keep values remain 0/1/2.
  See [GPU engine](../../gpu-engine.md).

These conclusions have automated regression coverage. Actual tablet/tab
interaction and complete application painting workflows remain manual checks.
