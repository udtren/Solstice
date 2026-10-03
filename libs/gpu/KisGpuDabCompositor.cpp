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
struct UploadLayout {
    VkDeviceSize tableOffset = 0, maskOffset = 0, maskBytes = 0, capacity = 0;
    QVector<VkDeviceSize> offsets;
    QHash<const float *, VkDeviceSize> sourceOffsets;
};
bool planUpload(int tileCount,
                const QVector<KisGpuDabCompositor::Dab> &dabs,
                const KisGpuDabCompositor::Mask *mask,
                UploadLayout &layout,
                QString *error)
{
    if (tileCount <= 0 || tileCount > 65535 || dabs.isEmpty() || dabs.size() > 65536)
        return false;
    layout.tableOffset = aligned(VkDeviceSize(tileCount) * sizeof(TileRecord));
    VkDeviceSize bytes = layout.tableOffset + VkDeviceSize(dabs.size()) * sizeof(DabRecord);
    // Consecutive identical dabs commonly share their fixed paint device.
    QHash<const float *, QSize> sourceSizes;
    for (const auto &dab : dabs) {
        if (!dab.pixels || dab.size.isEmpty() || dab.mirrorFlags > 3
            || quint64(dab.size.width()) * dab.size.height() > KisGpuDabCompositor::MaxUploadBytes / 16)
            return false;
        if (sourceSizes.contains(dab.pixels) && sourceSizes.value(dab.pixels) != dab.size)
            return false;
        sourceSizes.insert(dab.pixels, dab.size);
        if (!layout.sourceOffsets.contains(dab.pixels)) {
            layout.sourceOffsets.insert(dab.pixels, bytes);
            bytes += VkDeviceSize(dab.size.width()) * dab.size.height() * 16;
        }
        layout.offsets << layout.sourceOffsets.value(dab.pixels);
    }
    layout.maskOffset = bytes;

    if (mask) {
        if (!mask->pixels || mask->bounds.isEmpty())
            return false;
        layout.maskBytes = VkDeviceSize(mask->bounds.width()) * mask->bounds.height();
        // The shader reads packed uint words, including the last partial word.
        bytes += aligned(layout.maskBytes);
    }
    if (bytes > KisGpuDabCompositor::MaxUploadBytes) {
        if (error)
            *error = QStringLiteral("GPU brush staging budget reached");
        return false;
    }

    constexpr VkDeviceSize quantum = 256 * 1024;
    layout.capacity = (bytes + quantum - 1) & ~(quantum - 1);
    return true;
}
} // namespace
KisGpuDabCompositor::KisGpuDabCompositor(KisGpuContext &context)
    : m_context(context)
{
}
KisGpuDabCompositor::~KisGpuDabCompositor() = default;
quint64
KisGpuDabCompositor::requiredUploadBytes(int tileCount, const QVector<Dab> &dabs, const Mask *mask, QString *error)
{
    UploadLayout layout;
    return planUpload(tileCount, dabs, mask, layout, error) ? layout.capacity : 0;
}
quint64 KisGpuDabCompositor::uploadBytes() const
{
    return m_upload ? m_upload->size() : 0;
}
void KisGpuDabCompositor::releaseUpload()
{
    m_upload.reset();
}
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
    if (quint32(mode) >= quint32(CompositeMode::Count) || channelMask > 0xf
        || ((mode == CompositeMode::AlphaDarkenHard || mode == CompositeMode::AlphaDarkenCreamy) && channelMask != 0xf))
        return false;
    UploadLayout layout;
    if (!planUpload(tiles.size(), dabs, mask, layout, error))
        return false;
    const auto bytes = layout.capacity;
    const auto tableOffset = layout.tableOffset;
    const auto maskOffset = layout.maskOffset;
    const auto maskBytes = layout.maskBytes;
    const auto &offsets = layout.offsets;
    auto &sourceOffsets = layout.sourceOffsets;
    if (!m_upload || m_upload->size() < bytes) {
        // Previous work has completed; do not temporarily hold both allocations.
        m_upload.reset();
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
