/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPULAYERSTACKCOMPOSITOR_H
#define KISGPULAYERSTACKCOMPOSITOR_H

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
 * Phase 0 prototype: flattens a stack of layers with the normal blend mode.
 *
 * The caller provides, per output tile, the GPU tile of every layer (0 for a
 * transparent tile) and the destination tile. Address tables are uploaded
 * into an internal host-visible buffer, so a compositor must not record a new
 * pass while a previous one is still executing on the GPU.
 */
class KRITAGPU_EXPORT KisGpuLayerStackCompositor
{
public:
    ~KisGpuLayerStackCompositor();

    static std::unique_ptr<KisGpuLayerStackCompositor>
    create(KisGpuContext &context, KisGpuTileFormat format, QString *errorMessage = nullptr);

    /**
     * @param layerTiles layer-major table, layerTiles[layer * tileCount + tile]
     * @param dstTiles destination tile per output tile (tileCount entries)
     * @param opacities per-layer opacity in 0..1
     */
    bool record(KisGpuCommandList &commands,
                const QVector<VkDeviceAddress> &layerTiles,
                const QVector<VkDeviceAddress> &dstTiles,
                const QVector<float> &opacities,
                QString *errorMessage = nullptr);

private:
    KisGpuLayerStackCompositor(KisGpuContext &context);
    bool ensureTableCapacity(VkDeviceSize bytes, QString *errorMessage);

    KisGpuContext &m_context;
    std::shared_ptr<KisGpuComputePipeline> m_pipeline; // KisGpuContext::sharedComputePipeline
    std::unique_ptr<KisGpuBuffer> m_tables;
};

#endif // KISGPULAYERSTACKCOMPOSITOR_H
