---
type: pitfall
updated: 2026-11-18
sources:
  - "libs/image/kis_image.cc (KisImage::copyFromImageImpl())"
  - "libs/image/kis_layer.cc (KisLayer::setImage())"
  - "libs/image/KisClipStackLayer.cpp (constructor, syncColorSpace())"
  - "libs/image/tests/KisClippingTest.cpp (testImageCopyKeepsClipping)"
  - "libs/ui/flake/kis_shape_layer_canvas.cpp (KisShapeLayerCanvas::setImage(), slotStartAsyncRepaint())"
  - "plugins/paintops/defaultpaintops/brush/tests/KisVectorBrushStrokeTest.cpp (testDocumentCopyWithVectorLayer)"
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

## Vector layers: a repaint started from `setImage()` (2026-11-18)

Autosave froze Solstice a few minutes after drawing on a vector layer. The
stacks of the frozen process showed the GUI thread in `~KisImage` inside
`KisShapeLayerCanvas::slotStartAsyncRepaint()`, itself inside the image's
copy constructor, and a worker in `~KisImage` of the same image.

The path: a copied vector layer resets its cache when the resolution is not
72 ppi (2026-W44 intake, upstream 6f43b89f81), which marks an update for
`setImage()`. `setImage()` called `updateCanvas()`, whose signal compressor
emits at once, so `slotStartAsyncRepaint()` took a `KisImageSP`, queued the
repaint job (which takes more) and released it. It needs no brush stroke: a
path shape does the same. A crash in `KisNode::graphListener()` is the other
outcome.

`setImage()` now records the dirty rectangle and starts the compressor from
the event loop. A compressor that emits on the first `start()` is a
synchronous call: anything it reaches is "code reached from `setImage()`".

### The first fix was not enough (same day)

Deferring the repaint left the copy with a pending update, and
`KisDocument::initiateSavingInBackground()` asserts that a copy has none:
the user got the safe-assert dialog at every autosave. My scratch run had
`KRITA_NO_ASSERT_MSG=1` and never showed it; read `solstice.log` for
"ASSERT" after such runs.

The copy should not have needed a repaint at all. Two things marked it:

- its container model started at 72 ppi, so `slotImageResolutionChanged()`
  saw a change against its (unchanged) image and reset the cache, which
  also cleared the picture the canvas had just copied;
- the shape manager forwards an update of an empty rectangle to a new
  canvas, and `updateCanvas()` without an image took any update as a
  reason to repaint everything in `setImage()`.

The copy constructor now copies the model's resolution and empty updates
are ignored; the deferred start stays for a copy that really changed
resolution (a paste into another document).
