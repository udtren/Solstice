/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuProjectionCompositor.h"

#include "KisGpuTileAccess.h"
#include "KisGpuTileBackend.h"

#include <KisGpuCommandList.h>
#include <KisGpuContext.h>
#include <KisGpuTilePool.h>

#include <KoColorSpace.h>
#include <KoCompositeOpRegistry.h>

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include <memory>
#include <vector>

#include <kis_debug.h>

#include "kis_paint_device.h"

namespace
{
constexpr int TileSize = 64;

/// Per-thread resources of one composite() call, reused across calls. Its
/// command list's begin() waits for the previous submission of this context,
/// so the compositor's table buffer is never overwritten while in use.
struct WorkContext {
    explicit WorkContext(KisGpuContext &context)
        : commands(context)
    {
    }

    KisGpuCommandList commands;
    std::unique_ptr<KisGpuLayerCompositor> compositor32;
    std::unique_ptr<KisGpuLayerCompositor> compositor16;
};

QMutex s_contextsMutex;
/**
 * Intentionally never destroyed, like KisGpuTileBackend: destroying Vulkan
 * objects during static destruction runs after the Vulkan loader and layers
 * may already have been torn down (crashed with the validation layer).
 */
std::vector<std::unique_ptr<WorkContext>> *s_freeContexts = new std::vector<std::unique_ptr<WorkContext>>();

std::unique_ptr<WorkContext> acquireContext(KisGpuContext &context)
{
    QMutexLocker locker(&s_contextsMutex);
    if (!s_freeContexts->empty()) {
        std::unique_ptr<WorkContext> work = std::move(s_freeContexts->back());
        s_freeContexts->pop_back();
        return work;
    }
    return std::unique_ptr<WorkContext>(new WorkContext(context));
}

void releaseContext(std::unique_ptr<WorkContext> work)
{
    QMutexLocker locker(&s_contextsMutex);
    s_freeContexts->push_back(std::move(work));
}
} // namespace

bool KisGpuProjectionCompositor::blendOpForCompositeOp(const QString &id, KisGpuBlendOp *op)
{
    static const QHash<QString, KisGpuBlendOp> ops = {
        {COMPOSITE_OVER, KisGpuBlendOp::Over},
        {COMPOSITE_MULT, KisGpuBlendOp::Multiply},
        {COMPOSITE_SCREEN, KisGpuBlendOp::Screen},
        {COMPOSITE_ADD, KisGpuBlendOp::Add},
        {COMPOSITE_LINEAR_DODGE, KisGpuBlendOp::Add},
        {COMPOSITE_SUBTRACT, KisGpuBlendOp::Subtract},
        {COMPOSITE_DARKEN, KisGpuBlendOp::Darken},
        {COMPOSITE_LIGHTEN, KisGpuBlendOp::Lighten},
        {COMPOSITE_DIFF, KisGpuBlendOp::Difference},
        {COMPOSITE_OVERLAY, KisGpuBlendOp::Overlay},
        {COMPOSITE_HARD_LIGHT, KisGpuBlendOp::HardLight},
        {COMPOSITE_EXCLUSION, KisGpuBlendOp::Exclusion},
    };
    auto it = ops.constFind(id);
    if (it == ops.constEnd()) {
        return false;
    }
    *op = it.value();
    return true;
}

