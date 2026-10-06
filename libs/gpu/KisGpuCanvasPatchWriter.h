/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUCANVASPATCHWRITER_H
#define KISGPUCANVASPATCHWRITER_H

#include <QPoint>
#include <QSize>
#include <QString>
#include <QVector>

#include <memory>

#include "KisGpuTilePool.h"
#include "KisGpuVulkanFunctions.h"
#include "kritagpu_export.h"

class KisGpuContext;
class KisGpuCommandList;
class KisGpuBuffer;
class KisGpuComputePipeline;

/**
 * Writes canvas texture patches from GPU tiles (shaders/canvas_patches.comp,
 * GPU engine phase 3.2): edge-extended blocks in the layout that
 * KisTextureTile::update() uploads, display-converted.
 *
 * Tables live in an internal host-visible buffer: do not record a new pass
 * while the previous one is still executing.
 */
class KRITAGPU_EXPORT KisGpuCanvasPatchWriter
{
public:
    struct Patch {
        /// Grid pixel coordinates of the top-left pixel of the patch center.
        QPoint srcOrigin;
        QSize centerSize;
        /// Left and top margins (repeated edge pixels).
        QPoint margin;
        QSize bufferSize;
        /// Destination offset in pixels inside the output buffer.
        quint32 dstOffset = 0;
    };

    struct Conversion {
        enum Mode {
            Identity = 0,
            MatrixShaper = 1,
        };
        Mode mode = Identity;
        /// Row-major 3x3 matrix applied to linear RGB (MatrixShaper).
        float matrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        /// 3 * curveSize values (R, G, B tables over [0, 1]); empty = linear.
        QVector<float> srcCurves;
        QVector<float> dstCurves;
        int curveSize = 0;
    };

    ~KisGpuCanvasPatchWriter();

    static std::unique_ptr<KisGpuCanvasPatchWriter> create(KisGpuContext &context,
                                                           KisGpuTileFormat sourceFormat,
                                                           bool halfFloatOutput,
                                                           QString *errorMessage = nullptr);

    /// Bytes per output pixel (8 for half float, 16 for float).
    quint32 outputPixelSize() const;

    bool record(KisGpuCommandList &commands,
                const QVector<VkDeviceAddress> &sourceTiles,
                int gridWidth,
                const QVector<Patch> &patches,
                VkDeviceAddress output,
                const Conversion &conversion,
                QString *errorMessage = nullptr);

private:
    explicit KisGpuCanvasPatchWriter(KisGpuContext &context);

    KisGpuContext &m_context;
    std::shared_ptr<KisGpuComputePipeline> m_pipeline; // KisGpuContext::sharedComputePipeline
    std::unique_ptr<KisGpuBuffer> m_tables;
    bool m_halfFloatOutput = false;
};

#endif // KISGPUCANVASPATCHWRITER_H
