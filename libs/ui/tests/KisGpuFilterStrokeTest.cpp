/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <simpletest.h>

#include <QObject>
#include <QScopeGuard>

#include <KisGlobalResourcesInterface.h>
#include <KisGpuContext.h>
#include <KoColorModelStandardIds.h>
#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>

#include "filter/kis_filter.h"
#include "filter/kis_filter_configuration.h"
#include "filter/kis_filter_registry.h"
#include "gpu/KisGpuConvolutionWorker.h"
#include "gpu/KisGpuMergeBatch.h"
#include "gpu/KisGpuTileBackend.h"
#include "kis_image.h"
#include "kis_paint_device.h"
#include "kis_paint_layer.h"
#include "kis_resources_snapshot.h"
#include "kis_undo_stores.h"
#include "strokes/kis_filter_stroke_strategy.h"

#include <cmath>
#include <random>
#include <vector>

/**
 * GPU engine phase 4.98: the blur filters applied through
 * KisFilterStrokeStrategy (the Filter dialog's apply), with the convolution
 * on the GPU, against the CPU (docs/agent/gpu-engine.md).
 */
class KisGpuFilterStrokeTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testFilterStroke_data();
    void testFilterStroke();
};

namespace
{
const KoColorSpace *rgbaFloat(bool f16)
{
    return KoColorSpaceRegistry::instance()->colorSpace(RGBAColorModelID.id(),
                                                        f16 ? Float16BitsColorDepthID.id()
                                                            : Float32BitsColorDepthID.id(),
                                                        QString());
}

std::vector<float> readFloats(KisPaintDeviceSP device, const QRect &rect)
{
    KisPaintDeviceSP floats = new KisPaintDevice(*device);
    floats->convertTo(rgbaFloat(false));
    std::vector<float> pixels(size_t(rect.width()) * rect.height() * 4);
    floats->readBytes(reinterpret_cast<quint8 *>(pixels.data()), rect);
    return pixels;
}

struct StrokeResult {
    std::vector<float> applied;
    std::vector<float> undone;
    quint64 gpuRuns = 0;
};
} // namespace

void KisGpuFilterStrokeTest::testFilterStroke_data()
{
    QTest::addColumn<bool>("f16");
    QTest::addColumn<QString>("filterId");
    for (bool f16 : {false, true})
        for (const QString id : {"gaussian blur", "unsharp", "gaussianhighpass"})
            QTest::newRow(qPrintable(QString("%1-%2").arg(f16 ? "f16" : "f32", id))) << f16 << id;
}

