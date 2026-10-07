/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUGRIDWARPPASS_H
#define KISGPUGRIDWARPPASS_H

#include <QPoint>
#include <QRect>
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
 * Grid warp on the GPU (phase 4.97, shaders/grid_warp.comp): the polygon
 * painting of GridIterationTools::PaintDevicePolygonOp for a list of
 * operations recorded on the CPU (KisGpuGridWarpWorker), bit-identical to the
 * CPU. A later operation overwrites the pixels of an earlier one.
 *
 * The pass writes every pixel of the destination tile grid: the owning
 * operation's pixel, the default pixel where no operation writes.
 *
 * Requires shaderFloat64. Tables and the owner buffer are internal: do not
 * record again before the previous recording has completed.
 */
class KRITAGPU_EXPORT KisGpuGridWarpPass
{
public:
    /// One operation; the layout matches the shader's Op (std430).
    struct Op {
        double edge[4][4]; ///< x1, y1, y2, slope of each polygon edge
        double a[2];
        double c[2];
        double d[2];
        double srcBase[2];
        double dstBase[2];
        double fallback[2];
        double qA;
        double qBConst;
        double qDDiv;
        double xCoeff;
        double yCoeff;
        double padding;
        qint32 rect[4]; ///< x, y, width, height
        qint32 edgeDir[4]; ///< +1, -1, or 0 (no edge)
        qint32 info[4]; ///< [0]: Type
    };
    enum Type {
        Copy = 0,
        Warp = 1,
        Fallback = 2,
    };

    struct Pass {
        const Op *ops = nullptr;
        int opCount = 0;
        QRect ownerRect; ///< bounds of every operation's rect
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

    ~KisGpuGridWarpPass();

    /// nullptr without shaderFloat64 or on pipeline failure.
    static std::unique_ptr<KisGpuGridWarpPass>
    create(KisGpuContext &context, KisGpuTileFormat format, QString *errorMessage = nullptr);

    bool record(KisGpuCommandList &commands, const Pass &pass, QString *errorMessage = nullptr);

    /// After the recording's submission completed: a sample fell outside the
    /// source tile grid, so the result must not be used.
    bool failed() const;

private:
    explicit KisGpuGridWarpPass(KisGpuContext &context);

    KisGpuContext &m_context;
    std::shared_ptr<KisGpuComputePipeline> m_claim;
    std::shared_ptr<KisGpuComputePipeline> m_resolve;
    std::unique_ptr<KisGpuBuffer> m_tables;
    std::unique_ptr<KisGpuBuffer> m_owners;
    std::unique_ptr<KisGpuBuffer> m_flag;
};

#endif // KISGPUGRIDWARPPASS_H
