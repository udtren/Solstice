/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUCOMPUTEPIPELINE_H
#define KISGPUCOMPUTEPIPELINE_H

#include <QString>

#include <memory>

#include "KisGpuVulkanFunctions.h"
#include "kritagpu_export.h"

class KisGpuContext;

/**
 * A compute pipeline whose only inputs are push constants.
 *
 * Shaders receive buffers through 64-bit device addresses in the push
 * constant block (GL_EXT_buffer_reference), so no descriptor sets are needed.
 */
class KRITAGPU_EXPORT KisGpuComputePipeline
{
public:
    ~KisGpuComputePipeline();

    KisGpuComputePipeline(const KisGpuComputePipeline &) = delete;
    KisGpuComputePipeline &operator=(const KisGpuComputePipeline &) = delete;

    /**
     * @param spirv SPIR-V words (embedded at build time, see kis_gpu_add_shaders())
     * @param pushConstantSize size of the shader's push constant block in bytes (max 128)
     */
    static std::unique_ptr<KisGpuComputePipeline> create(KisGpuContext &context,
                                                         const quint32 *spirv,
                                                         size_t spirvSizeBytes,
                                                         quint32 pushConstantSize,
                                                         QString *errorMessage = nullptr);

    template<typename PushConstants>
    void dispatch(VkCommandBuffer commandBuffer,
                  const PushConstants &constants,
                  quint32 groupsX,
                  quint32 groupsY = 1,
                  quint32 groupsZ = 1) const
    {
        dispatchRaw(commandBuffer, &constants, sizeof(PushConstants), groupsX, groupsY, groupsZ);
    }

    void dispatchRaw(VkCommandBuffer commandBuffer,
                     const void *constants,
                     quint32 constantsSize,
                     quint32 groupsX,
                     quint32 groupsY,
                     quint32 groupsZ) const;

private:
    explicit KisGpuComputePipeline(KisGpuContext &context);

    KisGpuContext &m_context;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    quint32 m_pushConstantSize = 0;
};

#endif // KISGPUCOMPUTEPIPELINE_H
