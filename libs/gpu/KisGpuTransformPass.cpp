/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuTransformPass.h"

#include "KisGpuBuffer.h"
#include "KisGpuCommandList.h"
#include "KisGpuComputePipeline.h"
#include "KisGpuContext.h"

#include <cstring>

namespace
{
const quint32 TransformPassRgba32f[] = {
#include "transform_pass_rgba32f.spv.inc"
};
const quint32 TransformPassRgba16f[] = {
#include "transform_pass_rgba16f.spv.inc"
};

struct Params {
    double scale;
    double shear;
    double dx;
    qint32 weightsPositionScale;
    qint32 srcStart;
    qint32 srcEnd;
    qint32 clampToEdge;
    qint32 vertical;
    qint32 lineFirst;
    qint32 lineCount;
    qint32 maxSpan;
    qint32 srcGridOrigin[2];
    qint32 srcGridWidth;
    qint32 srcGridHeight;
    qint32 dstGridOrigin[2];
    qint32 dstGridWidth;
    qint32 dstGridHeight;
    qint32 padding[2];
    float defaultPixel[4];
};
static_assert(sizeof(Params) == 112, "must match the shader Params block");

struct PushConstants {
    VkDeviceAddress params;
    VkDeviceAddress srcTiles;
    VkDeviceAddress dstTiles;
    VkDeviceAddress lines;
    VkDeviceAddress weights;
};

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}
} // namespace

KisGpuTransformPass::KisGpuTransformPass(KisGpuContext &context)
    : m_context(context)
{
}

KisGpuTransformPass::~KisGpuTransformPass() = default;

std::unique_ptr<KisGpuTransformPass>
KisGpuTransformPass::create(KisGpuContext &context, KisGpuTileFormat format, QString *errorMessage)
{
    if (!context.deviceInfo().supportsFloat64) {
        if (errorMessage)
            *errorMessage = QStringLiteral("GPU transforms require shaderFloat64");
        return nullptr;
    }
    std::unique_ptr<KisGpuTransformPass> pass(new KisGpuTransformPass(context));
    const bool f16 = format == KisGpuTileFormat::RGBA16F;
    pass->m_pipeline = context.sharedComputePipeline(f16 ? TransformPassRgba16f : TransformPassRgba32f,
                                                     f16 ? sizeof(TransformPassRgba16f) : sizeof(TransformPassRgba32f),
                                                     sizeof(PushConstants),
                                                     errorMessage);
    return pass->m_pipeline ? std::move(pass) : nullptr;
}

bool KisGpuTransformPass::record(KisGpuCommandList &commands, const Pass &pass, QString *errorMessage)
{
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };
    const int lineCount = pass.lineRanges.size() / 2;
    if (pass.dstGridWidth <= 0 || pass.dstGridHeight <= 0 || pass.srcGridWidth < 0 || pass.srcGridHeight < 0
        || pass.dstTiles.size() != pass.dstGridWidth * pass.dstGridHeight
        || pass.srcTiles.size() != pass.srcGridWidth * pass.srcGridHeight || pass.lineRanges.size() != lineCount * 2
        || pass.maxSpan <= 0 || pass.weights.size() != 256 * (pass.maxSpan + 2) || pass.weightsPositionScale <= 0
        || pass.weightsPositionScale > 256 || pass.scale == 0.0)
        return fail(QStringLiteral("invalid transform pass"));
    if (qint64(pass.dstGridHeight) * 64 > 65535)
        return fail(QStringLiteral("transform pass destination too tall"));

    const VkDeviceSize paramsBytes = alignUp(sizeof(Params), 16);
    const VkDeviceSize srcOffset = paramsBytes;
    const VkDeviceSize srcBytes = alignUp(VkDeviceSize(qMax(1, pass.srcTiles.size())) * sizeof(VkDeviceAddress), 16);
    const VkDeviceSize dstOffset = srcOffset + srcBytes;
    const VkDeviceSize dstBytes = alignUp(VkDeviceSize(pass.dstTiles.size()) * sizeof(VkDeviceAddress), 16);
    const VkDeviceSize linesOffset = dstOffset + dstBytes;
    const VkDeviceSize linesBytes = alignUp(VkDeviceSize(qMax(2, pass.lineRanges.size())) * sizeof(qint32), 16);
    const VkDeviceSize weightsOffset = linesOffset + linesBytes;
    const VkDeviceSize weightsBytes = alignUp(VkDeviceSize(pass.weights.size()) * sizeof(qint32), 16);
    const VkDeviceSize totalBytes = weightsOffset + weightsBytes;
    if (!m_tables || m_tables->size() < totalBytes) {
        m_tables.reset();
        m_tables = KisGpuBuffer::create(m_context, totalBytes, KisGpuBuffer::Location::Upload, errorMessage);
        if (!m_tables)
            return false;
    }

    quint8 *mapped = static_cast<quint8 *>(m_tables->mapped());
    Params params{};
    params.scale = pass.scale;
    params.shear = pass.shear;
    params.dx = pass.dx;
    params.weightsPositionScale = pass.weightsPositionScale;
    params.srcStart = pass.srcStart;
    params.srcEnd = pass.srcEnd;
    params.clampToEdge = pass.clampToEdge ? 1 : 0;
    params.vertical = pass.vertical ? 1 : 0;
    params.lineFirst = pass.lineFirst;
    params.lineCount = lineCount;
    params.maxSpan = pass.maxSpan;
    params.srcGridOrigin[0] = pass.srcGridOrigin.x();
    params.srcGridOrigin[1] = pass.srcGridOrigin.y();
    params.srcGridWidth = pass.srcGridWidth;
    params.srcGridHeight = pass.srcGridHeight;
    params.dstGridOrigin[0] = pass.dstGridOrigin.x();
    params.dstGridOrigin[1] = pass.dstGridOrigin.y();
    params.dstGridWidth = pass.dstGridWidth;
    params.dstGridHeight = pass.dstGridHeight;
    std::memcpy(params.defaultPixel, pass.defaultPixel, sizeof(params.defaultPixel));
    std::memcpy(mapped, &params, sizeof(params));
    if (!pass.srcTiles.isEmpty())
        std::memcpy(mapped + srcOffset,
                    pass.srcTiles.constData(),
                    size_t(pass.srcTiles.size()) * sizeof(VkDeviceAddress));
    std::memcpy(mapped + dstOffset, pass.dstTiles.constData(), size_t(pass.dstTiles.size()) * sizeof(VkDeviceAddress));
    if (!pass.lineRanges.isEmpty())
        std::memcpy(mapped + linesOffset, pass.lineRanges.constData(), size_t(pass.lineRanges.size()) * sizeof(qint32));
    std::memcpy(mapped + weightsOffset, pass.weights.constData(), size_t(pass.weights.size()) * sizeof(qint32));

    const VkDeviceAddress base = m_tables->deviceAddress();
    PushConstants constants{base, base + srcOffset, base + dstOffset, base + linesOffset, base + weightsOffset};
    commands.computeBarrier();
    m_pipeline->dispatch(commands.commandBuffer(),
                         constants,
                         quint32(pass.dstGridWidth),
                         quint32(pass.dstGridHeight * 64));
    return true;
}
