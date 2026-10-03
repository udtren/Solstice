/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include "KisGpuGLSharedImage.h"

#include "KisGpuBuffer.h"
#include "KisGpuCommandList.h"
#include "KisGpuContext.h"
#include "KisGpuGLInterop_p.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include <algorithm>
#include <iterator>
#include <vector>

#include <kis_debug.h>

using namespace KisGpuGLInterop;

struct KisGpuGLSharedImage::Private {
    Private(KisGpuContext &_context)
        : context(_context)
    {
    }

    KisGpuContext &context;
    QOpenGLContext *glContext = nullptr;
    GLInteropFunctions gl;

    QSize size;
    KisGpuTileFormat format = KisGpuTileFormat::RGBA16F;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkSemaphore vulkanDone = VK_NULL_HANDLE;
    VkSemaphore glDone = VK_NULL_HANDLE;
    bool glDonePending = false;

    GLuint glMemoryObject = 0;
    GLuint glTexture = 0;
    GLuint glVulkanDone = 0;
    GLuint glGLDone = 0;

    VkSemaphore createExportableSemaphore(QString *errorMessage);
    bool importSemaphore(VkSemaphore semaphore, GLuint *glSemaphore, QString *errorMessage);
    bool transitionToGeneral(QString *errorMessage);
};

KisGpuGLSharedImage::KisGpuGLSharedImage(KisGpuContext &context)
    : d(new Private(context))
{
}

KisGpuGLSharedImage::~KisGpuGLSharedImage()
{
    if (d->glContext && QOpenGLContext::currentContext() == d->glContext) {
        QOpenGLFunctions *f = d->glContext->functions();
        // Make sure GL no longer uses the memory before Vulkan frees it.
        f->glFinish();
        if (d->glTexture) {
            f->glDeleteTextures(1, &d->glTexture);
        }
        if (d->glMemoryObject) {
            d->gl.glDeleteMemoryObjectsEXT(1, &d->glMemoryObject);
        }
        if (d->glVulkanDone) {
            d->gl.glDeleteSemaphoresEXT(1, &d->glVulkanDone);
        }
        if (d->glGLDone) {
            d->gl.glDeleteSemaphoresEXT(1, &d->glGLDone);
        }
    } else if (d->glTexture) {
        warnKrita << "GPU engine: shared image destroyed without its GL context; GL objects leak";
    }

    const KisGpuVulkanFunctions &vk = d->context.vk();
    d->context.waitIdle();
    if (d->vulkanDone) {
        vk.vkDestroySemaphore(d->context.device(), d->vulkanDone, nullptr);
    }
    if (d->glDone) {
        vk.vkDestroySemaphore(d->context.device(), d->glDone, nullptr);
    }
    if (d->image) {
        vk.vkDestroyImage(d->context.device(), d->image, nullptr);
    }
    if (d->memory) {
        vk.vkFreeMemory(d->context.device(), d->memory, nullptr);
    }
}

