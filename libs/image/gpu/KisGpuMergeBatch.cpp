/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuMergeBatch.h"

#include <QBitArray>

#include <atomic>
#include <mutex>

#include <kis_debug.h>

#include <KoColorModelStandardIds.h>
#include <KoColorSpace.h>

#include "KisGpuEngineSettings.h"
#include "kis_abstract_projection_plane.h"
#include "kis_image.h"
#include "kis_layer.h"
#include "kis_layer_projection_plane.h"
#include "kis_paint_device.h"
#include "kis_painter.h"
#include "kis_projection_leaf.h"

#ifdef HAVE_KRITA_GPU_ENGINE
#include "KisGpuProjectionCompositor.h"
#include "KisGpuTileAccess.h"
#endif

namespace
{
/// -1: not decided yet (read the environment), 0: off, 1: on
std::atomic<int> s_enabled{-1};
std::atomic<quint64> s_gpuCompositeCount{0};

constexpr int TileSize = 64;
} // namespace

bool KisGpuMergeBatch::isEnabled()
{
#ifdef HAVE_KRITA_GPU_ENGINE
    // Decided once per process, by exactly one thread: switching while merges
    // run would mix GPU merges with CPU-style job exclusivity
    // (KisUpdaterContext). setEnabled() (tests) before the first call wins.
    static std::once_flag decided;
    std::call_once(decided, []() {
        int enabled = 0;
        if (qEnvironmentVariableIsSet("KRITA_GPU_PROJECTION")) {
            enabled = qEnvironmentVariableIntValue("KRITA_GPU_PROJECTION") == 1 ? 1 : 0;
        } else {
            enabled = KisGpuEngineSettings::enabledInConfig() ? 1 : 0;
        }
        int undecided = -1;
        s_enabled.compare_exchange_strong(undecided, enabled);
    });
    return s_enabled.load() == 1;
#else
    return false;
#endif
}

void KisGpuMergeBatch::setEnabled(bool enabled)
{
    s_enabled.store(enabled ? 1 : 0);
}

quint64 KisGpuMergeBatch::gpuCompositeCount()
{
    return s_gpuCompositeCount.load();
}

bool KisGpuMergeBatch::mayCompositeOnGpu(KisNodeSP node)
{
    if (!isEnabled() || !node) {
        return false;
    }
    KisImageSP image = node->image().toStrongRef();
    return KisGpuEngineSettings::isGpuColorSpace(image ? image->colorSpace() : node->colorSpace());
}

QRect KisGpuMergeBatch::tileAligned(const QRect &rect)
{
    if (rect.isEmpty()) {
        return rect;
    }
    auto floorTile = [](int v) {
        return (v >= 0 ? v / TileSize : -((-v + TileSize - 1) / TileSize)) * TileSize;
    };
    const int left = floorTile(rect.left());
    const int top = floorTile(rect.top());
    const int right = floorTile(rect.right()) + TileSize - 1;
    const int bottom = floorTile(rect.bottom()) + TileSize - 1;
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

KisGpuMergeBatch::KisGpuMergeBatch() = default;

KisGpuMergeBatch::~KisGpuMergeBatch()
{
    KIS_SAFE_ASSERT_RECOVER_NOOP(m_entries.isEmpty() && "GPU merge batch destroyed without flush()");
}

bool KisGpuMergeBatch::tryAdd(KisProjectionLeafSP leaf, KisPaintDeviceSP projection, const QRect &rect)
{
#ifdef HAVE_KRITA_GPU_ENGINE
    if (!isEnabled() || !projection || rect.isEmpty()) {
        return false;
    }

    // Only plain layer planes: layer styles and other planes have their own apply().
    KisAbstractProjectionPlaneSP plane = leaf->projectionPlane();
    if (!dynamic_cast<KisLayerProjectionPlane *>(plane.data())) {
        return false;
    }
    KisLayer *layer = qobject_cast<KisLayer *>(leaf->node().data());
    if (!layer) {
        return false;
    }
    // KisUpdaterContext keeps jobs that share a tile apart only for these
    // images: a float group inside an RGBA8 image stays on the CPU.
    if (!mayCompositeOnGpu(KisNodeSP(layer))) {
        return false;
    }
    KisPaintDeviceSP device = layer->projection();
    if (!device) {
        return false;
    }

    KisGpuBlendOp op;
    if (!KisGpuProjectionCompositor::blendOpForCompositeOp(layer->compositeOpId(), &op)) {
        return false;
    }

    // Empty flags mean all channels; RGBA float channels use storage order.
    const QBitArray flags = leaf->channelFlags();
    bool alphaLocked = false;
    quint32 channelMask = 0xf;
    if (!flags.isEmpty()) {
        if (flags.size() != 4) {
            return false;
        }
        channelMask = 0;
        for (int i = 0; i < 4; ++i) {
            if (flags.testBit(i))
                channelMask |= 1u << i;
        }
        alphaLocked = !flags.testBit(3);
    }

    if (!(*device->colorSpace() == *projection->colorSpace()) || (device->x() - projection->x()) % TileSize
        || (device->y() - projection->y()) % TileSize) {
        return false;
    }

    static bool reportedUnavailable = false;
    if (!KisGpuTileAccess::isSupported(projection)) {
        if (!reportedUnavailable) {
            reportedUnavailable = true;
            QString reason;
            KisGpuTileAccess::isSupported(projection, &reason);
            dbgImage << "GPU projection not used:" << reason;
        }
        return false;
    }

    if (!m_entries.isEmpty() && (m_projection != projection || m_rect != rect)) {
        flush();
    }

    m_projection = projection;
    m_rect = rect;
    Entry entry;
    entry.leaf = leaf;
    entry.device = device;
    entry.op = quint32(op);
    entry.opacity = leaf->opacity() / 255.0f;
    entry.alphaLocked = alphaLocked;
    entry.channelMask = channelMask;
    m_entries << entry;
    return true;
#else
    Q_UNUSED(leaf);
    Q_UNUSED(projection);
    Q_UNUSED(rect);
    return false;
#endif
}

void KisGpuMergeBatch::flush()
{
    if (m_entries.isEmpty()) {
        return;
    }

    bool done = false;
#ifdef HAVE_KRITA_GPU_ENGINE
    QVector<KisGpuProjectionCompositor::Layer> layers;
    for (const Entry &entry : m_entries) {
        KisGpuProjectionCompositor::Layer layer;
        layer.device = entry.device;
        layer.op = KisGpuBlendOp(entry.op);
        layer.opacity = entry.opacity;
        layer.alphaLocked = entry.alphaLocked;
        layer.channelMask = entry.channelMask;
        layers << layer;
    }

    QString error;
    done = KisGpuProjectionCompositor::composite(m_projection, m_rect, layers, &error);
    if (done) {
        s_gpuCompositeCount++;
    } else {
        warnImage << "GPU projection failed, compositing on the CPU:" << error;
    }
#endif

    if (!done) {
        compositeOnCpu();
    }

    m_entries.clear();
    m_projection = nullptr;
    m_rect = QRect();
}

void KisGpuMergeBatch::compositeOnCpu()
{
    // Exactly what KisAsyncMerger::compositeWithProjection() does per leaf.
    for (const Entry &entry : m_entries) {
        KisPainter gc(m_projection);
        entry.leaf->projectionPlane()->apply(&gc, m_rect);
    }
}
