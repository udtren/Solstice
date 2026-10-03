/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuTilePool.h"

#include "KisGpuBuffer.h"
#include "KisGpuCommandList.h"
#include "KisGpuContext.h"

#include <QMutexLocker>

#include <algorithm>
#include <map>

#include <kis_debug.h>

quint32 kisGpuTileFormatPixelSize(KisGpuTileFormat format)
{
    switch (format) {
    case KisGpuTileFormat::RGBA32F:
        return 16;
    case KisGpuTileFormat::RGBA16F:
        return 8;
    }
    return 0;
}

KisGpuTilePool::KisGpuTilePool(KisGpuContext &context, KisGpuTileFormat format, quint32 tilesPerChunk)
    : m_context(context)
    , m_format(format)
    , m_tilesPerChunk(tilesPerChunk)
    , m_tileBytes(VkDeviceSize(TileSize) * TileSize * kisGpuTileFormatPixelSize(format))
{
    KIS_SAFE_ASSERT_RECOVER_NOOP(tilesPerChunk > 0);
}

KisGpuTilePool::~KisGpuTilePool()
{
    if (m_allocated) {
        warnKrita << "GPU engine: tile pool destroyed with" << m_allocated << "live slots";
    }
}

KisGpuTileFormat KisGpuTilePool::format() const
{
    return m_format;
}

quint32 KisGpuTilePool::pixelSize() const
{
    return kisGpuTileFormatPixelSize(m_format);
}

VkDeviceSize KisGpuTilePool::tileBytes() const
{
    return m_tileBytes;
}

quint32 KisGpuTilePool::tilesPerChunk() const
{
    return m_tilesPerChunk;
}

quint32 KisGpuTilePool::allocate(QString *errorMessage, VkDeviceSize maxReservedBytes)
{
    QMutexLocker locker(&m_mutex);

    if (m_freeSlots.isEmpty()) {
        const VkDeviceSize available = maxReservedBytes > m_reserved ? maxReservedBytes - m_reserved : 0;
        const quint32 count = quint32(qMin(VkDeviceSize(m_tilesPerChunk), available / m_tileBytes));
        if (!count) {
            if (errorMessage)
                *errorMessage = QStringLiteral("GPU tile memory budget reached");
            return InvalidSlot;
        }
        std::unique_ptr<KisGpuBuffer> chunk =
            KisGpuBuffer::create(m_context, m_tileBytes * count, KisGpuBuffer::Location::Device, errorMessage);
        if (!chunk) {
            return InvalidSlot;
        }
        size_t chunkIndex = 0;
        while (chunkIndex < m_chunks.size() && m_chunks[chunkIndex])
            ++chunkIndex;
        if (chunkIndex == m_chunks.size()) {
            m_chunks.emplace_back();
            m_chunkSlots.push_back(0);
            m_chunkUsed.push_back(0);
        }
        const quint32 firstSlot = quint32(chunkIndex) * m_tilesPerChunk;
        m_chunks[chunkIndex] = std::move(chunk);
        m_chunkSlots[chunkIndex] = count;
        m_reserved += m_tileBytes * count;

        // Hand out low slots first so that allocation order is deterministic.
        m_freeSlots.reserve(m_freeSlots.size() + int(count));
        for (quint32 i = count; i > 0; i--) {
            m_freeSlots.append(firstSlot + i - 1);
        }
    }

    const quint32 slot = m_freeSlots.takeLast();
    m_allocated++;
    m_chunkUsed[slot / m_tilesPerChunk]++;
    return slot;
}

void KisGpuTilePool::release(quint32 slot)
{
    QMutexLocker locker(&m_mutex);
    KIS_SAFE_ASSERT_RECOVER_RETURN(slot != InvalidSlot && slot / m_tilesPerChunk < m_chunks.size());
    KIS_SAFE_ASSERT_RECOVER_RETURN(m_allocated > 0);
    m_freeSlots.append(slot);
    m_allocated--;
    m_chunkUsed[slot / m_tilesPerChunk]--;
}