std::unique_ptr<KisGpuGLSharedImage> KisGpuGLSharedImage::create(KisGpuContext &context,
                                                                 QOpenGLContext *glContext,
                                                                 const QSize &size,
                                                                 KisGpuTileFormat format,
                                                                 QString *errorMessage)
{
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return nullptr;
    };

    if (!context.deviceInfo().supportsExternalMemoryInterop) {
        return fail(QStringLiteral("The Vulkan device does not support Win32 external memory"));
    }
    if (!glContext || QOpenGLContext::currentContext() != glContext) {
        return fail(QStringLiteral("The OpenGL context must be current"));
    }
    if (glContext->isOpenGLES()) {
        return fail(QStringLiteral("OpenGL ES/ANGLE cannot import Vulkan memory; desktop OpenGL is required"));
    }

    std::unique_ptr<KisGpuGLSharedImage> shared(new KisGpuGLSharedImage(context));
    Private *d = shared->d.get();
    d->glContext = glContext;
    d->size = size;
    d->format = format;

    if (!d->gl.load(glContext, errorMessage)) {
        return nullptr;
    }

    // The GL implementation must run on the same GPU as the Vulkan device.
    const bool sameDevice = d->gl.sameDevice(glContext, context.deviceInfo().deviceUuid);
    if (!sameDevice) {
        return fail(QStringLiteral("The OpenGL context runs on a different GPU than the Vulkan device"));
    }

    const KisGpuVulkanFunctions &vk = context.vk();
    const auto getMemoryHandle = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(vk.vkGetMemoryWin32HandleKHR);
    if (!getMemoryHandle) {
        return fail(QStringLiteral("vkGetMemoryWin32HandleKHR is not available"));
    }

    VkExternalMemoryImageCreateInfo externalInfo{};
    externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = &externalInfo;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format =
        format == KisGpuTileFormat::RGBA32F ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {quint32(size.width()), quint32(size.height()), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_STORAGE_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vk.vkCreateImage(context.device(), &imageInfo, nullptr, &d->image);
    if (result != VK_SUCCESS) {
        return fail(QStringLiteral("vkCreateImage failed: %1").arg(kisGpuVkResultString(result)));
    }

    VkMemoryRequirements requirements{};
    vk.vkGetImageMemoryRequirements(context.device(), d->image, &requirements);
    const int memoryType = context.findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (memoryType < 0) {
        return fail(QStringLiteral("No device-local memory type for the shared image"));
    }

    VkMemoryDedicatedAllocateInfo dedicatedInfo{};
    dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedInfo.image = d->image;
    VkExportMemoryAllocateInfo exportInfo{};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    exportInfo.pNext = &dedicatedInfo;
    exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = &exportInfo;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = quint32(memoryType);

    result = vk.vkAllocateMemory(context.device(), &allocateInfo, nullptr, &d->memory);
    if (result != VK_SUCCESS) {
        return fail(QStringLiteral("vkAllocateMemory (exportable) failed: %1").arg(kisGpuVkResultString(result)));
    }
    vk.vkBindImageMemory(context.device(), d->image, d->memory, 0);

    VkMemoryGetWin32HandleInfoKHR handleInfo{};
    handleInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    handleInfo.memory = d->memory;
    handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    HANDLE memoryHandle = nullptr;
    result = getMemoryHandle(context.device(), &handleInfo, &memoryHandle);
    if (result != VK_SUCCESS) {
        return fail(QStringLiteral("vkGetMemoryWin32HandleKHR failed: %1").arg(kisGpuVkResultString(result)));
    }

    // Importing does not transfer ownership of a Win32 handle; close it afterwards.
    d->gl.glCreateMemoryObjectsEXT(1, &d->glMemoryObject);
    const GLint dedicated = GL_TRUE;
    d->gl.glMemoryObjectParameterivEXT(d->glMemoryObject, GL_DEDICATED_MEMORY_OBJECT_EXT_, &dedicated);
    d->gl.glImportMemoryWin32HandleEXT(d->glMemoryObject,
                                       requirements.size,
                                       GL_HANDLE_TYPE_OPAQUE_WIN32_EXT_,
                                       memoryHandle);
    CloseHandle(memoryHandle);

    QOpenGLFunctions *f = glContext->functions();
    f->glGenTextures(1, &d->glTexture);
    f->glBindTexture(GL_TEXTURE_2D, d->glTexture);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT_, GL_OPTIMAL_TILING_EXT_);
    d->gl.glTexStorageMem2DEXT(GL_TEXTURE_2D,
                               1,
                               format == KisGpuTileFormat::RGBA32F ? GL_RGBA32F_ : GL_RGBA16F_,
                               size.width(),
                               size.height(),
                               d->glMemoryObject,
                               0);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    f->glBindTexture(GL_TEXTURE_2D, 0);

    const GLenum glError = f->glGetError();
    if (glError != GL_NO_ERROR) {
        return fail(QStringLiteral("OpenGL memory import failed (GL error 0x%1)").arg(glError, 0, 16));
    }

    d->vulkanDone = d->createExportableSemaphore(errorMessage);
    d->glDone = d->createExportableSemaphore(errorMessage);
    if (!d->vulkanDone || !d->glDone || !d->importSemaphore(d->vulkanDone, &d->glVulkanDone, errorMessage)
        || !d->importSemaphore(d->glDone, &d->glGLDone, errorMessage)) {
        return nullptr;
    }

    if (!d->transitionToGeneral(errorMessage)) {
        return nullptr;
    }
    return shared;
}

VkSemaphore KisGpuGLSharedImage::Private::createExportableSemaphore(QString *errorMessage)
{
    VkExportSemaphoreCreateInfo exportInfo{};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkSemaphoreCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &exportInfo;

    VkSemaphore semaphore = VK_NULL_HANDLE;
    const VkResult result = context.vk().vkCreateSemaphore(context.device(), &createInfo, nullptr, &semaphore);
    if (result != VK_SUCCESS) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Exportable semaphore creation failed: %1").arg(kisGpuVkResultString(result));
        }
        return VK_NULL_HANDLE;
    }
    return semaphore;
}

