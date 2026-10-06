/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuTileFill.h"

#include "KisGpuBuffer.h"
#include "KisGpuCommandList.h"
#include "KisGpuComputePipeline.h"
#include "KisGpuContext.h"

#include <cstring>

namespace
{
const quint32 FillTilesRgba32f[] = {
#include "fill_tiles_rgba32f.spv.inc"
};

const quint32 FillTilesRgba16f[] = {
#include "fill_tiles_rgba16f.spv.inc"
};

struct PushConstants {
    VkDeviceAddress tiles;
    quint32 tileCount;
    quint32 padding;
    float color[4];
};
static_assert(sizeof(PushConstants) == 32, "must match the shader push constant block");
} // namespace

KisGpuTileFill::KisGpuTileFill(KisGpuContext &context)
    : m_context(context)
{
}

KisGpuTileFill::~KisGpuTileFill() = default;

std::unique_ptr<KisGpuTileFill>
KisGpuTileFill::create(KisGpuContext &context, KisGpuTileFormat format, QString *errorMessage)
{
    std::unique_ptr<KisGpuTileFill> fill(new KisGpuTileFill(context));
    const bool f16 = format == KisGpuTileFormat::RGBA16F;
    fill->m_pipeline = context.sharedComputePipeline(f16 ? FillTilesRgba16f : FillTilesRgba32f,
                                                     f16 ? sizeof(FillTilesRgba16f) : sizeof(FillTilesRgba32f),
                                                     sizeof(PushConstants),
                                                     errorMessage);
    return fill->m_pipeline ? std::move(fill) : nullptr;
}

bool KisGpuTileFill::record(KisGpuCommandList &commands,
                            const QVector<VkDeviceAddress> &tiles,
                            const float color[4],
                            QString *errorMessage)
{
    if (tiles.isEmpty()) {
        return true;
    }
    const VkDeviceSize bytes = VkDeviceSize(tiles.size()) * sizeof(VkDeviceAddress);
    if (!m_table || m_table->size() < bytes) {
        m_table = KisGpuBuffer::create(m_context, bytes, KisGpuBuffer::Location::Upload, errorMessage);
        if (!m_table) {
            return false;
        }
    }
    std::memcpy(m_table->mapped(), tiles.constData(), size_t(bytes));

    PushConstants constants{};
    constants.tiles = m_table->deviceAddress();
    constants.tileCount = quint32(tiles.size());
    std::memcpy(constants.color, color, sizeof(constants.color));
    m_pipeline->dispatch(commands.commandBuffer(), constants, quint32(tiles.size()));
    return true;
}
