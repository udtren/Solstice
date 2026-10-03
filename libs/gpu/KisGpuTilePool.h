/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUTILEPOOL_H
#define KISGPUTILEPOOL_H

#include <QMutex>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

#include "KisGpuVulkanFunctions.h"
#include "kritagpu_export.h"

class KisGpuContext;
class KisGpuBuffer;
class KisGpuCommandList;

/// Pixel formats of GPU-resident tiles. Only RGBA float is supported by design.
enum class KisGpuTileFormat {
    RGBA32F,
    RGBA16F,
};

KRITAGPU_EXPORT quint32 kisGpuTileFormatPixelSize(KisGpuTileFormat format);

/**
 * Fixed-size slots for 64x64 tiles in device-local memory.
 *
 * The tile size matches KisTileData::WIDTH/HEIGHT so that a CPU tile maps
 * 1:1 onto a GPU slot. Slots live in large chunk buffers to keep the number
 * of Vulkan allocations small; shaders address a slot by its device address.
 * Pixels inside a slot are tightly packed rows (64 * pixelSize bytes).
 *
 * The pool is thread-safe. It does not track GPU usage: a caller must not
 * release a slot that queued GPU work still reads or writes.
 */
class KRITAGPU_EXPORT KisGpuTilePool
{
public:
    static constexpr int TileSize = 64;
    static constexpr quint32 InvalidSlot = 0xFFFFFFFFu;

    KisGpuTilePool(KisGpuContext &context, KisGpuTileFormat format, quint32 tilesPerChunk = 4096);
    ~KisGpuTilePool();

    KisGpuTilePool(const KisGpuTilePool &) = delete;
    KisGpuTilePool &operator=(const KisGpuTilePool &) = delete;

    KisGpuTileFormat format() const;
    quint32 pixelSize() const;
    VkDeviceSize tileBytes() const;
    quint32 tilesPerChunk() const;

    /// Returns InvalidSlot when device memory is exhausted.
    quint32 allocate(QString *errorMessage = nullptr, VkDeviceSize maxReservedBytes = ~VkDeviceSize(0));
    void release(quint32 slot);
    /// Release empty chunks. All released slots must have finished their GPU work.
    void trim();

    VkDeviceAddress deviceAddress(quint32 slot) const;
    /// Chunk buffer holding @p slot and the byte offset of the slot inside it.
    const KisGpuBuffer *buffer(quint32 slot, VkDeviceSize *offset) const;

    /**
     * Records copies of consecutive tiles from @p src into @p slots.
     * Tile i is read from srcOffset + i * tileBytes().
     */
    void recordUpload(KisGpuCommandList &commands,
                      const KisGpuBuffer &src,
                      VkDeviceSize srcOffset,
                      const QVector<quint32> &slots) const;
    /**
     * Records uploads into @p commandBuffer: tile i is read from
     * srcOffsets[i] in @p src and written to slots[i].
     */
    void recordUploadAt(VkCommandBuffer commandBuffer,
                        const KisGpuBuffer &src,
                        const QVector<VkDeviceSize> &srcOffsets,
                        const QVector<quint32> &slots) const;
    /// Records copies of @p slots into consecutive tiles of @p dst.
    void recordReadback(KisGpuCommandList &commands,
                        const QVector<quint32> &slots,
                        const KisGpuBuffer &dst,
                        VkDeviceSize dstOffset) const;

    /// Records slot-to-slot copies (srcSlots[i] -> dstSlots[i]) inside the pool.
    void recordSlotCopies(KisGpuCommandList &commands,
                          const QVector<quint32> &srcSlots,
                          const QVector<quint32> &dstSlots) const;

    /// Records zero-filling of whole slots (transparent RGBA float pixels).
    void recordClearSlots(KisGpuCommandList &commands, const QVector<quint32> &slots) const;

    quint32 allocatedSlots() const;
    quint32 capacitySlots() const;
    VkDeviceSize reservedBytes() const;

private:
    void recordCopies(VkCommandBuffer commandBuffer,
                      const KisGpuBuffer &host,
                      const QVector<VkDeviceSize> &hostOffsets,
                      const QVector<quint32> &slots,
                      bool upload) const;
    QVector<VkDeviceSize> consecutiveOffsets(VkDeviceSize start, int count) const;

    KisGpuContext &m_context;
    const KisGpuTileFormat m_format;
    const quint32 m_tilesPerChunk;
    const VkDeviceSize m_tileBytes;

    mutable QMutex m_mutex;
    std::vector<std::unique_ptr<KisGpuBuffer>> m_chunks;
    std::vector<quint32> m_chunkSlots;
    std::vector<quint32> m_chunkUsed;
    QVector<quint32> m_freeSlots;
    quint32 m_allocated = 0;
    VkDeviceSize m_reserved = 0;
};

#endif // KISGPUTILEPOOL_H
