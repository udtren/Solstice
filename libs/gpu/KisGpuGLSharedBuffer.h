/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUGLSHAREDBUFFER_H
#define KISGPUGLSHAREDBUFFER_H

#include <QString>
#include <QVector>

#include <memory>

#include "KisGpuVulkanFunctions.h"
#include "kritagpu_export.h"

class QOpenGLContext;
class KisGpuContext;

/**
 * A device-local Vulkan buffer whose memory is shared with an OpenGL buffer
 * object (VK_KHR_external_memory_win32 + GL_EXT_memory_object_win32), used
 * as a pixel unpack buffer: Vulkan writes pixels, GL uploads them into
 * textures with glTexSubImage2D without touching the CPU.
 *
 * One use cycle:
 *  1. Vulkan: a submission that writes the buffer waits for
 *     vulkanWriteWaits() and signals vulkanDoneSignal(); then
 *     finishVulkanWrite(submitted) records whether it was submitted;
 *  2. GL (context of the share group current): glAcquire(), read the
 *     buffer, glRelease().
 * If GL never consumes a cycle (the update was dropped), the next Vulkan
 * write waits for the Vulkan signal itself (vulkanWriteWaits() handles it).
 *
 * The GL side is created lazily by glBuffer() in the current context.
 */
class KRITAGPU_EXPORT KisGpuGLSharedBuffer
{
public:
    ~KisGpuGLSharedBuffer();

    KisGpuGLSharedBuffer(const KisGpuGLSharedBuffer &) = delete;
    KisGpuGLSharedBuffer &operator=(const KisGpuGLSharedBuffer &) = delete;

    static std::unique_ptr<KisGpuGLSharedBuffer>
    create(KisGpuContext &context, VkDeviceSize size, QString *errorMessage = nullptr);

    /**
     * Checks that @p glContext (current) can import Vulkan memory from
     * @p context: desktop OpenGL, the memory/semaphore extensions, and the
     * same physical GPU.
     */
    static bool glInteropSupported(KisGpuContext &context, QOpenGLContext *glContext, QString *reason = nullptr);

    /// Verify actual bytes through the production import/semaphore path in the current GL context.
    /// Runs synchronously; call once before enabling canvas uploads, not for every update.
    static bool testGLInterop(KisGpuContext &context, QString *reason = nullptr);

    VkDeviceSize size() const;
    VkBuffer vulkanBuffer() const;
    VkDeviceAddress deviceAddress() const;

    /// Waits the next Vulkan write of the buffer must perform.
    QVector<VkSemaphoreSubmitInfo> vulkanWriteWaits() const;
    /// Signal of the Vulkan write; GL waits for it in glAcquire().
    VkSemaphoreSubmitInfo vulkanDoneSignal() const;
    /// Commits the cycle state after the write's submission (or its failure).
    void finishVulkanWrite(bool submitted);
    /**
     * Commits the cycle state after a Vulkan submission that read the
     * content instead of GL (it waited for vulkanWriteWaits() and signaled
     * nothing): the buffer is idle again if it was submitted.
     */
    void finishVulkanRead(bool submitted);
    /// True while GL may still need the content (written, not yet released).
    bool isPendingForGL() const;

    /// GL buffer name in the current context's share group (imported on first use); 0 on failure.
    quint32 glBuffer();
    /**
     * Makes GL wait for the Vulkan write. False if the buffer cannot be
     * imported into GL: the state does not change and GL must not read it.
     */
    bool glAcquire();
    void glRelease();

    /// Exclusive owner, no future users: drain Vulkan/GL work and delete GL objects.
    /// Requires the importing share group current if GL objects exist. False means
    /// keep the object alive and retry later; never destroy it after a failed drain.
    bool prepareForDestruction();

    /// Tests: the next @p count glAcquire() calls fail as if GL could not import the buffer.
    static void injectGLImportFailuresForTesting(int count);
    /// Tests: imports appear successful but use unrelated, zero-filled GL storage.
    static void injectUnsharedGLBuffersForTesting(int count);

private:
    explicit KisGpuGLSharedBuffer(KisGpuContext &context);

    struct Private;
    std::unique_ptr<Private> d;
};

#endif // KISGPUGLSHAREDBUFFER_H
