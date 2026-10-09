/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QTest>
#include <QtMath>

#include "KisBrushTestMain.h"

#include <KisAsynchronousStrokeUpdateHelper.h>
#include <KisBrushStrokeLayer.h>
#include <KisGlobalResourcesInterface.h>
#include <KoCanvasResourceProvider.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOpRegistry.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_settings.h>
#include <kis_filter_strategy.h>
#include <kis_image.h>
#include <kis_paint_device.h>
#include <kis_paint_information.h>
#include <kis_resources_snapshot.h>
#include <kis_undo_stores.h>
#include <strokes/KisFreehandStrokeInfo.h>
#include <strokes/freehand_stroke.h>

/**
 * Stage 2a of the brush stroke layer plan (docs/agent/brush-stroke-layer-plan.md):
 * a stroke painted on a KisBrushStrokeLayer through the brush tool's stroke
 * path is recorded, its record draws the same pixels again, and undo and
 * redo keep the record and the pixels together.
 */
class KisBrushStrokeLayerTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testRecordAndRedraw_data();
    void testRecordAndRedraw();
    void testOnlyBrushStrokesOnPaintLayers();
    void testScaleImageRedraws();
    void testScaleFallsBackAfterOtherEdits();
};

namespace
{
const QRect imageBounds(0, 0, 200, 100);

KisPaintOpPresetSP loadPreset(const QString &fileName)
{
    return KisGlobalResourcesInterface::instance()
        ->source<KisPaintOpPreset>(ResourceType::PaintOpPresets)
        .bestMatch(QString(), fileName, QString())
        .dynamicCast<KisPaintOpPreset>();
}

/// A stroke as the brush tool sends it: a point, lines with rising and
/// falling pressure, and a curve
void paintStroke(KisImageSP image, KisNodeSP node, KisPaintOpPresetSP preset, const KoColor &color)
{
    KoCanvasResourceProvider provider;
    provider.setResource(KoCanvasResource::ForegroundColor, QVariant::fromValue(color));
    provider.setResource(KoCanvasResource::BackgroundColor,
                         QVariant::fromValue(KoColor(Qt::white, image->colorSpace())));
    provider.setResource(KoCanvasResource::CurrentPaintOpPreset, QVariant::fromValue(preset));
    provider.setResource(KoCanvasResource::Opacity, 1.0);
    provider.setResource(KoCanvasResource::CurrentCompositeOp, COMPOSITE_OVER);
    provider.setResource(KoCanvasResource::CurrentEffectiveCompositeOp, COMPOSITE_OVER);
    provider.setResource(KoCanvasResource::EffectiveZoom, 1.0);
    provider.setResource(KoCanvasResource::HdrExposure, 0.0);
    provider.setResource(KoCanvasResource::EraserMode, false);
    provider.setResource(KoCanvasResource::GlobalAlphaLock, false);
    provider.setResource(KoCanvasResource::MirrorHorizontal, false);
    provider.setResource(KoCanvasResource::MirrorVertical, false);
    provider.setResource(KoCanvasResource::EffectiveLodAvailability, false);
    provider.setResource(KoCanvasResource::Size, preset->settings()->paintOpSize());

    KisResourcesSnapshotSP resources = new KisResourcesSnapshot(image, node, &provider, nullptr, {}, preset);
    resources->setOpacity(1.0);
    resources->setMirroring(false, false);
    resources->setFGColorOverride(color);

    FreehandStrokeStrategy *stroke =
        new FreehandStrokeStrategy(resources, new KisFreehandStrokeInfo(), kundo2_noi18n("test stroke"));
    KisStrokeId id = image->startStroke(stroke);

    KisPaintInformation previous(QPointF(20, 50), 0.2);
    image->addJob(id, new FreehandStrokeStrategy::Data(0, previous));
    for (int i = 1; i <= 60; i++) {
        const qreal t = i / 60.0;
        KisPaintInformation next(QPointF(20 + t * 110, 50 + 30 * qSin(t * 2 * M_PI)), qSin(t * M_PI) + 0.05);
        next.setCurrentTime(i * 4);
        image->addJob(id, new FreehandStrokeStrategy::Data(0, previous, next));
        previous = next;
    }
    KisPaintInformation end(QPointF(180, 30), 0.5);
    end.setCurrentTime(300);
    image->addJob(id, new FreehandStrokeStrategy::Data(0, previous, QPointF(150, 90), QPointF(170, 10), end));
    image->addJob(id, new KisAsynchronousStrokeUpdateHelper::UpdateData(true));
    image->endStroke(id);
    image->waitForDone();
}

QImage toImage(KisPaintDeviceSP device)
{
    return device->convertToQImage(nullptr, imageBounds);
}

QImage toImage(KisPaintDeviceSP device, const QRect &rect)
{
    return device->convertToQImage(nullptr, rect);
}

/// The share of the stroke's pixels that are half transparent
qreal edgeBlur(const QImage &image)
{
    int covered = 0;
    int partial = 0;
    for (int y = 0; y < image.height(); y++) {
        for (int x = 0; x < image.width(); x++) {
            const int alpha = qAlpha(image.pixel(x, y));
            if (alpha > 25) {
                covered++;
                partial += alpha < 230;
            }
        }
    }
    return covered ? qreal(partial) / covered : 0.0;
}

void scaleImage4x(KisImageSP image)
{
    image->scaleImage(QSize(imageBounds.width() * 4, imageBounds.height() * 4),
                      image->xRes(),
                      image->yRes(),
                      KisFilterStrategyRegistry::instance()->value(QStringLiteral("Bicubic")));
    image->waitForDone();
}

int coveredPixels(const QImage &image)
{
    int covered = 0;
    for (int y = 0; y < image.height(); y++) {
        for (int x = 0; x < image.width(); x++) {
            covered += qAlpha(image.pixel(x, y)) > 25;
        }
    }
    return covered;
}
} // namespace

