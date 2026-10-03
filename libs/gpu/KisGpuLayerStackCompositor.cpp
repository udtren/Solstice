/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuLayerStackCompositor.h"

#include "KisGpuBuffer.h"
#include "KisGpuCommandList.h"
#include "KisGpuComputePipeline.h"
#include "KisGpuContext.h"

#include <cstring>

#include <kis_debug.h>

namespace
{
const quint32 CompositeOverStackRgba32f[] = {
#include "composite_over_stack_rgba32f.spv.inc"
};

const quint32 CompositeOverStackRgba16f[] = {
#include "composite_over_stack_rgba16f.spv.inc"
};

struct PushConstants {
    VkDeviceAddress layerTiles;
    VkDeviceAddress dstTiles;
    VkDeviceAddress opacities;
    quint32 tileCount;
    quint32 layerCount;
};
static_assert(sizeof(PushConstants) == 32, "must match the shader push constant block");

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}
} // namespace

KisGpuLayerStackCompositor::KisGpuLayerStackCompositor(KisGpuContext &context)
    : m_context(context)
{
}

KisGpuLayerStackCompositor::~KisGpuLayerStackCompositor() = default;

std::unique_ptr<KisGpuLayerStackCompositor>
KisGpuLayerStackCompositor::create(KisGpuContext &context, KisGpuTileFormat format, QString *errorMessage)
{
    std::unique_ptr<KisGpuLayerStackCompositor> compositor(new KisGpuLayerStackCompositor(context));

    const quint32 *spirv = nullptr;
    size_t spirvSize = 0;
    switch (format) {
    case KisGpuTileFormat::RGBA32F:
        spirv = CompositeOverStackRgba32f;
        spirvSize = sizeof(CompositeOverStackRgba32f);
        break;
    case KisGpuTileFormat::RGBA16F:
        spirv = CompositeOverStackRgba16f;
        spirvSize = sizeof(CompositeOverStackRgba16f);
        break;
    }

    compositor->m_pipeline =
        KisGpuComputePipeline::create(context, spirv, spirvSize, sizeof(PushConstants), errorMessage);
    if (!compositor->m_pipeline) {
        return nullptr;
    }
    return compositor;
}

bool KisGpuLayerStackCompositor::ensureTableCapacity(VkDeviceSize bytes, QString *errorMessage)
{
    if (m_tables && m_tables->size() >= bytes) {
        return true;
    }
    m_tables = KisGpuBuffer::create(m_context, bytes, KisGpuBuffer::Location::Upload, errorMessage);
    return bool(m_tables);
}

bool KisGpuLayerStackCompositor::record(KisGpuCommandList &commands,
                                        const QVector<VkDeviceAddress> &layerTiles,
                                        const QVector<VkDeviceAddress> &dstTiles,
                                        const QVector<float> &opacities,
                                        QString *errorMessage)
{
    const int tileCount = dstTiles.size();
    const int layerCount = opacities.size();
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(layerTiles.size() == tileCount * layerCount, false);
    if (!tileCount) {
        return true;
    }

    const VkDeviceSize layerTableBytes = VkDeviceSize(layerTiles.size()) * sizeof(VkDeviceAddress);
    const VkDeviceSize dstTableOffset = alignUp(layerTableBytes, 16);
    const VkDeviceSize dstTableBytes = VkDeviceSize(dstTiles.size()) * sizeof(VkDeviceAddress);
    const VkDeviceSize opacityOffset = alignUp(dstTableOffset + dstTableBytes, 16);
    const VkDeviceSize totalBytes = opacityOffset + VkDeviceSize(qMax(1, layerCount)) * sizeof(float);

    if (!ensureTableCapacity(totalBytes, errorMessage)) {
        return false;
    }

    quint8 *mapped = static_cast<quint8 *>(m_tables->mapped());
    std::memcpy(mapped, layerTiles.constData(), layerTableBytes);
    std::memcpy(mapped + dstTableOffset, dstTiles.constData(), dstTableBytes);
    std::memcpy(mapped + opacityOffset, opacities.constData(), size_t(layerCount) * sizeof(float));

    PushConstants constants{};
    constants.layerTiles = m_tables->deviceAddress();
    constants.dstTiles = m_tables->deviceAddress() + dstTableOffset;
    constants.opacities = m_tables->deviceAddress() + opacityOffset;
    constants.tileCount = quint32(tileCount);
    constants.layerCount = quint32(layerCount);

    m_pipeline->dispatch(commands.commandBuffer(), constants, quint32(tileCount));
    return true;
}
