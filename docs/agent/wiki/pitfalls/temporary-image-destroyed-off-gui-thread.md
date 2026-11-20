---
type: pitfall
updated: 2026-11-18
sources:
  - libs/image/kis_image.cc (KisImage::KisImage, moveToThread)
  - libs/ui/brushstrokelayer/KisBrushStrokeLayerReplay.cpp (KisBrushStrokeLayer::renderStrokes)
  - libs/ui/flake/KisBrushStrokeShape.cpp (Data::render)
  - libs/ui/flake/kis_shape_layer.cc (TransformShapeLayerDeferred)
  - plugins/paintops/defaultpaintops/brush/tests/KisVectorBrushStrokeTest.cpp (waitForImage)
related:
  - compressed-updates-outlive-their-model.md
  - tests-load-installed-plugins.md
---

# A temporary image destroyed outside the GUI thread

`KisImage`'s constructor moves the image to the GUI thread. While strokes
run on it, queued signals start timers on its children there, if the GUI
thread's event loop is running. `KisBrushStrokeLayer::renderStrokes()`
creates such an image and dropped its last reference in the calling thread.
From a worker thread Qt then warns "Timers cannot be stopped from another
thread", the timer stays registered for a dead object, and the application
crashes when it fires (seen in `KisVectorBrushStrokeTest` as a crash in
`processEvents()` one test later).

It needs a running GUI event loop during the replay, so tests that block in
`waitForDone()` never showed it. The application always has one.

**Avoid it:** when a temporary `KisImage` is made outside the GUI thread,
have the GUI thread destroy it, as `renderStrokes()` does now
(`retireInGuiThread()`): the image goes on a list that a queued call and
`aboutToQuit` empty. Do not capture the image in the queued call itself: an
event that is never processed is deleted while the application object is
torn down, and destroying an image then crashed
`KisBrushStrokeLayerTransformTest` at exit. Images still listed when the
process ends are leaked on purpose.

## The hand-off must give up the caller's references (2026-11-18)

The first version of `retireInGuiThread()` copied the shared pointers into
the list while `renderStrokes()` still held its own. Under load the GUI
thread emptied the list before `renderStrokes()` returned, so the worker's
local pointer was the last reference after all and the image died in the
worker again (`~KisQueuesProgressUpdater` stopping its `QTimer`). It showed
as the same warning and a crash in a later test, about once in several runs
on a busy machine and never on a quiet one. The function now takes the
caller's pointers and clears them before the list is visible to the GUI
thread.

`KisVectorBrushStrokeTest` counts the warning with a message handler and
fails in `cleanupTestCase()` if it appeared. To find such a place, crash in
the handler: the test's crash report then names the destructor.

## Related: blocking waits and vector layers

`TransformShapeLayerDeferred` changes shapes through a blocking queued call
to the GUI thread. A test whose GUI thread enters `KisImage::waitForDone()`
just after the worker decided the GUI thread was not waiting deadlocks
(about one run in three while scaling an image with a vector layer). Wait
with the event loop running instead: process events until
`KisImage::isIdle()`.

Also in tests: `KisDocument`'s undo stack only tries to lock the image
(`tryBarrierLock()`) and silently skips an undo or redo while it is busy,
for example while a vector layer repaints 25 ms after a change. Settle the
image and the layer's repaint before each step.