void KisBrushStrokeLayerTest::testRecordAndRedraw_data()
{
    QTest::addColumn<QString>("preset");
    QTest::newRow("ink, build up") << QStringLiteral("d)_Ink-3_Gpen.kpp");
    QTest::newRow("pressure size, wash") << QStringLiteral("b)_Basic-5_Size_default.kpp");
}

void KisBrushStrokeLayerTest::testRecordAndRedraw()
{
    QFETCH(QString, preset);
    KisPaintOpPresetSP brush = loadPreset(preset);
    QVERIFY(brush);

    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisSurrogateUndoStore *undoStore = new KisSurrogateUndoStore();
    KisImageSP image = new KisImage(undoStore, imageBounds.width(), imageBounds.height(), cs, "brush stroke layer");
    KisBrushStrokeLayerSP layer = new KisBrushStrokeLayer(image, "strokes", OPACITY_OPAQUE_U8, cs);
    image->addNode(layer, image->root());

    paintStroke(image, layer, brush, KoColor(Qt::black, cs));
    paintStroke(image, layer, brush, KoColor(Qt::red, cs));
    const QImage painted = toImage(layer->paintDevice());
    QVERIFY(coveredPixels(painted) > 300);

    // recorded: two strokes, with their jobs and seeds
    QCOMPARE(layer->strokes().size(), 2);
    QCOMPARE(layer->strokes().first()->jobs.size(), 62);
    QVERIFY(layer->strokes().first()->seed != 0);
    QVERIFY(layer->strokes().first()->seed != layer->strokes().last()->seed);

    // the record draws the same pixels again
    auto redraw = [&]() {
        const QPoint offset(layer->paintDevice()->x(), layer->paintDevice()->y());
        return toImage(KisBrushStrokeLayer::renderStrokes(layer->strokes(), cs, imageBounds, offset));
    };
    QCOMPARE(redraw(), painted);

    // moving the layer moves the strokes with it
    layer->paintDevice()->moveTo(QPoint(7, -3));
    QCOMPARE(redraw(), toImage(layer->paintDevice()));
    layer->paintDevice()->moveTo(QPoint(0, 0));

    // undo removes the last stroke and its pixels together; redo restores both
    undoStore->undo();
    image->waitForDone();
    QCOMPARE(layer->strokes().size(), 1);
    QCOMPARE(redraw(), toImage(layer->paintDevice()));
    undoStore->redo();
    image->waitForDone();
    QCOMPARE(layer->strokes().size(), 2);
    QCOMPARE(toImage(layer->paintDevice()), painted);
}

