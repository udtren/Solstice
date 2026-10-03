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
#include "KisGpuGLSharedBuffer.h"

#include "KisGpuCommandList.h"
#include "KisGpuContext.h"
#include "KisGpuGLInterop_p.h"

#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QScopeGuard>

#include <atomic>
#include <vector>

#include <kis_debug.h>

using namespace KisGpuGLInterop;

namespace
{
constexpr GLenum GL_PIXEL_UNPACK_BUFFER_ = 0x88EC;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER_BINDING_ = 0x88EF;

enum class CycleState {
    /// Never written, or GL's release has been waited for.
    Idle,
    /// Written by Vulkan; GL has not acquired it.
    WrittenByVulkan,
    /// GL acquired it and has not released it yet.
    InUseByGL,
    /// GL released it; the next Vulkan write must wait for that.
    ReleasedByGL,
};

std::atomic<int> s_injectedImportFailures{0};
std::atomic<int> s_injectedUnsharedBuffers{0};
} // namespace

struct KisGpuGLSharedBuffer::Private {
    explicit Private(KisGpuContext &_context)
        : context(_context)
    {
    }

    KisGpuContext &context;
    VkDeviceSize size = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize allocationSize = 0;
    VkDeviceAddress address = 0;
    VkSemaphore vulkanDone = VK_NULL_HANDLE;
    VkSemaphore glDone = VK_NULL_HANDLE;
    // Keep the exported NT handles alive with the imported objects. Closing
    // them immediately after glImport* fails with Qt's shared GL contexts
    // on NVIDIA 596.86, consistent with deferred handle processing.
    HANDLE memoryHandle = nullptr;
    HANDLE vulkanDoneHandle = nullptr;
    HANDLE glDoneHandle = nullptr;

    mutable QMutex mutex;
    CycleState state = CycleState::Idle;

    // GL side (created lazily in the current context's share group)
    bool glInitialized = false;
    bool glFailed = false;
    GLInteropFunctions gl;
    GLuint glMemoryObject = 0;
    GLuint glBufferName = 0;
    GLuint glVulkanDone = 0;
    GLuint glGLDone = 0;
    QPointer<QOpenGLContextGroup> glShareGroup;

    VkSemaphore createExportableSemaphore();
    HANDLE exportSemaphore(VkSemaphore semaphore);
    bool initializeGL();
};

KisGpuGLSharedBuffer::KisGpuGLSharedBuffer(KisGpuContext &context)
    : d(new Private(context))
{
}

KisGpuGLSharedBuffer::~KisGpuGLSharedBuffer()
{
    // Imported instances must first drain their work and delete their GL
    // objects with prepareForDestruction() in the importing share group.
    const KisGpuVulkanFunctions &vk = d->context.vk();
    d->context.waitIdle();
    if (d->memoryHandle)
        CloseHandle(d->memoryHandle);
    if (d->vulkanDoneHandle)
        CloseHandle(d->vulkanDoneHandle);
    if (d->glDoneHandle)
        CloseHandle(d->glDoneHandle);
    if (d->vulkanDone) {
        vk.vkDestroySemaphore(d->context.device(), d->vulkanDone, nullptr);
    }
    if (d->glDone) {
        vk.vkDestroySemaphore(d->context.device(), d->glDone, nullptr);
    }
    if (d->buffer) {
        vk.vkDestroyBuffer(d->context.device(), d->buffer, nullptr);
    }
    if (d->memory) {
        vk.vkFreeMemory(d->context.device(), d->memory, nullptr);
    }
}

bool KisGpuGLSharedBuffer::glInteropSupported(KisGpuContext &context, QOpenGLContext *glContext, QString *reason)
{
    auto fail = [reason](const QString &message) {
        if (reason) {
            *reason = message;
        }
        return false;
    };
    if (!context.deviceInfo().supportsExternalMemoryInterop) {
        return fail(QStringLiteral("the Vulkan device does not support Win32 external memory"));
    }
    if (!glContext || QOpenGLContext::currentContext() != glContext) {
        return fail(QStringLiteral("the OpenGL context is not current"));
    }
    if (glContext->isOpenGLES()) {
        return fail(QStringLiteral("OpenGL ES/ANGLE cannot import Vulkan memory"));
    }
    GLInteropFunctions gl;
    if (!gl.load(glContext, reason)) {
        return false;
    }
    if (!gl.sameDevice(glContext, context.deviceInfo().deviceUuid)) {
        return fail(QStringLiteral("OpenGL runs on a different GPU than Vulkan"));
    }
    return true;
}

