/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUMASKINGWORKER_H
#define KISGPUMASKINGWORKER_H

#include <QRect>
#include <QString>

#include "kis_types.h"
#include "kritaimage_export.h"

class KoColorSpace;

/**
 * GPU engine (Solstice): the masking brush composite on the GPU, so that a
 * stroke with a masking brush can be painted with the GPU brush. The mask is
 * painted on the CPU (a small gray device); the stroke and the composite stay
 * on the GPU. Bit-identical to KisMaskingBrushRenderer::updateProjection().
 * See docs/agent/gpu-engine.md ("Masking brush").
 */
class KRITAIMAGE_EXPORT KisGpuMaskingWorker
{
public:
    /**
     * Whether strokes into @p colorSpace with the masking composite
     * @p compositeOpId can use the GPU: the GPU brush is on, the device has
     * shaderFloat64, the color space is RGBA32F and the mode is one without
     * strength. The brush op and the stroke decide with it once per stroke.
     */
    static bool supports(const KoColorSpace *colorSpace, const QString &compositeOpId);

    /**
     * Inside @p rect, writes @p strokeDevice into @p dstDevice with its alpha
     * composited with @p maskDevice (GrayA8). False when it could not run;
     * the devices are then unchanged and the caller composites on the CPU.
     */
    static bool apply(KisPaintDeviceSP strokeDevice,
                      KisPaintDeviceSP maskDevice,
                      KisPaintDeviceSP dstDevice,
                      const QRect &rect,
                      const QString &compositeOpId);

    /// Composites that ran on the GPU (tests, diagnostics).
    static quint64 runCount();
};

#endif // KISGPUMASKINGWORKER_H
