/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KISGPUBRUSHPAINTER_H
#define KISGPUBRUSHPAINTER_H
#include "kis_types.h"
#include "kritaimage_export.h"
#include <QList>
#include <QRect>
#include <QVector>
class KisPainter;
struct KisRenderedDab;
class KRITAIMAGE_EXPORT KisGpuBrushPainter
{
public:
    /// Phase 4.1 development gate: KRITA_GPU_BRUSH=1 plus the GPU engine.
    static bool isEnabled();
    static bool supports(KisPainter *painter);
    /// Entire batch in a sequential stroke job, never parallel rectangle jobs.
    /// Paints one pass only; the brush op schedules each mirrored pass after
    /// its existing CPU pixel/position reflection jobs have finished.
    /// False means no GPU write was submitted; use the unchanged CPU batch.
    /// True also covers an empty selection intersection (nothing to paint).
    /// Successful submission does not wait for GPU completion. Source/mask
    /// snapshots are retained in a bounded ring; tile readers synchronize on demand.
    /// Optional disjoint CPU paint rectangles clip the dabs on GPU, preserving
    /// fractional mirror-axis rounding. Overlapping rectangles use CPU fallback.
    static bool
    paint(KisPainter *painter, const QList<KisRenderedDab> &dabs, const QVector<QRect> *paintRects = nullptr);
    /// Original + reflected passes in one submission, without mutating the
    /// supplied dabs. False leaves every pass for the normal per-pass fallback.
    static bool paintMirrored(KisPainter *painter, const QList<KisRenderedDab> &dabs, const QVector<QRect> &paintRects);
    static quint64 batchCount();
    struct StagingStatistics {
        quint64 bytes = 0;
        int contexts = 0;
    };
    static StagingStatistics stagingStatistics();
    /// Tests only: drain and release the staging ring while the backend is alive.
    static bool resetStagingForTesting();
    /// Supported RGBA32F Wash blend modes with selection and CPU-compatible channel flags.
    /// Caller must provide tile-exclusive projection scheduling in a float image.
    /// False leaves the destination unchanged for CPU fallback; does not wait.
    static bool paintWashPreview(KisPainter *painter, KisPaintDeviceSP source, const QRect &rect);
    static quint64 washPreviewCount();
    /// Final Wash merge inside the caller's transaction and barrier-protected
    /// merge jobs. Requires disjoint complete 64px tiles on the image grid.
    /// False leaves this rectangle unchanged for CPU fallback; does not wait.
    static bool mergeWash(KisPainter *painter, KisPaintDeviceSP source, const QRect &rect);
    static quint64 washMergeCount();

private:
    static bool compositeWash(KisPainter *painter, KisPaintDeviceSP source, const QRect &rect);
    static bool paintImpl(KisPainter *painter,
                          const QList<KisRenderedDab> &dabs,
                          const QVector<QRect> *paintRects,
                          bool combineMirrors);
};
#endif
