/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUTILEFILL_H
#define KISGPUTILEFILL_H

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
 * Fills whole tiles with one RGBA float color (straight alpha).
 *
 * Like KisGpuLayerStackCompositor, the address table lives in an internal
 * host-visible buffer: do not record a new fill while the previous one is
 * still executing.
 */
class KRITAGPU_EXPORT KisGpuTileFill
{
public:
    ~KisGpuTileFill();

    static std::unique_ptr<KisGpuTileFill>
    create(KisGpuContext &context, KisGpuTileFormat format, QString *errorMessage = nullptr);

    bool record(KisGpuCommandList &commands,
                const QVector<VkDeviceAddress> &tiles,
                const float color[4],
                QString *errorMessage = nullptr);

private:
    explicit KisGpuTileFill(KisGpuContext &context);

    KisGpuContext &m_context;
    std::shared_ptr<KisGpuComputePipeline> m_pipeline; // KisGpuContext::sharedComputePipeline
    std::unique_ptr<KisGpuBuffer> m_table;
};

#endif // KISGPUTILEFILL_H
