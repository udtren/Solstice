/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuConvolutionWorker.h"

#include <atomic>

#ifdef HAVE_KRITA_GPU_ENGINE
#include <KisGpuCommandList.h>
#include <KisGpuContext.h>
#include <KisGpuSeparableConvolutionPass.h>

#include <KoChannelInfo.h>
#include <KoColor.h>
#include <KoColorSpace.h>

#include <QElapsedTimer>

#include <cstring>
#include <half.h>

#include "KisGpuMergeBatch.h"
#include "KisGpuTileAccess.h"
#include "KisGpuTileBackend.h"
#include "KisPaintTrace.h"
#include "kis_default_bounds.h"
#include "kis_paint_device.h"
#include "kis_painter.h"
#endif

namespace
{
std::atomic<quint64> s_runs{0};
const qint64 MinimumGpuPixels = 32768;

#ifdef HAVE_KRITA_GPU_ENGINE
void defaultPixelAsFloat(KisPaintDeviceSP device, float *pixel)
{
    const KoColor color = device->defaultPixel();
    if (device->pixelSize() == 8) {
        const half *channels = reinterpret_cast<const half *>(color.data());
        for (int i = 0; i < 4; ++i)
            pixel[i] = float(channels[i]);
    } else {
        std::memcpy(pixel, color.data(), 4 * sizeof(float));
    }
}

/// The convolved channels in tile memory order, from the color space's
/// channel flags (KisConvolutionWorker::convolvableChannelList()). False if
/// the layout is not RGBA with alpha last.
bool convolvedChannels(const KoColorSpace *colorSpace, const QBitArray &channelFlags, bool *convolved)
{
    const QList<KoChannelInfo *> channels = colorSpace->channels();
    if (channels.size() != 4 || (!channelFlags.isEmpty() && channelFlags.size() != 4))
        return false;
    bool seen[4] = {false, false, false, false};
    for (int c = 0; c < channels.size(); ++c) {
        const KoChannelInfo *channel = channels[c];
        if (channel->size() <= 0)
            return false;
        const int index = channel->pos() / channel->size();
        if (index < 0 || index > 3 || seen[index])
            return false;
        if ((channel->channelType() == KoChannelInfo::ALPHA) != (index == 3))
            return false;
        seen[index] = true;
        convolved[index] = channelFlags.isEmpty() || channelFlags.testBit(c);
    }
    return true;
}

void setGrid(const KisGpuTileAccess &access, QPoint *origin, int *width, int *height, QVector<VkDeviceAddress> *tiles)
{
    const QRect grid = access.tileGrid();
    *origin = access.tileOrigin(grid.left(), grid.top());
    *width = grid.width();
    *height = grid.height();
    *tiles = access.addresses();
}
#endif
} // namespace

bool KisGpuConvolutionWorker::isEnabled()
{
#ifdef HAVE_KRITA_GPU_ENGINE
    return KisGpuMergeBatch::isEnabled() && qgetenv("KRITA_GPU_CONVOLUTION") != "0";
#else
    return false;
#endif
}

quint64 KisGpuConvolutionWorker::runCount()
{
    return s_runs.load();
}

bool KisGpuConvolutionWorker::canRun(KisPaintDeviceSP device)
{
#ifdef HAVE_KRITA_GPU_ENGINE
    if (!isEnabled() || !device || !KisGpuTileAccess::isSupported(device))
        return false;
    if (device->defaultBounds()->wrapAroundMode() || device->defaultBounds()->currentLevelOfDetail() != 0)
        return false;
    KisGpuTileBackend *backend = KisGpuTileBackend::instance();
    return backend && !backend->hasFailed() && backend->context().deviceInfo().supportsFloat64;
#else
    Q_UNUSED(device);
    return false;
#endif
}

