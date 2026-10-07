/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuSeparableConvolutionPass.h"

#include "KisGpuBuffer.h"
#include "KisGpuCommandList.h"
#include "KisGpuComputePipeline.h"
#include "KisGpuContext.h"

#include <cstring>

namespace
{
const quint32 HorizontalRgba32f[] = {
#include "separable_convolution_h_rgba32f.spv.inc"
};
const quint32 HorizontalRgba16f[] = {
#include "separable_convolution_h_rgba16f.spv.inc"
};
const quint32 VerticalRgba32f[] = {
#include "separable_convolution_v_rgba32f.spv.inc"
};
const quint32 VerticalRgba16f[] = {
#include "separable_convolution_v_rgba16f.spv.inc"
};

struct Params {
    double invFactor;
    qint32 halfWidth;
    qint32 halfHeight;
    qint32 applyRect[4];
    qint32 dataRect[4];
    qint32 repeatBorder;
    qint32 premultiply;
    qint32 bandTop;
    qint32 bandRows;
    qint32 convolved[4];
    qint32 srcGridOrigin[2];
    qint32 srcGridWidth;
    qint32 srcGridHeight;
    qint32 dstGridOrigin[2];
    qint32 dstGridWidth;
    qint32 dstGridHeight;
    qint32 dstFirstTileRow;
    qint32 dstTileRows;
    qint32 padding[2];
    float defaultPixel[4];
};
static_assert(sizeof(Params) == 144, "must match the shader Params block");

struct PushConstants {
    VkDeviceAddress params;
    VkDeviceAddress srcTiles;
    VkDeviceAddress dstTiles;
    VkDeviceAddress horizontal;
    VkDeviceAddress vertical;
    VkDeviceAddress intermediate;
};

// The intermediate buffer of one band (rows of dvec4) stays below this size
// unless a single tile row with its margins needs more.
const VkDeviceSize IntermediateBudget = VkDeviceSize(64) << 20;
// Beyond this, the CPU convolution is used.
const VkDeviceSize IntermediateLimit = VkDeviceSize(1) << 30;
const int MaxDispatchRows = 65535;

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

void setRect(qint32 *target, const QRect &rect)
{
    target[0] = rect.x();
    target[1] = rect.y();
    target[2] = rect.width();
    target[3] = rect.height();
}
} // namespace

KisGpuSeparableConvolutionPass::KisGpuSeparableConvolutionPass(KisGpuContext &context)
    : m_context(context)
{
}

KisGpuSeparableConvolutionPass::~KisGpuSeparableConvolutionPass() = default;

std::unique_ptr<KisGpuSeparableConvolutionPass>
KisGpuSeparableConvolutionPass::create(KisGpuContext &context, KisGpuTileFormat format, QString *errorMessage)
{
    if (!context.deviceInfo().supportsFloat64) {
        if (errorMessage)
            *errorMessage = QStringLiteral("GPU convolutions require shaderFloat64");
        return nullptr;
    }
    std::unique_ptr<KisGpuSeparableConvolutionPass> pass(new KisGpuSeparableConvolutionPass(context));
    const bool f16 = format == KisGpuTileFormat::RGBA16F;
    pass->m_horizontal = context.sharedComputePipeline(f16 ? HorizontalRgba16f : HorizontalRgba32f,
                                                       f16 ? sizeof(HorizontalRgba16f) : sizeof(HorizontalRgba32f),
                                                       sizeof(PushConstants),
                                                       errorMessage);
    if (!pass->m_horizontal)
        return nullptr;
    pass->m_vertical = context.sharedComputePipeline(f16 ? VerticalRgba16f : VerticalRgba32f,
                                                     f16 ? sizeof(VerticalRgba16f) : sizeof(VerticalRgba32f),
                                                     sizeof(PushConstants),
                                                     errorMessage);
    return pass->m_vertical ? std::move(pass) : nullptr;
}

int KisGpuSeparableConvolutionPass::lastBandCount() const
{
    return m_lastBandCount;
}

