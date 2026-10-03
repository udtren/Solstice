/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUVULKANFUNCTIONS_H
#define KISGPUVULKANFUNCTIONS_H

#include <QtGlobal>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <vulkan/vulkan.h>

#include <QString>

#include "kritagpu_export.h"

/**
 * Vulkan entry points used by the GPU engine.
 *
 * The Vulkan loader (vulkan-1.dll / libvulkan.so.1) is loaded at runtime so
 * that Krita still starts on machines without a Vulkan driver. Every function
 * the engine needs must be listed in one of the X-macro tables below.
 */

#define KIS_GPU_VK_GLOBAL_FUNCTIONS(X)                                                                                 \
    X(vkCreateInstance)                                                                                                \
    X(vkEnumerateInstanceVersion)                                                                                      \
    X(vkEnumerateInstanceExtensionProperties)                                                                          \
    X(vkEnumerateInstanceLayerProperties)

#define KIS_GPU_VK_INSTANCE_FUNCTIONS(X)                                                                               \
    X(vkDestroyInstance)                                                                                               \
    X(vkEnumeratePhysicalDevices)                                                                                      \
    X(vkGetPhysicalDeviceProperties)                                                                                   \
    X(vkGetPhysicalDeviceProperties2)                                                                                  \
    X(vkGetPhysicalDeviceFeatures2)                                                                                    \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                                        \
    X(vkGetPhysicalDeviceMemoryProperties)                                                                             \
    X(vkEnumerateDeviceExtensionProperties)                                                                            \
    X(vkCreateDevice)                                                                                                  \
    X(vkGetDeviceProcAddr)

#define KIS_GPU_VK_DEVICE_FUNCTIONS(X)                                                                                 \
    X(vkDestroyDevice)                                                                                                 \
    X(vkGetDeviceQueue)                                                                                                \
    X(vkDeviceWaitIdle)                                                                                                \
    X(vkQueueSubmit2)                                                                                                  \
    X(vkCreateBuffer)                                                                                                  \
    X(vkDestroyBuffer)                                                                                                 \
    X(vkGetBufferMemoryRequirements)                                                                                   \
    X(vkBindBufferMemory)                                                                                              \
    X(vkGetBufferDeviceAddress)                                                                                        \
    X(vkCreateImage)                                                                                                   \
    X(vkDestroyImage)                                                                                                  \
    X(vkGetImageMemoryRequirements)                                                                                    \
    X(vkBindImageMemory)                                                                                               \
    X(vkAllocateMemory)                                                                                                \
    X(vkFreeMemory)                                                                                                    \
    X(vkMapMemory)                                                                                                     \
    X(vkUnmapMemory)                                                                                                   \
    X(vkCreateShaderModule)                                                                                            \
    X(vkDestroyShaderModule)                                                                                           \
    X(vkCreatePipelineLayout)                                                                                          \
    X(vkDestroyPipelineLayout)                                                                                         \
    X(vkCreateComputePipelines)                                                                                        \
    X(vkDestroyPipeline)                                                                                               \
    X(vkCreateCommandPool)                                                                                             \
    X(vkDestroyCommandPool)                                                                                            \
    X(vkResetCommandPool)                                                                                              \
    X(vkAllocateCommandBuffers)                                                                                        \
    X(vkBeginCommandBuffer)                                                                                            \
    X(vkEndCommandBuffer)                                                                                              \
    X(vkCmdBindPipeline)                                                                                               \
    X(vkCmdPushConstants)                                                                                              \
    X(vkCmdDispatch)                                                                                                   \
    X(vkCmdPipelineBarrier2)                                                                                           \
    X(vkCmdCopyBuffer)                                                                                                 \
    X(vkCmdCopyBufferToImage)                                                                                          \
    X(vkCmdCopyImageToBuffer)                                                                                          \
    X(vkCmdFillBuffer)                                                                                                 \
    X(vkCmdWriteTimestamp2)                                                                                            \
    X(vkCmdResetQueryPool)                                                                                             \
    X(vkCreateQueryPool)                                                                                               \
    X(vkDestroyQueryPool)                                                                                              \
    X(vkGetQueryPoolResults)                                                                                           \
    X(vkCreateSemaphore)                                                                                               \
    X(vkDestroySemaphore)                                                                                              \
    X(vkWaitSemaphores)                                                                                                \
    X(vkGetSemaphoreCounterValue)

/**
 * Device functions of optional extensions. They are stored untyped so that
 * this header does not need the platform headers (<windows.h>); users cast
 * them to the PFN type of the platform header. nullptr if not enabled.
 */
#define KIS_GPU_VK_DEVICE_OPTIONAL_FUNCTIONS(X)                                                                        \
    X(vkGetMemoryWin32HandleKHR)                                                                                       \
    X(vkGetSemaphoreWin32HandleKHR)

struct KRITAGPU_EXPORT KisGpuVulkanFunctions {
#define KIS_GPU_DECLARE_VK_FUNCTION(name) PFN_##name name = nullptr;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    KIS_GPU_VK_GLOBAL_FUNCTIONS(KIS_GPU_DECLARE_VK_FUNCTION)
    KIS_GPU_VK_INSTANCE_FUNCTIONS(KIS_GPU_DECLARE_VK_FUNCTION)
    KIS_GPU_VK_DEVICE_FUNCTIONS(KIS_GPU_DECLARE_VK_FUNCTION)
#undef KIS_GPU_DECLARE_VK_FUNCTION
#define KIS_GPU_DECLARE_OPTIONAL_VK_FUNCTION(name) PFN_vkVoidFunction name = nullptr;
    KIS_GPU_VK_DEVICE_OPTIONAL_FUNCTIONS(KIS_GPU_DECLARE_OPTIONAL_VK_FUNCTION)
#undef KIS_GPU_DECLARE_OPTIONAL_VK_FUNCTION

    /**
     * Load the system Vulkan loader and resolve the global entry points.
     * The loader library stays loaded for the lifetime of the process.
     */
    bool loadGlobal(QString *errorMessage);
    bool loadInstance(VkInstance instance, QString *errorMessage);
    bool loadDevice(VkDevice device, QString *errorMessage);
};

/// Human-readable name of a VkResult for diagnostics.
KRITAGPU_EXPORT QString kisGpuVkResultString(VkResult result);

#endif // KISGPUVULKANFUNCTIONS_H