bool KisGpuConvolutionWorker::applySeparable(KisPaintDeviceSP device,
                                             const QRect &rect,
                                             const QVector<double> &horizontal,
                                             const QVector<double> &vertical,
                                             double factor,
                                             const QBitArray &channelFlags,
                                             KisConvolutionBorderOp borderOp)
{
#ifdef HAVE_KRITA_GPU_ENGINE
    if (!canRun(device) || rect.isEmpty() || horizontal.size() % 2 != 1 || vertical.size() % 2 != 1)
        return false;
    KisGpuSeparableConvolutionPass::Pass pass;
    if (!convolvedChannels(device->colorSpace(), channelFlags, pass.convolved))
        return false;
    if (!pass.convolved[0] && !pass.convolved[1] && !pass.convolved[2] && !pass.convolved[3])
        return false;
    const int halfWidth = (horizontal.size() - 1) / 2;
    const int halfHeight = (vertical.size() - 1) / 2;

    // KisConvolutionPainter::applyMatrix(): BORDER_REPEAT clamps the source
    // into the requested rect and the image bounds; anything else reads the
    // device as it is.
    QRect readRect = rect.adjusted(-halfWidth, -halfHeight, halfWidth, halfHeight);
    // Below about 180x180 source pixels, the fixed cost of a GPU call (about
    // 1.7ms) exceeds the CPU convolution (phase 4.98 benchmark).
    if (qint64(readRect.width()) * readRect.height() < MinimumGpuPixels)
        return false;
    if (borderOp == BORDER_REPEAT) {
        const QRect boundsRect = device->defaultBounds()->bounds();
        pass.dataRect = boundsRect != KisDefaultBounds().bounds() ? rect | boundsRect : rect | device->exactBounds();
        if (!pass.dataRect.isValid())
            return false;
        pass.repeatBorder = true;
        readRect &= pass.dataRect;
    }

    KisGpuContext &context = KisGpuTileBackend::instance()->context();
    const KisGpuTileFormat format = device->pixelSize() == 8 ? KisGpuTileFormat::RGBA16F : KisGpuTileFormat::RGBA32F;
    QString error;
    std::unique_ptr<KisGpuSeparableConvolutionPass> recorder =
        KisGpuSeparableConvolutionPass::create(context, format, &error);
    if (!recorder)
        return false;
    KisPaintTrace::Scope trace("filter.gpu_convolution", device.data());
    QElapsedTimer timer;
    timer.start();

    // The CPU convolution reads oldRawData(): take the same pixels. Whole
    // tiles are shared, so this is cheap and keeps GPU-resident tiles.
    KisGpuTileAccess::syncToCpu(device, readRect);
    KisPaintDeviceSP source = new KisPaintDevice(device->colorSpace());
    source->prepareClone(device);
    KisPainter::copyAreaOptimizedOldData(readRect.topLeft(), device, source, readRect);
    KisPaintDeviceSP result = new KisPaintDevice(device->colorSpace());
    result->prepareClone(device);
    const qint64 copied = timer.nsecsElapsed();

    pass.horizontal = horizontal;
    pass.vertical = vertical;
    pass.invFactor = 1.0 / (factor != 0.0 ? factor : 1.0);
    pass.applyRect = rect;
    defaultPixelAsFloat(device, pass.defaultPixel);

    KisGpuCommandList commands(context);
    if (!commands.isValid())
        return false;
    KisGpuTileAccess srcAccess(source, readRect, KisGpuTileAccess::ReadOnly);
    KisGpuTileAccess dstAccess(result, rect, KisGpuTileAccess::WriteOnly);
    const QVector<KisGpuTileAccess *> accesses{&srcAccess, &dstAccess};
    commands.begin();
    if (!srcAccess.prepare(commands, &error) || !dstAccess.prepare(commands, &error)) {
        KisGpuTileAccess::finishUnsubmitted(commands, accesses);
        return false;
    }
    const qint64 prepared = timer.nsecsElapsed();
    setGrid(srcAccess, &pass.srcGridOrigin, &pass.srcGridWidth, &pass.srcGridHeight, &pass.srcTiles);
    setGrid(dstAccess, &pass.dstGridOrigin, &pass.dstGridWidth, &pass.dstGridHeight, &pass.dstTiles);
    if (!recorder->record(commands, pass, &error)) {
        KisGpuTileAccess::finishUnsubmitted(commands, accesses);
        return false;
    }
    const quint64 value = KisGpuTileAccess::submitAndFinish(commands, accesses);
    const qint64 submitted = timer.nsecsElapsed();
    if (!value || !commands.wait())
        return false;
    const qint64 waited = timer.nsecsElapsed();

    // The caller continues on the CPU (copies, Unsharp Mask's sharpening):
    // download in one batch, then copy into the device.
    KisGpuTileAccess::syncToCpu(result, rect);
    const qint64 downloaded = timer.nsecsElapsed();
    KisPainter::copyAreaOptimized(rect.topLeft(), result, device, rect);
    if (qEnvironmentVariableIntValue("KRITA_GPU_CONVOLUTION_DEBUG") == 1)
        qInfo() << "GPU convolution" << rect << "taps" << horizontal.size() << vertical.size() << "bands"
                << recorder->lastBandCount() << "snapshot ms" << copied / 1e6 << "prepare ms"
                << (prepared - copied) / 1e6 << "record+submit ms" << (submitted - prepared) / 1e6 << "wait ms"
                << (waited - submitted) / 1e6 << "download ms" << (downloaded - waited) / 1e6 << "copy ms"
                << (timer.nsecsElapsed() - downloaded) / 1e6;
    ++s_runs;
    return true;
#else
    Q_UNUSED(device);
    Q_UNUSED(rect);
    Q_UNUSED(horizontal);
    Q_UNUSED(vertical);
    Q_UNUSED(factor);
    Q_UNUSED(channelFlags);
    Q_UNUSED(borderOp);
    return false;
#endif
}
