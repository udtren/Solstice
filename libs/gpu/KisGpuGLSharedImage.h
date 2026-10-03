/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUGLSHAREDIMAGE_H
#define KISGPUGLSHAREDIMAGE_H

#include <QSize>
#include <QString>
#include <QVector>

#include <memory>

#include "KisGpuTilePool.h"
#include "KisGpuVulkanFunctions.h"
#include "kritagpu_export.h"

class QOpenGLContext;
class KisGpuContext;
class KisGpuCommandList;

/**
 * A Vulkan image whose memory is shared with an OpenGL texture
 * (VK_KHR_external_memory_win32 + GL_EXT_memory_object_win32).
 *
 * This is the bridge between GPU-resident image data and Krita's existing
 * OpenGL canvas: the Qt build has no Vulkan support, so the canvas keeps
 * rendering with desktop OpenGL and samples textures that Vulkan writes.
 *
 * Synchronization uses two exported binary semaphores:
 *  - "Vulkan done": signaled by a Vulkan submission (vulkanDoneSignal()),
 *    waited for by GL (glAcquire()) before sampling;
 *  - "GL done": signaled by GL (glRelease()), waited for by the next Vulkan
 *    submission that writes the image (glDoneWait()).
 * The image stays in VK_IMAGE_LAYOUT_GENERAL / GL_LAYOUT_GENERAL_EXT.
 *
 * Every GL call (create, glAcquire, glRelease, destruction) requires the GL
 * context passed to create() to be current. Requires desktop OpenGL on the
 * same physical GPU as the Vulkan device (not ANGLE).
 */
class KRITAGPU_EXPORT KisGpuGLSharedImage
{
public:
    ~KisGpuGLSharedImage();

    KisGpuGLSharedImage(const KisGpuGLSharedImage &) = delete;
    KisGpuGLSharedImage &operator=(const KisGpuGLSharedImage &) = delete;

    static std::unique_ptr<KisGpuGLSharedImage> create(KisGpuContext &context,
                                                       QOpenGLContext *glContext,
                                                       const QSize &size,
                                                       KisGpuTileFormat format,
                                                       QString *errorMessage = nullptr);

    QSize size() const;
    VkImage image() const;
    quint32 glTexture() const;

    /**
     * Records copies of 64x64 tiles into the image. Tile i is placed at
     * column (i % tilesPerRow), row (i / tilesPerRow); InvalidSlot entries
     * are skipped. The pool format must match the image format.
     */
    void recordCopyFromTiles(KisGpuCommandList &commands,
                             const KisGpuTilePool &pool,
                             const QVector<quint32> &slots,
                             int tilesPerRow) const;

    /// Add to the waits of the next Vulkan submission that writes the image.
    /// Empty if GL has not released the image since the last Vulkan write.
    QVector<VkSemaphoreSubmitInfo> takeGLDoneWait();
    /// Add to the signals of the Vulkan submission that writes the image.
    VkSemaphoreSubmitInfo vulkanDoneSignal() const;

    /// GL side: waits (on the GPU) for the last Vulkan write before sampling.
    void glAcquire();
    /// GL side: signals that GL finished reading; the next Vulkan write waits for it.
    void glRelease();

private:
    KisGpuGLSharedImage(KisGpuContext &context);

    struct Private;
    std::unique_ptr<Private> d;
};

#endif // KISGPUGLSHAREDIMAGE_H
