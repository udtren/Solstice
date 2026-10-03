/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuBuffer.h"

#include "KisGpuContext.h"

KisGpuBuffer::KisGpuBuffer(KisGpuContext &context)
    : m_context(context)
{
}

KisGpuBuffer::~KisGpuBuffer()
{
    const KisGpuVulkanFunctions &vk = m_context.vk();
    if (m_mapped) {
        vk.vkUnmapMemory(m_context.device(), m_memory);
    }
    if (m_buffer) {
        vk.vkDestroyBuffer(m_context.device(), m_buffer, nullptr);
    }
    if (m_memory) {
        vk.vkFreeMemory(m_context.device(), m_memory, nullptr);
    }
}

std::unique_ptr<KisGpuBuffer>
KisGpuBuffer::create(KisGpuContext &context, VkDeviceSize size, Location location, QString *errorMessage)
{
    const KisGpuVulkanFunctions &vk = context.vk();
    std::unique_ptr<KisGpuBuffer> buffer(new KisGpuBuffer(context));
    buffer->m_size = size;
    buffer->m_location = location;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vk.vkCreateBuffer(context.device(), &bufferInfo, nullptr, &buffer->m_buffer);
    if (result != VK_SUCCESS) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("vkCreateBuffer failed: %1").arg(kisGpuVkResultString(result));
        }
        return nullptr;
    }

    VkMemoryRequirements requirements{};
    vk.vkGetBufferMemoryRequirements(context.device(), buffer->m_buffer, &requirements);

    VkMemoryPropertyFlags required = 0;
    VkMemoryPropertyFlags preferred = 0;
    switch (location) {
    case Location::Device:
        required = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case Location::Upload:
        required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        preferred = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case Location::Readback:
        required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        preferred = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        break;
    }

    const int memoryType = context.findMemoryType(requirements.memoryTypeBits, required, preferred);
    if (memoryType < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No compatible memory type for a GPU buffer");
        }
        return nullptr;
    }
    buffer->m_deviceLocal = context.memoryTypeFlags(memoryType) & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = &flagsInfo;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = quint32(memoryType);

    result = vk.vkAllocateMemory(context.device(), &allocateInfo, nullptr, &buffer->m_memory);
    if (result != VK_SUCCESS) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("vkAllocateMemory(%1 bytes) failed: %2")
                                .arg(requirements.size)
                                .arg(kisGpuVkResultString(result));
        }
        return nullptr;
    }

    result = vk.vkBindBufferMemory(context.device(), buffer->m_buffer, buffer->m_memory, 0);
    if (result != VK_SUCCESS) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("vkBindBufferMemory failed: %1").arg(kisGpuVkResultString(result));
        }
        return nullptr;
    }

    if (location != Location::Device) {
        result = vk.vkMapMemory(context.device(), buffer->m_memory, 0, VK_WHOLE_SIZE, 0, &buffer->m_mapped);
        if (result != VK_SUCCESS) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("vkMapMemory failed: %1").arg(kisGpuVkResultString(result));
            }
            return nullptr;
        }
    }

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = buffer->m_buffer;
    buffer->m_address = vk.vkGetBufferDeviceAddress(context.device(), &addressInfo);

    return buffer;
}

VkBuffer KisGpuBuffer::handle() const
{
    return m_buffer;
}

VkDeviceSize KisGpuBuffer::size() const
{
    return m_size;
}

VkDeviceAddress KisGpuBuffer::deviceAddress() const
{
    return m_address;
}

KisGpuBuffer::Location KisGpuBuffer::location() const
{
    return m_location;
}

bool KisGpuBuffer::isDeviceLocal() const
{
    return m_deviceLocal;
}

void *KisGpuBuffer::mapped() const
{
    return m_mapped;
}
