---
type: pitfall
updated: 2026-10-27
sources:
  - "libs/image/kis_image.cc (KisImage::copyFromImageImpl())"
  - "libs/image/kis_layer.cc (KisLayer::setImage())"
  - "libs/image/KisClipStackLayer.cpp (constructor, syncColorSpace())"
  - "libs/image/tests/KisClippingTest.cpp (testImageCopyKeepsClipping)"
related:
  - weak-pointer-map-keys.md
---

# No strong image reference while an image is being copied

Saving a document copies its image. `KisImage`'s copy constructor runs
`copyFromImageImpl()`, which clones the root and calls `setImage()` on
every node, all before any `KisImageSP` holds the new image: its reference
count is still 0.

Code reached from there must not call `image().toStrongRef()` or build a
`KisImageSP` from it. The temporary reference raises the count to 1. When it
is released the count drops back to 0, and the shared pointer deletes the
image that is still being constructed. The crash then shows up later and
somewhere else, in `~KisImagePrivate` (`rootLayer->graphListener()`).

This happened with Clipping Mask on 2026-10-27: saving a `.kra` with a
clipped layer crashed Solstice. `KisLayer::setImage()` forwarded the image
to the base's `KisClipStackLayer`, and that layer read the image's color
space through `toStrongRef()`.

Read what you need through the weak pointer instead
(`image.isValid() ? image.data()->colorSpace() : ...`), or pass it in as a
parameter. The same applies to anything a node creates lazily: it can be
created during the copy.
