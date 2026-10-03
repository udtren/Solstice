/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuTileBackend.h"

#include <KisGpuBuffer.h>
#include <KisGpuCommandList.h>
#include <KisGpuContext.h>

#include <QMutexLocker>

#include <algorithm>
#include <cstring>
#include <mutex>

#include <kis_debug.h>

#include "tiles3/kis_tile_data.h"
#include "tiles3/kis_tile_data_store_iterators.h"

namespace
{
/// Tiles per download submission; the readback buffer holds this many tiles.
constexpr int DownloadBatchTiles = 256;

std::once_flag s_createOnce;
KisGpuTileBackend *s_instance = nullptr;
QString s_unavailableReason;
QMutex s_failureListenerMutex;
std::function<void()> *s_failureListener = new std::function<void()>();
std::atomic<bool> s_failureNotified{false};

/// Calls the failure listener once per process (the failed state is final).
void notifyFailure()
{
    if (s_failureNotified.exchange(true)) {
        return;
    }
    std::function<void()> listener;
    {
        QMutexLocker locker(&s_failureListenerMutex);
        listener = *s_failureListener;
    }
    if (listener) {
        listener();
    }
}
} // namespace

KisGpuTileBackend::KisGpuTileBackend(std::unique_ptr<KisGpuContext> context)
    : m_context(std::move(context))
    , m_pool32(new KisGpuTilePool(*m_context, KisGpuTileFormat::RGBA32F))
    , m_pool16(new KisGpuTilePool(*m_context, KisGpuTileFormat::RGBA16F))
    , m_transferCommands(new KisGpuCommandList(*m_context))
{
    constexpr quint64 bytesPerMiB = quint64(1) << 20;
    quint64 budget = qMin(quint64(8192) * bytesPerMiB, m_context->deviceInfo().deviceLocalBytes / 4);
    bool valid = false;
    const quint64 requested = qEnvironmentVariable("KRITA_GPU_TILE_BUDGET_MIB").toULongLong(&valid);
    if (valid && requested > 0 && requested <= 1048576)
        budget = requested * bytesPerMiB;
    m_memoryBudget.store(qMax(bytesPerMiB, budget));
}

KisGpuTileBackend *KisGpuTileBackend::instance()
{
    std::call_once(s_createOnce, []() {
        QString error;
        std::unique_ptr<KisGpuContext> context = KisGpuContext::create(&error);
        if (!context) {
            s_unavailableReason = error;
            warnKrita << "GPU engine unavailable:" << error;
            return;
        }
        s_instance = new KisGpuTileBackend(std::move(context));
    });
    return s_instance;
}

KisGpuTileBackend *KisGpuTileBackend::existingInstance()
{
    return s_instance;
}

QString KisGpuTileBackend::unavailableReason()
{
    return s_unavailableReason;
}

KisGpuContext &KisGpuTileBackend::context()
{
    return *m_context;
}

KisGpuTilePool *KisGpuTileBackend::pool(qint32 pixelSize)
{
    switch (pixelSize) {
    case 16:
        return m_pool32.get();
    case 8:
        return m_pool16.get();
    default:
        return nullptr;
    }
}

KisTileGpuState *KisGpuTileBackend::pinState(KisTileData *td, QString *errorMessage)
{
    {
        QMutexLocker locker(&m_residencyMutex);
        if (KisTileGpuState *state = td->gpuState()) {
            if (state->slot != KisTileGpuState::InvalidSlot) {
                ++state->pins;
                return state;
            }
        }
    }

    KisGpuTilePool *tilePool = pool(td->pixelSize());
    if (!tilePool) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported pixel size %1 for GPU tiles").arg(td->pixelSize());
        }
        return nullptr;
    }

    collectGarbage();

    for (int attempt = 0; attempt < 2; ++attempt) {
        // Loading from disk may take store locks: never do it with the
        // residency mutex held. The read lock prevents swapping during install.
        td->blockSwapping();
        KisTileGpuState *result = nullptr;
        {
            QMutexLocker locker(&m_residencyMutex);
            KisTileGpuState *state = td->gpuState();
            if (!state || state->slot == KisTileGpuState::InvalidSlot) {
                const quint64 other = (tilePool == m_pool32.get() ? m_pool16 : m_pool32)->reservedBytes();
                const quint64 budget = m_memoryBudget.load();
                const quint32 slot = tilePool->allocate(errorMessage, budget > other ? budget - other : 0);
                if (slot != KisGpuTilePool::InvalidSlot) {
                    if (!state)
                        state = td->installGpuState(new KisTileGpuState);
                    state->slot = slot;
                    state->lastUse = 0;
                }
            }
            if (state && state->slot != KisTileGpuState::InvalidSlot) {
                ++state->pins;
                result = state;
            }
        }
        td->unblockSwapping();
        if (result)
            return result;
        // Do not hold a swap lock while taking the store's iteration lock.
        if (!attempt)
            evictTiles(tilePool->tileBytes() * tilePool->tilesPerChunk());
    }
    return nullptr;
}