/// A plain paint layer records nothing; the strokes are its own.
void KisBrushStrokeLayerTest::testOnlyBrushStrokesOnPaintLayers()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisImageSP image = new KisImage(nullptr, imageBounds.width(), imageBounds.height(), cs, "paint layer");
    KisPaintLayerSP paint = new KisPaintLayer(image, "paint", OPACITY_OPAQUE_U8, cs);
    image->addNode(paint, image->root());
    KisBrushStrokeLayerSP strokes = new KisBrushStrokeLayer(image, "strokes", OPACITY_OPAQUE_U8, cs);
    image->addNode(strokes, image->root());

    paintStroke(image, paint, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    QVERIFY(strokes->strokes().isEmpty());

    // a duplicate keeps the record
    paintStroke(image, strokes, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    KisNodeSP copy = strokes->clone();
    QVERIFY(copy->inherits("KisBrushStrokeLayer"));
    QCOMPARE(dynamic_cast<KisBrushStrokeLayer *>(copy.data())->strokes().size(), 1);
}

/// Stage 2b: scaling the image draws the strokes again at the new size;
/// undo returns to the small strokes and pixels.
void KisBrushStrokeLayerTest::testScaleImageRedraws()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisSurrogateUndoStore *undoStore = new KisSurrogateUndoStore();
    KisImageSP image = new KisImage(undoStore, imageBounds.width(), imageBounds.height(), cs, "scale");
    KisBrushStrokeLayerSP layer = new KisBrushStrokeLayer(image, "strokes", OPACITY_OPAQUE_U8, cs);
    image->addNode(layer, image->root());
    KisPaintLayerSP pixels = new KisPaintLayer(image, "pixels", OPACITY_OPAQUE_U8, cs);
    image->addNode(pixels, image->root());

    KisPaintOpPresetSP brush = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    paintStroke(image, layer, brush, KoColor(Qt::black, cs));
    paintStroke(image, pixels, brush, KoColor(Qt::black, cs));
    const QImage small = toImage(layer->paintDevice());
    const QPointF firstPoint = layer->strokes().first()->jobs.first().pi1.pos();

    scaleImage4x(image);
    const QRect large(0, 0, imageBounds.width() * 4, imageBounds.height() * 4);
    QCOMPARE(image->bounds(), large);

    // the record follows the scale and is what the layer shows
    QCOMPARE(layer->strokes().size(), 1);
    QCOMPARE(layer->strokes().first()->jobs.first().pi1.pos(), firstPoint * 4);
    const QPoint offset(layer->paintDevice()->x(), layer->paintDevice()->y());
    QCOMPARE(toImage(KisBrushStrokeLayer::renderStrokes(layer->strokes(), cs, large, offset), large),
             toImage(layer->paintDevice(), large));

    // drawn again, it is sharper than the paint layer's resampled pixels
    const qreal redrawnBlur = edgeBlur(toImage(layer->paintDevice(), large));
    const qreal resampledBlur = edgeBlur(toImage(pixels->paintDevice(), large));
    qInfo() << "edge blur: redrawn" << redrawnBlur << "resampled" << resampledBlur;
    QVERIFY(redrawnBlur < resampledBlur * 0.5);

    // undo restores the small strokes and pixels
    undoStore->undo();
    image->waitForDone();
    QCOMPARE(image->bounds(), imageBounds);
    QCOMPARE(layer->strokes().first()->jobs.first().pi1.pos(), firstPoint);
    QCOMPARE(toImage(layer->paintDevice()), small);
}

/// Pixels that are not recorded strokes are never dropped: the layer is
/// then scaled like a paint layer, and its record is left as it was.
void KisBrushStrokeLayerTest::testScaleFallsBackAfterOtherEdits()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisImageSP image =
        new KisImage(new KisSurrogateUndoStore(), imageBounds.width(), imageBounds.height(), cs, "fallback");
    KisBrushStrokeLayerSP layer = new KisBrushStrokeLayer(image, "strokes", OPACITY_OPAQUE_U8, cs);
    image->addNode(layer, image->root());

    paintStroke(image, layer, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    // an edit outside the brush tool (as a filter or a fill would make)
    layer->paintDevice()->fill(QRect(150, 70, 30, 20), KoColor(Qt::blue, cs));
    const QPointF firstPoint = layer->strokes().first()->jobs.first().pi1.pos();

    KisPaintLayerSP reference = new KisPaintLayer(image, "reference", OPACITY_OPAQUE_U8, cs);
    reference->paintDevice()->makeCloneFrom(layer->paintDevice(), layer->paintDevice()->extent());
    image->addNode(reference, image->root());

    scaleImage4x(image);
    const QRect large(0, 0, imageBounds.width() * 4, imageBounds.height() * 4);
    QCOMPARE(layer->strokes().first()->jobs.first().pi1.pos(), firstPoint);
    QCOMPARE(toImage(layer->paintDevice(), large), toImage(reference->paintDevice(), large));
    QVERIFY(coveredPixels(toImage(layer->paintDevice(), QRect(600, 280, 120, 80))) > 5000);
}

SOLSTICE_BRUSH_TEST_MAIN_WITH_BUNDLES(KisBrushStrokeLayerTest, QStringLiteral("Krita_4_Default_Resources.bundle"))

#include "KisBrushStrokeLayerTest.moc"
