/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUGRIDWARPWORKER_H
#define KISGPUGRIDWARPWORKER_H

#include <QPolygonF>
#include <QRect>

#include <memory>

#include "kis_types.h"
#include "kritaimage_export.h"

/**
 * GPU engine (Solstice, phase 4.97): the polygon painting of a grid warp
 * (GridIterationTools::PaintDevicePolygonOp, used by
 * KisLiquifyTransformWorker::run()) on the GPU, bit-identical to the CPU
 * (docs/agent/gpu-engine.md).
 *
 * Usage: pass a Recorder to GridIterationTools::iterateThroughGrid() in place
 * of a PaintDevicePolygonOp. It records the operations the CPU would paint,
 * in order, with the CPU's own geometry; run() then paints them on the GPU.
 */
class KRITAIMAGE_EXPORT KisGpuGridWarpWorker
{
public:
    /// The PolygonOp interface of GridIterationTools::PaintDevicePolygonOp.
    class KRITAIMAGE_EXPORT Recorder
    {
    public:
        /// @p sameColorSpace: PaintDevicePolygonOp's color space check.
        explicit Recorder(bool sameColorSpace);
        ~Recorder();
        Recorder(const Recorder &) = delete;
        Recorder &operator=(const Recorder &) = delete;

        void fastCopyArea(QRect areaToCopy);
        void fastCopyArea(QRect areaToCopy, bool lazy);
        void operator()(const QPolygonF &srcPolygon, const QPolygonF &dstPolygon);
        void operator()(const QPolygonF &srcPolygon, const QPolygonF &dstPolygon, const QPolygonF &clipDstPolygon);
        void copyPreviousRects();
        void finalize();
        void setCanMergeRects(bool canMergeRects);
        /// Expected number of operations (e.g. the number of grid cells).
        void reserve(int operations);

        /// False if an operation cannot run on the GPU (e.g. a polygon with
        /// more than four edges).
        bool isValid() const;
        int operationCount() const;

    private:
        friend class KisGpuGridWarpWorker;
        struct Private;
        std::unique_ptr<Private> d;
    };

    /**
     * Cheap checks before recording: engine, formats, LOD, wrap-around, the
     * same color space, default pixel and offset, distinct devices.
     */
    static bool canRun(KisPaintDeviceSP src, KisPaintDeviceSP dst);

    /**
     * Paints the operations of @p recorder from @p src into @p dst, which
     * must have been cleared. On false nothing usable was written: clear
     * @p dst and paint on the CPU.
     * Thread: the caller owns both devices' tiles.
     */
    static bool run(KisPaintDeviceSP src, KisPaintDeviceSP dst, const Recorder &recorder);

    /// Enabled with the GPU engine; KRITA_GPU_LIQUIFY=0 or KRITA_GPU_TRANSFORM=0 disable it.
    static bool isEnabled();
    /// Number of grid warps that ran on the GPU (tests, diagnostics).
    static quint64 runCount();
};

#endif // KISGPUGRIDWARPWORKER_H