void KisGpuTileBackend::unpinStates(const QVector<KisTileGpuState *> &states)
{
    QMutexLocker locker(&m_residencyMutex);
    for (KisTileGpuState *state : states) {
        KIS_SAFE_ASSERT_RECOVER_RETURN(state->pins > 0);
        --state->pins;
    }
}

bool KisGpuTileBackend::tryEvict(KisTileData *td)
{
    QMutexLocker locker(&m_residencyMutex);
    KisTileGpuState *state = td->gpuState();
    if (!state || state->slot == KisTileGpuState::InvalidSlot)
        return true;
    if (state->pins || state->lastUse > m_context->completedValue())
        return false;
    // This is voluntary memory reclamation. On failure, keep the only valid
    // copy on the GPU; ordinary CPU access retains its retry/loss reporting.
    if (!downloadBatch({td}).isEmpty())
        return false;
    QMutexLocker stateLocker(&state->mutex);
    if (!state->cpuValid())
        return false;
    const quint32 slot = state->slot;
    state->setValid(KisTileGpuState::CpuValid);
    state->slot = KisTileGpuState::InvalidSlot;
    pool(td->pixelSize())->release(slot);
    return true;
}

quint64 KisGpuTileBackend::evictTiles(quint64 bytes)
{
    quint64 evicted = 0;
    KisTileDataStore *store = KisTileDataStore::instance();
    // Store iteration pins object lifetimes without adding references that
    // would keep unused undo data alive indefinitely. Try-lock swap locks.
    auto *iterator = store->beginIteration();
    QVector<std::pair<int, KisTileData *>> candidates;
    while (iterator->hasNext()) {
        KisTileData *td = iterator->next();
        if (KisTileGpuState *state = td->gpuState()) {
            const int rank = (td->historical() ? 0 : 2) + (state->cpuValid() ? 0 : 1);
            candidates.append({rank, td});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });
    for (const auto &candidate : candidates) {
        if (evicted >= bytes)
            break;
        KisTileData *td = candidate.second;
        if (store->tryEvictGpuTileData(td))
            evicted += quint64(td->pixelSize()) * 64 * 64;
    }
    store->endIteration(iterator);
    m_pool32->trim();
    m_pool16->trim();
    return evicted;
}

quint64 KisGpuTileBackend::memoryBudget() const
{
    return m_memoryBudget.load();
}
quint64 KisGpuTileBackend::reservedTileBytes() const
{
    return m_pool32->reservedBytes() + m_pool16->reservedBytes();
}
void KisGpuTileBackend::setMemoryBudgetForTesting(quint64 bytes)
{
    m_memoryBudget.store(bytes);
}

void KisGpuTileBackend::ensureCpuValid(KisTileData *td)
{
    downloadToCpu({td});
}

