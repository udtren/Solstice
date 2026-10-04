/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISTILEGPUSTATE_H
#define KISTILEGPUSTATE_H

#include <QAtomicInt>
#include <QMutex>
#include <QVector>
#include <QtGlobal>
#include <atomic>

#include "kritaimage_export.h"

class KisTileData;
class KisTiledDataManager;
class QRect;

/**
 * GPU residency of one KisTileData (GPU engine, docs/agent/gpu-engine.md).
 *
 * A tile data used by the GPU owns a slot until it is evicted from the pool.
 * The CPU copy (KisTileData::data()) and the GPU copy are each either valid
 * or stale; at least one is valid at any time:
 *
 *  - CPU access goes through KisTile::lockForRead/Write ->
 *    KisTileData::blockSwapping(), which downloads the GPU copy first when
 *    the CPU copy is stale;
 *  - a CPU write (KisTile::lockForWrite) marks the GPU copy stale;
 *  - a GPU write (KisGpuTileAccess) marks the CPU copy stale.
 *
 * The validity flags and the CPU write generation live in one atomic word,
 * so "publish the GPU copy only if the CPU has not written since" and "a CPU
 * write invalidates the GPU copy" cannot interleave (compare-and-swap).
 *
 * Disk swapping first downloads and evicts an idle GPU slot. Prepared and
 * in-flight accesses prevent eviction; the state survives without a slot.
 */
struct KisTileGpuState {
    enum Flag {
        CpuValid = 0x1,
        GpuValid = 0x2,
        /// The GPU copy could not be downloaded: the CPU copy holds older
        /// content and is used as a last resort (KisGpuTileBackend reports it).
        ContentLost = 0x4,
    };

    static constexpr quint32 InvalidSlot = 0xFFFFFFFFu;

    /// Serializes transfers (upload/download) of this tile data.
    QMutex mutex;
    /// Slot may be evicted while unpinned; the state remains attached to its tile data.
    std::atomic<quint32> slot{InvalidSlot};
    /// Prepared GPU accesses; protected by the backend's residency mutex.
    quint32 pins = 0;
    /// Timeline value of the last submission that accessed the slot (guarded by mutex).
    quint64 lastUse = 0;

    bool cpuValid() const
    {
        return m_word.loadAcquire() & CpuValid;
    }

    bool gpuValid() const
    {
        return m_word.loadAcquire() & GpuValid;
    }

    bool contentLost() const
    {
        return m_word.loadAcquire() & ContentLost;
    }

    /// Counts CPU write locks; changes whenever the CPU copy may have changed.
    int generation() const
    {
        return m_word.loadAcquire() >> GenerationShift;
    }

    /// Sets the CpuValid/GpuValid bits to exactly @p validFlags.
    void setValid(int validFlags)
    {
        int old = m_word.loadAcquire();
        while (!m_word.testAndSetOrdered(old, (old & ~ValidMask) | (validFlags & ValidMask), old)) { }
    }

    void addValid(int flag)
    {
        m_word.fetchAndOrOrdered(flag & ValidMask);
    }

    /**
     * A CPU write lock: bumps the generation and makes the GPU copy stale,
     * atomically. The only valid copy is never dropped (the CPU copy has been
     * made valid by blockSwapping() before any write lock).
     */
    void notifyCpuWrite()
    {
        int old = m_word.loadAcquire();
        int next;
        do {
            next = int(uint(old) + uint(GenerationStep)); // wraps around, no signed overflow
            if (old & CpuValid) {
                next &= ~GpuValid;
            }
        } while (!m_word.testAndSetOrdered(old, next, old));
    }

    /**
     * Marks the GPU copy valid only if no CPU write happened since
     * @p expectedGeneration was read. Returns false (and changes nothing)
     * otherwise.
     */
    bool markGpuValidIfGeneration(int expectedGeneration)
    {
        int old = m_word.loadAcquire();
        do {
            if ((old >> GenerationShift) != expectedGeneration) {
                return false;
            }
        } while (!m_word.testAndSetOrdered(old, old | GpuValid, old));
        return true;
    }

    /// Download failed for good: fall back to the (older) CPU copy and record it.
    void markContentLost()
    {
        int old = m_word.loadAcquire();
        while (!m_word.testAndSetOrdered(old, (old & ~ValidMask) | CpuValid | ContentLost, old)) { }
    }

private:
    static constexpr int ValidMask = CpuValid | GpuValid;
    static constexpr int GenerationShift = 3;
    static constexpr int GenerationStep = 1 << GenerationShift;

    QAtomicInt m_word{CpuValid};
};

/**
 * Entry points from the tile engine into the GPU backend. Without the GPU
 * engine, bulk prefetch is a no-op and no tile data gets a KisTileGpuState,
 * so state-specific hooks are never reached (KisTileGpuHooksStub.cpp).
 */
namespace KisTileGpuHooks
{
/// Batch prefetch for bulk readers. The caller must exclude writes to the region.
/// Does nothing without an existing GPU backend; never creates missing tiles.
KRITAIMAGE_EXPORT void prepareCpuRead(KisTiledDataManager *manager, const QRect &rect);
/// Downloads the GPU copy into td->data(). Called with the swap lock held for reading.
KRITAIMAGE_EXPORT void ensureCpuValid(KisTileData *td);
/// Releases the slot (after the GPU has finished using it) and deletes @p state.
KRITAIMAGE_EXPORT void destroyState(KisTileGpuState *state, qint32 pixelSize);
/// Voluntary eviction. Caller holds the tile data's swap lock for writing.
KRITAIMAGE_EXPORT bool tryEvict(KisTileData *td);
/// Batched voluntary eviction; caller holds all swap write locks and lifetimes.
KRITAIMAGE_EXPORT quint64 tryEvictBatch(const QVector<KisTileData *> &tiles);
} // namespace KisTileGpuHooks

#endif // KISTILEGPUSTATE_H