bool KisGpuProjectionCompositor::composite(KisPaintDeviceSP projection,
                                           const QRect &rect,
                                           const QVector<Layer> &layers,
                                           QString *errorMessage,
                                           const KisGpuLayerCompositor::Mask *mask)
{
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    if (rect.isEmpty() || layers.isEmpty()) {
        return true;
    }
    if (!KisGpuTileAccess::isSupported(projection, errorMessage)) {
        return false;
    }

    KisGpuTileBackend *backend = KisGpuTileBackend::instance();
    const qint32 pixelSize = projection->pixelSize();
    if (mask
        && (pixelSize != 16 || !mask->data || mask->bounds.isEmpty()
            || qint64(mask->bounds.width()) * mask->bounds.height() > 4096 * 4096 || layers.size() != 1
            || (layers[0].op != KisGpuBlendOp::Over && layers[0].op != KisGpuBlendOp::Erase))) {
        return fail(QStringLiteral("coverage requires one RGBA32F Normal/Erase layer"));
    }
    KisGpuContext &context = backend->context();

    // KisUpdaterContext keeps merge jobs that share a tile of the image grid
    // apart; that only protects projections whose tiles are on that grid.
    if (projection->x() % TileSize || projection->y() % TileSize) {
        return fail(QStringLiteral("projection offset is not a multiple of 64"));
    }

    for (const Layer &layer : layers) {
        if (layer.channelMask > 0xf
            || (layer.channelMask != 0xf
                && (pixelSize != 16 || (layer.op != KisGpuBlendOp::Over && layer.op != KisGpuBlendOp::Erase)))) {
            return fail(QStringLiteral("restricted channels require RGBA32F Normal/Erase"));
        }
        if (!(*layer.device->colorSpace() == *projection->colorSpace())) {
            return fail(QStringLiteral("layer color space differs from the projection"));
        }
        if ((layer.device->x() - projection->x()) % TileSize || (layer.device->y() - projection->y()) % TileSize) {
            return fail(QStringLiteral("layer tiles are not aligned with the projection tiles"));
        }
    }

    std::unique_ptr<WorkContext> work = acquireContext(context);
    std::unique_ptr<KisGpuLayerCompositor> &compositor = pixelSize == 8 ? work->compositor16 : work->compositor32;
    if (!compositor) {
        compositor =
            KisGpuLayerCompositor::create(context,
                                          pixelSize == 8 ? KisGpuTileFormat::RGBA16F : KisGpuTileFormat::RGBA32F,
                                          errorMessage);
    }
    if (!compositor || !work->commands.isValid()) {
        releaseContext(std::move(work));
        return false;
    }

    // The projection's tiles are composited in place on the GPU. Merge jobs
    // that share a projection tile never run concurrently while the GPU
    // projection is enabled (KisUpdaterContext::walkerIntersectsJob), and
    // the clip rect leaves the rest of each tile untouched.
    KisGpuCommandList &commands = work->commands;
    commands.begin();

    KisGpuTileAccess target(projection, rect, KisGpuTileAccess::ReadWrite);
    std::vector<std::unique_ptr<KisGpuTileAccess>> accesses;
    QVector<VkDeviceAddress> layerTiles;
    QVector<KisGpuLayerCompositor::Layer> layerParams;

    bool ok = target.prepare(commands, errorMessage);
    const QRect grid = target.tileGrid();
    const int tileCount = target.tileCount();
    const QPoint gridOrigin = target.tileOrigin(grid.left(), grid.top());
    layerTiles.reserve(tileCount * layers.size());

    for (int i = 0; ok && i < layers.size(); i++) {
        const Layer &layer = layers[i];
        const QRect extent = layer.device->extent();
        std::unique_ptr<KisGpuTileAccess> access(
            new KisGpuTileAccess(layer.device, rect & extent, KisGpuTileAccess::ReadOnly));
        if (!access->tileCount()) {
            // Nothing of this layer inside the rect.
            continue;
        }
        if (!access->prepare(commands, errorMessage)) {
            ok = false;
            accesses.push_back(std::move(access));
            break;
        }

        // Place the layer's tiles in the projection grid; tiles outside the
        // layer's extent stay 0 (skipped), like KisLayerProjectionPlane.
        const QVector<VkDeviceAddress> addresses = access->addresses();
        const QRect layerGrid = access->tileGrid();
        QVector<VkDeviceAddress> placed(tileCount, 0);
        for (int row = 0; row < layerGrid.height(); row++) {
            for (int col = 0; col < layerGrid.width(); col++) {
                const QPoint origin = access->tileOrigin(layerGrid.left() + col, layerGrid.top() + row) - gridOrigin;
                const int gridCol = origin.x() / TileSize;
                const int gridRow = origin.y() / TileSize;
                KIS_SAFE_ASSERT_RECOVER(gridCol >= 0 && gridRow >= 0 && gridCol < grid.width()
                                        && gridRow < grid.height())
                {
                    continue;
                }
                placed[gridRow * grid.width() + gridCol] = addresses[row * layerGrid.width() + col];
            }
        }
        layerTiles += placed;
        layerParams << KisGpuLayerCompositor::Layer{layer.op, layer.opacity, layer.alphaLocked, layer.channelMask};
        accesses.push_back(std::move(access));
    }

    if (ok) {
        KisGpuLayerCompositor::Mask gridMask;
        if (mask)
            gridMask = {mask->data, mask->bounds.translated(-gridOrigin)};
        commands.computeBarrier();
        ok = compositor->record(commands,
                                layerTiles,
                                target.addresses(),
                                layerParams,
                                grid.width(),
                                rect.translated(-gridOrigin),
                                errorMessage,
                                mask ? &gridMask : nullptr);
    }

    QVector<KisGpuTileAccess *> accessList;
    accessList << &target;
    for (auto &access : accesses) {
        accessList << access.get();
    }

    if (ok) {
        ok = KisGpuTileAccess::submitAndFinish(commands, accessList) != 0;
        if (!ok && errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("GPU submission refused or failed");
        }
    } else {
        // Nothing must run: the projection keeps its content and the merge
        // falls back to the CPU.
        KisGpuTileAccess::finishUnsubmitted(commands, accessList);
    }

    // No wait: the result stays on the GPU. CPU readers of the projection
    // download it (ordered after this submission); the next use of this work
    // context waits in commands.begin().
    releaseContext(std::move(work));
    return ok;
}
