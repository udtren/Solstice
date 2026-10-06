/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUTRANSFORMWORKER_H
#define KISGPUTRANSFORMWORKER_H

#include <QRect>
#include <QVector>

#include "kis_types.h"
#include "kritaimage_export.h"

/**
 * GPU engine (Solstice, phase 4.94): the resampling passes of
 * KisTransformWorker on the GPU, bit-identical to the CPU passes
 * (docs/agent/gpu-engine.md).
 *
 * KisTransformWorker plans the passes with its own CPU classes
 * (KisFilterWeightsApplicator::setupLine(), KisFilterWeightsBuffer) and hands
 * the plan over. Supported: an x pass followed by a y pass over a whole
 * device (run(), so every pixel lies inside the source rect) on RGBA32F/F16
 * devices with the GPU engine and shaderFloat64. On false the device is
 * unchanged and the caller runs the CPU passes.
 */
class KRITAIMAGE_EXPORT KisGpuTransformWorker
{
public:
    struct PlannedPass {
        bool vertical = false;
        double scale = 1.0;
        double shear = 0.0;
        double dx = 0.0;
        qint32 weightsPositionScale = 256; ///< KisFixedPoint raw value
        qint32 srcStart = 0;
        qint32 srcEnd = 0;
        bool clampToEdge = false;
        qint32 lineFirst = 0;
        QVector<qint32> lineRanges; ///< [dstStart, dstEnd) per line
        qint32 maxSpan = 0;
        QVector<qint32> weights; ///< per offset 0..255: span, centerIndex, maxSpan weights
        QRect srcRect; ///< the bound rect before the pass
        QRect dstRect; ///< the bound rect after the pass
    };

    /// Cheap checks before planning: engine, device format, LOD, wrap-around.
    static bool canRun(KisPaintDeviceSP device);

    /**
     * Applies @p xPass then @p yPass in place to @p device. All pixels must
     * lie inside xPass.srcRect. On false the device is unchanged.
     * Thread: the caller owns the device's tiles.
     */
    static bool runPlannedPasses(KisPaintDeviceSP device, const PlannedPass &xPass, const PlannedPass &yPass);

    /// Enabled with the GPU engine; KRITA_GPU_TRANSFORM=0 disables it.
    static bool isEnabled();
    /// Number of transforms that ran on the GPU (tests, diagnostics).
    static quint64 runCount();
};

#endif // KISGPUTRANSFORMWORKER_H
