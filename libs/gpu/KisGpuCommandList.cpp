/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuCommandList.h"

#include "KisGpuBuffer.h"
#include "KisGpuContext.h"

#include <kis_debug.h>

namespace
{
void recordMemoryBarrier(const KisGpuVulkanFunctions &vk,
                         VkCommandBuffer commandBuffer,
                         VkPipelineStageFlags2 srcStage,
                         VkAccessFlags2 srcAccess,
                         VkPipelineStageFlags2 dstStage,
                         VkAccessFlags2 dstAccess)
{
    VkMemoryBarrier2 memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    memoryBarrier.srcStageMask = srcStage;
    memoryBarrier.srcAccessMask = srcAccess;
    memoryBarrier.dstStageMask = dstStage;
    memoryBarrier.dstAccessMask = dstAccess;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &memoryBarrier;
    vk.vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

/// Orders the following commands after everything submitted before them.
void recordFullBarrier(const KisGpuVulkanFunctions &vk, VkCommandBuffer commandBuffer)
{
    recordMemoryBarrier(vk,
                        commandBuffer,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_ACCESS_2_MEMORY_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
}
} // namespace

KisGpuCommandList::KisGpuCommandList(KisGpuContext &context)
    : m_context(context)
{
    const KisGpuVulkanFunctions &vk = context.vk();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = context.queueFamilyIndex();
    if (vk.vkCreateCommandPool(context.device(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        warnKrita << "GPU engine: failed to create a command pool";
        return;
    }

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = m_pool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 2;
    VkCommandBuffer buffers[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    if (vk.vkAllocateCommandBuffers(context.device(), &allocateInfo, buffers) != VK_SUCCESS) {
        warnKrita << "GPU engine: failed to allocate command buffers";
        return;
    }
    m_commandBuffer = buffers[0];
    m_preamble = buffers[1];

    VkQueryPoolCreateInfo queryInfo{};
    queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = MaxTimestamps;
    if (vk.vkCreateQueryPool(context.device(), &queryInfo, nullptr, &m_queryPool) != VK_SUCCESS) {
        m_queryPool = VK_NULL_HANDLE;
    }
}

KisGpuCommandList::~KisGpuCommandList()
{
    wait();
    const KisGpuVulkanFunctions &vk = m_context.vk();
    if (m_queryPool) {
        vk.vkDestroyQueryPool(m_context.device(), m_queryPool, nullptr);
    }
    if (m_pool) {
        vk.vkDestroyCommandPool(m_context.device(), m_pool, nullptr);
    }
}

bool KisGpuCommandList::isValid() const
{
    return m_commandBuffer != VK_NULL_HANDLE;
}

VkCommandBuffer KisGpuCommandList::begin()
{
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(isValid(), VK_NULL_HANDLE);

    wait();
    const KisGpuVulkanFunctions &vk = m_context.vk();
    vk.vkResetCommandPool(m_context.device(), m_pool, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk.vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    m_recording = true;
    m_preambleRecording = false;

    if (m_queryPool) {
        vk.vkCmdResetQueryPool(m_commandBuffer, m_queryPool, 0, MaxTimestamps);
    }

    // Order this list after everything submitted before it: the engine relies
    // on submission order instead of tracking per-resource dependencies.
    recordFullBarrier(vk, m_commandBuffer);
    return m_commandBuffer;
}

VkCommandBuffer KisGpuCommandList::commandBuffer() const
{
    return m_commandBuffer;
}

VkCommandBuffer KisGpuCommandList::preamble()
{
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(m_recording, VK_NULL_HANDLE);
    if (!m_preambleRecording) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        m_context.vk().vkBeginCommandBuffer(m_preamble, &beginInfo);
        m_preambleRecording = true;
        // The preamble runs first: it needs the same ordering after earlier
        // submissions (e.g. uploads into slots a previous list still reads).
        recordFullBarrier(m_context.vk(), m_preamble);
    }
    return m_preamble;
}

quint64 KisGpuCommandList::submit(const QVector<VkSemaphoreSubmitInfo> &waitSemaphores,
                                  const QVector<VkSemaphoreSubmitInfo> &signalSemaphores)
{
    const KisGpuVulkanFunctions &vk = m_context.vk();
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(m_recording, 0);
    m_recording = false;
    QVector<VkCommandBuffer> buffers;
    bool ok = true;
    if (m_preambleRecording) {
        m_preambleRecording = false;
        ok = vk.vkEndCommandBuffer(m_preamble) == VK_SUCCESS;
        buffers << m_preamble;
    }
    ok = vk.vkEndCommandBuffer(m_commandBuffer) == VK_SUCCESS && ok;
    buffers << m_commandBuffer;
    if (!ok) {
        return 0;
    }
    // The main buffer starts with a full barrier, which also orders it after
    // the preamble (earlier in the same batch).
    m_lastSubmission = m_context.submit(buffers, waitSemaphores, signalSemaphores);
    return m_lastSubmission;
}

void KisGpuCommandList::abandon()
{
    if (m_preambleRecording) {
        m_preambleRecording = false;
        m_context.vk().vkEndCommandBuffer(m_preamble);
    }
    if (m_recording) {
        m_recording = false;
        m_context.vk().vkEndCommandBuffer(m_commandBuffer);
    }
}

bool KisGpuCommandList::isRecording() const
{
    return m_recording;
}

bool KisGpuCommandList::wait()
{
    if (!m_lastSubmission) {
        return true;
    }
    const bool ok = m_context.wait(m_lastSubmission);
    if (ok) {
        m_lastSubmission = 0;
    }
    return ok;
}

void KisGpuCommandList::barrier(VkPipelineStageFlags2 srcStage,
                                VkAccessFlags2 srcAccess,
                                VkPipelineStageFlags2 dstStage,
                                VkAccessFlags2 dstAccess)
{
    recordMemoryBarrier(m_context.vk(), m_commandBuffer, srcStage, srcAccess, dstStage, dstAccess);
}

void KisGpuCommandList::computeBarrier()
{
    const VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    const VkAccessFlags2 access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
        | VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier(stages, access, stages, access);
}

void KisGpuCommandList::copyBuffer(const KisGpuBuffer &src,
                                   VkDeviceSize srcOffset,
                                   const KisGpuBuffer &dst,
                                   VkDeviceSize dstOffset,
                                   VkDeviceSize size)
{
    VkBufferCopy region{};
    region.srcOffset = srcOffset;
    region.dstOffset = dstOffset;
    region.size = size;
    m_context.vk().vkCmdCopyBuffer(m_commandBuffer, src.handle(), dst.handle(), 1, &region);
}

void KisGpuCommandList::writeTimestamp(quint32 index)
{
    KIS_SAFE_ASSERT_RECOVER_RETURN(index < MaxTimestamps);
    if (m_queryPool) {
        m_context.vk().vkCmdWriteTimestamp2(m_commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_queryPool, index);
    }
}

double KisGpuCommandList::elapsedMs(quint32 fromIndex, quint32 toIndex) const
{
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(fromIndex < MaxTimestamps && toIndex < MaxTimestamps, 0.0);
    if (!m_queryPool) {
        return 0.0;
    }
    quint64 values[2] = {0, 0};
    const KisGpuVulkanFunctions &vk = m_context.vk();
    vk.vkGetQueryPoolResults(m_context.device(),
                             m_queryPool,
                             fromIndex,
                             1,
                             sizeof(quint64),
                             &values[0],
                             sizeof(quint64),
                             VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    vk.vkGetQueryPoolResults(m_context.device(),
                             m_queryPool,
                             toIndex,
                             1,
                             sizeof(quint64),
                             &values[1],
                             sizeof(quint64),
                             VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    return double(values[1] - values[0]) * double(m_context.deviceInfo().timestampPeriodNs) / 1.0e6;
}
