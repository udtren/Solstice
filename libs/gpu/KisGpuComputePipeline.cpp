/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuComputePipeline.h"

#include "KisGpuContext.h"

#include <kis_debug.h>

KisGpuComputePipeline::KisGpuComputePipeline(KisGpuContext &context)
    : m_context(context)
{
}

KisGpuComputePipeline::~KisGpuComputePipeline()
{
    const KisGpuVulkanFunctions &vk = m_context.vk();
    if (m_pipeline) {
        vk.vkDestroyPipeline(m_context.device(), m_pipeline, nullptr);
    }
    if (m_layout) {
        vk.vkDestroyPipelineLayout(m_context.device(), m_layout, nullptr);
    }
}

std::unique_ptr<KisGpuComputePipeline> KisGpuComputePipeline::create(KisGpuContext &context,
                                                                     const quint32 *spirv,
                                                                     size_t spirvSizeBytes,
                                                                     quint32 pushConstantSize,
                                                                     QString *errorMessage)
{
    const KisGpuVulkanFunctions &vk = context.vk();
    std::unique_ptr<KisGpuComputePipeline> pipeline(new KisGpuComputePipeline(context));
    pipeline->m_pushConstantSize = pushConstantSize;

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = spirvSizeBytes;
    moduleInfo.pCode = spirv;

    VkShaderModule module = VK_NULL_HANDLE;
    VkResult result = vk.vkCreateShaderModule(context.device(), &moduleInfo, nullptr, &module);
    if (result != VK_SUCCESS) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("vkCreateShaderModule failed: %1").arg(kisGpuVkResultString(result));
        }
        return nullptr;
    }

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.offset = 0;
    range.size = pushConstantSize;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = pushConstantSize ? 1 : 0;
    layoutInfo.pPushConstantRanges = pushConstantSize ? &range : nullptr;

    result = vk.vkCreatePipelineLayout(context.device(), &layoutInfo, nullptr, &pipeline->m_layout);
    if (result != VK_SUCCESS) {
        vk.vkDestroyShaderModule(context.device(), module, nullptr);
        if (errorMessage) {
            *errorMessage = QStringLiteral("vkCreatePipelineLayout failed: %1").arg(kisGpuVkResultString(result));
        }
        return nullptr;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipeline->m_layout;

    result =
        vk.vkCreateComputePipelines(context.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline->m_pipeline);
    vk.vkDestroyShaderModule(context.device(), module, nullptr);
    if (result != VK_SUCCESS) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("vkCreateComputePipelines failed: %1").arg(kisGpuVkResultString(result));
        }
        return nullptr;
    }

    return pipeline;
}

void KisGpuComputePipeline::dispatchRaw(VkCommandBuffer commandBuffer,
                                        const void *constants,
                                        quint32 constantsSize,
                                        quint32 groupsX,
                                        quint32 groupsY,
                                        quint32 groupsZ) const
{
    KIS_SAFE_ASSERT_RECOVER_RETURN(constantsSize == m_pushConstantSize);

    const KisGpuVulkanFunctions &vk = m_context.vk();
    vk.vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    if (constantsSize) {
        vk.vkCmdPushConstants(commandBuffer, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, constantsSize, constants);
    }
    vk.vkCmdDispatch(commandBuffer, groupsX, groupsY, groupsZ);
}
