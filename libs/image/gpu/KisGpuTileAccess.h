/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUTILEACCESS_H
#define KISGPUTILEACCESS_H

#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

#include <memory>

#include <KisGpuVulkanFunctions.h>

#include "kis_types.h"
#include "kritaimage_export.h"

class KisGpuCommandList;
class KisGpuBuffer;

/**
 * GPU access to the 64x64 tiles of a paint device that cover a rect
 * (GPU engine phase 1, docs/agent/gpu-engine.md).
 *
 * Usage:
 *
 *     KisGpuTileAccess dst(device, rect, KisGpuTileAccess::ReadWrite);
 *     commands.begin();
 *     dst.prepare(commands);             // copy-on-write + uploads
 *     ... record compute work on dst.addresses() ...
 *     if (!KisGpuTileAccess::submitAndFinish(commands, {&dst})) {
 *         ... nothing ran (engine stopped, submission failed): use the CPU ...
 *     }
 *     device->setDirty(rect);
 *
 * Writes follow the CPU rules exactly: shared tile data is copied first and
 * the change is registered in the device's memento manager, so a GPU write
 * inside a KisTransaction is undoable. After submitAndFinish(), the CPU copy
 * of every written tile is stale; the next CPU lock downloads it transparently.
 *
 * Only RGBA F32 and RGBA F16 devices are supported. The caller must own the
 * tiles for the duration (the usual stroke/processing discipline): no CPU
 * thread may lock them between prepare() and submitAndFinish().
 */
class KRITAIMAGE_EXPORT KisGpuTileAccess
{
public:
    /** Append-only upload storage shared by accesses prepared for one submission.
     * Not thread-safe. Live accesses retain each buffer until GPU completion.
     */
    class UploadArena
    {
    public:
        explicit UploadArena(VkDeviceSize chunkBytes)
            : m_chunkBytes(chunkBytes)
        {
        }
        UploadArena(const UploadArena &) = delete;
        UploadArena &operator=(const UploadArena &) = delete;
        int allocationCount() const
        {
            return m_allocationCount;
        }
        VkDeviceSize reservedBytes() const
        {
            return m_reservedBytes;
        }

    private:
        friend class KisGpuTileAccess;
        VkDeviceSize m_chunkBytes;
        std::shared_ptr<KisGpuBuffer> m_current;
        VkDeviceSize m_used = 0;
        int m_allocationCount = 0;
        VkDeviceSize m_reservedBytes = 0;
    };

    enum Mode {
        /// The GPU reads the tiles. Missing tiles read as the default pixel.
        ReadOnly,
        /// The GPU reads and writes the tiles; missing tiles are created.
        ReadWrite,
        /// The GPU overwrites every pixel of every tile; old content is not uploaded.
        WriteOnly,
    };

    /// True if the GPU engine is available and @p device has an RGBA F32/F16 color space.
    static bool isSupported(KisPaintDeviceSP device, QString *reason = nullptr);

    KisGpuTileAccess(KisPaintDeviceSP device, const QRect &rect, Mode mode);
    ~KisGpuTileAccess();

    KisGpuTileAccess(const KisGpuTileAccess &) = delete;
    KisGpuTileAccess &operator=(const KisGpuTileAccess &) = delete;

    Mode mode() const;

    /// Tile columns/rows covered, in data manager tile coordinates.
    QRect tileGrid() const;
    int tileCount() const;
    /// Device coordinates of the top-left pixel of tile (col, row).
    QPoint tileOrigin(int col, int row) const;

    /**
     * Performs copy-on-write and records uploads of stale tiles and GPU-side
     * clone copies into @p commands. Call once, before using addresses().
     */
    bool prepare(KisGpuCommandList &commands, QString *errorMessage = nullptr);
    bool prepare(KisGpuCommandList &commands, UploadArena &uploads, QString *errorMessage = nullptr);

    /// Device address of each tile, row-major over tileGrid(). Valid after prepare().
    QVector<VkDeviceAddress> addresses() const;

    /**
     * Submits @p commands and publishes the result of every access in
     * @p accesses, atomically with respect to all other tile accesses:
     * written tiles become GPU-valid/CPU-stale, uploaded tiles become
     * GPU-valid, and every slot used stays alive until the work completes.
     *
     * Uploads of CPU content snapshotted by prepare() are recorded here,
     * into the preamble of the submission, and skipped for tiles whose GPU
     * copy has become current in the meantime (it is at least as new). A
     * snapshot that overlapped a CPU write is used for this submission but
     * not published as the tile's GPU copy.
     *
     * If the engine has failed or the submission fails, nothing runs, every
     * access is finished as failed (written tiles keep their previous
     * content), and 0 is returned.
     *
     * @return the timeline value of the submission, or 0
     */
    static quint64 submitAndFinish(KisGpuCommandList &commands,
                                   const QVector<KisGpuTileAccess *> &accesses,
                                   const QVector<VkSemaphoreSubmitInfo> &waitSemaphores = {},
                                   const QVector<VkSemaphoreSubmitInfo> &signalSemaphores = {});

    /**
     * Abandons the recording in @p commands and finishes @p accesses without
     * running anything (e.g. after a recording error): written tiles keep
     * their previous content.
     */
    static void finishUnsubmitted(KisGpuCommandList &commands, const QVector<KisGpuTileAccess *> &accesses);

    /// Downloads every stale tile of @p rect in batched submissions (e.g. before saving).
    static void syncToCpu(KisPaintDeviceSP device, const QRect &rect);

private:
    bool prepareImpl(KisGpuCommandList &commands, QString *errorMessage, UploadArena *uploads);
    /// Records the uploads still needed into commands.preamble() (residency mutex held).
    void recordUploads(KisGpuCommandList &commands);
    /// Publishes flags and lastUse (under the residency mutex).
    void publish(quint64 timelineValue);
    /// Releases replaced tile data and staging (after restoring, if nothing ran).
    void complete(quint64 timelineValue);

    struct Private;
    std::unique_ptr<Private> d;
};

#endif // KISGPUTILEACCESS_H
