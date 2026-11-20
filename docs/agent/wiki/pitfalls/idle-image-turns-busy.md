---
type: pitfall
updated: 2026-11-18
sources:
  - libs/ui/KisIdleTasksManager.cpp (setImage(), triggerIdleTask(), slotImageIsIdle(), slotTaskIsCompleted())
  - libs/image/kis_idle_watcher.cpp (triggerCountdownNoDelay(), restartCountdown(), slotIdleCheckTick(), IDLE_CHECK_COUNT)
  - libs/ui/KisIdleTaskStrokeStrategy.cpp (preferredIdleWatcherInterval())
  - libs/image/kis_update_scheduler.cpp (isIdle(), tryBarrierLock())
  - plugins/extensions/testcontrol/TestControlSupport.cpp (lockImageForRead(), ImageLockTimeoutMs)
  - "docs/agent/test-control.md (\"Busy reads after wait_idle (2026-11-18)\")"
related:
  - idle-tasks-wait-for-open-strokes.md
  - synthetic-canvas-input.md
---

# An idle image turns busy by itself

`KisImage::isIdle()` being true says nothing about the next few hundred
milliseconds. `KisIdleTasksManager` (one per `KisViewManager`; layer
thumbnails, Overview, Histogram, Channels) waits for exactly that state and
then starts strokes of its own, one task after the other:

- after a no-delay trigger (`setImage()` for a new view, `addIdleTask()`,
  `triggerIdleTask()`), at the first 50 ms tick of `KisIdleWatcher` that
  finds the image idle; a change to the image does not postpone it;
- after a change (`sigImageModified`), after a 50 ms compressor and five
  50 ms ticks, so about 300 ms after the image fell idle.

Code that waits for `isIdle()` and then calls `tryBarrierLock()` therefore
fails now and then: `tryBarrierLock()` returns false while a stroke is
queued or running, idle tasks included.

The case found (2026-11-18): test control's `wait_idle` returns after three
idle polls 20 ms apart, and the next read (`query_state`, `query_pixels`,
`query_layers`) took `tryBarrierLock(true)` once and failed with "The image
is busy" in 4 of 60 rounds of new document, new layer, `wait_idle`, read. A
debugger log of `KisImage::startStroke()` showed two idle tasks starting
between the layer's own stroke and each failed read. The image was free
again 20 to 35 ms later (800x600).

**Fix used.** The reads keep trying the lock every 20 ms for up to one
second (`TestControlSupport::lockImageForRead()`). Idle tasks only read, so
the delayed read sees the same pixels.

**How to apply.**

- After waiting for an idle image, take the lock with a bounded retry, not
  with a single `tryBarrierLock()`.
- Do not use `barrierLock()` to get past it in a test or a probe: it calls
  `requestStrokeEnd()`, which makes tools end their strokes (the Transform
  Tool applies its transform).
- Do not wait for "no idle task pending" instead: the manager has no such
  query, the tasks restart after every change, and the wait would be 300 ms
  or more each time.
- The opposite problem, idle tasks that never start because a stroke stays
  open, is in [Idle tasks wait for every open stroke](idle-tasks-wait-for-open-strokes.md).