void KisGpuTilePool::trim()
{
    QMutexLocker locker(&m_mutex);
    for (size_t i = 0; i < m_chunks.size(); ++i) {
        if (m_chunks[i] && !m_chunkUsed[i]) {
            m_freeSlots.erase(std::remove_if(m_freeSlots.begin(),
                                             m_freeSlots.end(),
                                             [this, i](quint32 slot) {
                                                 return slot / m_tilesPerChunk == i;
                                             }),
                              m_freeSlots.end());
            m_reserved -= m_tileBytes * m_chunkSlots[i];
            m_chunks[i].reset();
            m_chunkSlots[i] = 0;
        }
    }
}

VkDeviceAddress KisGpuTilePool::deviceAddress(quint32 slot) const
{
    VkDeviceSize offset = 0;
    const KisGpuBuffer *chunk = buffer(slot, &offset);
    return chunk ? chunk->deviceAddress() + offset : 0;
}

const KisGpuBuffer *KisGpuTilePool::buffer(quint32 slot, VkDeviceSize *offset) const
{
    QMutexLocker locker(&m_mutex);
    const quint32 chunkIndex = slot / m_tilesPerChunk;
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(slot != InvalidSlot && chunkIndex < m_chunks.size(), nullptr);
    if (offset) {
        *offset = VkDeviceSize(slot % m_tilesPerChunk) * m_tileBytes;
    }
    return m_chunks[chunkIndex].get();
}

QVector<VkDeviceSize> KisGpuTilePool::consecutiveOffsets(VkDeviceSize start, int count) const
{
    QVector<VkDeviceSize> offsets(count);
    for (int i = 0; i < count; i++) {
        offsets[i] = start + VkDeviceSize(i) * m_tileBytes;
    }
    return offsets;
}

void KisGpuTilePool::recordUpload(KisGpuCommandList &commands,
                                  const KisGpuBuffer &src,
                                  VkDeviceSize srcOffset,
                                  const QVector<quint32> &slots) const
{
    recordCopies(commands.commandBuffer(), src, consecutiveOffsets(srcOffset, slots.size()), slots, true);
}

void KisGpuTilePool::recordUploadAt(VkCommandBuffer commandBuffer,
                                    const KisGpuBuffer &src,
                                    const QVector<VkDeviceSize> &srcOffsets,
                                    const QVector<quint32> &slots) const
{
    KIS_SAFE_ASSERT_RECOVER_RETURN(srcOffsets.size() == slots.size());
    recordCopies(commandBuffer, src, srcOffsets, slots, true);
}

void KisGpuTilePool::recordReadback(KisGpuCommandList &commands,
                                    const QVector<quint32> &slots,
                                    const KisGpuBuffer &dst,
                                    VkDeviceSize dstOffset) const
{
    recordCopies(commands.commandBuffer(), dst, consecutiveOffsets(dstOffset, slots.size()), slots, false);
}