void KisGpuTileBackend::downloadToCpu(const QVector<KisTileData *> &tiles)
{
    QVector<KisTileData *> pending;
    for (KisTileData *td : tiles) {
        KisTileGpuState *state = td->gpuState();
        if (state && !state->cpuValid()) {
            pending << td;
        }
    }
    if (pending.isEmpty()) {
        return;
    }

    // Batches hold one pixel size; within a batch, tile states are locked in
    // address order (lock order: states sorted by address, then the transfer mutex).
    std::sort(pending.begin(), pending.end(), [](KisTileData *a, KisTileData *b) {
        if (a->pixelSize() != b->pixelSize()) {
            return a->pixelSize() < b->pixelSize();
        }
        return a->gpuState() < b->gpuState();
    });
    pending.erase(std::unique(pending.begin(), pending.end()), pending.end());

    QVector<KisTileData *> failed;
    int start = 0;
    while (start < pending.size()) {
        int end = start;
        while (end < pending.size() && end - start < DownloadBatchTiles
               && pending[end]->pixelSize() == pending[start]->pixelSize()) {
            end++;
        }
        failed += downloadBatch(pending.mid(start, end - start));
        start = end;
    }

    // Retry tile by tile (e.g. after a failed allocation of the batch buffer).
    QVector<KisTileData *> lost;
    for (KisTileData *td : failed) {
        lost += downloadBatch({td});
    }

    // The GPU copy is unreachable even with exactly sized buffers (device
    // lost, out of memory). The newer content is gone. The CPU buffer (the
    // content the CPU last had) is the only thing left to show, but it is
    // not silently treated as the real content: the tile is marked
    // ContentLost, the engine stops accepting GPU work, and the loss is
    // reported (contentLossCount(), the log, and later the UI).
    if (lost.isEmpty()) {
        return;
    }

    {
        // Stop the engine first, under the residency mutex: every
        // submitAndFinish() either submitted before this point or sees the
        // failed state and refuses to submit.
        QMutexLocker locker(&m_residencyMutex);
        m_failed.store(true);
    }

    for (KisTileData *td : lost) {
        KisTileGpuState *state = td->gpuState();
        QMutexLocker locker(&state->mutex);
        if (!state->cpuValid()) {
            state->markContentLost();
            m_lostTiles++;
        }
    }
    errKrita << "GPU engine: could not read back" << lost.size()
             << "tiles; their latest content is lost and they show the content the CPU last had."
             << "The GPU engine is stopped.";
    notifyFailure();
}

QVector<KisTileData *> KisGpuTileBackend::downloadBatch(const QVector<KisTileData *> &tiles)
{
    QVector<KisTileData *> failed;

    QVector<KisTileGpuState *> states;
    for (KisTileData *td : tiles) {
        KisTileGpuState *state = td->gpuState();
        state->mutex.lock();
        states << state;
    }

    // Another thread may have downloaded some tiles while we waited for their locks.
    QVector<KisTileData *> stale;
    QVector<quint32> slots;
    for (int i = 0; i < tiles.size(); i++) {
        if (!states[i]->cpuValid()) {
            KIS_SAFE_ASSERT_RECOVER(states[i]->gpuValid())
            {
                // Neither copy is valid: a GPU write was prepared but not finished.
                continue;
            }
            stale << tiles[i];
            slots << states[i]->slot;
        }
    }

    if (!stale.isEmpty()) {
        KisGpuTilePool *tilePool = pool(stale.first()->pixelSize());
        QMutexLocker transferLocker(&m_transferMutex);

        // Exactly the tiles of this batch: a retry of one tile needs one tile
        // of memory, even if a large batch buffer could not be allocated.
        const VkDeviceSize bytes = tilePool->tileBytes() * VkDeviceSize(stale.size());
        const bool allocationAllowed = bytes <= m_injectedReadbackLimit.load();
        if (!m_readback || m_readback->size() < bytes) {
            m_readback.reset();
            if (allocationAllowed) {
                m_readback = KisGpuBuffer::create(*m_context, bytes, KisGpuBuffer::Location::Readback);
            }
        }

        bool ok = allocationAllowed && m_readback && m_transferCommands->isValid();
        if (ok && m_injectedDownloadFailures.load() > 0 && m_injectedDownloadFailures.fetch_sub(1) > 0) {
            ok = false;
        }
        if (ok) {
            m_transferCommands->begin();
            tilePool->recordReadback(*m_transferCommands, slots, *m_readback, 0);
            m_transferCommands->barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                        VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                        VK_PIPELINE_STAGE_2_HOST_BIT,
                                        VK_ACCESS_2_HOST_READ_BIT);
            const quint64 value = m_transferCommands->submit();
            ok = ok && value && m_transferCommands->wait();
        }

        if (ok) {
            const quint8 *src = static_cast<const quint8 *>(m_readback->mapped());
            const size_t tileBytes = size_t(tilePool->tileBytes());
            for (int i = 0; i < stale.size(); i++) {
                std::memcpy(stale[i]->data(), src + size_t(i) * tileBytes, tileBytes);
                stale[i]->gpuState()->addValid(KisTileGpuState::CpuValid);
            }
        } else {
            warnKrita << "GPU engine: download of" << stale.size() << "tiles failed";
            failed = stale;
        }
    }

    for (KisTileGpuState *state : states) {
        state->mutex.unlock();
    }
    return failed;
}

