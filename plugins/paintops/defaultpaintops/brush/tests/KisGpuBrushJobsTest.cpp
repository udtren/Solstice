/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../KisBrushOpSettings.h"
#include "../kis_brushop.h"
#include <KisGpuContext.h>
#include <KisLocalStrokeResources.h>
#include <KisRunnableBasedStrokeStrategy.h>
#include <KisRunnableStrokeJobData.h>
#include <KisRunnableStrokeJobsInterface.h>
#include <KisStandardOptionData.h>
#include <KoColor.h>
#include <KoColorModelStandardIds.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOpRegistry.h>
#include <QRegion>
#include <QScopeGuard>
#include <brushengine/kis_paint_information.h>
#include <cmath>
#include <gpu/KisGpuBrushPainter.h>
#include <gpu/KisGpuMergeBatch.h>
#include <gpu/KisGpuTileBackend.h>
#include <kis_image.h>
#include <kis_paint_device.h>
#include <kis_paint_layer.h>
#include <kis_painter.h>
#include <kis_pixel_selection.h>
#include <kis_selection.h>
#include <kis_transaction.h>
#include <memory>
#include <simpletest.h>
#include <vector>

namespace
{
class BrushOp : public KisBrushOp
{
public:
    using KisBrushOp::KisBrushOp;
    using KisBrushOp::paintAt;
};

class JobsStroke : public KisRunnableBasedStrokeStrategy
{
public:
    JobsStroke()
        : KisRunnableBasedStrokeStrategy(QLatin1String("gpu-brush-jobs-test"))
    {
        enableJob(JOB_DOSTROKE);
    }
};

const QRect sampleRect(-128, -128, 768, 768);
std::vector<float> pixels(KisPaintDeviceSP device)
{
    std::vector<float> result(size_t(sampleRect.width()) * sampleRect.height() * 4);
    device->readBytes(reinterpret_cast<quint8 *>(result.data()), sampleRect);
    return result;
}
float difference(const std::vector<float> &a, const std::vector<float> &b)
{
    float error = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!std::isfinite(a[i]) || !std::isfinite(b[i]))
            return INFINITY;
        error = qMax(error, std::abs(a[i] - b[i]));
    }
    return error;
}
} // namespace

class KisGpuBrushJobsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        if (!KisGpuTileBackend::instance())
            QSKIP("No GPU backend");
        KisGpuMergeBatch::setEnabled(true);
    }
    void cleanupTestCase()
    {
        if (auto *backend = KisGpuTileBackend::existingInstance()) {
            backend->flush();
            QCOMPARE(backend->context().validationErrorCount(), 0);
        }
    }
    void testJobs_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<int>("mirrors");
        QTest::addColumn<bool>("selected");
        QTest::addColumn<bool>("locked");
        QTest::addColumn<QString>("scenario");
        for (const auto &mode : {COMPOSITE_OVER, COMPOSITE_ALPHA_DARKEN, COMPOSITE_ERASE}) {
            for (int mirrors = 0; mirrors < 4; ++mirrors) {
                QTest::newRow(qPrintable(mode + QString::number(mirrors)))
                    << mode << mirrors << false << false << QStringLiteral("near");
            }
        }
        QTest::newRow("selected-alpha-lock") << COMPOSITE_OVER << 3 << true << true << QStringLiteral("near");
        QTest::newRow("selected-erase") << COMPOSITE_ERASE << 3 << true << false << QStringLiteral("near");
        QTest::newRow("erase-submit-failure") << COMPOSITE_ERASE << 3 << true << false << QStringLiteral("failure");
        QTest::newRow("selected-alpha-darken")
            << COMPOSITE_ALPHA_DARKEN << 3 << true << false << QStringLiteral("near");
        QTest::newRow("fractional-clipping") << COMPOSITE_OVER << 3 << false << false << QStringLiteral("fractional");
        QTest::newRow("distant-mirrors") << COMPOSITE_OVER << 3 << false << false << QStringLiteral("distant");
        QTest::newRow("combined-submit-failure") << COMPOSITE_OVER << 3 << true << true << QStringLiteral("failure");
        QTest::newRow("single-submit-failure") << COMPOSITE_OVER << 0 << false << false << QStringLiteral("failure");
        QTest::newRow("integer-owning-image") << COMPOSITE_OVER << 3 << false << false << QStringLiteral("integer");
        for (const auto &mode : {COMPOSITE_MULT,
                                 COMPOSITE_SCREEN,
                                 COMPOSITE_OVERLAY,
                                 COMPOSITE_LINEAR_BURN,
                                 COMPOSITE_LINEAR_LIGHT,
                                 COMPOSITE_PIN_LIGHT}) {
            QTest::newRow(qPrintable(mode + "-selected-locked")) << mode << 3 << true << true << QStringLiteral("near");
            QTest::newRow(qPrintable(mode + "-fractional"))
                << mode << 3 << false << false << QStringLiteral("fractional");
            QTest::newRow(qPrintable(mode + "-failed-submit"))
                << mode << 3 << true << true << QStringLiteral("failure");
        }
    }
    void testJobs()
    {
        QFETCH(QString, mode);
        QFETCH(int, mirrors);
        QFETCH(bool, selected);
        QFETCH(bool, locked);
        QFETCH(QString, scenario);
        const QByteArray previousBrush = qgetenv("KRITA_GPU_BRUSH");
        const auto restore = qScopeGuard([&]() {
            if (previousBrush.isNull())
                qunsetenv("KRITA_GPU_BRUSH");
            else
                qputenv("KRITA_GPU_BRUSH", previousBrush);
            KisGpuTileBackend::instance()->context().injectSubmitFailuresForTesting(0);
        });
        const auto *cs = KoColorSpaceRegistry::instance()->colorSpace(RGBAColorModelID.id(),
                                                                      Float32BitsColorDepthID.id(),
                                                                      QString());
        QVERIFY(cs);
        std::vector<float> reference;
        QRegion referenceDirty;
        // Both runs use the real brush, dab cache, update jobs and image worker
        // scheduler. Only the compositor opt-in differs; no synthetic dabs.
        for (bool gpu : {false, true}) {
            qputenv("KRITA_GPU_BRUSH", gpu ? "1" : "0");
            KisImageSP image = new KisImage(nullptr,
                                            512,
                                            512,
                                            scenario == "integer" ? KoColorSpaceRegistry::instance()->rgb8() : cs,
                                            "brush jobs");
            image->setWorkingThreadsLimit(4);
            KisPaintLayerSP layer = new KisPaintLayer(image, "paint", OPACITY_OPAQUE_U8, cs);
            image->addNode(layer, image->root());
            image->waitForDone();
            auto device = layer->paintDevice();
            const float background[] = {0.6f, 0.1f, 0.25f, 0.45f};
            device->fill(sampleRect.x(),
                         sampleRect.y(),
                         sampleRect.width(),
                         sampleRect.height(),
                         reinterpret_cast<const quint8 *>(background));
            KisPaintDeviceSP snapshot = new KisPaintDevice(*device);
            const auto before = pixels(device);
            KisSelectionSP selection;
            if (selected) {
                selection = new KisSelection();
                std::vector<quint8> mask(512 * 512);
                for (int y = 0; y < 512; ++y)
                    for (int x = 0; x < 512; ++x)
                        mask[y * 512 + x] = quint8((x + 3 * y) % 256);
                selection->pixelSelection()->writeBytes(mask.data(), QRect(0, 0, 512, 512));
            }
            KisPainter painter(device, selection);
            painter.setCompositeOpId(mode);
            if (locked) {
                QBitArray flags(4, true);
                flags.setBit(3, false);
                painter.setChannelFlags(flags);
            }
            const QPointF axis = scenario == "distant" ? QPointF(250, 250)
                : scenario == "fractional"             ? QPointF(128.5, 127.25)
                                                       : QPointF(128, 128);
            painter.setMirrorInformation(axis, mirrors & 1, mirrors & 2);
            KisPaintOpSettingsSP settings = new KisBrushOpSettings(toQShared(new KisLocalStrokeResources()));
            settings->setProperty("paintop", "paintbrush");
            settings->setProperty(
                "brush_definition",
                QStringLiteral(
                    "<Brush useAutoSpacing=\"0\" angle=\"0.37\" spacing=\"0.1\" density=\"1\" BrushVersion=\"2\" "
                    "type=\"auto_brush\" randomness=\"0\"><MaskGenerator spikes=\"2\" hfade=\"0.7\" ratio=\"0.6\" "
                    "diameter=\"79\" id=\"default\" type=\"circle\" antialiasEdges=\"1\" vfade=\"0.6\"/></Brush>"));
            KisOpacityOptionData opacity;
            opacity.useCurve = false;
            opacity.strengthValue = 0.63;
            opacity.write(settings.data());
            KisFlowOptionData flow;
            flow.useCurve = false;
            flow.strengthValue = 0.47;
            flow.write(settings.data());
            auto *strategy = new JobsStroke;
            painter.setRunnableStrokeJobsInterface(strategy->runnableJobsInterface());
            std::unique_ptr<BrushOp> brush;
            const auto stroke = image->startStroke(strategy);
            image->addJob(stroke, new KisRunnableStrokeJobData([&]() {
                              brush = std::make_unique<BrushOp>(settings, &painter, layer, image);
                          }));
            KisTransaction transaction(device);
            const auto firstBatch = KisGpuBrushPainter::batchCount();
            for (int update = 0; update < 3; ++update) {
                image->addJob(stroke, new KisRunnableStrokeJobData([&, update]() {
                                  painter.setPaintColor(KoColor(QColor(40 + 60 * update, 130, 210), cs));
                                  for (int dab = 0; dab < 8; ++dab)
                                      brush->paintAt(
                                          KisPaintInformation(QPointF(116 + dab * 3, 120 + update * 4), 0.8));
                              }));
                // Mutated dab-generation jobs finish before this sequential job.
                image->addJob(stroke, new KisRunnableStrokeJobData([&, update]() {
                                  if (gpu && scenario == "failure" && update == 0)
                                      KisGpuTileBackend::instance()->context().injectSubmitFailuresForTesting(1);
                                  QVector<KisRunnableStrokeJobData *> jobs;
                                  brush->doAsynchronousUpdate(jobs);
                                  strategy->runnableJobsInterface()->addRunnableJobs(jobs);
                              }));
            }
            image->endStroke(stroke);
            image->waitForDone();
            const auto batches = KisGpuBrushPainter::batchCount() - firstBatch;
            QVector<KisRunnableStrokeJobData *> pending;
            QVERIFY(!brush->doAsynchronousUpdate(pending).second);
            QVERIFY(pending.isEmpty());
            brush.reset();
            painter.setRunnableStrokeJobsInterface(nullptr);
            QScopedPointer<KUndo2Command> command(transaction.endAndTake());
            command->redo();
            // Transaction commands can schedule projection refreshes. Finish
            // those workers before CPU snapshots, especially for an integer image.
            image->waitForDone();
            const auto after = pixels(device);
            QVERIFY(difference(before, after) > 0.01f);
            QCOMPARE(pixels(snapshot), before);
            QRegion dirty;
            for (const auto &rect : painter.takeDirtyRegion())
                dirty += rect;
            QVERIFY(!dirty.isEmpty());
            if (!gpu) {
                QCOMPARE(batches, quint64(0));
                reference = after;
                referenceDirty = dirty;
            } else {
                const float error = difference(reference, after);
                if (error > 2e-5f) {
                    for (size_t i = 0; i < after.size(); ++i)
                        if (std::abs(reference[i] - after[i]) == error) {
                            qInfo() << "worst channel" << i << "CPU" << reference[i] << "GPU" << after[i];
                            break;
                        }
                }
                QVERIFY2(error <= 2e-5f, qPrintable(QString::number(error)));
                QCOMPARE(dirty, referenceDirty);
                if (scenario == "integer")
                    QCOMPARE(batches, quint64(0));
                else if (scenario == "near" || scenario == "fractional" || scenario == "distant")
                    QCOMPARE(batches, quint64(3)); // one completed submit per update, even for four passes
                else if (scenario == "failure")
                    QCOMPARE(batches, mirrors ? quint64(6) : quint64(2));
                else
                    QVERIFY(batches > 0);
            }
            command->undo();
            image->waitForDone();
            QCOMPARE(pixels(device), before);
            command->redo();
            image->waitForDone();
            QCOMPARE(pixels(device), after);
        }
    }
};
SIMPLE_TEST_MAIN(KisGpuBrushJobsTest)
#include "KisGpuBrushJobsTest.moc"