bool KisGpuGLSharedImage::Private::importSemaphore(VkSemaphore semaphore, GLuint *glSemaphore, QString *errorMessage)
{
    const auto getSemaphoreHandle =
        reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(context.vk().vkGetSemaphoreWin32HandleKHR);
    if (!getSemaphoreHandle) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("vkGetSemaphoreWin32HandleKHR is not available");
        }
        return false;
    }

    VkSemaphoreGetWin32HandleInfoKHR handleInfo{};
    handleInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
    handleInfo.semaphore = semaphore;
    handleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    HANDLE handle = nullptr;
    const VkResult result = getSemaphoreHandle(context.device(), &handleInfo, &handle);
    if (result != VK_SUCCESS) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("vkGetSemaphoreWin32HandleKHR failed: %1").arg(kisGpuVkResultString(result));
        }
        return false;
    }

    gl.glGenSemaphoresEXT(1, glSemaphore);
    gl.glImportSemaphoreWin32HandleEXT(*glSemaphore, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT_, handle);
    CloseHandle(handle);
    return true;
}

bool KisGpuGLSharedImage::Private::transitionToGeneral(QString *errorMessage)
{
    KisGpuCommandList commands(context);
    if (!commands.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not create a command list");
        }
        return false;
    }

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;

    context.vk().vkCmdPipelineBarrier2(commands.begin(), &dependency);
    return commands.submit() && commands.wait();
}

QSize KisGpuGLSharedImage::size() const
{
    return d->size;
}

VkImage KisGpuGLSharedImage::image() const
{
    return d->image;
}

quint32 KisGpuGLSharedImage::glTexture() const
{
    return d->glTexture;
}

void KisGpuGLSharedImage::recordCopyFromTiles(KisGpuCommandList &commands,
                                              const KisGpuTilePool &pool,
                                              const QVector<quint32> &slots,
                                              int tilesPerRow) const
{
    KIS_SAFE_ASSERT_RECOVER_RETURN(pool.format() == d->format);
    KIS_SAFE_ASSERT_RECOVER_RETURN(tilesPerRow > 0);

    const int tileSize = KisGpuTilePool::TileSize;

    // Group copies by source chunk buffer: one command per chunk.
    std::vector<std::pair<VkBuffer, std::vector<VkBufferImageCopy>>> groups;
    for (int i = 0; i < slots.size(); i++) {
        if (slots[i] == KisGpuTilePool::InvalidSlot) {
            continue;
        }
        const int x = (i % tilesPerRow) * tileSize;
        const int y = (i / tilesPerRow) * tileSize;
        if (x + tileSize > d->size.width() || y + tileSize > d->size.height()) {
            continue;
        }

        VkDeviceSize offset = 0;
        const KisGpuBuffer *chunk = pool.buffer(slots[i], &offset);
        KIS_SAFE_ASSERT_RECOVER(chunk)
        {
            continue;
        }

        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {x, y, 0};
        region.imageExtent = {quint32(tileSize), quint32(tileSize), 1};

        auto it = std::find_if(groups.begin(), groups.end(), [chunk](const auto &group) {
            return group.first == chunk->handle();
        });
        if (it == groups.end()) {
            groups.push_back({chunk->handle(), {}});
            it = std::prev(groups.end());
        }
        it->second.push_back(region);
    }

    const KisGpuVulkanFunctions &vk = d->context.vk();
    for (const auto &group : groups) {
        vk.vkCmdCopyBufferToImage(commands.commandBuffer(),
                                  group.first,
                                  d->image,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  quint32(group.second.size()),
                                  group.second.data());
    }
}

QVector<VkSemaphoreSubmitInfo> KisGpuGLSharedImage::takeGLDoneWait()
{
    if (!d->glDonePending) {
        return {};
    }
    d->glDonePending = false;

    VkSemaphoreSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    info.semaphore = d->glDone;
    info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    return {info};
}

VkSemaphoreSubmitInfo KisGpuGLSharedImage::vulkanDoneSignal() const
{
    VkSemaphoreSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    info.semaphore = d->vulkanDone;
    info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    return info;
}

void KisGpuGLSharedImage::glAcquire()
{
    KIS_SAFE_ASSERT_RECOVER_RETURN(QOpenGLContext::currentContext() == d->glContext);
    const GLuint texture = d->glTexture;
    const GLenum layout = GL_LAYOUT_GENERAL_EXT_;
    d->gl.glWaitSemaphoreEXT(d->glVulkanDone, 0, nullptr, 1, &texture, &layout);
}

void KisGpuGLSharedImage::glRelease()
{
    KIS_SAFE_ASSERT_RECOVER_RETURN(QOpenGLContext::currentContext() == d->glContext);
    const GLuint texture = d->glTexture;
    const GLenum layout = GL_LAYOUT_GENERAL_EXT_;
    d->gl.glSignalSemaphoreEXT(d->glGLDone, 0, nullptr, 1, &texture, &layout);
    d->glContext->functions()->glFlush();
    d->glDonePending = true;
}
