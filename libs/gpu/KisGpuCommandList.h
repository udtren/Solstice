/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUCOMMANDLIST_H
#define KISGPUCOMMANDLIST_H

#include <QVector>

#include "KisGpuVulkanFunctions.h"
#include "kritagpu_export.h"

class KisGpuContext;
class KisGpuBuffer;
struct KisGpuSubmitTiming;

/**
 * A reusable primary command buffer with its own pool and timestamp queries.
 *
 * A command list is used by one thread at a time. begin() waits for the
 * previous submission of this list before resetting it, so a list can be
 * re-recorded in a loop without extra bookkeeping.
 *
 * Every list starts with a full memory barrier, so its commands observe all
 * writes of earlier submissions to the same queue (submission order is the
 * engine's dependency model).
 */
class KRITAGPU_EXPORT KisGpuCommandList
{
public:
    static constexpr quint32 MaxTimestamps = 64;

    explicit KisGpuCommandList(KisGpuContext &context);
    ~KisGpuCommandList();

    KisGpuCommandList(const KisGpuCommandList &) = delete;
    KisGpuCommandList &operator=(const KisGpuCommandList &) = delete;

    bool isValid() const;

    VkCommandBuffer begin();
    VkCommandBuffer commandBuffer() const;

    /**
     * A second command buffer that is submitted *before* the main one in the
     * same batch, begun on first use. Lets a caller decide at submission
     * time what must run first (e.g. KisGpuTileAccess uploads), after the
     * main work has already been recorded. Like the main buffer, it starts
     * with a full memory barrier.
     */
    VkCommandBuffer preamble();

    /// End main-buffer recording early; preamble recording remains available.
    /// Idempotent until begin(). Do not record main commands after this call.
    bool finishMainRecording();

    /// Ends recording and submits. Returns the timeline value, or 0 on failure.
    quint64 submit(const QVector<VkSemaphoreSubmitInfo> &waitSemaphores = {},
                   const QVector<VkSemaphoreSubmitInfo> &signalSemaphores = {},
                   KisGpuSubmitTiming *timing = nullptr);
    /// Ends recording without submitting (no-op if not recording). The list
    /// can be begun again.
    void abandon();
    bool isRecording() const;
    /// Waits for the last submission of this list.
    bool wait();

    /// Global memory barrier between two pipeline stages.
    void barrier(VkPipelineStageFlags2 srcStage,
                 VkAccessFlags2 srcAccess,
                 VkPipelineStageFlags2 dstStage,
                 VkAccessFlags2 dstAccess);
    /// Shorthand: compute/transfer writes become visible to compute/transfer reads and writes.
    void computeBarrier();

    void copyBuffer(const KisGpuBuffer &src,
                    VkDeviceSize srcOffset,
                    const KisGpuBuffer &dst,
                    VkDeviceSize dstOffset,
                    VkDeviceSize size);

    /// Records a timestamp into slot @p index (0 .. MaxTimestamps-1).
    void writeTimestamp(quint32 index);
    /// Elapsed GPU time between two timestamp slots, valid after wait().
    double elapsedMs(quint32 fromIndex, quint32 toIndex) const;

private:
    KisGpuContext &m_context;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkCommandBuffer m_preamble = VK_NULL_HANDLE;
    bool m_preambleRecording = false;
    VkQueryPool m_queryPool = VK_NULL_HANDLE;
    quint64 m_lastSubmission = 0;
    bool m_recording = false;
    bool m_mainEnded = false;
    bool m_mainEndSucceeded = false;
};

#endif // KISGPUCOMMANDLIST_H
