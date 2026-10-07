# Overview live update: technical notes

User guide: [`../overview-live-update.md`](../overview-live-update.md).

Added 2026-10-07 at the user's request: the Overview thumbnail follows the
canvas while painting. The user chose to use the same CPU path with and
without the GPU engine and to measure afterwards.

## Before

`OverviewWidget` regenerates its thumbnail as an idle task
(`KisIdleTasksManager`, `KisImageThumbnailStrokeStrategy`): only after the
image has been idle for 4 x 50 ms (`KisIdleTaskStrokeStrategy::preferredIdleWatcherInterval()`).
The thumbnail stroke is queued like other strokes, so it cannot run during a
user's stroke; shortening the interval alone would not update while
painting.

## Live update

| Part | Location |
| --- | --- |
| Change notification | `KisImage::sigImageUpdated(QRect)` (the signal the canvas uses), connected with `Qt::DirectConnection` in `OverviewWidget::setCanvas()`; `slotImageUpdated()` runs on image worker threads, unites the rect under `m_dirtyLock` and queues `KisSignalCompressor::start()` |
| Throttle | `m_liveCompressor`: 100 ms, `FIRST_ACTIVE` |
| Update | `startLiveUpdate()` (GUI thread) and `finishLiveUpdate()`; one `QtConcurrent` job at a time (`m_liveWatcher`) |
| Setting | `Solstice/OverviewLiveUpdate` (default true), Settings → General → Window check box `m_chkOverviewLiveUpdate`; read on `KisConfigNotifier::configChanged()` |

`startLiveUpdate()` maps the changed image rect to thumbnail pixels (plus one
pixel), then on a worker thread:

1. `projection->createThumbnailDevice(2 x thumbnail size, image bounds,
   2 x target)` samples only that part of the thumbnail at twice its size
   (once when the thumbnail is as large as the image);
2. converts it to display colors (`KisDisplayConfig` profile, intent and
   flags, as the idle task does);
3. scales it to the target (smooth; fast for pixel art).

`finishLiveUpdate()` paints the result into `m_pixmap` (source composition)
and restarts the compressor if more changes arrived. Results are dropped when
`m_liveGeneration` changed (canvas switch or a new thumbnail size from the
idle task). Changes over a quarter of the image are skipped; the idle task
regenerates the whole thumbnail afterwards, as before.

## Cost

Measured with a temporary benchmark (removed), 2480x3508 RGBA32F, thumbnail
400 px high, per update on one worker thread:

| Changed area | Convert, then scale | Sample at thumbnail size (used) |
| --- | --- | --- |
| 300x300 | 5.9 ms | 2.0 ms |
| 800x800 | 32 ms | 2.1 ms |
| 1240x1754 (a quarter) | 108 ms | 7.4 ms |

Converting the whole changed area to display colors first dominated; the
sampling route keeps the cost proportional to thumbnail pixels.

With the GPU engine the projection tiles are GPU-resident; reading the
changed area makes the tile engine download those tiles. The effect on
input-to-display latency has not been measured yet (paint-trace procedure in
`gpu-engine.md`).

## Limitations

- With level of detail (instant preview), `sigImageUpdated` reports changes
  of the LOD plane while the LOD 0 projection is unchanged until the stroke's
  final pass, so the live update shows the result only then.
- Reads happen while strokes write the projection (as for the canvas); a
  live update may show a partially updated tile until the next update or the
  idle regeneration.
- Live samples are point samples at 2x and smoothed; edges of updated areas
  can differ slightly from the idle task's thumbnail until it runs.

## Manual checks

- The Overview follows strokes during painting, at roughly ten updates per
  second; after the stroke it matches the full regeneration.
- Fill or filter a large area: the Overview updates when it finishes.
- Turning the option off restores updates after idle only.
- Resizing the docker, switching documents and closing documents while
  painting cause no stale or misplaced patches.
- Painting latency with and without the option, with the GPU engine on.