bool KisGpuSeparableConvolutionPass::record(KisGpuCommandList &commands, const Pass &pass, QString *errorMessage)
{
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };
    const int halfWidth = (pass.horizontal.size() - 1) / 2;
    const int halfHeight = (pass.vertical.size() - 1) / 2;
    if (pass.horizontal.size() % 2 != 1 || pass.vertical.size() % 2 != 1 || pass.applyRect.isEmpty()
        || pass.dstGridWidth <= 0 || pass.dstGridHeight <= 0 || pass.srcGridWidth < 0 || pass.srcGridHeight < 0
        || pass.dstTiles.size() != pass.dstGridWidth * pass.dstGridHeight
        || pass.srcTiles.size() != pass.srcGridWidth * pass.srcGridHeight
        || (pass.repeatBorder && !pass.dataRect.contains(pass.applyRect)))
        return fail(QStringLiteral("invalid convolution pass"));
    const QRect dstGrid(pass.dstGridOrigin, QSize(pass.dstGridWidth * 64, pass.dstGridHeight * 64));
    if (!dstGrid.contains(pass.applyRect))
        return fail(QStringLiteral("convolution destination does not cover the apply rect"));

    // Bands of whole destination tile rows; each needs 2 * halfHeight extra
    // intermediate rows.
    const int width = pass.applyRect.width();
    const VkDeviceSize rowBytes = VkDeviceSize(width) * 4 * sizeof(double);
    const qint64 budgetRows = qint64(IntermediateBudget / rowBytes);
    int bandTileRows = int(qBound(qint64(1), (budgetRows - 2 * halfHeight) / 64, qint64(pass.dstGridHeight)));
    while (bandTileRows > 1 && bandTileRows * 64 + 2 * halfHeight > MaxDispatchRows)
        --bandTileRows;
    if (bandTileRows * 64 + 2 * halfHeight > MaxDispatchRows || width > MaxDispatchRows * 64)
        return fail(QStringLiteral("convolution kernel too large"));
    const VkDeviceSize intermediateBytes = rowBytes * VkDeviceSize(bandTileRows * 64 + 2 * halfHeight);
    if (intermediateBytes > IntermediateLimit)
        return fail(QStringLiteral("convolution intermediate buffer too large"));
    const int bandCount = (pass.dstGridHeight + bandTileRows - 1) / bandTileRows;

    const VkDeviceSize paramsStride = alignUp(sizeof(Params), 64);
    const VkDeviceSize srcOffset = paramsStride * VkDeviceSize(bandCount);
    const VkDeviceSize srcBytes = alignUp(VkDeviceSize(qMax(1, pass.srcTiles.size())) * sizeof(VkDeviceAddress), 64);
    const VkDeviceSize dstOffset = srcOffset + srcBytes;
    const VkDeviceSize dstBytes = alignUp(VkDeviceSize(pass.dstTiles.size()) * sizeof(VkDeviceAddress), 64);
    const VkDeviceSize horizontalOffset = dstOffset + dstBytes;
    const VkDeviceSize horizontalBytes = alignUp(VkDeviceSize(pass.horizontal.size()) * sizeof(double), 64);
    const VkDeviceSize verticalOffset = horizontalOffset + horizontalBytes;
    const VkDeviceSize verticalBytes = alignUp(VkDeviceSize(pass.vertical.size()) * sizeof(double), 64);
    const VkDeviceSize totalBytes = verticalOffset + verticalBytes;
    if (!m_tables || m_tables->size() < totalBytes) {
        m_tables.reset();
        m_tables = KisGpuBuffer::create(m_context, totalBytes, KisGpuBuffer::Location::Upload, errorMessage);
        if (!m_tables)
            return false;
    }
    if (!m_intermediate || m_intermediate->size() < intermediateBytes) {
        m_intermediate.reset();
        m_intermediate =
            KisGpuBuffer::create(m_context, intermediateBytes, KisGpuBuffer::Location::Device, errorMessage);
        if (!m_intermediate)
            return false;
    }

    quint8 *mapped = static_cast<quint8 *>(m_tables->mapped());
    Params params{};
    params.invFactor = pass.invFactor;
    params.halfWidth = halfWidth;
    params.halfHeight = halfHeight;
    setRect(params.applyRect, pass.applyRect);
    setRect(params.dataRect, pass.repeatBorder ? pass.dataRect : pass.applyRect);
    params.repeatBorder = pass.repeatBorder ? 1 : 0;
    params.premultiply = pass.convolved[3] ? 1 : 0;
    for (int c = 0; c < 4; ++c)
        params.convolved[c] = pass.convolved[c] ? 1 : 0;
    params.srcGridOrigin[0] = pass.srcGridOrigin.x();
    params.srcGridOrigin[1] = pass.srcGridOrigin.y();
    params.srcGridWidth = pass.srcGridWidth;
    params.srcGridHeight = pass.srcGridHeight;
    params.dstGridOrigin[0] = pass.dstGridOrigin.x();
    params.dstGridOrigin[1] = pass.dstGridOrigin.y();
    params.dstGridWidth = pass.dstGridWidth;
    params.dstGridHeight = pass.dstGridHeight;
    std::memcpy(params.defaultPixel, pass.defaultPixel, sizeof(params.defaultPixel));

    struct Band {
        int rows = 0;
        int tileRows = 0;
    };
    QVector<Band> bands(bandCount);
    for (int band = 0; band < bandCount; ++band) {
        params.dstFirstTileRow = band * bandTileRows;
        params.dstTileRows = qMin(bandTileRows, pass.dstGridHeight - params.dstFirstTileRow);
        const int gridTop = pass.dstGridOrigin.y() + params.dstFirstTileRow * 64;
        const int gridBottom = gridTop + params.dstTileRows * 64; // exclusive
        params.bandTop = qMax(pass.applyRect.top(), gridTop);
        params.bandRows = qMax(0, qMin(pass.applyRect.bottom() + 1, gridBottom) - params.bandTop);
        bands[band].rows = params.bandRows;
        bands[band].tileRows = params.dstTileRows;
        std::memcpy(mapped + paramsStride * VkDeviceSize(band), &params, sizeof(params));
    }
    if (!pass.srcTiles.isEmpty())
        std::memcpy(mapped + srcOffset,
                    pass.srcTiles.constData(),
                    size_t(pass.srcTiles.size()) * sizeof(VkDeviceAddress));
    std::memcpy(mapped + dstOffset, pass.dstTiles.constData(), size_t(pass.dstTiles.size()) * sizeof(VkDeviceAddress));
    std::memcpy(mapped + horizontalOffset,
                pass.horizontal.constData(),
                size_t(pass.horizontal.size()) * sizeof(double));
    std::memcpy(mapped + verticalOffset, pass.vertical.constData(), size_t(pass.vertical.size()) * sizeof(double));

    const VkDeviceAddress base = m_tables->deviceAddress();
    commands.computeBarrier();
    for (int band = 0; band < bandCount; ++band) {
        const PushConstants constants{base + paramsStride * VkDeviceSize(band),
                                      base + srcOffset,
                                      base + dstOffset,
                                      base + horizontalOffset,
                                      base + verticalOffset,
                                      m_intermediate->deviceAddress()};
        if (bands[band].rows > 0) {
            m_horizontal->dispatch(commands.commandBuffer(),
                                   constants,
                                   quint32((width + 63) / 64),
                                   quint32(bands[band].rows + 2 * halfHeight));
            commands.computeBarrier();
        }
        m_vertical->dispatch(commands.commandBuffer(),
                             constants,
                             quint32(pass.dstGridWidth),
                             quint32(bands[band].tileRows * 64));
        // The next band's horizontal pass overwrites the intermediate rows.
        commands.computeBarrier();
    }
    m_lastBandCount = bandCount;
    return true;
}