void KisGpuTilePool::recordCopies(VkCommandBuffer commandBuffer,
                                  const KisGpuBuffer &host,
                                  const QVector<VkDeviceSize> &hostOffsets,
                                  const QVector<quint32> &slots,
                                  bool upload) const
{
    QMutexLocker locker(&m_mutex);

    // One vkCmdCopyBuffer per chunk keeps the command count independent of the tile count.
    std::vector<std::vector<VkBufferCopy>> regionsPerChunk(m_chunks.size());
    for (int i = 0; i < slots.size(); i++) {
        const quint32 slot = slots[i];
        const quint32 chunkIndex = slot / m_tilesPerChunk;
        KIS_SAFE_ASSERT_RECOVER(slot != InvalidSlot && chunkIndex < m_chunks.size())
        {
            continue;
        }
        const VkDeviceSize slotOffset = VkDeviceSize(slot % m_tilesPerChunk) * m_tileBytes;
        const VkDeviceSize hostTileOffset = hostOffsets[i];

        VkBufferCopy region{};
        region.srcOffset = upload ? hostTileOffset : slotOffset;
        region.dstOffset = upload ? slotOffset : hostTileOffset;
        region.size = m_tileBytes;
        regionsPerChunk[chunkIndex].push_back(region);
    }

    const KisGpuVulkanFunctions &vk = m_context.vk();
    for (size_t chunkIndex = 0; chunkIndex < regionsPerChunk.size(); chunkIndex++) {
        const std::vector<VkBufferCopy> &regions = regionsPerChunk[chunkIndex];
        if (regions.empty()) {
            continue;
        }
        const VkBuffer chunk = m_chunks[chunkIndex]->handle();
        vk.vkCmdCopyBuffer(commandBuffer,
                           upload ? host.handle() : chunk,
                           upload ? chunk : host.handle(),
                           quint32(regions.size()),
                           regions.data());
    }
}

void KisGpuTilePool::recordSlotCopies(KisGpuCommandList &commands,
                                      const QVector<quint32> &srcSlots,
                                      const QVector<quint32> &dstSlots) const
{
    KIS_SAFE_ASSERT_RECOVER_RETURN(srcSlots.size() == dstSlots.size());
    QMutexLocker locker(&m_mutex);

    // Group by (source chunk, destination chunk): one command per pair.
    std::map<std::pair<quint32, quint32>, std::vector<VkBufferCopy>> groups;
    for (int i = 0; i < srcSlots.size(); i++) {
        const quint32 srcChunk = srcSlots[i] / m_tilesPerChunk;
        const quint32 dstChunk = dstSlots[i] / m_tilesPerChunk;
        KIS_SAFE_ASSERT_RECOVER(srcChunk < m_chunks.size() && dstChunk < m_chunks.size())
        {
            continue;
        }
        VkBufferCopy region{};
        region.srcOffset = VkDeviceSize(srcSlots[i] % m_tilesPerChunk) * m_tileBytes;
        region.dstOffset = VkDeviceSize(dstSlots[i] % m_tilesPerChunk) * m_tileBytes;
        region.size = m_tileBytes;
        groups[{srcChunk, dstChunk}].push_back(region);
    }

    const KisGpuVulkanFunctions &vk = m_context.vk();
    for (const auto &group : groups) {
        vk.vkCmdCopyBuffer(commands.commandBuffer(),
                           m_chunks[group.first.first]->handle(),
                           m_chunks[group.first.second]->handle(),
                           quint32(group.second.size()),
                           group.second.data());
    }
}

void KisGpuTilePool::recordClearSlots(KisGpuCommandList &commands, const QVector<quint32> &slots) const
{
    QMutexLocker locker(&m_mutex);
    const KisGpuVulkanFunctions &vk = m_context.vk();
    for (quint32 slot : slots) {
        const quint32 chunkIndex = slot / m_tilesPerChunk;
        KIS_SAFE_ASSERT_RECOVER(chunkIndex < m_chunks.size())
        {
            continue;
        }
        vk.vkCmdFillBuffer(commands.commandBuffer(),
                           m_chunks[chunkIndex]->handle(),
                           VkDeviceSize(slot % m_tilesPerChunk) * m_tileBytes,
                           m_tileBytes,
                           0);
    }
}

quint32 KisGpuTilePool::allocatedSlots() const
{
    QMutexLocker locker(&m_mutex);
    return m_allocated;
}

quint32 KisGpuTilePool::capacitySlots() const
{
    QMutexLocker locker(&m_mutex);
    quint32 capacity = 0;
    for (quint32 count : m_chunkSlots)
        capacity += count;
    return capacity;
}

VkDeviceSize KisGpuTilePool::reservedBytes() const
{
    QMutexLocker locker(&m_mutex);
    return m_reserved;
}
