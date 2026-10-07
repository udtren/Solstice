/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuGridWarpWorker.h"

#include <QVector>

#include <atomic>

#include <KisRegion.h>

#include "kis_algebra_2d.h"
#include "kis_four_point_interpolator_backward.h"

#ifdef HAVE_KRITA_GPU_ENGINE
#include <KisGpuCommandList.h>
#include <KisGpuContext.h>
#include <KisGpuGridWarpPass.h>

#include <KoColor.h>
#include <KoColorSpace.h>

#include <QElapsedTimer>

#include <cstring>
#include <half.h>

#include "KisGpuMergeBatch.h"
#include "KisGpuTileAccess.h"
#include "KisGpuTileBackend.h"
#include "KisGpuTransformWorker.h"
#include "KisPaintTrace.h"
#include "kis_default_bounds_base.h"
#include "kis_paint_device.h"
#endif

namespace
{
std::atomic<quint64> s_runs{0};

#ifdef HAVE_KRITA_GPU_ENGINE
using Op = KisGpuGridWarpPass::Op;
#else
struct Op {
};
#endif

/// Copy operations are split so that one claim workgroup stays short.
constexpr int CopyChunk = 128;
/// Source pixels a warp may sample around its source polygon; samples
/// outside the source tile grid make the GPU result invalid (CPU fallback).
constexpr int SampleMargin = 8;
} // namespace

struct KisGpuGridWarpWorker::Recorder::Private {
    bool sameColorSpace = true;
    bool canMergeRects = false;
    bool valid = true;
    QVector<QRect> rectsToCopy;
    QVector<Op> ops;
    QRect ownerRect;
    QRect srcRect;

    void addCopy(const QRect &rect);
};

void KisGpuGridWarpWorker::Recorder::Private::addCopy(const QRect &rect)
{
    // KisPainter::copyAreaOptimized(): with equal default pixels (canRun()),
    // the whole rect ends up as the source's pixels.
    if (rect.isEmpty())
        return;
#ifdef HAVE_KRITA_GPU_ENGINE
    for (int y = rect.top(); y <= rect.bottom(); y += CopyChunk) {
        for (int x = rect.left(); x <= rect.right(); x += CopyChunk) {
            const QRect chunk = QRect(x, y, CopyChunk, CopyChunk) & rect;
            Op op{};
            op.rect[0] = chunk.x();
            op.rect[1] = chunk.y();
            op.rect[2] = chunk.width();
            op.rect[3] = chunk.height();
            op.info[0] = KisGpuGridWarpPass::Copy;
            ops.append(op);
        }
    }
#endif
    ownerRect |= rect;
    srcRect |= rect;
}

KisGpuGridWarpWorker::Recorder::Recorder(bool sameColorSpace)
    : d(new Private)
{
    d->sameColorSpace = sameColorSpace;
}

KisGpuGridWarpWorker::Recorder::~Recorder()
{
    KIS_SAFE_ASSERT_RECOVER_NOOP(d->rectsToCopy.isEmpty());
}

void KisGpuGridWarpWorker::Recorder::fastCopyArea(QRect areaToCopy)
{
    fastCopyArea(areaToCopy, d->canMergeRects);
}

void KisGpuGridWarpWorker::Recorder::fastCopyArea(QRect areaToCopy, bool lazy)
{
    if (lazy) {
        d->rectsToCopy.append(areaToCopy.adjusted(0, 0, -1, -1));
    } else {
        d->addCopy(areaToCopy);
    }
}

void KisGpuGridWarpWorker::Recorder::operator()(const QPolygonF &srcPolygon, const QPolygonF &dstPolygon)
{
    operator()(srcPolygon, dstPolygon, dstPolygon);
}