bool KisGpuGLSharedBuffer::testGLInterop(KisGpuContext &context, QString *reason)
{
    QOpenGLContext *glContext = QOpenGLContext::currentContext();
    if (!glInteropSupported(context, glContext, reason)) {
        return false;
    }
    auto fail = [reason](const QString &message) {
        if (reason) {
            *reason = message;
        }
        return false;
    };
    using GetBufferSubData = void(APIENTRY *)(GLenum, GLintptr, GLsizeiptr, void *);
    using CopyBufferSubData = void(APIENTRY *)(GLenum, GLenum, GLintptr, GLintptr, GLsizeiptr);
    const auto getBufferSubData = reinterpret_cast<GetBufferSubData>(glContext->getProcAddress("glGetBufferSubData"));
    const auto copyBufferSubData =
        reinterpret_cast<CopyBufferSubData>(glContext->getProcAddress("glCopyBufferSubData"));
    if (!getBufferSubData || !copyBufferSubData) {
        return fail(QStringLiteral("interop self-test: GL buffer read/copy unavailable"));
    }

    constexpr VkDeviceSize probeSize = 1 << 20;
    auto shared = create(context, probeSize, reason);
    if (!shared) {
        return false;
    }
    QOpenGLFunctions *f = glContext->functions();
    GLint previousBuffer = 0;
    f->glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING_, &previousBuffer);
    constexpr GLenum copyRead = 0x8F36;
    constexpr GLenum copyWrite = 0x8F37;
    GLint previousRead = 0;
    GLint previousWrite = 0;
    f->glGetIntegerv(copyRead, &previousRead);
    f->glGetIntegerv(copyWrite, &previousWrite);
    GLuint readback = 0;
    f->glGenBuffers(1, &readback);
    f->glBindBuffer(copyWrite, readback);
    f->glBufferData(copyWrite, GLsizeiptr(probeSize), nullptr, GL_STREAM_READ);
    // Unlike the persistent canvas pool, this temporary probe owns and deletes
    // its GL objects here, while their context is still current.
    auto cleanup = qScopeGuard([&]() {
        f->glFinish();
        Private *d = shared->d.get();
        if (d->glBufferName)
            f->glDeleteBuffers(1, &d->glBufferName);
        if (d->glMemoryObject)
            d->gl.glDeleteMemoryObjectsEXT(1, &d->glMemoryObject);
        if (d->glVulkanDone)
            d->gl.glDeleteSemaphoresEXT(1, &d->glVulkanDone);
        if (d->glGLDone)
            d->gl.glDeleteSemaphoresEXT(1, &d->glGLDone);
        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER_, GLuint(previousBuffer));
        f->glDeleteBuffers(1, &readback);
        f->glBindBuffer(copyRead, GLuint(previousRead));
        f->glBindBuffer(copyWrite, GLuint(previousWrite));
    });

    KisGpuCommandList commands(context);
    if (!commands.isValid()) {
        return fail(QStringLiteral("interop self-test: command allocation failed"));
    }
    std::vector<quint32> actual(size_t(probeSize / sizeof(quint32)));
    // 1.0f then 0.5f: detect zero/stale data, including reuse after a GL release.
    for (quint32 expected : {0x3f800000u, 0x3f000000u}) {
        commands.begin();
        context.vk().vkCmdFillBuffer(commands.commandBuffer(), shared->vulkanBuffer(), 0, probeSize, expected);
        const quint64 submitted = commands.submit(shared->vulkanWriteWaits(), {shared->vulkanDoneSignal()});
        shared->finishVulkanWrite(submitted != 0);
        if (!submitted || !commands.wait()) {
            return fail(QStringLiteral("interop self-test: Vulkan fill failed"));
        }
        // Import lazily AFTER Vulkan writes, just like a real canvas update.
        if (!shared->glAcquire()) {
            return fail(QStringLiteral("interop self-test: GL import failed"));
        }
        // Read through a GPU copy: direct GetBufferSubData on imported memory
        // can return a stale CPU shadow on NVIDIA after an external write.
        f->glBindBuffer(copyRead, shared->glBuffer());
        copyBufferSubData(copyRead, copyWrite, 0, 0, GLsizeiptr(probeSize));
        getBufferSubData(copyWrite, 0, GLsizeiptr(probeSize), actual.data());
        const GLenum error = f->glGetError();
        shared->glRelease();
        if (error != GL_NO_ERROR) {
            return fail(QStringLiteral("interop self-test: GL read error 0x%1").arg(error, 0, 16));
        }
        for (size_t i = 0; i < actual.size(); ++i) {
            if (actual[i] != expected) {
                return fail(QStringLiteral("interop self-test: byte %1 expected 0x%2, GL read 0x%3 (GL error 0)")
                                .arg(i * sizeof(quint32))
                                .arg(expected, 0, 16)
                                .arg(actual[i], 0, 16));
            }
        }
    }
    // Consume the last GL signal before destroying the Vulkan semaphore.
    commands.begin();
    const bool submitted = commands.submit(shared->vulkanWriteWaits()) != 0;
    shared->finishVulkanRead(submitted);
    if (!submitted || !commands.wait()) {
        return fail(QStringLiteral("interop self-test: release wait failed"));
    }
    return true;
}

