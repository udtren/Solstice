/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KisGpuDabCompositor.h"
#include "KisGpuBuffer.h"
#include "KisGpuCommandList.h"
#include "KisGpuComputePipeline.h"
#include "KisGpuContext.h"
#include <QHash>
#include <cstring>
namespace
{
const quint32 Shader[] = {
#include "paint_dabs.spv.inc"
};
struct DabRecord {
    VkDeviceAddress pixels;
    qint32 x, y, width, height;
    float opacity;
    float flow, averageOpacity;
    quint32 mirrorFlags, padding[2];
    qint32 clipX, clipY, clipWidth, clipHeight;
};
struct TileRecord {
    VkDeviceAddress pixels;
    qint32 x, y;
};
struct PushConstants {
    VkDeviceAddress tiles, dabs;
    quint32 gridWidth, dabCount;
    qint32 x, y;
    quint32 mode, channelMask;
    VkDeviceAddress mask;
    qint32 maskX, maskY, maskWidth, maskHeight;
};
static_assert(sizeof(DabRecord) == 64 && sizeof(PushConstants) == 64, "shader layout");
static_assert(sizeof(TileRecord) == 16, "shader tile layout");
VkDeviceSize aligned(VkDeviceSize bytes)
{
    return (bytes + 15) & ~VkDeviceSize(15);
}
} // namespace
KisGpuDabCompositor::KisGpuDabCompositor(KisGpuContext &context)
    : m_context(context)
{
}
KisGpuDabCompositor::~KisGpuDabCompositor() = default;
std::unique_ptr<KisGpuDabCompositor> KisGpuDabCompositor::create(KisGpuContext &context, QString *error)
{
    std::unique_ptr<KisGpuDabCompositor> result(new KisGpuDabCompositor(context));
    result->m_pipeline = KisGpuComputePipeline::create(context, Shader, sizeof(Shader), sizeof(PushConstants), error);
    return result->m_pipeline ? std::move(result) : nullptr;
}
bool KisGpuDabCompositor::record(KisGpuCommandList &commands,
                                 const QVector<VkDeviceAddress> &tiles,
                                 int gridWidth,
                                 QPoint origin,
                                 const QVector<Dab> &dabs,
                                 CompositeMode mode,
                                 const Mask *mask,
                                 quint32 channelMask,
                                 QString *error,
                                 const QVector<QPoint> &tileOrigins)
{
    if (tiles.isEmpty() || dabs.isEmpty())
        return true;
    if (gridWidth <= 0 || tiles.size() > 65535)
        return false;
    if (!tileOrigins.isEmpty() && tileOrigins.size() != tiles.size())
        return false;
    if (channelMask > 0xf || (mode != CompositeMode::Normal && mode != CompositeMode::Erase && channelMask != 0xf))
        return false;
    const VkDeviceSize tableOffset = aligned(VkDeviceSize(tiles.size()) * sizeof(TileRecord));
    VkDeviceSize bytes = tableOffset + VkDeviceSize(dabs.size()) * sizeof(DabRecord);
    QVector<VkDeviceSize> offsets;
    // Consecutive identical dabs commonly share their fixed paint device.
    QHash<const float *, VkDeviceSize> sourceOffsets;
    QHash<const float *, QSize> sourceSizes;
    for (const auto &dab : dabs) {
        if (!dab.pixels || dab.size.isEmpty() || dab.mirrorFlags > 3)
            return false;
        if (sourceSizes.contains(dab.pixels) && sourceSizes.value(dab.pixels) != dab.size)
            return false;
        sourceSizes.insert(dab.pixels, dab.size);
        if (!sourceOffsets.contains(dab.pixels)) {
            sourceOffsets.insert(dab.pixels, bytes);
            bytes += VkDeviceSize(dab.size.width()) * dab.size.height() * 16;
        }
        offsets << sourceOffsets.value(dab.pixels);
    }
    const VkDeviceSize maskOffset = bytes;
    VkDeviceSize maskBytes = 0;
    if (mask) {
        if (!mask->pixels || mask->bounds.isEmpty())
            return false;
        maskBytes = VkDeviceSize(mask->bounds.width()) * mask->bounds.height();
        // The shader reads packed uint words, including the last partial word.
        bytes += aligned(maskBytes);
    }
    if (bytes > (VkDeviceSize(64) << 20)) {
        if (error)
            *error = QStringLiteral("GPU brush staging budget reached");
        return false;
    }
    if (!m_upload || m_upload->size() < bytes) {
        m_upload = KisGpuBuffer::create(m_context, bytes, KisGpuBuffer::Location::Upload, error);
        if (!m_upload)
            return false;
    }
    auto *out = static_cast<quint8 *>(m_upload->mapped());
    if (mask) {
        std::memset(out + maskOffset, 0, size_t(aligned(maskBytes)));
        std::memcpy(out + maskOffset, mask->pixels, size_t(maskBytes));
    }
    auto *tileRecords = reinterpret_cast<TileRecord *>(out);
    for (int i = 0; i < tiles.size(); ++i) {
        const QPoint tileOrigin =
            tileOrigins.isEmpty() ? origin + QPoint(i % gridWidth, i / gridWidth) * 64 : tileOrigins[i];
        tileRecords[i] = {tiles[i], tileOrigin.x(), tileOrigin.y()};
    }
    auto *records = reinterpret_cast<DabRecord *>(out + tableOffset);
    for (int i = 0; i < dabs.size(); ++i) {
        const auto &dab = dabs[i];
        const QRect clip = dab.clip.isNull() ? QRect(dab.origin, dab.size) : dab.clip;
        records[i] = {m_upload->deviceAddress() + offsets[i],
                      dab.origin.x(),
                      dab.origin.y(),
                      dab.size.width(),
                      dab.size.height(),
                      dab.opacity,
                      dab.flow,
                      dab.averageOpacity,
                      dab.mirrorFlags,
                      {0, 0},
                      clip.x(),
                      clip.y(),
                      clip.width(),
                      clip.height()};
        if (sourceOffsets.remove(dab.pixels)) {
            std::memcpy(out + offsets[i], dab.pixels, size_t(dab.size.width()) * dab.size.height() * 16);
        }
    }
    PushConstants params{m_upload->deviceAddress(),
                         m_upload->deviceAddress() + tableOffset,
                         quint32(gridWidth),
                         quint32(dabs.size()),
                         origin.x(),
                         origin.y(),
                         quint32(mode),
                         channelMask,
                         mask ? m_upload->deviceAddress() + maskOffset : 0,
                         mask ? mask->bounds.x() : 0,
                         mask ? mask->bounds.y() : 0,
                         mask ? mask->bounds.width() : 0,
                         mask ? mask->bounds.height() : 0};
    commands.computeBarrier();
    m_pipeline->dispatch(commands.commandBuffer(), params, quint32(tiles.size()));
    return true;
}