void KisGpuGridWarpWorker::Recorder::operator()(const QPolygonF &srcPolygon,
                                                const QPolygonF &dstPolygon,
                                                const QPolygonF &clipDstPolygon)
{
    // Mirrors PaintDevicePolygonOp::operator()() decision by decision.
    const qreal epsilon = 0.001;
    const QRect boundRect = clipDstPolygon.boundingRect().toAlignedRect();
    if (boundRect.isEmpty())
        return;

    const bool samePolygon = d->sameColorSpace && KisAlgebra2D::fuzzyPointCompare(srcPolygon, dstPolygon, epsilon)
        && KisAlgebra2D::fuzzyPointCompare(srcPolygon, clipDstPolygon, epsilon);

    if (samePolygon && KisAlgebra2D::isPolygonPixelAlignedRect(dstPolygon, epsilon)) {
        fastCopyArea(dstPolygon.boundingRect().toAlignedRect());
        return;
    }

    copyPreviousRects();

#ifdef HAVE_KRITA_GPU_ENGINE
    Op op{};
    // QPolygonF::containsPoint(): the edges of the polygon, closed implicitly.
    if (clipDstPolygon.isEmpty())
        return; // contains no pixel
    const int pointCount = clipDstPolygon.size();
    const bool closingEdge = clipDstPolygon.last() != clipDstPolygon.first();
    const int edgeCount = pointCount - 1 + (closingEdge ? 1 : 0);
    if (edgeCount > 4) {
        d->valid = false;
        return;
    }
    for (int i = 0; i < edgeCount; ++i) {
        // qt_polygon_isect_line(): the parts that do not depend on the pixel.
        const QPointF &p1 = clipDstPolygon[i];
        const QPointF &p2 = i + 1 < pointCount ? clipDstPolygon[i + 1] : clipDstPolygon.first();
        qreal x1 = p1.x();
        qreal y1 = p1.y();
        qreal x2 = p2.x();
        qreal y2 = p2.y();
        int dir = 1;
        if (qFuzzyCompare(y1, y2)) {
            dir = 0;
        } else if (y2 < y1) {
            std::swap(x1, x2);
            std::swap(y1, y2);
            dir = -1;
        }
        op.edgeDir[i] = dir;
        if (dir != 0) {
            op.edge[i][0] = x1;
            op.edge[i][1] = y1;
            op.edge[i][2] = y2;
            op.edge[i][3] = (x2 - x1) / (y2 - y1);
        }
    }

    const KisFourPointInterpolatorBackward interp(srcPolygon, dstPolygon);
    const KisFourPointInterpolatorBackward::Coefficients c = interp.coefficients();
    op.a[0] = c.a.x();
    op.a[1] = c.a.y();
    op.c[0] = c.c.x();
    op.c[1] = c.c.y();
    op.d[0] = c.d.x();
    op.d[1] = c.d.y();
    op.srcBase[0] = c.srcBase.x();
    op.srcBase[1] = c.srcBase.y();
    op.dstBase[0] = c.dstBase.x();
    op.dstBase[1] = c.dstBase.y();
    const QPointF fallback = interp.fallbackSourcePoint();
    op.fallback[0] = fallback.x();
    op.fallback[1] = fallback.y();
    op.qA = c.qA;
    op.qBConst = c.qBConst;
    op.qDDiv = c.qDDiv;
    op.xCoeff = c.xCoeff;
    op.yCoeff = c.yCoeff;
    op.rect[0] = boundRect.x();
    op.rect[1] = boundRect.y();
    op.rect[2] = boundRect.width();
    op.rect[3] = boundRect.height();
    op.info[0] = interp.isValid(0.1) ? KisGpuGridWarpPass::Warp : KisGpuGridWarpPass::Fallback;
    d->ops.append(op);
#endif
    d->ownerRect |= boundRect;
    d->srcRect |= srcPolygon.boundingRect().toAlignedRect().adjusted(-SampleMargin,
                                                                     -SampleMargin,
                                                                     SampleMargin + 1,
                                                                     SampleMargin + 1);
}

void KisGpuGridWarpWorker::Recorder::copyPreviousRects()
{
    QVector<QRect>::iterator end = KisRegion::mergeSparseRects(d->rectsToCopy.begin(), d->rectsToCopy.end());
    for (QVector<QRect>::iterator it = d->rectsToCopy.begin(); it < end; it++) {
        fastCopyArea(it->adjusted(0, 0, 1, 1), false);
    }
    d->rectsToCopy = QVector<QRect>();
}

void KisGpuGridWarpWorker::Recorder::finalize()
{
    copyPreviousRects();
}

void KisGpuGridWarpWorker::Recorder::setCanMergeRects(bool canMergeRects)
{
    d->canMergeRects = canMergeRects;
}

void KisGpuGridWarpWorker::Recorder::reserve(int operations)
{
    d->ops.reserve(operations);
}

bool KisGpuGridWarpWorker::Recorder::isValid() const
{
    return d->valid;
}

int KisGpuGridWarpWorker::Recorder::operationCount() const
{
    return d->ops.size();
}

bool KisGpuGridWarpWorker::isEnabled()
{
#ifdef HAVE_KRITA_GPU_ENGINE
    return KisGpuMergeBatch::isEnabled() && qgetenv("KRITA_GPU_TRANSFORM") != "0"
        && qgetenv("KRITA_GPU_LIQUIFY") != "0";
#else
    return false;
#endif
}

quint64 KisGpuGridWarpWorker::runCount()
{
    return s_runs.load();
}

bool KisGpuGridWarpWorker::canRun(KisPaintDeviceSP src, KisPaintDeviceSP dst)
{
#ifdef HAVE_KRITA_GPU_ENGINE
    if (!isEnabled() || !src || !dst || src == dst)
        return false;
    if (!KisGpuTileAccess::isSupported(src) || !KisGpuTileAccess::isSupported(dst))
        return false;
    if (*src->colorSpace() != *dst->colorSpace() || src->pixelSize() != dst->pixelSize()
        || src->defaultPixel() != dst->defaultPixel())
        return false;
    // The CPU copies (KisPainter::copyAreaOptimized()) are plain copies only
    // when both devices have the same offset (fastBitBlt()); otherwise the
    // Copy composite op clears transparent pixels in SIMD batches, which the
    // GPU does not reproduce. The Transform Tool's source is a copy of the
    // node's device, so its offset is the same.
    if (src->x() != dst->x() || src->y() != dst->y())
        return false;
    for (const KisPaintDeviceSP &device : {src, dst}) {
        if (device->defaultBounds()->wrapAroundMode() || device->defaultBounds()->currentLevelOfDetail() != 0)
            return false;
    }
    KisGpuTileBackend *backend = KisGpuTileBackend::instance();
    return backend && !backend->hasFailed() && backend->context().deviceInfo().supportsFloat64;
#else
    Q_UNUSED(src);
    Q_UNUSED(dst);
    return false;
#endif
}