std::unique_ptr<KisGpuGLSharedBuffer>
KisGpuGLSharedBuffer::create(KisGpuContext &context, VkDeviceSize size, QString *errorMessage)
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

    std::unique_ptr<KisGpuGLSharedBuffer> shared(new KisGpuGLSharedBuffer(context));
    Private *d = shared->d.get();
    const KisGpuVulkanFunctions &vk = context.vk();
    d->size = size;

    VkExternalMemoryBufferCreateInfo externalInfo{};
    externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = &externalInfo;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vk.vkCreateBuffer(context.device(), &bufferInfo, nullptr, &d->buffer);
    if (result != VK_SUCCESS) {
        return fail(QStringLiteral("vkCreateBuffer (exportable) failed: %1").arg(kisGpuVkResultString(result)));
    }

    VkMemoryRequirements requirements{};
    vk.vkGetBufferMemoryRequirements(context.device(), d->buffer, &requirements);
    const int memoryType = context.findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType < 0) {
        return fail(QStringLiteral("No device-local memory type for the shared buffer"));
    }

    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryDedicatedAllocateInfo dedicatedInfo{};
    dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedInfo.pNext = &flagsInfo;
    dedicatedInfo.buffer = d->buffer;
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
        return fail(
            QStringLiteral("vkAllocateMemory (exportable buffer) failed: %1").arg(kisGpuVkResultString(result)));
    }
    d->allocationSize = requirements.size;
    vk.vkBindBufferMemory(context.device(), d->buffer, d->memory, 0);

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = d->buffer;
    d->address = vk.vkGetBufferDeviceAddress(context.device(), &addressInfo);

    d->vulkanDone = d->createExportableSemaphore();
    d->glDone = d->createExportableSemaphore();
    if (!d->vulkanDone || !d->glDone) {
        return fail(QStringLiteral("Could not create exportable semaphores"));
    }
    return shared;
}

