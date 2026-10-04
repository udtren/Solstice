/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUPROJECTIONCOMPOSITOR_H
#define KISGPUPROJECTIONCOMPOSITOR_H

#include <QRect>
#include <QString>
#include <QVector>

#include <KisGpuLayerCompositor.h>

#include "kis_types.h"
#include "kritaimage_export.h"

/**
 * GPU engine phase 2: composites a run of layers onto a projection device on
 * the GPU (docs/agent/gpu-engine.md).
 *
 * The projection's tiles are composited in place on the GPU (phase 3): they
 * become GPU-valid and CPU-stale, and CPU readers download them on demand.
 * Layers are read through their GPU-resident tiles (KisGpuTileAccess,
 * uploaded only when stale). Only pixels inside the rect change.
 *
 * Requires that no other thread writes the same projection tiles meanwhile:
 * KisUpdaterContext does not run merge jobs that share a tile while the GPU
 * projection is enabled.
 *
 * Thread-safe: concurrent calls use separate work contexts.
 */
class KRITAIMAGE_EXPORT KisGpuProjectionCompositor
{
public:
    struct Layer {
        KisPaintDeviceSP device;
        KisGpuBlendOp op = KisGpuBlendOp::Over;
        float opacity = 1.0f;
        bool alphaLocked = false;
        quint32 channelMask = 0xf;
        bool halfBrush = false;
        bool explicitChannelFlags = false;
    };

    /// Maps a Krita composite op id to a GPU blend op; false if unsupported.
    static bool blendOpForCompositeOp(const QString &compositeOpId, KisGpuBlendOp *op);

    /// Test isolation: refuses while a context is leased; waits outside the pool lock.
    static bool resetWorkContextsForTesting();
    static int workContextCountForTesting();
    static int leasedWorkContextCountForTesting();

    /**
     * Composites @p layers (bottom to top) onto @p projection inside @p rect.
     *
     * Every layer device must have the projection's color space (RGBA F32 or
     * F16) and an offset that differs from the projection's by a multiple of
     * 64 pixels in both directions. Only tiles inside a layer's extent are
     * read, matching KisLayerProjectionPlane (which clips to the extent).
     *
     * Successful submissions return without waiting. Reusing a busy context
     * may wait when all cached contexts are in flight.
     * Optional coverage is a tightly packed CPU snapshot in image coordinates,
     * copied before return. It supports one RGBA32F layer in a supported blend mode,
     * or one RGBA16F layer in a supported brush mode, including channel locks,
     * and is limited to 16 MiB.
     *
     * @return false on failure; the projection is then unchanged
     */
    static bool composite(KisPaintDeviceSP projection,
                          const QRect &rect,
                          const QVector<Layer> &layers,
                          QString *errorMessage = nullptr,
                          const KisGpuLayerCompositor::Mask *mask = nullptr);
};

#endif // KISGPUPROJECTIONCOMPOSITOR_H