void KisGpuTileBackend::injectDownloadFailuresForTesting(int count)
{
    m_injectedDownloadFailures.store(count);
}

quint64 KisGpuTileBackend::contentLossCount() const
{
    return m_lostTiles.load();
}

bool KisGpuTileBackend::hasFailed() const
{
    return m_failed.load();
}

void KisGpuTileBackend::injectReadbackLimitForTesting(quint64 maxBytes)
{
    m_injectedReadbackLimit.store(maxBytes);
}

void KisGpuTileBackend::resetFailureForTesting()
{
    m_failed.store(false);
    s_failureNotified.store(false);
}

void KisGpuTileBackend::setFailureListener(std::function<void()> listener)
{
    QMutexLocker locker(&s_failureListenerMutex);
    *s_failureListener = std::move(listener);
}

QMutex &KisGpuTileBackend::residencyMutex()
{
    return m_residencyMutex;
}

void KisGpuTileBackend::destroyState(KisTileGpuState *state, qint32 pixelSize)
{
    KisGpuTilePool *tilePool = pool(pixelSize);
    if (tilePool && state->slot != KisTileGpuState::InvalidSlot) {
        QMutexLocker locker(&m_garbageMutex);
        m_deferredSlots.push_back({tilePool, state->slot, state->lastUse});
    }
    delete state;
}

void KisGpuTileBackend::retireAfter(quint64 timelineValue, std::shared_ptr<void> resource)
{
    QMutexLocker locker(&m_garbageMutex);
    m_retired.emplace_back(timelineValue, std::move(resource));
}

void KisGpuTileBackend::collectGarbage()
{
    const quint64 completed = m_context->completedValue();

    QMutexLocker locker(&m_garbageMutex);
    auto slotEnd =
        std::partition(m_deferredSlots.begin(), m_deferredSlots.end(), [completed](const DeferredSlot &slot) {
            return slot.timelineValue > completed;
        });
    for (auto it = slotEnd; it != m_deferredSlots.end(); ++it) {
        it->pool->release(it->slot);
    }
    m_deferredSlots.erase(slotEnd, m_deferredSlots.end());

    auto retiredEnd = std::partition(m_retired.begin(), m_retired.end(), [completed](const auto &entry) {
        return entry.first > completed;
    });
    m_retired.erase(retiredEnd, m_retired.end());
    m_pool32->trim();
    m_pool16->trim();
}

void KisGpuTileBackend::flush()
{
    m_context->waitIdle();
    collectGarbage();
}

quint32 KisGpuTileBackend::allocatedSlots(qint32 pixelSize)
{
    KisGpuTilePool *tilePool = pool(pixelSize);
    return tilePool ? tilePool->allocatedSlots() : 0;
}

void KisTileGpuHooks::ensureCpuValid(KisTileData *td)
{
    KisGpuTileBackend *backend = KisGpuTileBackend::existingInstance();
    KIS_SAFE_ASSERT_RECOVER_RETURN(backend);
    backend->ensureCpuValid(td);
}

void KisTileGpuHooks::destroyState(KisTileGpuState *state, qint32 pixelSize)
{
    KisGpuTileBackend *backend = KisGpuTileBackend::existingInstance();
    if (!backend) {
        delete state;
        return;
    }
    backend->destroyState(state, pixelSize);
}

bool KisTileGpuHooks::tryEvict(KisTileData *td)
{
    KisGpuTileBackend *backend = KisGpuTileBackend::existingInstance();
    return backend && backend->tryEvict(td);
}