#ifdef HAVE_KRITA_GPU_ENGINE
namespace
{
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

void setGrid(const KisGpuTileAccess &access, QPoint *origin, int *width, int *height, QVector<VkDeviceAddress> *tiles)
{
    const QRect grid = access.tileGrid();
    *origin = access.tileOrigin(grid.left(), grid.top());
    *width = grid.width();
    *height = grid.height();
    *tiles = access.addresses();
}
} // namespace
#endif

bool KisGpuGridWarpWorker::run(KisPaintDeviceSP src, KisPaintDeviceSP dst, const Recorder &recorder)
{
#ifdef HAVE_KRITA_GPU_ENGINE
    if (!recorder.isValid() || !canRun(src, dst))
        return false;
    const Recorder::Private &plan = *recorder.d;
    if (plan.ops.isEmpty()) {
        ++s_runs;
        return true; // nothing to paint; dst stays cleared
    }
    QElapsedTimer timer;
    timer.start();
    KisGpuContext &context = KisGpuTileBackend::instance()->context();
    const KisGpuTileFormat format = src->pixelSize() == 8 ? KisGpuTileFormat::RGBA16F : KisGpuTileFormat::RGBA32F;
    std::unique_ptr<KisGpuGridWarpPass> recorderPass = KisGpuGridWarpPass::create(context, format);
    if (!recorderPass)
        return false;
    KisPaintTrace::Scope trace("transform.gpu_grid_warp", dst.data());

    KisGpuCommandList commands(context);
    if (!commands.isValid())
        return false;
    KisGpuTileAccess srcAccess(src, plan.srcRect, KisGpuTileAccess::ReadOnly);
    KisGpuTileAccess dstAccess(dst, plan.ownerRect, KisGpuTileAccess::WriteOnly);
    commands.begin();
    QString error;
    const QVector<KisGpuTileAccess *> accesses{&srcAccess, &dstAccess};
    if (!srcAccess.prepare(commands, &error) || !dstAccess.prepare(commands, &error)) {
        KisGpuTileAccess::finishUnsubmitted(commands, accesses);
        return false;
    }
    const qint64 prepared = timer.nsecsElapsed();
    KisGpuGridWarpPass::Pass pass;
    pass.ops = plan.ops.constData();
    pass.opCount = plan.ops.size();
    pass.ownerRect = plan.ownerRect;
    setGrid(srcAccess, &pass.srcGridOrigin, &pass.srcGridWidth, &pass.srcGridHeight, &pass.srcTiles);
    setGrid(dstAccess, &pass.dstGridOrigin, &pass.dstGridWidth, &pass.dstGridHeight, &pass.dstTiles);
    defaultPixelAsFloat(src, pass.defaultPixel);
    if (!recorderPass->record(commands, pass, &error)) {
        KisGpuTileAccess::finishUnsubmitted(commands, accesses);
        return false;
    }
    const qint64 recorded = timer.nsecsElapsed();
    const quint64 value = KisGpuTileAccess::submitAndFinish(commands, accesses);
    const qint64 submitted = timer.nsecsElapsed();
    if (!value || !commands.wait())
        return false;
    const qint64 waited = timer.nsecsElapsed();
    if (recorderPass->failed()) {
        if (qEnvironmentVariableIntValue("KRITA_GPU_TRANSFORM_DEBUG") == 1)
            qInfo() << "GPU grid warp: a sample fell outside the source rect, using the CPU";
        return false;
    }
    // The caller's CPU copies and the projection read the result next;
    // download it in one batch instead of tile by tile.
    KisGpuTileAccess::syncToCpu(dst, plan.ownerRect);
    if (qEnvironmentVariableIntValue("KRITA_GPU_TRANSFORM_DEBUG") == 1)
        qInfo() << "GPU grid warp ops" << pass.opCount << "prepare ms" << prepared / 1e6 << "record ms"
                << (recorded - prepared) / 1e6 << "submit ms" << (submitted - recorded) / 1e6 << "wait ms"
                << (waited - submitted) / 1e6 << "download ms" << (timer.nsecsElapsed() - waited) / 1e6 << "src tiles"
                << pass.srcTiles.size() << "dst tiles" << pass.dstTiles.size();
    ++s_runs;
    return true;
#else
    Q_UNUSED(src);
    Q_UNUSED(dst);
    Q_UNUSED(recorder);
    return false;
#endif
}