VkSemaphore KisGpuGLSharedBuffer::Private::createExportableSemaphore()
{
    VkExportSemaphoreCreateInfo exportInfo{};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkSemaphoreCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &exportInfo;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    if (context.vk().vkCreateSemaphore(context.device(), &createInfo, nullptr, &semaphore) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return semaphore;
}

HANDLE KisGpuGLSharedBuffer::Private::exportSemaphore(VkSemaphore semaphore)
{
    const auto getHandle =
        reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(context.vk().vkGetSemaphoreWin32HandleKHR);
    if (!getHandle) {
        return nullptr;
    }
    VkSemaphoreGetWin32HandleInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
    info.semaphore = semaphore;
    info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    HANDLE handle = nullptr;
    return getHandle(context.device(), &info, &handle) == VK_SUCCESS ? handle : nullptr;
}

bool KisGpuGLSharedBuffer::Private::initializeGL()
{
    if (glInitialized || glFailed) {
        return glInitialized;
    }
    glFailed = true;

    QOpenGLContext *glContext = QOpenGLContext::currentContext();
    QString error;
    if (!glContext || !gl.load(glContext, &error)) {
        warnKrita << "GPU engine: cannot import a shared buffer into OpenGL:" << error;
        return false;
    }

    const auto getMemoryHandle =
        reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(context.vk().vkGetMemoryWin32HandleKHR);
    if (!getMemoryHandle) {
        return false;
    }
    QOpenGLFunctions *f = glContext->functions();
    // Do not attribute an earlier canvas operation's error to this import.
    glShareGroup = glContext->shareGroup();
    while (const GLenum error = f->glGetError()) {
        warnKrita << "GPU engine: GL error before shared buffer import" << Qt::hex << error;
    }
    auto checkError = [f](const char *stage) {
        const GLenum error = f->glGetError();
        if (error != GL_NO_ERROR) {
            warnKrita << "GPU engine: shared buffer" << stage << "failed, GL error" << Qt::hex << error;
        }
        return error == GL_NO_ERROR;
    };
    VkMemoryGetWin32HandleInfoKHR handleInfo{};
    handleInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    handleInfo.memory = memory;
    handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    if (getMemoryHandle(context.device(), &handleInfo, &memoryHandle) != VK_SUCCESS) {
        return false;
    }

    // Importing does not transfer ownership of Win32 handles. Our destructor
    // closes them; do not close them before the driver completes the import.
    gl.glCreateMemoryObjectsEXT(1, &glMemoryObject);
    const GLint dedicated = GL_TRUE;
    gl.glMemoryObjectParameterivEXT(glMemoryObject, GL_DEDICATED_MEMORY_OBJECT_EXT_, &dedicated);
    gl.glImportMemoryWin32HandleEXT(glMemoryObject, allocationSize, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT_, memoryHandle);
    if (!checkError("memory import")) {
        return false;
    }

    GLint previousBuffer = 0;
    f->glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING_, &previousBuffer);
    f->glGenBuffers(1, &glBufferName);
    f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER_, glBufferName);
    if (s_injectedUnsharedBuffers.load() > 0 && s_injectedUnsharedBuffers.fetch_sub(1) > 0) {
        const std::vector<quint8> zeros(size_t(size), 0);
        f->glBufferData(GL_PIXEL_UNPACK_BUFFER_, GLsizeiptr(size), zeros.data(), GL_STATIC_DRAW);
    } else {
        gl.glBufferStorageMemEXT(GL_PIXEL_UNPACK_BUFFER_, GLsizeiptr(size), glMemoryObject, 0);
    }
    f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER_, GLuint(previousBuffer));
    if (!checkError("storage binding")) {
        return false;
    }

    vulkanDoneHandle = exportSemaphore(vulkanDone);
    glDoneHandle = exportSemaphore(glDone);
    if (!vulkanDoneHandle || !glDoneHandle) {
        return false;
    }
    gl.glGenSemaphoresEXT(1, &glVulkanDone);
    gl.glImportSemaphoreWin32HandleEXT(glVulkanDone, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT_, vulkanDoneHandle);
    gl.glGenSemaphoresEXT(1, &glGLDone);
    gl.glImportSemaphoreWin32HandleEXT(glGLDone, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT_, glDoneHandle);

    const GLenum glError = f->glGetError();
    if (glError != GL_NO_ERROR) {
        warnKrita << "GPU engine: OpenGL import of a shared buffer failed, GL error" << Qt::hex << glError;
        return false;
    }

    glFailed = false;
    glInitialized = true;
    return true;
}

VkDeviceSize KisGpuGLSharedBuffer::size() const
{
    return d->size;
}

VkBuffer KisGpuGLSharedBuffer::vulkanBuffer() const
{
    return d->buffer;
}

VkDeviceAddress KisGpuGLSharedBuffer::deviceAddress() const
{
    return d->address;
}

QVector<VkSemaphoreSubmitInfo> KisGpuGLSharedBuffer::vulkanWriteWaits() const
{
    QMutexLocker locker(&d->mutex);
    VkSemaphoreSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    switch (d->state) {
    case CycleState::Idle:
        return {};
    case CycleState::ReleasedByGL:
        // Wait until GL has finished reading the previous content.
        info.semaphore = d->glDone;
        return {info};
    case CycleState::WrittenByVulkan:
        // The previous content was never consumed by GL: wait for our own
        // signal so that the binary semaphore can be signaled again.
        info.semaphore = d->vulkanDone;
        return {info};
    case CycleState::InUseByGL:
        KIS_SAFE_ASSERT_RECOVER_NOOP(false && "shared buffer reused while GL reads it");
        return {};
    }
    return {};
}

