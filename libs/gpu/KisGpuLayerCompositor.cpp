/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuLayerCompositor.h"

#include "KisGpuBuffer.h"
#include "KisGpuCommandList.h"
#include "KisGpuComputePipeline.h"
#include "KisGpuContext.h"

#include <cstring>

#include <kis_debug.h>

namespace
{
const quint32 CompositeLayersRgba32f[] = {
#include "composite_layers_rgba32f.spv.inc"
};

const quint32 CompositeLayersRgba16f[] = {
#include "composite_layers_rgba16f.spv.inc"
};
const quint32 CompositeLayersExtendedRgba32f[] = {
#include "composite_layers_extended_rgba32f.spv.inc"
};
const quint32 CompositeLayersExtendedRgba16f[] = {
#include "composite_layers_extended_rgba16f.spv.inc"
};

struct PushConstants {
    VkDeviceAddress layerTiles;
    VkDeviceAddress dstTiles;
    VkDeviceAddress layers;
    quint32 tileCount;
    quint32 layerCount;
    quint32 gridWidth;
    quint32 padding[3];
    qint32 clip[4];
    VkDeviceAddress mask;
    qint32 maskOrigin[2];
    qint32 maskSize[2];
    quint32 maskPadding[2];
};
static_assert(sizeof(PushConstants) == 96, "must match the shader push constant block");

struct LayerParams {
    float opacity;
    quint32 op;
    quint32 flags;
    quint32 channelMask;
};
static_assert(sizeof(LayerParams) == 16, "must match the shader LayerParams struct");

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}
} // namespace

KisGpuLayerCompositor::KisGpuLayerCompositor(KisGpuContext &context)
    : m_context(context)
{
}

KisGpuLayerCompositor::~KisGpuLayerCompositor() = default;

std::unique_ptr<KisGpuLayerCompositor>
KisGpuLayerCompositor::create(KisGpuContext &context, KisGpuTileFormat format, QString *errorMessage)
{
    std::unique_ptr<KisGpuLayerCompositor> compositor(new KisGpuLayerCompositor(context));
    const bool f16 = format == KisGpuTileFormat::RGBA16F;
    compositor->m_f16 = f16;
    compositor->m_pipeline =
        KisGpuComputePipeline::create(context,
                                      f16 ? CompositeLayersRgba16f : CompositeLayersRgba32f,
                                      f16 ? sizeof(CompositeLayersRgba16f) : sizeof(CompositeLayersRgba32f),
                                      sizeof(PushConstants),
                                      errorMessage);
    return compositor->m_pipeline ? std::move(compositor) : nullptr;
}

