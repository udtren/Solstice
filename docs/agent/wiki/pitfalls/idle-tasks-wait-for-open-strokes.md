---
type: pitfall
updated: 2026-10-16
sources:
  - libs/ui/KisLayerThumbnailCache.cpp (startProvisionalThumbnail(), ThumbnailsStroke)
  - libs/ui/kis_node_juggler_compressed.cpp (compressor, selfDestructionCompressor)
  - libs/ui/kis_node_manager.cpp (KisNodeManager::Private::lazyGetJuggler, 750 ms)
  - libs/image/kis_idle_watcher.cpp, libs/ui/KisIdleTasksManager.cpp
  - libs/ui/tests/KisLayerThumbnailCacheTest.cpp (testDuplicateGetsThumbnailWhileJugglerStrokeIsOpen, benchmarkDuplicateThumbnail)
related:
  - weak-pointer-map-keys.md
---

# Idle tasks wait for every open stroke

`KisIdleTasksManager` runs its tasks (layer thumbnails, Overview, Histogram,
Channels) only when `KisImage::isIdle()` is true: no stroke may be queued or
open. It also waits 4 x 50 ms (`KisIdleTaskStrokeStrategy::preferredIdleWatcherInterval()`)
after changes, or one 50 ms tick after a no-delay trigger. It then runs the
queued tasks one at a time.

A stroke that is kept open on purpose therefore delays every idle task. The
case found (2026-10-16, upstream behavior): **Duplicate Layer** (and raise,
lower, remove) runs in a `KisNodeJugglerCompressed` stroke. `KisNodeManager`
creates it with a 750 ms timeout and auto-delete, and it ends only when its
self-destruction compressor (3 x 750 ms, postponed by further actions and
the update timer) fires, or when another stroke requests the end. The
duplicate appeared in the Layers docker after about 15 ms, but its thumbnail
only after about 4.5 s (2480x3508 image, RGBA8 and F32 alike). Generating the
thumbnail takes about 2 ms.

**Fix used.** `KisLayerThumbnailCache::notifyNodeAdded()` starts a provisional
thumbnail for a node without a record on a worker thread (`QtConcurrent`,
`QFutureWatcher` owned by the cache). It is stored with a sequence number that
never matches `thumbnailSeqNo()`, so the idle task still regenerates it once
the image is idle. Thumbnail now after about 30 ms. The provisional result of
a duplicated group may show its projection while it is still being updated;
the idle task replaces it.

**How to apply.** When something must react quickly to a change made inside a
long-lived stroke (node juggler, a Move Tool stroke, transform), do not rely
on an idle task. Read the data off the stroke system, as the Overview live
update and this provisional thumbnail do, and let the idle task correct it.
Shortening the juggler's window instead would change undo merging of
repeated layer actions.