void KisGpuFilterStrokeTest::testFilterStroke()
{
    QFETCH(bool, f16);
    QFETCH(QString, filterId);
    KisGpuTileBackend *backend = KisGpuTileBackend::instance();
    if (!backend)
        QSKIP(qPrintable(QStringLiteral("GPU engine unavailable: %1").arg(KisGpuTileBackend::unavailableReason())));
    if (!backend->context().deviceInfo().supportsFloat64)
        QSKIP("shaderFloat64 is not supported");
    KisFilterSP filter = KisFilterRegistry::instance()->value(filterId);
    if (!filter)
        QSKIP(qPrintable(QString("filter %1 is not loaded").arg(filterId)));
    KisFilterConfigurationSP config =
        filter->defaultConfiguration(KisGlobalResourcesInterface::instance())->cloneWithResourcesSnapshot();
    if (filterId == "gaussian blur") {
        config->setProperty("horizRadius", 14);
        config->setProperty("vertRadius", 9);
    } else if (filterId == "unsharp") {
        config->setProperty("halfSize", 6);
    } else {
        config->setProperty("blurAmount", 8);
    }

    const bool previousProjection = KisGpuMergeBatch::isEnabled();
    const auto restore = qScopeGuard([&]() {
        KisGpuMergeBatch::setEnabled(previousProjection);
        qunsetenv("KRITA_GPU_CONVOLUTION");
    });
    const KoColorSpace *cs = rgbaFloat(f16);
    const QRect bounds(0, 0, 1300, 900);
    const QRect content(20, 30, 1210, 820);

    auto run = [&](bool gpuEnabled) {
        KisGpuMergeBatch::setEnabled(gpuEnabled);
        qputenv("KRITA_GPU_CONVOLUTION", gpuEnabled ? "1" : "0");
        KisSurrogateUndoStore *undoStore = new KisSurrogateUndoStore();
        KisImageSP image = new KisImage(undoStore, bounds.width(), bounds.height(), cs, "filter stroke");
        KisPaintLayerSP layer = new KisPaintLayer(image, "layer", OPACITY_OPAQUE_U8, cs);
        image->addNode(layer, image->root());

        // Smooth content with transparent areas, in [0.25, 0.65] so that
        // sharpening stays below 1 (the selection-free copy is exact anyway).
        std::mt19937 random(17);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        KisPaintDeviceSP floats = new KisPaintDevice(rgbaFloat(false));
        std::vector<float> pixels(size_t(content.width()) * content.height() * 4);
        for (int y = 0; y < content.height(); ++y)
            for (int x = 0; x < content.width(); ++x) {
                float *p = &pixels[(size_t(y) * content.width() + x) * 4];
                p[0] = 0.25f + 0.4f * (0.5f + 0.5f * std::sin(x * 0.05f + y * 0.01f));
                p[1] = 0.25f + 0.4f * unit(random);
                p[2] = 0.25f + 0.4f * (0.5f + 0.5f * std::cos(y * 0.07f));
                p[3] = (x / 40 + y / 40) % 5 == 0 ? 0.0f : 0.3f + 0.7f * unit(random);
            }
        floats->writeBytes(reinterpret_cast<const quint8 *>(pixels.data()), content);
        floats->convertTo(cs);
        layer->paintDevice()->makeCloneFrom(floats, content);
        image->initialRefreshGraph();

        const quint64 before = KisGpuConvolutionWorker::runCount();
        KisResourcesSnapshotSP resources = new KisResourcesSnapshot(image, layer);
        KisStrokeId id = image->startStroke(new KisFilterStrokeStrategy(filter, config, resources));
        image->addJob(id, new KisFilterStrokeStrategy::FilterJobData());
        image->endStroke(id);
        image->waitForDone();

        StrokeResult result;
        result.gpuRuns = KisGpuConvolutionWorker::runCount() - before;
        result.applied = readFloats(layer->paintDevice(), bounds);
        undoStore->undo();
        image->waitForDone();
        result.undone = readFloats(layer->paintDevice(), bounds);
        undoStore->redo();
        image->waitForDone();
        if (readFloats(layer->paintDevice(), bounds) != result.applied)
            result.applied.clear(); // Redo did not restore the result
        return result;
    };

    const StrokeResult cpu = run(false);
    const StrokeResult gpu = run(true);
    QCOMPARE(cpu.gpuRuns, quint64(0));
    // Gaussian Blur prefers one call over the whole layer on the GPU; the
    // other filters keep the stroke's concurrent patches (sharpening and
    // grain extract run on the CPU per patch).
    if (filterId == "gaussian blur")
        QCOMPARE(gpu.gpuRuns, quint64(1));
    else
        QVERIFY2(gpu.gpuRuns > 1, qPrintable(QString::number(gpu.gpuRuns)));
    QVERIFY(!cpu.applied.empty() && !gpu.applied.empty());
    QVERIFY(gpu.undone == cpu.undone);

    const double tolerance = f16 ? 1e-3 : 1e-5;
    const float nullAlpha = f16 ? 2.0f / 1024 : 2.4e-7f;
    int failures = 0;
    double worst = 0.0;
    for (size_t pixel = 0; pixel < cpu.applied.size() / 4; ++pixel) {
        const bool alphaOnly = qMin(cpu.applied[pixel * 4 + 3], gpu.applied[pixel * 4 + 3]) < nullAlpha;
        for (size_t c = alphaOnly ? 3 : 0; c < 4; ++c) {
            const double e = cpu.applied[pixel * 4 + c];
            const double a = gpu.applied[pixel * 4 + c];
            const double relative = std::abs(a - e) / qMax(1.0, std::abs(e));
            if (!(relative <= tolerance)) {
                if (failures++ < 5)
                    qInfo() << "mismatch at" << QPoint(int(pixel) % bounds.width(), int(pixel) / bounds.width())
                            << "channel" << c << "CPU" << e << "GPU" << a;
            } else {
                worst = qMax(worst, relative);
            }
        }
    }
    qInfo() << "GPU convolution calls" << gpu.gpuRuns << "worst relative difference" << worst;
    QCOMPARE(failures, 0);
    QVERIFY(gpu.applied != gpu.undone);
}

SIMPLE_TEST_MAIN(KisGpuFilterStrokeTest)

#include "KisGpuFilterStrokeTest.moc"