void KisGpuGLSharedBuffer::finishVulkanWrite(bool submitted)
{
    QMutexLocker locker(&d->mutex);
    if (submitted) {
        // The waits returned by vulkanWriteWaits() were consumed and the
        // done semaphore was signaled.
        d->state = CycleState::WrittenByVulkan;
    }
}

void KisGpuGLSharedBuffer::finishVulkanRead(bool submitted)
{
    QMutexLocker locker(&d->mutex);
    KIS_SAFE_ASSERT_RECOVER_RETURN(d->state != CycleState::InUseByGL);
    if (submitted) {
        // The waits returned by vulkanWriteWaits() were consumed.
        d->state = CycleState::Idle;
    }
}

bool KisGpuGLSharedBuffer::isPendingForGL() const
{
    QMutexLocker locker(&d->mutex);
    return d->state == CycleState::WrittenByVulkan || d->state == CycleState::InUseByGL;
}

VkSemaphoreSubmitInfo KisGpuGLSharedBuffer::vulkanDoneSignal() const
{
    VkSemaphoreSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    info.semaphore = d->vulkanDone;
    info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    return info;
}

quint32 KisGpuGLSharedBuffer::glBuffer()
{
    return d->initializeGL() ? d->glBufferName : 0;
}

bool KisGpuGLSharedBuffer::glAcquire()
{
    QMutexLocker locker(&d->mutex);
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(d->state == CycleState::WrittenByVulkan, false);
    if (s_injectedImportFailures.load() > 0 && s_injectedImportFailures.fetch_sub(1) > 0) {
        return false;
    }
    if (!d->initializeGL()) {
        return false;
    }
    const GLuint buffer = d->glBufferName;
    d->gl.glWaitSemaphoreEXT(d->glVulkanDone, 1, &buffer, 0, nullptr, nullptr);
    d->state = CycleState::InUseByGL;
    return true;
}

void KisGpuGLSharedBuffer::injectGLImportFailuresForTesting(int count)
{
    s_injectedImportFailures.store(count);
}

void KisGpuGLSharedBuffer::injectUnsharedGLBuffersForTesting(int count)
{
    s_injectedUnsharedBuffers.store(count);
}

void KisGpuGLSharedBuffer::glRelease()
{
    QMutexLocker locker(&d->mutex);
    KIS_SAFE_ASSERT_RECOVER_RETURN(d->state == CycleState::InUseByGL);
    const GLuint buffer = d->glBufferName;
    d->gl.glSignalSemaphoreEXT(d->glGLDone, 1, &buffer, 0, nullptr, nullptr);
    QOpenGLContext::currentContext()->functions()->glFlush();
    d->state = CycleState::ReleasedByGL;
}

bool KisGpuGLSharedBuffer::prepareForDestruction()
{
    // Only unused pool entries call this; no thread can begin another cycle.
    if (d->state == CycleState::InUseByGL) {
        return false;
    }
    const bool hasGL = d->glBufferName || d->glMemoryObject || d->glVulkanDone || d->glGLDone;
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (hasGL && (!current || !d->glShareGroup || current->shareGroup() != d->glShareGroup)) {
        return false;
    }
    // glRelease flushes the producing context. A Vulkan wait therefore also
    // covers reads made in a different context of the same share group.
    KisGpuCommandList commands(d->context);
    if (!commands.isValid()) {
        return false;
    }
    commands.begin();
    const bool submitted = commands.submit(vulkanWriteWaits()) != 0;
    finishVulkanRead(submitted);
    if (!submitted || !commands.wait()) {
        return false;
    }
    if (hasGL) {
        auto *f = current->functions();
        if (d->glBufferName)
            f->glDeleteBuffers(1, &d->glBufferName);
        if (d->glMemoryObject)
            d->gl.glDeleteMemoryObjectsEXT(1, &d->glMemoryObject);
        if (d->glVulkanDone)
            d->gl.glDeleteSemaphoresEXT(1, &d->glVulkanDone);
        if (d->glGLDone)
            d->gl.glDeleteSemaphoresEXT(1, &d->glGLDone);
        d->glBufferName = d->glMemoryObject = d->glVulkanDone = d->glGLDone = 0;
        // Finish deferred GL deletion before the destructor closes NT handles.
        f->glFinish();
    }
    return true;
}
