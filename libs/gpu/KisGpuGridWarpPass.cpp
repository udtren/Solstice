/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuGridWarpPass.h"

#include "KisGpuBuffer.h"
#include "KisGpuCommandList.h"
#include "KisGpuComputePipeline.h"
#include "KisGpuContext.h"

#include <cstring>

namespace
{
const quint32 GridWarpClaim[] = {
#include "grid_warp_claim.spv.inc"
};
const quint32 GridWarpResolveRgba32f[] = {
#include "grid_warp_resolve_rgba32f.spv.inc"
};
const quint32 GridWarpResolveRgba16f[] = {
#include "grid_warp_resolve_rgba16f.spv.inc"
};

static_assert(sizeof(KisGpuGridWarpPass::Op) == 320, "must match the shader Op struct");

struct Params {
    qint32 ownerRect[4];
    qint32 srcGridOrigin[2];
    qint32 srcGridWidth;
    qint32 srcGridHeight;
    qint32 dstGridOrigin[2];
    qint32 dstGridWidth;
    qint32 dstGridHeight;
    qint32 opCount;
    qint32 padding[3];
    float defaultPixel[4];
};
static_assert(sizeof(Params) == 80, "must match the shader Params block");

struct PushConstants {
    VkDeviceAddress params;
    VkDeviceAddress srcTiles;
    VkDeviceAddress dstTiles;
    VkDeviceAddress ops;
    VkDeviceAddress owners;
    VkDeviceAddress flag;
};

constexpr quint32 MaxGroupsX = 65535;

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}
} // namespace

KisGpuGridWarpPass::KisGpuGridWarpPass(KisGpuContext &context)
    : m_context(context)
{
}

KisGpuGridWarpPass::~KisGpuGridWarpPass() = default;

std::unique_ptr<KisGpuGridWarpPass>
KisGpuGridWarpPass::create(KisGpuContext &context, KisGpuTileFormat format, QString *errorMessage)
{
    if (!context.deviceInfo().supportsFloat64) {
        if (errorMessage)
            *errorMessage = QStringLiteral("GPU grid warps require shaderFloat64");
        return nullptr;
    }
    std::unique_ptr<KisGpuGridWarpPass> pass(new KisGpuGridWarpPass(context));
    const bool f16 = format == KisGpuTileFormat::RGBA16F;
    pass->m_claim =
        context.sharedComputePipeline(GridWarpClaim, sizeof(GridWarpClaim), sizeof(PushConstants), errorMessage);
    pass->m_resolve =
        context.sharedComputePipeline(f16 ? GridWarpResolveRgba16f : GridWarpResolveRgba32f,
                                      f16 ? sizeof(GridWarpResolveRgba16f) : sizeof(GridWarpResolveRgba32f),
                                      sizeof(PushConstants),
                                      errorMessage);
    pass->m_flag = KisGpuBuffer::create(context, 16, KisGpuBuffer::Location::Readback, errorMessage);
    if (!pass->m_claim || !pass->m_resolve || !pass->m_flag)
        return nullptr;
    return pass;
}

