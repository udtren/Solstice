/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuTransformWorker.h"

#include <atomic>

#ifdef HAVE_KRITA_GPU_ENGINE
#include <KisGpuCommandList.h>
#include <KisGpuContext.h>
#include <KisGpuTransformPass.h>

#include <KoColor.h>
#include <KoColorSpace.h>

#include <QElapsedTimer>

#include <cstring>
#include <half.h>

#include "KisGpuMergeBatch.h"
#include "KisGpuTileAccess.h"
#include "KisGpuTileBackend.h"
#include "KisPaintTrace.h"
#include "kis_default_bounds_base.h"
#include "kis_paint_device.h"
#endif

namespace
{
std::atomic<quint64> s_runs{0};

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

KisGpuTransformPass::Pass toPass(const KisGpuTransformWorker::PlannedPass &planned, KisPaintDeviceSP device)
{
    KisGpuTransformPass::Pass pass;
    pass.scale = planned.scale;
    pass.shear = planned.shear;
    pass.dx = planned.dx;
    pass.weightsPositionScale = planned.weightsPositionScale;
    pass.srcStart = planned.srcStart;
    pass.srcEnd = planned.srcEnd;
    pass.clampToEdge = planned.clampToEdge;
    pass.vertical = planned.vertical;
    pass.lineFirst = planned.lineFirst;
    pass.lineRanges = planned.lineRanges;
    pass.maxSpan = planned.maxSpan;
    pass.weights = planned.weights;
    defaultPixelAsFloat(device, pass.defaultPixel);
    return pass;
}

void setGrid(const KisGpuTileAccess &access, QPoint *origin, int *width, int *height, QVector<VkDeviceAddress> *tiles)
{
    const QRect grid = access.tileGrid();
    *origin = access.tileOrigin(grid.left(), grid.top());
    *width = grid.width();
    *height = grid.height();
    *tiles = access.addresses();
}

/// One pass from @p src (read inside @p srcRect) into @p dst (written over the
/// tile grid of @p writeRect), in its own submission.
bool runPass(KisGpuContext &context,
             KisGpuTransformPass &recorder,
             KisPaintDeviceSP src,
             const QRect &srcRect,
             KisPaintDeviceSP dst,
             const QRect &writeRect,
             KisGpuTransformPass::Pass &pass)
{
    QElapsedTimer timer;
    timer.start();
    KisGpuCommandList commands(context);
    if (!commands.isValid())
        return false;
    KisGpuTileAccess srcAccess(src, srcRect, KisGpuTileAccess::ReadOnly);
    KisGpuTileAccess dstAccess(dst, writeRect, KisGpuTileAccess::WriteOnly);
    commands.begin();
    QString error;
    const QVector<KisGpuTileAccess *> accesses{&srcAccess, &dstAccess};
    if (!srcAccess.prepare(commands, &error) || !dstAccess.prepare(commands, &error)) {
        KisGpuTileAccess::finishUnsubmitted(commands, accesses);
        return false;
    }
    const qint64 prepared = timer.nsecsElapsed();
    setGrid(srcAccess, &pass.srcGridOrigin, &pass.srcGridWidth, &pass.srcGridHeight, &pass.srcTiles);
    setGrid(dstAccess, &pass.dstGridOrigin, &pass.dstGridWidth, &pass.dstGridHeight, &pass.dstTiles);
    if (!recorder.record(commands, pass, &error)) {
        KisGpuTileAccess::finishUnsubmitted(commands, accesses);
        return false;
    }
    const qint64 recorded = timer.nsecsElapsed();
    const quint64 value = KisGpuTileAccess::submitAndFinish(commands, accesses);
    const qint64 submitted = timer.nsecsElapsed();
    const bool ok = value && commands.wait();
    if (qEnvironmentVariableIntValue("KRITA_GPU_TRANSFORM_DEBUG") == 1)
        qInfo() << "GPU transform pass" << (pass.vertical ? "y" : "x") << "prepare ms" << prepared / 1e6 << "record ms"
                << (recorded - prepared) / 1e6 << "submit ms" << (submitted - recorded) / 1e6 << "wait ms"
                << (timer.nsecsElapsed() - submitted) / 1e6 << "src tiles" << pass.srcTiles.size() << "dst tiles"
                << pass.dstTiles.size();
    return ok;
}
#endif
} // namespace

bool KisGpuTransformWorker::isEnabled()
{
#ifdef HAVE_KRITA_GPU_ENGINE
    return KisGpuMergeBatch::isEnabled() && qgetenv("KRITA_GPU_TRANSFORM") != "0";
#else
    return false;
#endif
}

quint64 KisGpuTransformWorker::runCount()
{
    return s_runs.load();
}

bool KisGpuTransformWorker::canRun(KisPaintDeviceSP device)
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

bool KisGpuTransformWorker::runPlannedPasses(KisPaintDeviceSP device,
                                             const PlannedPass &xPass,
                                             const PlannedPass &yPass)
{
#ifdef HAVE_KRITA_GPU_ENGINE
    if (!canRun(device) || xPass.vertical || !yPass.vertical || xPass.srcRect.isEmpty()
        || yPass.srcRect != xPass.dstRect)
        return false;
    KisGpuContext &context = KisGpuTileBackend::instance()->context();
    const KisGpuTileFormat format = device->pixelSize() == 8 ? KisGpuTileFormat::RGBA16F : KisGpuTileFormat::RGBA32F;
    std::unique_ptr<KisGpuTransformPass> recorder = KisGpuTransformPass::create(context, format);
    if (!recorder)
        return false;
    KisPaintTrace::Scope trace("transform.gpu_passes", device.data());

    KisPaintDeviceSP temporary = new KisPaintDevice(device->colorSpace());
    temporary->setDefaultBounds(device->defaultBounds());
    temporary->setDefaultPixel(device->defaultPixel());

    // x pass: the device into the temporary device.
    KisGpuTransformPass::Pass first = toPass(xPass, device);
    if (!runPass(context, *recorder, device, xPass.srcRect, temporary, xPass.dstRect, first))
        return false;
    // y pass: the temporary device into the device. In place, the CPU passes
    // leave the default pixel everywhere outside the final line ranges, so
    // the whole union is written. The recorder's tables are free again after
    // the first submission's wait.
    KisGpuTransformPass::Pass second = toPass(yPass, device);
    const QRect writeRect = xPass.srcRect | xPass.dstRect | yPass.dstRect;
    if (!runPass(context, *recorder, temporary, yPass.srcRect, device, writeRect, second))
        return false;
    // KisTransformWorker purges default tiles on the CPU next; download the
    // written tiles in one batch instead of one by one (about 250ms -> 15ms
    // for a 2480x3508 RGBA32F layer).
    KisGpuTileAccess::syncToCpu(device, writeRect);
    ++s_runs;
    return true;
#else
    Q_UNUSED(device);
    Q_UNUSED(xPass);
    Q_UNUSED(yPass);
    return false;
#endif
}
