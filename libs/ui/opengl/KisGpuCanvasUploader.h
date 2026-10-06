/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUCANVASUPLOADER_H
#define KISGPUCANVASUPLOADER_H

#include <QRect>
#include <QSharedPointer>
#include <QString>
#include <QVector>

#include <vector>

#include <KisGpuCanvasPatchWriter.h>
#include <KoColorConversionTransformation.h>

#include "kis_types.h"
#include "kritaui_export.h"

class QOpenGLFunctions;
class KoColorSpace;
class KisGpuGLSharedBuffer;
class KisTextureTileUpdateInfo;
typedef QSharedPointer<KisTextureTileUpdateInfo> KisTextureTileUpdateInfoSP;

/**
 * One GPU-written set of canvas patches in a buffer shared with OpenGL
 * (GPU engine phase 3.2). Owned by the KisTextureTileUpdateInfo objects
 * that reference it; the buffer returns to its pool when the last one is
 * destroyed. The tiles may belong to several update infos of one canvas
 * batch (KisCanvas2): GL holds then nest, see glAcquire().
 */
class KRITAUI_EXPORT KisGpuCanvasUpload
{
public:
    /// @p byteSize: bytes of @p buffer the patches occupy.
    KisGpuCanvasUpload(KisGpuGLSharedBuffer *buffer, quint64 byteSize);
    ~KisGpuCanvasUpload();

    KisGpuCanvasUpload(const KisGpuCanvasUpload &) = delete;
    KisGpuCanvasUpload &operator=(const KisGpuCanvasUpload &) = delete;

    /// GL side (GUI thread, canvas context current).
    quint32 glBuffer();
    /**
     * False if GL cannot import the buffer; GL must not read it then.
     * Successful calls nest: GL may read the buffer until the matching
     * number of glRelease() calls. The last one ends the GL use for good.
     */
    bool glAcquire();
    void glRelease();
    /// glAcquire() succeeded: the GL buffer holds the patches.
    bool isAcquired() const;
    /// glAcquire() failed: GL cannot read the buffer.
    bool hasFailed() const;
    /**
     * Instead of GL: copies the patches to @p data on the CPU through Vulkan
     * (waiting for the Vulkan write). Only before a successful glAcquire().
     */
    bool readBack(std::vector<quint8> *data);

private:
    KisGpuGLSharedBuffer *m_buffer;
    quint64 m_byteSize;
    int m_holds = 0;
    bool m_acquired = false;
    bool m_released = false;
    bool m_failed = false;
};

/// Binds an upload's GL buffer as GL_PIXEL_UNPACK_BUFFER for its scope.
class KRITAUI_EXPORT KisGpuCanvasUploadBinder
{
public:
    KisGpuCanvasUploadBinder(KisGpuCanvasUpload *upload, QOpenGLFunctions *f);
    ~KisGpuCanvasUploadBinder();

private:
    QOpenGLFunctions *m_f;
};

/**
 * GPU engine phase 3.2: produces canvas texture patches on the GPU from the
 * GPU-resident projection, so that the canvas never reads the projection on
 * the CPU (docs/agent/gpu-engine.md).
 */
class KRITAUI_EXPORT KisGpuCanvasUploader
{
public:
    /**
     * Called by KisOpenGLImageTextures::initGL() with the canvas context
     * current: checks actual Vulkan-to-GL bytes (once per process) before
     * enabling uploads. A silent import failure also selects the CPU path.
     */
    static void checkGLInterop();
    /// Tests: forces the interop check result.
    static void setGLInteropAvailable(bool available);
    /// Tests: reruns the production check on the next checkGLInterop() call.
    static void resetGLInteropForTesting();

    /// GPU projection enabled, GL interop available, engine not failed.
    static bool isEnabled();

    /**
     * True if upload() supports @p projection and @p dstColorSpace with
     * @p intent (it can still fail, e.g. on GPU errors). Cheap after the
     * first call for a pair of profiles.
     */
    static bool canUpload(KisPaintDeviceSP projection,
                          const KoColorSpace *dstColorSpace,
                          KoColorConversionTransformation::Intent intent);