bool KisGpuGridWarpPass::record(KisGpuCommandList &commands, const Pass &pass, QString *errorMessage)
{
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };
    if (pass.opCount <= 0 || !pass.ops || pass.ownerRect.isEmpty() || pass.dstGridWidth <= 0 || pass.dstGridHeight <= 0
        || pass.srcGridWidth < 0 || pass.srcGridHeight < 0
        || pass.dstTiles.size() != pass.dstGridWidth * pass.dstGridHeight
        || pass.srcTiles.size() != pass.srcGridWidth * pass.srcGridHeight)
        return fail(QStringLiteral("invalid grid warp pass"));
    if (qint64(pass.dstGridHeight) * 64 > 65535)
        return fail(QStringLiteral("grid warp destination too tall"));
    const quint32 groupsY = (quint32(pass.opCount) + MaxGroupsX - 1) / MaxGroupsX;
    if (groupsY > 65535)
        return fail(QStringLiteral("too many grid warp operations"));

    const VkDeviceSize paramsBytes = alignUp(sizeof(Params), 64);
    const VkDeviceSize srcOffset = paramsBytes;
    const VkDeviceSize srcBytes = alignUp(VkDeviceSize(qMax(1, pass.srcTiles.size())) * sizeof(VkDeviceAddress), 64);
    const VkDeviceSize dstOffset = srcOffset + srcBytes;
    const VkDeviceSize dstBytes = alignUp(VkDeviceSize(pass.dstTiles.size()) * sizeof(VkDeviceAddress), 64);
    const VkDeviceSize opsOffset = dstOffset + dstBytes;
    const VkDeviceSize opsBytes = VkDeviceSize(pass.opCount) * sizeof(Op);
    const VkDeviceSize totalBytes = opsOffset + opsBytes;
    if (!m_tables || m_tables->size() < totalBytes) {
        m_tables.reset();
        m_tables = KisGpuBuffer::create(m_context, totalBytes, KisGpuBuffer::Location::Upload, errorMessage);
        if (!m_tables)
            return false;
    }
    const VkDeviceSize ownerBytes =
        alignUp(VkDeviceSize(pass.ownerRect.width()) * VkDeviceSize(pass.ownerRect.height()) * sizeof(quint32), 16);
    if (!m_owners || m_owners->size() < ownerBytes) {
        m_owners.reset();
        m_owners = KisGpuBuffer::create(m_context, ownerBytes, KisGpuBuffer::Location::Device, errorMessage);
        if (!m_owners)
            return false;
    }

    quint8 *mapped = static_cast<quint8 *>(m_tables->mapped());
    Params params{};
    params.ownerRect[0] = pass.ownerRect.x();
    params.ownerRect[1] = pass.ownerRect.y();
    params.ownerRect[2] = pass.ownerRect.width();
    params.ownerRect[3] = pass.ownerRect.height();
    params.srcGridOrigin[0] = pass.srcGridOrigin.x();
    params.srcGridOrigin[1] = pass.srcGridOrigin.y();
    params.srcGridWidth = pass.srcGridWidth;
    params.srcGridHeight = pass.srcGridHeight;
    params.dstGridOrigin[0] = pass.dstGridOrigin.x();
    params.dstGridOrigin[1] = pass.dstGridOrigin.y();
    params.dstGridWidth = pass.dstGridWidth;
    params.dstGridHeight = pass.dstGridHeight;
    params.opCount = pass.opCount;
    std::memcpy(params.defaultPixel, pass.defaultPixel, sizeof(params.defaultPixel));
    std::memcpy(mapped, &params, sizeof(params));
    if (!pass.srcTiles.isEmpty())
        std::memcpy(mapped + srcOffset,
                    pass.srcTiles.constData(),
                    size_t(pass.srcTiles.size()) * sizeof(VkDeviceAddress));
    std::memcpy(mapped + dstOffset, pass.dstTiles.constData(), size_t(pass.dstTiles.size()) * sizeof(VkDeviceAddress));
    std::memcpy(mapped + opsOffset, pass.ops, size_t(opsBytes));

    const VkDeviceAddress base = m_tables->deviceAddress();
    const PushConstants constants{base,
                                  base + srcOffset,
                                  base + dstOffset,
                                  base + opsOffset,
                                  m_owners->deviceAddress(),
                                  m_flag->deviceAddress()};
    const VkCommandBuffer buffer = commands.commandBuffer();
    m_context.vk().vkCmdFillBuffer(buffer, m_owners->handle(), 0, ownerBytes, 0);
    m_context.vk().vkCmdFillBuffer(buffer, m_flag->handle(), 0, 16, 0);
    commands.computeBarrier();
    m_claim->dispatch(buffer, constants, qMin(quint32(pass.opCount), MaxGroupsX), groupsY);
    commands.computeBarrier();
    m_resolve->dispatch(buffer, constants, quint32(pass.dstGridWidth), quint32(pass.dstGridHeight * 64));
    // Make the flag visible to the host after the submission's wait.
    commands.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_HOST_BIT,
                     VK_ACCESS_2_HOST_READ_BIT);
    return true;
}

bool KisGpuGridWarpPass::failed() const
{
    quint32 value = 0;
    std::memcpy(&value, m_flag->mapped(), sizeof(value));
    return value != 0;
}