bool KisGpuLayerCompositor::record(KisGpuCommandList &commands,
                                   const QVector<VkDeviceAddress> &layerTiles,
                                   const QVector<VkDeviceAddress> &dstTiles,
                                   const QVector<Layer> &layers,
                                   int gridWidth,
                                   const QRect &clip,
                                   QString *errorMessage,
                                   const Mask *mask)
{
    const int tileCount = dstTiles.size();
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(layerTiles.size() == tileCount * layers.size(), false);
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(gridWidth > 0, false);
    if (!tileCount || layers.isEmpty() || clip.isEmpty()) {
        return true;
    }

    const VkDeviceSize layerTableBytes = VkDeviceSize(layerTiles.size()) * sizeof(VkDeviceAddress);
    const VkDeviceSize dstTableOffset = alignUp(layerTableBytes, 16);
    const VkDeviceSize dstTableBytes = VkDeviceSize(tileCount) * sizeof(VkDeviceAddress);
    const VkDeviceSize paramsOffset = alignUp(dstTableOffset + dstTableBytes, 16);
    const VkDeviceSize maskOffset = paramsOffset + VkDeviceSize(layers.size()) * sizeof(LayerParams);
    const qint64 maskBytes = mask ? qint64(mask->bounds.width()) * mask->bounds.height() : 0;
    if (mask && (!mask->data || mask->bounds.isEmpty() || maskBytes > 4096 * 4096 || layers.size() != 1)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("unsupported or oversized layer coverage mask");
        return false;
    }
    const VkDeviceSize totalBytes = maskOffset + alignUp(VkDeviceSize(maskBytes), 4);
    bool extended = false;
    for (const auto &layer : layers) {
        if (layer.halfBrush && (!m_f16 || (layer.op != KisGpuBlendOp::Over && layer.op != KisGpuBlendOp::Erase))) {
            if (errorMessage)
                *errorMessage = QStringLiteral("half brush arithmetic requires F16 Normal or Erase");
            return false;
        }
        if (layer.channelMask > 0xf || quint32(layer.op) >= quint32(KisGpuBlendOp::Count)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("unsupported layer channel mask");
            return false;
        }
        extended |= layer.op >= KisGpuBlendOp::SoftLightSvg;
    }

    // Compile the extra rounding constraints separately so they cannot affect
    // established modes through shared-expression optimization.
    if (extended && !m_extendedPipeline) {
        m_extendedPipeline = KisGpuComputePipeline::create(
            m_context,
            m_f16 ? CompositeLayersExtendedRgba16f : CompositeLayersExtendedRgba32f,
            m_f16 ? sizeof(CompositeLayersExtendedRgba16f) : sizeof(CompositeLayersExtendedRgba32f),
            sizeof(PushConstants),
            errorMessage);
        if (!m_extendedPipeline)
            return false;
    }

    if (!m_tables || m_tables->size() < totalBytes) {
        m_tables = KisGpuBuffer::create(m_context, totalBytes, KisGpuBuffer::Location::Upload, errorMessage);
        if (!m_tables) {
            return false;
        }
    }

    quint8 *mapped = static_cast<quint8 *>(m_tables->mapped());
    std::memcpy(mapped, layerTiles.constData(), size_t(layerTableBytes));
    std::memcpy(mapped + dstTableOffset, dstTiles.constData(), size_t(dstTableBytes));
    LayerParams *params = reinterpret_cast<LayerParams *>(mapped + paramsOffset);
    for (int i = 0; i < layers.size(); i++) {
        params[i].opacity = layers[i].opacity;
        params[i].op = quint32(layers[i].op);
        params[i].flags = (layers[i].alphaLocked ? 1u : 0u) | (layers[i].halfBrush ? 2u : 0u)
            | (layers[i].explicitChannelFlags ? 4u : 0u);
        params[i].channelMask = layers[i].channelMask;
    }

    PushConstants constants{};
    constants.layerTiles = m_tables->deviceAddress();
    constants.dstTiles = m_tables->deviceAddress() + dstTableOffset;
    constants.layers = m_tables->deviceAddress() + paramsOffset;
    constants.tileCount = quint32(tileCount);
    constants.layerCount = quint32(layers.size());
    constants.gridWidth = quint32(gridWidth);
    constants.clip[0] = clip.left();
    constants.clip[1] = clip.top();
    constants.clip[2] = clip.right() + 1;
    constants.clip[3] = clip.bottom() + 1;
    if (mask) {
        std::memset(mapped + maskOffset, 0, size_t(alignUp(VkDeviceSize(maskBytes), 4)));
        std::memcpy(mapped + maskOffset, mask->data, size_t(maskBytes));
        constants.mask = m_tables->deviceAddress() + maskOffset;
        constants.maskOrigin[0] = mask->bounds.x();
        constants.maskOrigin[1] = mask->bounds.y();
        constants.maskSize[0] = mask->bounds.width();
        constants.maskSize[1] = mask->bounds.height();
    }

    // A mixed stack must also keep old modes out of the extended shader.
    // Consecutive runs share the tables and submission; only their offsets and
    // pipeline change. Each run observes the preceding run's destination writes.
    for (int first = 0; first < layers.size();) {
        const bool useExtended = layers[first].op >= KisGpuBlendOp::SoftLightSvg;
        int end = first + 1;
        while (end < layers.size() && (layers[end].op >= KisGpuBlendOp::SoftLightSvg) == useExtended)
            ++end;
        constants.layerTiles = m_tables->deviceAddress() + VkDeviceSize(first) * tileCount * sizeof(VkDeviceAddress);
        constants.layers = m_tables->deviceAddress() + paramsOffset + VkDeviceSize(first) * sizeof(LayerParams);
        constants.layerCount = quint32(end - first);
        if (first)
            commands.computeBarrier();
        (useExtended ? m_extendedPipeline : m_pipeline)
            ->dispatch(commands.commandBuffer(), constants, quint32(tileCount));
        first = end;
    }
    return true;
}