    /**
     * Writes the patches of @p tiles (whose geometry is set) from
     * @p projection, display-converted to @p dstColorSpace, and attaches the
     * result to the tiles (KisTextureTileUpdateInfo::setGpuUpload()).
     * Returns false (and changes nothing) if the conversion or the formats
     * are not supported or the GPU fails; the caller then uses the CPU path.
     *
     * One submission for all tiles. Patches closer than one GPU tile share
     * a source access; distant ones keep separate accesses instead of
     * reading the tiles of their bounding rect.
     */
    static bool upload(KisPaintDeviceSP projection,
                       const QVector<KisTextureTileUpdateInfoSP> &tiles,
                       const KoColorSpace *dstColorSpace,
                       KoColorConversionTransformation::Intent intent,
                       KoColorConversionTransformation::ConversionFlags flags,
                       QString *errorMessage = nullptr);

    /**
     * The GPU form of converting @p src pixels to @p dst like
     * KoColorSpace::convertPixelsTo(): identity for equal profiles, or an
     * RGB matrix-shaper conversion. False if not supported (non RGBA float
     * spaces, absolute colorimetric intent, profiles without colorants/TRCs).
     */
    static bool conversionFor(const KoColorSpace *src,
                              const KoColorSpace *dst,
                              KoColorConversionTransformation::Intent intent,
                              KisGpuCanvasPatchWriter::Conversion *conversion);

    /**
     * GUI thread, canvas context current: acquire/release every distinct
     * upload of @p tiles. acquire() returns false if GL could not import a
     * buffer; the GPU canvas path is then disabled for the session and the
     * caller must rebuild the tiles on the CPU path (after release()).
     */
    static bool acquire(const QVector<KisTextureTileUpdateInfoSP> &tiles);
    static void release(const QVector<KisTextureTileUpdateInfoSP> &tiles);
    /**
     * GUI thread, after acquire() failed: every upload of @p tiles that GL
     * could not import is read back to the CPU through Vulkan, and its tiles
     * get that data as CPU pixels
     * (KisTextureTileUpdateInfo::replaceGpuUploadWithPixels()), so the
     * update is applied on the CPU upload path. The projection is not read
     * (no race with merges) and the patches are the ones already computed
     * for this update (no coordinate or level-of-detail conversion). False
     * if a readback failed; the update cannot be applied then.
     */
    static bool readBackFailedUploads(const QVector<KisTextureTileUpdateInfoSP> &tiles);

    /// Number of successful GPU uploads (diagnostics, tests).
    static quint64 uploadCount();
    /// Shared canvas buffers, including updates still waiting for GL and retired imports.
    static quint64 reservedBufferBytes();
    static quint64 bufferMemoryBudget();
    /// Tests: set when no workers are building updates; returns the old limit.
    static quint64 setBufferMemoryBudgetForTesting(quint64 bytes);
    /// Tests: current importing GL share group required; live updates are untouched.
    static void trimBuffersForTesting();

    /**
     * Diagnostics (KRITA_GPU_CANVAS_DEBUG=1, off by default): logs which
     * canvas path updates take and, for the first GPU tiles, what GL sees in
     * the shared buffer and in the texture after the upload.
     */
    static bool debugEnabled();
    static void debugLogBuild(bool onGpu, const QString &reason, const QRect &rect);
    /// GUI thread, after acquire(), before the tile's update(): the shared buffer as GL sees it.
    /// Returns true if it inspected the tile (two GPU tiles per upload).
    static bool debugInspectBuffer(const KisTextureTileUpdateInfo &tile);
    /// GUI thread, right after the tile's update() (its texture is still bound).
    static void debugInspectTexture(const KisTextureTileUpdateInfo &tile, quint32 format, quint32 type);
};

#endif // KISGPUCANVASUPLOADER_H
