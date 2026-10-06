/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUTRANSFORMPASS_H
#define KISGPUTRANSFORMPASS_H

#include <QPoint>
#include <QString>
#include <QVector>

#include <memory>

#include "KisGpuTilePool.h"
#include "KisGpuVulkanFunctions.h"
#include "kritagpu_export.h"

class KisGpuBuffer;
class KisGpuCommandList;
class KisGpuComputePipeline;
class KisGpuContext;

/**
 * One resampling pass of KisTransformWorker on the GPU (phase 4.94,
 * shaders/transform_pass.comp): KisFilterWeightsApplicator::processLine()
 * for every line, bit-identical to the CPU. The caller computes the line
 * ranges and weights on the CPU with the same classes.
 *
 * The pass writes every pixel of the destination tile grid: resampled pixels
 * inside each line's range, the default pixel elsewhere.
 *
 * Requires shaderFloat64. Tables live in an internal host-visible buffer:
 * do not record again before the previous recording has completed.
 */
class KRITAGPU_EXPORT KisGpuTransformPass
{
public:
    struct Pass {
        double scale = 1.0;
        double shear = 0.0;
        double dx = 0.0;
        qint32 weightsPositionScale = 256; ///< KisFixedPoint raw value
        qint32 srcStart = 0; ///< source line range along the line, [srcStart, srcEnd)
        qint32 srcEnd = 0;
        bool clampToEdge = false;
        bool vertical = false; ///< lines are columns (the y pass)
        qint32 lineFirst = 0;
        QVector<qint32> lineRanges; ///< [dstStart, dstEnd) per line, from lineFirst
        qint32 maxSpan = 0;
        QVector<qint32> weights; ///< per offset 0..255: span, centerIndex, maxSpan weights
        QPoint srcGridOrigin; ///< image coordinates of the first source tile
        int srcGridWidth = 0;
        int srcGridHeight = 0;
        QVector<VkDeviceAddress> srcTiles; ///< row-major; 0 reads the default pixel
        QPoint dstGridOrigin;
        int dstGridWidth = 0;
        int dstGridHeight = 0;
        QVector<VkDeviceAddress> dstTiles;
        float defaultPixel[4] = {0.0f, 0.0f, 0.0f, 0.0f}; ///< as float, tile memory order
    };

    ~KisGpuTransformPass();

    /// nullptr without shaderFloat64 or on pipeline failure.
    static std::unique_ptr<KisGpuTransformPass>
    create(KisGpuContext &context, KisGpuTileFormat format, QString *errorMessage = nullptr);

    bool record(KisGpuCommandList &commands, const Pass &pass, QString *errorMessage = nullptr);

private:
    explicit KisGpuTransformPass(KisGpuContext &context);

    KisGpuContext &m_context;
    std::shared_ptr<KisGpuComputePipeline> m_pipeline; // KisGpuContext::sharedComputePipeline
    std::unique_ptr<KisGpuBuffer> m_tables;
};

#endif // KISGPUTRANSFORMPASS_H
