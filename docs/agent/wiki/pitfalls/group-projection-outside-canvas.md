---
type: pitfall
updated: 2026-10-29
sources:
  - "libs/image/kis_group_layer.cc (copy constructor, setImage())"
  - "libs/image/kis_image.cc (KisImage::requestProjectionUpdate())"
  - "libs/image/kis_async_merger.cpp (KisAsyncMerger::setupProjection())"
  - "libs/image/kis_paint_device_cache.h (createThumbnail(), Precise bounds)"
  - "libs/image/tests/kis_group_layer_test.cpp (testCopyToSmallerImageCropsProjection)"
related:
  - strong-image-ref-during-copy.md
  - ../concepts/krita-copy-semantics.md
---

# Group projections stop at the canvas

A group layer's projection is a cache, and it is only maintained inside the
image bounds. `KisImage::requestProjectionUpdate()` crops every update to
`bounds()`, and the merger clears and recomposes a group's projection only
in that cropped rect (`KisAsyncMerger::setupProjection()`). Paint layers are
different: their own pixels outside the canvas are real data and move with
transforms.

Pixels outside the canvas in a group projection are therefore never
refreshed. They can only come from another image: the `KisGroupLayer` copy
constructor copies the projection, so a group copied from a larger document
(or from one where it sat elsewhere) brings pixels that land outside the
target canvas. The layer thumbnail and its tooltip are made from the
projection's exact bounds (`KisThumbnailBoundsMode::Precise`), so they kept
showing those stale pixels after Ctrl+T moved the children (user report,
2026-10-29).

`KisGroupLayer::setImage()` now crops the group's own projection to the
image bounds whenever the group joins an image. Note:

- Crop the group's own device, not `original()`: with one meaningful child,
  `original()` is that child's projection (the obligeChild shortcut), and
  cropping it would drop the child's real pixels.
- Use `image.isValid()` before reading the image in `setImage()`: it also
  runs while `KisImage`'s copy constructor clones the root
  ([No strong image reference during a copy](strong-image-ref-during-copy.md)).
- `KisSelectionBasedLayer` (filter and fill layers) also copies its cached
  original, and its thumbnail reads that original, so the same stale
  pixels may show there after a cross-document paste. Not changed or
  verified yet: check whether fill-layer generators write outside the
  image bounds before cropping in its `setImage()`.
