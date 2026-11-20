/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QTest>
#include <QtMath>

#include "KisBrushTestMain.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>

#include <KisAsynchronousStrokeUpdateHelper.h>
#include <KisBrushStrokeLayer.h>
#include <KisBrushStrokePathEdit.h>
#include <KisBrushStrokeShape.h>
#include <KisBrushStrokeShapeEraser.h>
#include <KisDocument.h>
#include <KisGlobalResourcesInterface.h>
#include <KisPart.h>
#include <KoCanvasResourceProvider.h>
#include <KoColorBackground.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOpRegistry.h>
#include <KoPathShape.h>
#include <KoShapeControllerBase.h>
#include <SvgWriter.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_settings.h>
#include <kis_filter_strategy.h>
#include <kis_group_layer.h>
#include <kis_image.h>
#include <kis_layer_utils.h>
#include <kis_paint_device.h>
#include <kis_paint_information.h>
#include <kis_paint_layer.h>
#include <kis_resources_snapshot.h>
#include <kis_shape_layer.h>
#include <kis_undo_stores.h>
#include <kundo2stack.h>
#include <strokes/KisFreehandStrokeInfo.h>
#include <strokes/freehand_stroke.h>

/**
 * Brush strokes on a vector layer (docs/agent/vector-brush-strokes.md): a
 * stroke painted on a KisShapeLayer through the brush tool's stroke path
 * becomes a KisBrushStrokeShape that shows the same pixels, draws them again
 * at a new size, and is kept in a .kra file.
 */
class KisVectorBrushStrokeTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void testStrokeBecomesShape_data();
    void testStrokeBecomesShape();
    void testScaleImageRedraws();
    void testMovedShapeKeepsPicture();
    void testRotatedShapeRedraws();
    void testKraRoundTrip();
    void testSvgKeepsStroke();
    void testPlainImageStaysImageShape();
    void testManyStrokesRedrawTime();
    void testDocumentCopyWithVectorLayer_data();
    void testDocumentCopyWithVectorLayer();
    void testCutStroke();
    void testEraserCutsShapes();
    void testEraserWholeLineAndCancel();
    void testEraserOnScaledLayer();
    void testCutUpToIntersections();
    void testEraserUpToIntersection();
    void testPathEditFunctions();
    void testShapeEdit();
    void testStrokeColorSizeBrush();
    void testStrokeBackgroundColor();
    void testTransformPreview();
    void testSimplify();
    void testAddDeletePoint();
    void testSwitchCorner();
    void testDensity_data();
    void testDensity();
    void testAddWidth();
    void testMapPoints();
    void testRedraw();
    void testRedrawWidth();
    void testPathPieces();
    void testJoin();
};

namespace
{
const QRect imageBounds(0, 0, 200, 100);

/// Waits for the image with the event loop running: a vector layer changes
/// its shapes in the GUI thread, which a blocking wait can deadlock with
void waitForImage(KisImageSP image)
{
    // idle on three polls in a row: work that is about to start (a vector
    // layer's repaint, an undo's stroke) counts as busy
    QElapsedTimer timer;
    timer.start();
    int idlePolls = 0;
    while (idlePolls < 3 && timer.elapsed() < 120000) {
        QApplication::processEvents();
        QThread::msleep(2);
        idlePolls = image->isIdle() ? idlePolls + 1 : 0;
    }
    QApplication::processEvents();
}

KisPaintOpPresetSP loadPreset(const QString &fileName)
{
    return KisGlobalResourcesInterface::instance()
        ->source<KisPaintOpPreset>(ResourceType::PaintOpPresets)
        .bestMatch(QString(), fileName, QString())
        .dynamicCast<KisPaintOpPreset>();
}

/// A stroke as the brush tool sends it: a point, lines with rising and
/// falling pressure, and a curve
void paintStroke(KisImageSP image, KisNodeSP node, KisPaintOpPresetSP preset, const KoColor &color, qreal opacity = 1.0)
{
    KoCanvasResourceProvider provider;
    provider.setResource(KoCanvasResource::ForegroundColor, QVariant::fromValue(color));
    provider.setResource(KoCanvasResource::BackgroundColor,
                         QVariant::fromValue(KoColor(Qt::white, image->colorSpace())));
    provider.setResource(KoCanvasResource::CurrentPaintOpPreset, QVariant::fromValue(preset));
    provider.setResource(KoCanvasResource::Opacity, opacity);
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
    resources->setOpacity(opacity);
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
    waitForImage(image);
}

/// A straight stroke from @p from to @p to at full pressure
void paintLine(KisImageSP image,
               KisNodeSP node,
               KisPaintOpPresetSP preset,
               const KoColor &color,
               const QPointF &from,
               const QPointF &to)
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
    resources->setFGColorOverride(color);

    FreehandStrokeStrategy *stroke =
        new FreehandStrokeStrategy(resources, new KisFreehandStrokeInfo(), kundo2_noi18n("test line"));
    KisStrokeId id = image->startStroke(stroke);
    KisPaintInformation previous(from, 1.0);
    image->addJob(id, new FreehandStrokeStrategy::Data(0, previous));
    const int steps = 20;
    for (int i = 1; i <= steps; i++) {
        KisPaintInformation next(from + (to - from) * (qreal(i) / steps), 1.0);
        next.setCurrentTime(i * 4);
        image->addJob(id, new FreehandStrokeStrategy::Data(0, previous, next));
        previous = next;
    }
    image->addJob(id, new KisAsynchronousStrokeUpdateHelper::UpdateData(true));
    image->endStroke(id);
    waitForImage(image);
}

QImage toImage(KisPaintDeviceSP device, const QRect &rect = imageBounds)
{
    return device->convertToQImage(nullptr, rect).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

/// The largest difference of a premultiplied channel between two pictures
int maxDifference(const QImage &a, const QImage &b)
{
    if (a.size() != b.size()) {
        return 255;
    }
    int result = 0;
    for (int y = 0; y < a.height(); y++) {
        const QRgb *lineA = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *lineB = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); x++) {
            result = qMax(result, qAbs(qAlpha(lineA[x]) - qAlpha(lineB[x])));
            result = qMax(result, qAbs(qRed(lineA[x]) - qRed(lineB[x])));
            result = qMax(result, qAbs(qGreen(lineA[x]) - qGreen(lineB[x])));
            result = qMax(result, qAbs(qBlue(lineA[x]) - qBlue(lineB[x])));
        }
    }
    return result;
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

/// Waits until the vector layer has painted its shapes
void repaint(KisImageSP image, KisShapeLayerSP layer)
{
    // the layer hears of some changes (the image's new size) through the
    // event loop
    for (int i = 0; i < 3; i++) {
        waitForImage(image);
        layer->forceUpdateTimedNode();
        waitForImage(image);
    }
}

QList<KisBrushStrokeShape *> strokeShapes(KisShapeLayerSP layer)
{
    QList<KisBrushStrokeShape *> result;
    Q_FOREACH (KoShape *shape, layer->shapes()) {
        if (KisBrushStrokeShape *stroke = dynamic_cast<KisBrushStrokeShape *>(shape)) {
            result << stroke;
        }
    }
    return result;
}

struct Document {
    QScopedPointer<KisDocument> document;

    KisImageSP image;
    KisShapeLayerSP layer;

    /// @p resolution in pixels per point, set before the layer exists, as
    /// in a document made with that resolution (1: 72 ppi)
    Document(qreal resolution = 1.0)
        : document(KisPart::instance()->createDocument())
    {
        document->setFileBatchMode(true);

        const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
        image =
            new KisImage(document->createUndoStore(), imageBounds.width(), imageBounds.height(), cs, "vector strokes");
        image->setResolution(resolution, resolution);
        document->setCurrentImage(image);
        layer = new KisShapeLayer(document->shapeController(), image, "vector", OPACITY_OPAQUE_U8);
        image->addNode(layer, image->root());
        waitForImage(image);
    }
};

/// One step back or forward. The document's undo stack skips a step while
/// the image is busy (it only tries to lock it), so the image and the
/// layer's repaint are settled first.
void undoStep(Document &d)
{
    repaint(d.image, d.layer);
    d.document->undoStack()->undo();
    waitForImage(d.image);
}

void redoStep(Document &d)
{
    repaint(d.image, d.layer);
    d.document->undoStack()->redo();
    waitForImage(d.image);
}

/// The same stroke on a paint layer
QImage paintLayerStroke(KisPaintOpPresetSP preset, const KoColor &color, qreal opacity)
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisImageSP image = new KisImage(nullptr, imageBounds.width(), imageBounds.height(), cs, "paint layer");
    KisPaintLayerSP layer = new KisPaintLayer(image, "paint", OPACITY_OPAQUE_U8, cs);
    image->addNode(layer, image->root());
    paintStroke(image, layer, preset, color, opacity);
    return toImage(layer->paintDevice());
}
} // namespace

namespace
{
QtMessageHandler s_previousHandler = nullptr;
QAtomicInt s_timerWarnings;

/// Counts Qt's warning about a timer stopped from another thread: an object
/// that lives in the GUI thread was destroyed elsewhere, and its timer then
/// fires on a dead object (see the wiki pitfall on temporary images)
void countTimerWarnings(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message.contains(QLatin1String("Timers cannot be stopped"))) {
        s_timerWarnings.ref();
    }
    if (s_previousHandler) {
        s_previousHandler(type, context, message);
    }
}
} // namespace

void KisVectorBrushStrokeTest::initTestCase()
{
    s_previousHandler = qInstallMessageHandler(countTimerWarnings);
}

void KisVectorBrushStrokeTest::cleanupTestCase()
{
    qInstallMessageHandler(s_previousHandler);
    QCOMPARE(s_timerWarnings.loadRelaxed(), 0);
}

void KisVectorBrushStrokeTest::testStrokeBecomesShape_data()
{
    QTest::addColumn<QString>("preset");
    QTest::addColumn<qreal>("opacity");
    QTest::newRow("ink, build up") << QStringLiteral("d)_Ink-3_Gpen.kpp") << 1.0;
    QTest::newRow("pressure size, wash") << QStringLiteral("b)_Basic-5_Size_default.kpp") << 1.0;
    QTest::newRow("wash, half opacity") << QStringLiteral("b)_Basic-5_Size_default.kpp") << 0.5;
}

void KisVectorBrushStrokeTest::testStrokeBecomesShape()
{
    QFETCH(QString, preset);
    QFETCH(qreal, opacity);
    KisPaintOpPresetSP brush = loadPreset(preset);
    QVERIFY(brush);
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    const KoColor color(Qt::black, cs);

    const QImage reference = paintLayerStroke(brush, color, opacity);
    QVERIFY(coveredPixels(reference) > 300);

    Document d;
    paintStroke(d.image, d.layer, brush, color, opacity);

    // one shape with the recorded stroke
    QCOMPARE(d.layer->shapes().size(), 1);
    QCOMPARE(strokeShapes(d.layer).size(), 1);
    KisBrushStrokeShape *shape = strokeShapes(d.layer).first();
    QVERIFY(shape->brushStroke());
    QCOMPARE(shape->brushStroke()->jobs.size(), 62);
    QVERIFY(!d.layer->hasTemporaryTarget());

    // the layer shows what a paint layer would, at once and after it has
    // painted the shape
    QVERIFY2(maxDifference(toImage(d.layer->projection()), reference) <= 2, "right after the stroke");
    repaint(d.image, d.layer);
    QVERIFY2(maxDifference(toImage(d.layer->projection()), reference) <= 2, "after the layer painted the shape");

    // the brush draws the shape's stroke as it was painted
    const QImage redrawn = toImage(KisBrushStrokeLayer::renderStrokes({shape->brushStroke()},
                                                                      cs,
                                                                      imageBounds,
                                                                      shape->brushStroke()->deviceOffset));
    QVERIFY2(maxDifference(redrawn, reference) <= 2, "the stroke drawn again");

    // a second stroke lies above the first
    paintStroke(d.image, d.layer, brush, KoColor(Qt::red, cs), opacity);
    QCOMPARE(strokeShapes(d.layer).size(), 2);
    QVERIFY(strokeShapes(d.layer).last()->zIndex() != strokeShapes(d.layer).first()->zIndex());

    // undo removes the strokes' shapes, redo returns them
    undoStep(d);
    waitForImage(d.image);
    QCOMPARE(d.layer->shapes().size(), 1);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), reference) <= 2);

    undoStep(d);
    waitForImage(d.image);
    QCOMPARE(d.layer->shapes().size(), 0);
    repaint(d.image, d.layer);
    QCOMPARE(coveredPixels(toImage(d.layer->projection())), 0);

    redoStep(d);
    waitForImage(d.image);
    QCOMPARE(d.layer->shapes().size(), 1);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), reference) <= 2);
}

/// Scaling the image draws the stroke again with a larger brush
void KisVectorBrushStrokeTest::testScaleImageRedraws()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisPaintOpPresetSP brush = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    QVERIFY(brush);

    Document d;
    KisPaintLayerSP pixels = new KisPaintLayer(d.image, "pixels", OPACITY_OPAQUE_U8, cs);
    d.image->addNode(pixels, d.image->root());
    paintStroke(d.image, d.layer, brush, KoColor(Qt::black, cs));
    paintStroke(d.image, pixels, brush, KoColor(Qt::black, cs));
    repaint(d.image, d.layer);
    const QImage small = toImage(d.layer->projection());

    d.image->scaleImage(QSize(imageBounds.width() * 4, imageBounds.height() * 4),
                        d.image->xRes(),
                        d.image->yRes(),
                        KisFilterStrategyRegistry::instance()->value(QStringLiteral("Bicubic")));
    repaint(d.image, d.layer);
    const QRect large(0, 0, imageBounds.width() * 4, imageBounds.height() * 4);
    QCOMPARE(d.image->bounds(), large);

    // what the brush draws at four times the size
    KisBrushStrokeShape *shape = strokeShapes(d.layer).first();
    const QImage expected = toImage(KisBrushStrokeLayer::renderStrokes({shape->brushStroke()},
                                                                       cs,
                                                                       imageBounds,
                                                                       shape->brushStroke()->deviceOffset,
                                                                       QTransform::fromScale(4, 4)),
                                    large);
    const QImage scaled = toImage(d.layer->projection(), large);
    QVERIFY(coveredPixels(scaled) > coveredPixels(small) * 12);
    QVERIFY2(maxDifference(scaled, expected) <= 2, "the layer shows the stroke drawn at the new size");

    // sharper than the paint layer's resampled pixels
    const qreal redrawnBlur = edgeBlur(scaled);
    const qreal resampledBlur = edgeBlur(toImage(pixels->paintDevice(), large));
    qInfo() << "edge blur: redrawn" << redrawnBlur << "resampled" << resampledBlur;
    QVERIFY(redrawnBlur < resampledBlur * 0.5);

    // undo returns the small stroke
    undoStep(d);
    repaint(d.image, d.layer);
    QCOMPARE(d.image->bounds(), imageBounds);
    QVERIFY(maxDifference(toImage(d.layer->projection()), small) <= 2);
}

/// A move by whole pixels shows the same pixels elsewhere
void KisVectorBrushStrokeTest::testMovedShapeKeepsPicture()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    Document d;
    paintStroke(d.image, d.layer, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    repaint(d.image, d.layer);
    const QRect wide = imageBounds.adjusted(-40, -40, 40, 40);
    const QImage before = toImage(d.layer->projection(), wide);

    KisBrushStrokeShape *shape = strokeShapes(d.layer).first();
    shape->update();
    shape->setTransformation(shape->transformation() * QTransform::fromTranslate(7, -3));
    shape->update();
    repaint(d.image, d.layer);

    const QImage after = toImage(d.layer->projection(), wide);
    QCOMPARE(after.copy(QRect(47, 37, 200, 100)), before.copy(QRect(40, 40, 200, 100)));
}

/// A rotated shape is drawn again along the rotated path
void KisVectorBrushStrokeTest::testRotatedShapeRedraws()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    Document d;
    paintStroke(d.image, d.layer, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    repaint(d.image, d.layer);
    const QRect wide(-200, -200, 600, 600);
    const int before = coveredPixels(toImage(d.layer->projection(), wide));

    KisBrushStrokeShape *shape = strokeShapes(d.layer).first();
    const QTransform rotation =
        QTransform::fromTranslate(-100, -50) * QTransform().rotate(90) * QTransform::fromTranslate(100, 50);
    shape->update();
    shape->setTransformation(shape->transformation() * rotation);
    shape->update();
    repaint(d.image, d.layer);

    const QImage rotated = toImage(d.layer->projection(), wide);
    const int after = coveredPixels(rotated);
    qInfo() << "covered pixels before" << before << "after a quarter turn" << after;
    QVERIFY(qAbs(after - before) < before * 0.15);

    // the stroke's first point, turned around the image's centre
    const QPointF first = rotation.map(QPointF(60, 71));
    Q_UNUSED(first);
    const KisRecordedBrushStroke::Job job = shape->brushStroke()->jobs[20];
    const QPoint turned = rotation.map(job.pi2.pos()).toPoint() - wide.topLeft();
    QVERIFY(qAlpha(rotated.pixel(turned)) > 100);
}

void KisVectorBrushStrokeTest::testKraRoundTrip()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("vector_brush_strokes.kra"));

    QImage pixels;
    {
        // as a new document: 300 ppi
        Document d(300.0 / 72.0);
        KisPaintOpPresetSP brush = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
        paintStroke(d.image, d.layer, brush, KoColor(Qt::black, cs));
        paintStroke(d.image, d.layer, brush, KoColor(Qt::red, cs));
        repaint(d.image, d.layer);
        pixels = toImage(d.layer->projection());
        QVERIFY(d.document->exportDocumentSync(path, KisDocument::nativeFormatMimeType()));
    }

    QScopedPointer<KisDocument> document(KisPart::instance()->createDocument());
    document->setFileBatchMode(true);
    QVERIFY(document->openPath(path, KisDocument::DontAddToRecent));
    KisImageSP image = document->image();
    waitForImage(image);

    KisShapeLayerSP layer;
    KisLayerUtils::recursiveApplyNodes(image->root(), [&](KisNodeSP node) {
        if (KisShapeLayer *shapeLayer = dynamic_cast<KisShapeLayer *>(node.data())) {
            layer = shapeLayer;
        }
    });
    QVERIFY(layer);
    const QList<KisBrushStrokeShape *> shapes = strokeShapes(layer);
    QCOMPARE(shapes.size(), 2);
    Q_FOREACH (KisBrushStrokeShape *shape, shapes) {
        QVERIFY(shape->brushStroke());
        QCOMPARE(shape->brushStroke()->jobs.size(), 62);
    }

    repaint(image, layer);
    QVERIFY(maxDifference(toImage(layer->projection()), pixels) <= 2);

    // the loaded strokes are drawn again at a new size
    image->scaleImage(QSize(imageBounds.width() * 4, imageBounds.height() * 4),
                      image->xRes(),
                      image->yRes(),
                      KisFilterStrategyRegistry::instance()->value(QStringLiteral("Bicubic")));
    repaint(image, layer);
    const QRect large(0, 0, imageBounds.width() * 4, imageBounds.height() * 4);
    const qreal blur = edgeBlur(toImage(layer->projection(), large));
    qInfo() << "edge blur after loading and scaling" << blur;
    QVERIFY(blur < 0.12);
}

/// Outside a layer's file (the clipboard, an SVG file) the shape carries
/// its stroke itself
void KisVectorBrushStrokeTest::testSvgKeepsStroke()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    Document d;
    paintStroke(d.image, d.layer, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    KisBrushStrokeShape *shape = strokeShapes(d.layer).first();

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    {
        SvgWriter writer(d.layer->shapes());
        QVERIFY(writer.save(buffer, QSizeF(imageBounds.size())));
    }
    buffer.close();
    QVERIFY(buffer.data().contains("data-solstice-brush-stroke="));

    buffer.open(QIODevice::ReadOnly);
    QSizeF fragmentSize;
    const QList<KoShape *> loaded = KisShapeLayer::createShapesFromSvg(&buffer,
                                                                       QString(),
                                                                       imageBounds,
                                                                       72.0 * d.image->xRes(),
                                                                       d.document->shapeController()->resourceManager(),
                                                                       false,
                                                                       &fragmentSize);
    QCOMPARE(loaded.size(), 1);
    KisBrushStrokeShape *copy = dynamic_cast<KisBrushStrokeShape *>(loaded.first());
    QVERIFY(copy);
    QVERIFY(copy->brushStroke());
    QCOMPARE(copy->brushStroke()->jobs.size(), shape->brushStroke()->jobs.size());
    QCOMPARE(copy->recordRect(), shape->recordRect());
    QCOMPARE(copy->absoluteTransformation(), shape->absoluteTransformation());
    QCOMPARE(copy->size(), shape->size());
    QVERIFY(maxDifference(copy->staticImage(), shape->staticImage()) <= 1);
    qDeleteAll(loaded);
}

/// An image element that is not a brush stroke still loads as an image
void KisVectorBrushStrokeTest::testPlainImageStaysImageShape()
{
    Document d;
    QByteArray svg(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
        "width=\"200pt\" height=\"100pt\" viewBox=\"0 0 200 100\">"
        "<image width=\"10px\" height=\"10px\" xlink:href=\"data:image/png;base64,");
    QImage picture(2, 2, QImage::Format_ARGB32);
    picture.fill(Qt::red);
    QBuffer png;
    png.open(QIODevice::WriteOnly);
    picture.save(&png, "PNG");
    svg += png.data().toBase64() + "\"/></svg>";

    QBuffer buffer(&svg);
    buffer.open(QIODevice::ReadOnly);
    QSizeF fragmentSize;
    const QList<KoShape *> loaded = KisShapeLayer::createShapesFromSvg(&buffer,
                                                                       QString(),
                                                                       imageBounds,
                                                                       72.0,
                                                                       d.document->shapeController()->resourceManager(),
                                                                       false,
                                                                       &fragmentSize);
    QCOMPARE(loaded.size(), 1);
    QVERIFY(!dynamic_cast<KisBrushStrokeShape *>(loaded.first()));
    QCOMPARE(loaded.first()->shapeId(), QStringLiteral("ImageShape"));
    qDeleteAll(loaded);
}

/// How long a layer of many strokes takes to draw again (reported, not
/// judged)
void KisVectorBrushStrokeTest::testManyStrokesRedrawTime()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisPaintOpPresetSP brush = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    Document d;
    const int count = 100;
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < count; i++) {
        paintStroke(d.image, d.layer, brush, KoColor(Qt::black, cs));
    }
    const qint64 painting = timer.restart();
    repaint(d.image, d.layer);
    const qint64 firstRepaint = timer.restart();
    QCOMPARE(strokeShapes(d.layer).size(), count);

    d.image->scaleImage(QSize(imageBounds.width() * 2, imageBounds.height() * 2),
                        d.image->xRes(),
                        d.image->yRes(),
                        KisFilterStrategyRegistry::instance()->value(QStringLiteral("Bicubic")));
    repaint(d.image, d.layer);
    const qint64 scaling = timer.restart();
    qInfo() << count << "strokes: painting" << painting << "ms, first repaint" << firstRepaint
            << "ms, scaling the image by 2 with redraw" << scaling << "ms";
}

void KisVectorBrushStrokeTest::testDocumentCopyWithVectorLayer_data()
{
    QTest::addColumn<bool>("brushStroke");
    QTest::newRow("path shape") << false;
    QTest::newRow("brush stroke") << true;
}

/// Saving and autosave copy the document. With a resolution other than 72
/// ppi the copied vector layer took its resolution for changed, dropped
/// its picture and asked for a repaint while the image's copy constructor
/// still ran; that repaint deleted the image under construction (a crash,
/// or a freeze on autosave). A copy keeps its picture now.
void KisVectorBrushStrokeTest::testDocumentCopyWithVectorLayer()
{
    QFETCH(bool, brushStroke);
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    Document d(300.0 / 72.0);

    if (brushStroke) {
        paintStroke(d.image, d.layer, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    } else {
        KoPathShape *triangle = new KoPathShape();
        triangle->moveTo(QPointF(5, 5));
        triangle->lineTo(QPointF(30, 5));
        triangle->lineTo(QPointF(30, 20));
        triangle->close();
        triangle->setBackground(QSharedPointer<KoColorBackground>(new KoColorBackground(Qt::red)));
        d.layer->addShape(triangle);
    }
    repaint(d.image, d.layer);
    const QImage original = toImage(d.layer->projection());
    QVERIFY(coveredPixels(original) > 300);

    QScopedPointer<KisDocument> copy(d.document->clone());
    QVERIFY(copy->image());
    KisShapeLayerSP copiedLayer;
    KisLayerUtils::recursiveApplyNodes(copy->image()->root(), [&](KisNodeSP node) {
        if (KisShapeLayer *shapeLayer = dynamic_cast<KisShapeLayer *>(node.data())) {
            copiedLayer = shapeLayer;
        }
    });
    QVERIFY(copiedLayer);
    QCOMPARE(copiedLayer->shapes().size(), 1);

    // the copy needs no update, as saving expects of it (an assert in
    // KisDocument::initiateSavingInBackground()), and shows the same pixels
    // at once and after a repaint
    QVERIFY(!KisLayerUtils::hasDelayedNodeWithUpdates(copy->image()->root()));
    QVERIFY(maxDifference(toImage(copiedLayer->projection()), original) <= 2);
    copiedLayer->shapes().first()->update();
    repaint(copy->image(), copiedLayer);
    QVERIFY(maxDifference(toImage(copiedLayer->projection()), original) <= 2);
}

namespace
{
/// The test stroke runs left to right; a vertical eraser line at x crosses
/// it once (x up to 130) or its closing curve (x above 130)
const qreal eraserRadius = 4.0;

qreal distanceToColumn(const QPointF &point, qreal x)
{
    return qAbs(point.x() - x);
}

/// How many pixels of two pictures differ clearly, away from the column at
/// x. The part after a cut starts its dab spacing anew, which can move its
/// last dabs by part of a pixel.
int differingPixelsAwayFrom(const QImage &a, const QImage &b, int x, int margin)
{
    int differing = 0;
    for (int y = 0; y < a.height(); y++) {
        for (int column = 0; column < a.width(); column++) {
            if (qAbs(column - x) >= margin && qAbs(qAlpha(a.pixel(column, y)) - qAlpha(b.pixel(column, y))) > 16) {
                differing++;
            }
        }
    }
    return differing;
}
} // namespace

/// The eraser's geometry: a cut leaves the parts on both sides, with a new
/// point on the eraser's edge
void KisVectorBrushStrokeTest::testCutStroke()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    Document d;
    paintStroke(d.image, d.layer, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();
    QCOMPARE(stroke->jobs.size(), 62);

    // not reached: the stroke itself
    bool touched = true;
    QVector<KisRecordedBrushStrokeSP> pieces =
        KisBrushStrokeShapeEraser::cutStroke(stroke, QPointF(75, -60), QPointF(75, -40), eraserRadius, &touched);
    QVERIFY(!touched);
    QCOMPARE(pieces.size(), 1);
    QCOMPARE(pieces.first(), stroke);

    // across the straight segments
    pieces = KisBrushStrokeShapeEraser::cutStroke(stroke, QPointF(75, 0), QPointF(75, 100), eraserRadius, &touched);
    QVERIFY(touched);
    QCOMPARE(pieces.size(), 2);
    const KisRecordedBrushStroke::Job headEnd = pieces[0]->jobs.last();
    const KisRecordedBrushStroke::Job tailStart = pieces[1]->jobs.first();
    QCOMPARE(pieces[0]->jobs.first().type, int(FreehandStrokeStrategy::Data::POINT));
    QCOMPARE(headEnd.type, int(FreehandStrokeStrategy::Data::LINE));
    QCOMPARE(tailStart.type, int(FreehandStrokeStrategy::Data::LINE));
    // the new end points lie where the line's edge meets the eraser's:
    // half the brush's size times the pen pressure further out
    const qreal halfBrush = stroke->preset->settings()->paintOpSize() * 0.5;
    QVERIFY(qAbs(distanceToColumn(headEnd.pi2.pos(), 75) - eraserRadius - halfBrush * qMin(1.0, headEnd.pi2.pressure()))
            < 0.01);
    QVERIFY(
        qAbs(distanceToColumn(tailStart.pi1.pos(), 75) - eraserRadius - halfBrush * qMin(1.0, tailStart.pi1.pressure()))
        < 0.01);
    QVERIFY(headEnd.pi2.pos().x() < 75);
    QVERIFY(tailStart.pi1.pos().x() > 75);
    // with the pen values between their neighbours'
    QVERIFY(headEnd.pi2.pressure() >= qMin(headEnd.pi1.pressure(), 1.0) - 0.2);
    QVERIFY(headEnd.pi2.pressure() > 0.5 && headEnd.pi2.pressure() <= 1.05);
    QVERIFY(headEnd.pi2.currentTime() >= headEnd.pi1.currentTime());
    // nothing of the kept jobs is inside the eraser, and no job is lost outside it
    int keptJobs = 0;
    for (const KisRecordedBrushStrokeSP &piece : pieces) {
        for (const KisRecordedBrushStroke::Job &job : piece->jobs) {
            QVERIFY(distanceToColumn(job.pi1.pos(), 75) >= eraserRadius - 0.01);
            keptJobs++;
        }
    }
    QVERIFY(keptJobs >= 56 && keptJobs <= 62);
    // the first part starts as the stroke did, the second anew
    QCOMPARE(pieces[0]->starts.first().dabSeqNo, stroke->starts.first().dabSeqNo);
    QVERIFY(!pieces[1]->starts.first().hasLastDab);
    QCOMPARE(pieces[1]->seed, stroke->seed);

    // across the closing curve: both parts of it stay curves
    pieces = KisBrushStrokeShapeEraser::cutStroke(stroke, QPointF(160, 0), QPointF(160, 100), 3.0, &touched);
    QVERIFY(touched);
    QCOMPARE(pieces.size(), 2);
    const KisRecordedBrushStroke::Job curveHead = pieces[0]->jobs.last();
    QCOMPARE(pieces[1]->jobs.size(), 1);
    const KisRecordedBrushStroke::Job curveTail = pieces[1]->jobs.first();
    QCOMPARE(curveHead.type, int(FreehandStrokeStrategy::Data::CURVE));
    QCOMPARE(curveTail.type, int(FreehandStrokeStrategy::Data::CURVE));
    QVERIFY(qAbs(distanceToColumn(curveHead.pi2.pos(), 160) - 3.0 - halfBrush * qMin(1.0, curveHead.pi2.pressure()))
            < 0.01);
    QVERIFY(qAbs(distanceToColumn(curveTail.pi1.pos(), 160) - 3.0 - halfBrush * qMin(1.0, curveTail.pi1.pressure()))
            < 0.01);
    QCOMPARE(curveHead.pi1.pos(), stroke->jobs.last().pi1.pos());
    QCOMPARE(curveTail.pi2.pos(), stroke->jobs.last().pi2.pos());

    // an eraser over the whole stroke leaves nothing
    pieces = KisBrushStrokeShapeEraser::cutStroke(stroke, QPointF(0, 50), QPointF(200, 50), 200.0, &touched);
    QVERIFY(touched);
    QVERIFY(pieces.isEmpty());
}

/// On the layer: the stroke's shape becomes two, the pixels beside the cut
/// stay, and one undo step returns the stroke
void KisVectorBrushStrokeTest::testEraserCutsShapes()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    Document d;
    paintStroke(d.image, d.layer, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    repaint(d.image, d.layer);
    const QImage before = toImage(d.layer->projection());
    const qreal zIndex = strokeShapes(d.layer).first()->zIndex();

    {
        KisBrushStrokeShapeEraser eraser(d.layer, KisBrushStrokeShapeEraser::TouchedPart);
        // in two steps, as a drag
        eraser.erase(QPointF(75, 0), QPointF(75, 50), eraserRadius);
        eraser.erase(QPointF(75, 50), QPointF(75, 100), eraserRadius);
        QVERIFY(eraser.hasChanges());
        QCOMPARE(strokeShapes(d.layer).size(), 2);
        KUndo2Command *command = eraser.finish();
        QVERIFY(command);
        d.document->undoStack()->push(command);
    }
    QCOMPARE(strokeShapes(d.layer).size(), 2);
    Q_FOREACH (KisBrushStrokeShape *shape, strokeShapes(d.layer)) {
        QCOMPARE(shape->zIndex(), zIndex);
        QVERIFY(shape->brushStroke());
    }

    repaint(d.image, d.layer);
    const QImage erased = toImage(d.layer->projection());
    // nothing left where the eraser's centre passed
    QCOMPARE(coveredPixels(erased.copy(74, 0, 3, 100)), 0);
    QVERIFY(coveredPixels(erased) < coveredPixels(before));
    QVERIFY(coveredPixels(erased) > coveredPixels(before) * 0.8);
    // the rest of the line is as it was
    const int differing = differingPixelsAwayFrom(erased, before, 75, 12);
    qInfo() << "pixels that differ beside the cut" << differing << "of" << coveredPixels(before);
    QVERIFY(differing <= 6);

    undoStep(d);
    QCOMPARE(strokeShapes(d.layer).size(), 1);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), before) <= 2);

    redoStep(d);
    QCOMPARE(strokeShapes(d.layer).size(), 2);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), erased) <= 2);

    // the parts are saved and loaded as strokes of their own
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("erased.kra"));
    QVERIFY(d.document->exportDocumentSync(path, KisDocument::nativeFormatMimeType()));
    QScopedPointer<KisDocument> document(KisPart::instance()->createDocument());
    document->setFileBatchMode(true);
    QVERIFY(document->openPath(path, KisDocument::DontAddToRecent));
    waitForImage(document->image());
    KisShapeLayerSP loaded;
    KisLayerUtils::recursiveApplyNodes(document->image()->root(), [&](KisNodeSP node) {
        if (KisShapeLayer *shapeLayer = dynamic_cast<KisShapeLayer *>(node.data())) {
            loaded = shapeLayer;
        }
    });
    QVERIFY(loaded);
    QCOMPARE(strokeShapes(loaded).size(), 2);
    repaint(document->image(), loaded);
    QVERIFY(maxDifference(toImage(loaded->projection()), erased) <= 2);
}

void KisVectorBrushStrokeTest::testEraserWholeLineAndCancel()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    Document d;
    KisPaintOpPresetSP brush = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    paintStroke(d.image, d.layer, brush, KoColor(Qt::black, cs));
    repaint(d.image, d.layer);
    const QImage before = toImage(d.layer->projection());

    // cancelled: the layer is as before
    {
        KisBrushStrokeShapeEraser eraser(d.layer, KisBrushStrokeShapeEraser::TouchedPart);
        eraser.erase(QPointF(75, 0), QPointF(75, 100), eraserRadius);
        eraser.erase(QPointF(100, 0), QPointF(100, 100), eraserRadius);
        QCOMPARE(strokeShapes(d.layer).size(), 3);
        eraser.cancel();
        QCOMPARE(strokeShapes(d.layer).size(), 1);
        QVERIFY(!eraser.finish());
    }
    // dropped without finishing: cancelled too
    {
        KisBrushStrokeShapeEraser eraser(d.layer, KisBrushStrokeShapeEraser::TouchedPart);
        eraser.erase(QPointF(75, 0), QPointF(75, 100), eraserRadius);
        QCOMPARE(strokeShapes(d.layer).size(), 2);
    }
    QCOMPARE(strokeShapes(d.layer).size(), 1);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), before) <= 2);

    // a miss changes nothing
    {
        KisBrushStrokeShapeEraser eraser(d.layer, KisBrushStrokeShapeEraser::WholeLine);
        eraser.erase(QPointF(5, 5), QPointF(8, 5), eraserRadius);
        QVERIFY(!eraser.hasChanges());
        QVERIFY(!eraser.finish());
    }

    // the whole line goes at a touch, and comes back on undo
    {
        KisBrushStrokeShapeEraser eraser(d.layer, KisBrushStrokeShapeEraser::WholeLine);
        eraser.erase(QPointF(75, 0), QPointF(75, 100), eraserRadius);
        QCOMPARE(strokeShapes(d.layer).size(), 0);
        d.document->undoStack()->push(eraser.finish());
    }
    repaint(d.image, d.layer);
    QCOMPARE(coveredPixels(toImage(d.layer->projection())), 0);
    undoStep(d);
    QCOMPARE(strokeShapes(d.layer).size(), 1);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), before) <= 2);
}

/// The eraser works in image pixels whatever the layer's size
void KisVectorBrushStrokeTest::testEraserOnScaledLayer()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    Document d;
    paintStroke(d.image, d.layer, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    d.image->scaleImage(QSize(imageBounds.width() * 2, imageBounds.height() * 2),
                        d.image->xRes(),
                        d.image->yRes(),
                        KisFilterStrategyRegistry::instance()->value(QStringLiteral("Bicubic")));
    repaint(d.image, d.layer);
    const QRect large(0, 0, imageBounds.width() * 2, imageBounds.height() * 2);
    const QImage before = toImage(d.layer->projection(), large);

    KisBrushStrokeShapeEraser eraser(d.layer, KisBrushStrokeShapeEraser::TouchedPart);
    eraser.erase(QPointF(150, 0), QPointF(150, 200), 8.0);
    QCOMPARE(strokeShapes(d.layer).size(), 2);
    d.document->undoStack()->push(eraser.finish());

    repaint(d.image, d.layer);
    const QImage erased = toImage(d.layer->projection(), large);
    QCOMPARE(coveredPixels(erased.copy(148, 0, 5, 200)), 0);
    QVERIFY(coveredPixels(erased) > coveredPixels(before) * 0.8);
    const int differing = differingPixelsAwayFrom(erased, before, 150, 24);
    qInfo() << "pixels that differ beside the cut at twice the size" << differing << "of" << coveredPixels(before);
    QVERIFY(differing <= 12);
}

/// Up to intersection: a touched line goes as far as the lines crossing it
void KisVectorBrushStrokeTest::testCutUpToIntersections()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    Document d;
    paintStroke(d.image, d.layer, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();

    using Eraser = KisBrushStrokeShapeEraser;
    auto column = [](qreal x) {
        return Eraser::OtherLine{QPolygonF({QPointF(x, 0), QPointF(x, 100)}), 1.0};
    };
    auto touch = [&](qreal x, const QVector<Eraser::OtherLine> &others, bool *touched) {
        return Eraser::cutStrokeUpToIntersections(stroke, QPointF(x, 0), QPointF(x, 100), 1.0, others, touched);
    };
    const QVector<Eraser::OtherLine> columns{column(60), column(100)};

    // the stroke's own path, for the other strokes of a layer
    const QVector<QPolygonF> lines = Eraser::strokeLines(stroke);
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.first().first(), QPointF(20, 50));
    QCOMPARE(lines.first().last(), QPointF(180, 30));

    // a miss
    bool touched = true;
    QVector<KisRecordedBrushStrokeSP> pieces =
        Eraser::cutStrokeUpToIntersections(stroke, QPointF(80, -50), QPointF(80, -40), 1.0, columns, &touched);
    QVERIFY(!touched);
    QCOMPARE(pieces.size(), 1);
    QCOMPARE(pieces.first(), stroke);

    // between the two crossings: that stretch goes, both outer parts stay
    pieces = touch(80, columns, &touched);
    QVERIFY(touched);
    QCOMPARE(pieces.size(), 2);
    QVERIFY(qAbs(pieces[0]->jobs.last().pi2.pos().x() - 60) < 0.01);
    QVERIFY(qAbs(pieces[1]->jobs.first().pi1.pos().x() - 100) < 0.01);
    QCOMPARE(pieces[0]->jobs.first().type, int(FreehandStrokeStrategy::Data::POINT));
    QCOMPARE(pieces[1]->jobs.last().pi2.pos(), QPointF(180, 30));

    // before the first crossing: from the line's start, with its first dab
    pieces = touch(40, columns, &touched);
    QCOMPARE(pieces.size(), 1);
    QCOMPARE(pieces[0]->jobs.first().type, int(FreehandStrokeStrategy::Data::LINE));
    QVERIFY(qAbs(pieces[0]->jobs.first().pi1.pos().x() - 60) < 0.01);
    QCOMPARE(pieces[0]->jobs.last().pi2.pos(), QPointF(180, 30));

    // after the last crossing, on the closing curve: up to the line's end
    pieces = touch(150, columns, &touched);
    QCOMPARE(pieces.size(), 1);
    QCOMPARE(pieces[0]->jobs.first().pi1.pos(), QPointF(20, 50));
    QVERIFY(qAbs(pieces[0]->jobs.last().pi2.pos().x() - 100) < 0.01);

    // no crossing at all: the whole line
    pieces = touch(80, {}, &touched);
    QVERIFY(touched);
    QVERIFY(pieces.isEmpty());

    // a line that ends at the stroke without crossing it counts: here it
    // stops two pixels short of the stroke's path (y 41.5 at x 80)
    const Eraser::OtherLine stem{QPolygonF({QPointF(80, 0), QPointF(80, 39.5)}), 2.5};
    pieces = touch(40, {stem}, &touched);
    QCOMPARE(pieces.size(), 1);
    QVERIFY(qAbs(pieces[0]->jobs.first().pi1.pos().x() - 80) < 0.01);
    // one that stops far short does not
    const Eraser::OtherLine farStem{QPolygonF({QPointF(80, 0), QPointF(80, 20)}), 2.5};
    QVERIFY(touch(40, {farStem}, &touched).isEmpty());
}

/// On the layer: two crossing strokes, each erased up to the other
void KisVectorBrushStrokeTest::testEraserUpToIntersection()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisPaintOpPresetSP brush = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    Document d;
    paintStroke(d.image, d.layer, brush, KoColor(Qt::black, cs));
    // crosses the stroke at (75, 50)
    paintLine(d.image, d.layer, brush, KoColor(Qt::black, cs), QPointF(75, 5), QPointF(75, 95));
    repaint(d.image, d.layer);
    const QImage before = toImage(d.layer->projection());
    QCOMPARE(strokeShapes(d.layer).size(), 2);
    QVERIFY(coveredPixels(before.copy(0, 0, 70, 100)) > 100);

    // touching the stroke's first part removes it up to the vertical line
    {
        KisBrushStrokeShapeEraser eraser(d.layer, KisBrushStrokeShapeEraser::UpToIntersection);
        eraser.erase(QPointF(40, 0), QPointF(40, 100), 2.0);
        QCOMPARE(strokeShapes(d.layer).size(), 2);
        d.document->undoStack()->push(eraser.finish());
    }
    repaint(d.image, d.layer);
    QImage erased = toImage(d.layer->projection());
    // nothing left of it before the line, all of it after
    QCOMPARE(coveredPixels(erased.copy(0, 0, 70, 100)), 0);
    // (drawn again from the cut: its dabs may land part of a pixel away)
    const int keptBefore = coveredPixels(before.copy(82, 0, 118, 100));
    QVERIFY(qAbs(coveredPixels(erased.copy(82, 0, 118, 100)) - keptBefore) <= keptBefore * 0.03);
    // the vertical line is whole
    QVERIFY(qAlpha(erased.pixel(75, 15)) > 100);
    QVERIFY(qAlpha(erased.pixel(75, 85)) > 100);

    // touching the vertical line's upper part removes it down to the stroke
    {
        KisBrushStrokeShapeEraser eraser(d.layer, KisBrushStrokeShapeEraser::UpToIntersection);
        eraser.erase(QPointF(70, 15), QPointF(80, 15), 2.0);
        QCOMPARE(strokeShapes(d.layer).size(), 2);
        d.document->undoStack()->push(eraser.finish());
    }
    repaint(d.image, d.layer);
    erased = toImage(d.layer->projection());
    // (the stroke itself rises to the right of the line from there)
    QCOMPARE(coveredPixels(erased.copy(70, 0, 5, 44)), 0);
    QCOMPARE(strokeShapes(d.layer).last()->brushStroke()->jobs.first().pi1.pos(), QPointF(75, 50));
    QVERIFY(qAlpha(erased.pixel(75, 85)) > 100);
    QVERIFY(coveredPixels(erased.copy(82, 0, 118, 100)) > 100);

    // two undo steps return both strokes
    undoStep(d);
    undoStep(d);
    QCOMPARE(strokeShapes(d.layer).size(), 2);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), before) <= 2);

    // the setting keeps the mode
    const KisBrushStrokeShapeEraser::Mode saved = KisBrushStrokeShapeEraser::configuredMode();
    KisBrushStrokeShapeEraser::setConfiguredMode(KisBrushStrokeShapeEraser::UpToIntersection);
    QCOMPARE(KisBrushStrokeShapeEraser::configuredMode(), KisBrushStrokeShapeEraser::UpToIntersection);
    KisBrushStrokeShapeEraser::setConfiguredMode(saved);
}

/// Editing the path: the nearest place, moving points with a falloff,
/// changing the width, splitting
void KisVectorBrushStrokeTest::testPathEditFunctions()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    Document d;
    paintStroke(d.image, d.layer, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();
    using Edit = KisBrushStrokePathEdit;

    // the path passes (75, 50)
    const Edit::Hit hit = Edit::nearestPoint(stroke, QPointF(75, 53));
    QVERIFY(hit.valid);
    QVERIFY(qAbs(hit.position.x() - 75) < 2.5);
    QVERIFY(qAbs(hit.position.y() - 50) < 2.5);
    QVERIFY(hit.distance > 1.0 && hit.distance < 3.5);
    QVERIFY(stroke->jobs[hit.jobIndex].type == int(FreehandStrokeStrategy::Data::LINE));
    // on the closing curve, which bulges below its end points
    const Edit::Hit onCurve = Edit::nearestPoint(stroke, QPointF(150, 65));
    QCOMPARE(onCurve.jobIndex, stroke->jobs.size() - 1);
    QVERIFY(onCurve.t > 0.05 && onCurve.t < 0.95);

    // one control point for each job end
    const QPolygonF points = Edit::controlPoints(stroke, hit.jobIndex);
    QCOMPARE(points.size(), 62);
    QCOMPARE(points.first(), QPointF(20, 50));
    QCOMPARE(points.last(), QPointF(180, 30));

    auto continuous = [](KisRecordedBrushStrokeSP changed) {
        for (int i = 1; i + 1 < changed->jobs.size(); i++) {
            if (changed->jobs[i].pi2.pos() != changed->jobs[i + 1].pi1.pos()) {
                return false;
            }
        }
        return true;
    };
    auto movedPoints = [&](KisRecordedBrushStrokeSP changed) {
        int moved = 0;
        for (int i = 0; i < stroke->jobs.size(); i++) {
            moved += changed->jobs[i].pi2.pos() != stroke->jobs[i].pi2.pos();
        }
        return moved;
    };

    // a drag with a reach: most at the dragged place, nothing far away
    const QPointF offset(0, -10);
    KisRecordedBrushStrokeSP moved = Edit::movePoints(stroke, hit, offset, 30.0);
    QVERIFY(moved != stroke);
    QVERIFY(continuous(moved));
    const QPointF anchorShift = moved->jobs[hit.jobIndex].pi2.pos() - stroke->jobs[hit.jobIndex].pi2.pos();
    QVERIFY(qAbs(anchorShift.x()) < 1e-9);
    QVERIFY(anchorShift.y() < -9.0 && anchorShift.y() >= -10.0);
    QCOMPARE(moved->jobs.first().pi1.pos(), stroke->jobs.first().pi1.pos());
    QCOMPARE(moved->jobs.last().pi2.pos(), stroke->jobs.last().pi2.pos());
    QCOMPARE(moved->jobs.last().control1, stroke->jobs.last().control1);
    const int count = movedPoints(moved);
    qInfo() << "control points moved with a reach of 30:" << count;
    QVERIFY(count > 10 && count < 40);
    // smaller towards the edge of the reach
    const QPointF nextShift = moved->jobs[hit.jobIndex + 4].pi2.pos() - stroke->jobs[hit.jobIndex + 4].pi2.pos();
    QVERIFY(nextShift.y() < 0.0 && nextShift.y() > anchorShift.y());
    // the original is untouched
    QCOMPARE(stroke->jobs[hit.jobIndex].pi2.pos(),
             Edit::nearestPoint(stroke, stroke->jobs[hit.jobIndex].pi2.pos()).position);

    // without a reach: one control point
    moved = Edit::movePoints(stroke, hit, offset, 0.0);
    QVERIFY(continuous(moved));
    QCOMPARE(movedPoints(moved), 1);

    // the pen pressure, and with it the width
    const qreal pressure = stroke->jobs[hit.jobIndex].pi2.pressure();
    KisRecordedBrushStrokeSP thin = Edit::scaleWidth(stroke, hit, 0.5, 30.0);
    QVERIFY(thin->jobs[hit.jobIndex].pi2.pressure() < pressure * 0.6);
    QVERIFY(thin->jobs[hit.jobIndex].pi2.pressure() >= pressure * 0.5 - 1e-9);
    QCOMPARE(thin->jobs.first().pi1.pressure(), stroke->jobs.first().pi1.pressure());
    QCOMPARE(thin->jobs[hit.jobIndex].pi2.pos(), stroke->jobs[hit.jobIndex].pi2.pos());
    KisRecordedBrushStrokeSP thick = Edit::scaleWidth(stroke, hit, 4.0, 30.0);
    QCOMPARE(thick->jobs[hit.jobIndex].pi2.pressure(), 1.0);

    // split: both parts, nothing lost
    const QVector<KisRecordedBrushStrokeSP> parts = Edit::splitAt(stroke, hit);
    QCOMPARE(parts.size(), 2);
    QCOMPARE(parts[0]->jobs.size() + parts[1]->jobs.size(), stroke->jobs.size() + 1);
    QCOMPARE(parts[0]->jobs.last().pi2.pos(), parts[1]->jobs.first().pi1.pos());
    QVERIFY(qAbs(parts[0]->jobs.last().pi2.pos().x() - hit.position.x()) < 0.01);
    QCOMPARE(parts[0]->jobs.first().pi1.pos(), QPointF(20, 50));
    QCOMPARE(parts[1]->jobs.last().pi2.pos(), QPointF(180, 30));
}

/// On the layer: an edit shows at once and is one undo step
void KisVectorBrushStrokeTest::testShapeEdit()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    Document d;
    paintStroke(d.image, d.layer, loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp")), KoColor(Qt::black, cs));
    repaint(d.image, d.layer);
    const QImage before = toImage(d.layer->projection());
    KisBrushStrokeShape *original = strokeShapes(d.layer).first();
    KisRecordedBrushStrokeSP stroke = original->brushStroke();
    const KisBrushStrokePathEdit::Hit hit = KisBrushStrokePathEdit::nearestPoint(stroke, QPointF(75, 50));
    QVERIFY(qAlpha(before.pixel(75, 50)) > 100);
    QCOMPARE(qAlpha(before.pixel(75, 38)), 0);

    // a drag in two steps: the line follows
    {
        KisBrushStrokeShapeEdit edit(d.layer, original);
        QCOMPARE(edit.originalStroke(), stroke);
        edit.setStrokes({KisBrushStrokePathEdit::movePoints(stroke, hit, QPointF(0, -6), 30.0)});
        edit.setStrokes({KisBrushStrokePathEdit::movePoints(stroke, hit, QPointF(0, -12), 30.0)});
        QCOMPARE(strokeShapes(d.layer).size(), 1);
        QVERIFY(strokeShapes(d.layer).first() != original);
        QCOMPARE(edit.shapes().size(), 1);
        KUndo2Command *command = edit.finish(kundo2_noi18n("move points"));
        QVERIFY(command);
        d.document->undoStack()->push(command);
    }
    repaint(d.image, d.layer);
    const QImage moved = toImage(d.layer->projection());
    QCOMPARE(qAlpha(moved.pixel(75, 50)), 0);
    QVERIFY(qAlpha(moved.pixel(75, 38)) > 100);
    // the line before the edit is as it was; after it the line is drawn
    // again along a longer path, so its dabs may land part of a pixel away
    QCOMPARE(moved.copy(0, 0, 40, 100), before.copy(0, 0, 40, 100));
    const int differing = differingPixelsAwayFrom(moved, before, 75, 45);
    qInfo() << "pixels that differ away from the moved points" << differing << "of" << coveredPixels(before);
    QVERIFY(differing <= 12);

    undoStep(d);
    QCOMPARE(strokeShapes(d.layer).size(), 1);
    QCOMPARE(strokeShapes(d.layer).first(), original);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), before) <= 2);
    redoStep(d);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), moved) <= 2);
    undoStep(d);

    // cancelled, and dropped without finishing: as before
    {
        KisBrushStrokeShapeEdit edit(d.layer, original);
        edit.setStrokes({KisBrushStrokePathEdit::movePoints(stroke, hit, QPointF(0, -12), 30.0)});
        edit.cancel();
        QCOMPARE(strokeShapes(d.layer).first(), original);
        QVERIFY(!edit.finish(kundo2_noi18n("nothing")));
    }
    {
        KisBrushStrokeShapeEdit edit(d.layer, original);
        edit.setStrokes({KisBrushStrokePathEdit::movePoints(stroke, hit, QPointF(0, -12), 30.0)});
    }
    QCOMPARE(strokeShapes(d.layer).size(), 1);
    QCOMPARE(strokeShapes(d.layer).first(), original);
    // an edit that changes nothing leaves no undo step
    {
        KisBrushStrokeShapeEdit edit(d.layer, original);
        QVERIFY(!edit.finish(kundo2_noi18n("nothing")));
    }

    // a thinner line where the width is reduced
    {
        KisBrushStrokeShapeEdit edit(d.layer, original);
        edit.setStrokes({KisBrushStrokePathEdit::scaleWidth(stroke, hit, 0.25, 30.0)});
        d.document->undoStack()->push(edit.finish(kundo2_noi18n("width")));
    }
    repaint(d.image, d.layer);
    const QImage thin = toImage(d.layer->projection());
    QVERIFY(coveredPixels(thin.copy(60, 0, 30, 100)) < coveredPixels(before.copy(60, 0, 30, 100)) * 0.8);
    QCOMPARE(thin.copy(0, 0, 40, 100), before.copy(0, 0, 40, 100));
    undoStep(d);

    // cut in two: two shapes that look like the one
    {
        KisBrushStrokeShapeEdit edit(d.layer, original);
        edit.setStrokes(KisBrushStrokePathEdit::splitAt(stroke, hit));
        QCOMPARE(strokeShapes(d.layer).size(), 2);
        d.document->undoStack()->push(edit.finish(kundo2_noi18n("cut")));
    }
    repaint(d.image, d.layer);
    const QImage cut = toImage(d.layer->projection());
    QVERIFY(qAlpha(cut.pixel(75, 50)) > 100);
    QVERIFY(qAbs(coveredPixels(cut) - coveredPixels(before)) <= coveredPixels(before) * 0.03);
    undoStep(d);
    QCOMPARE(strokeShapes(d.layer).size(), 1);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), before) <= 2);
}

/// A line takes another color, brush size or brush; several lines in one
/// undo step
void KisVectorBrushStrokeTest::testStrokeColorSizeBrush()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisPaintOpPresetSP ink = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    Document d(300.0 / 72.0);
    paintStroke(d.image, d.layer, ink, KoColor(Qt::black, cs));
    paintLine(d.image, d.layer, ink, KoColor(Qt::black, cs), QPointF(30, 90), QPointF(170, 90));
    repaint(d.image, d.layer);
    const QImage before = toImage(d.layer->projection());
    QCOMPARE(strokeShapes(d.layer).size(), 2);
    const qreal size = strokeShapes(d.layer).first()->brushStroke()->preset->settings()->paintOpSize();

    auto alphaOf = [](const QImage &image) {
        return image.convertToFormat(QImage::Format_Alpha8);
    };

    // the functions leave the stroke itself alone
    KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();
    const KoColor red(Qt::red, cs);
    KisRecordedBrushStrokeSP changed = KisBrushStrokePathEdit::withColor(stroke, red);
    QVERIFY(changed != stroke);
    QCOMPARE(changed->fgColor, red);
    QCOMPARE(stroke->fgColor, KoColor(Qt::black, cs));
    QCOMPARE(KisBrushStrokePathEdit::withColor(changed, red), changed);
    changed = KisBrushStrokePathEdit::withBrushSize(stroke, 12.0);
    QCOMPARE(changed->preset->settings()->paintOpSize(), 12.0);
    QCOMPARE(stroke->preset->settings()->paintOpSize(), size);
    QCOMPARE(KisBrushStrokePathEdit::withBrushSize(stroke, size), stroke);
    QCOMPARE(changed->jobs.size(), stroke->jobs.size());

    // both lines red, in one undo step: the same shapes of ink
    QList<KisBrushStrokeShape *> replacements;
    KUndo2Command *command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        strokeShapes(d.layer),
        [&](const KisBrushStrokeShape *shape) {
            return KisBrushStrokePathEdit::withColor(shape->brushStroke(), red);
        },
        kundo2_noi18n("color"),
        &replacements);
    QVERIFY(command);
    QCOMPARE(replacements.size(), 2);
    d.document->undoStack()->push(command);
    QCOMPARE(strokeShapes(d.layer).size(), 2);
    repaint(d.image, d.layer);
    const QImage colored = toImage(d.layer->projection());
    QVERIFY(maxDifference(alphaOf(colored).convertToFormat(QImage::Format_ARGB32_Premultiplied),
                          alphaOf(before).convertToFormat(QImage::Format_ARGB32_Premultiplied))
            <= 2);
    QVERIFY(qRed(colored.pixel(100, 90)) > 200);
    QVERIFY(qBlue(colored.pixel(100, 90)) < 40);
    QVERIFY(qRed(colored.pixel(75, 50)) > 150);
    // nothing to change: no undo step
    QVERIFY(!KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        strokeShapes(d.layer),
        [&](const KisBrushStrokeShape *shape) {
            return KisBrushStrokePathEdit::withColor(shape->brushStroke(), red);
        },
        kundo2_noi18n("color")));
    undoStep(d);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), before) <= 2);

    // a larger brush for one line: more ink there, the other line as it was
    KisBrushStrokeShape *straight = strokeShapes(d.layer).last();
    QVERIFY(qAbs(straight->brushStroke()->jobs.first().pi1.pos().y() - 90) < 0.01);
    command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        {straight},
        [&](const KisBrushStrokeShape *shape) {
            return KisBrushStrokePathEdit::withBrushSize(shape->brushStroke(), size * 3.0);
        },
        kundo2_noi18n("size"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage wide = toImage(d.layer->projection());
    QVERIFY(coveredPixels(wide.copy(0, 75, 200, 25)) > coveredPixels(before.copy(0, 75, 200, 25)) * 2);
    QCOMPARE(wide.copy(0, 0, 200, 70), before.copy(0, 0, 200, 70));
    undoStep(d);

    // another brush at the same width; as a stroke takes it, with its
    // resources
    KisPaintOpPresetSP basic = loadPreset(QStringLiteral("b)_Basic-5_Size_default.kpp"));
    KoCanvasResourceProvider provider;
    provider.setResource(KoCanvasResource::ForegroundColor, QVariant::fromValue(KoColor(Qt::black, cs)));
    provider.setResource(KoCanvasResource::BackgroundColor, QVariant::fromValue(KoColor(Qt::white, cs)));
    provider.setResource(KoCanvasResource::CurrentPaintOpPreset, QVariant::fromValue(basic));
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
    provider.setResource(KoCanvasResource::Size, basic->settings()->paintOpSize());
    KisResourcesSnapshotSP snapshot = new KisResourcesSnapshot(d.image, d.layer, &provider);
    KisPaintOpPresetSP brush = snapshot->currentPaintOpPreset();
    QVERIFY(brush);

    command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        strokeShapes(d.layer),
        [&](const KisBrushStrokeShape *shape) {
            return KisBrushStrokePathEdit::withBrush(shape->brushStroke(), brush);
        },
        kundo2_noi18n("brush"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    Q_FOREACH (KisBrushStrokeShape *shape, strokeShapes(d.layer)) {
        QCOMPARE(shape->brushStroke()->preset->name(), basic->name());
        QCOMPARE(shape->brushStroke()->preset->settings()->paintOpSize(), size);
        QCOMPARE(shape->brushStroke()->fgColor, KoColor(Qt::black, cs));
    }
    repaint(d.image, d.layer);
    const QImage swapped = toImage(d.layer->projection());
    QVERIFY(coveredPixels(swapped) > 100);
    QVERIFY(maxDifference(swapped, before) > 16);

    // saved and loaded with the new brush
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("swapped.kra"));
    QVERIFY(d.document->exportDocumentSync(path, KisDocument::nativeFormatMimeType()));
    {
        QScopedPointer<KisDocument> document(KisPart::instance()->createDocument());
        document->setFileBatchMode(true);
        QVERIFY(document->openPath(path, KisDocument::DontAddToRecent));
        waitForImage(document->image());
        KisShapeLayerSP loaded;
        KisLayerUtils::recursiveApplyNodes(document->image()->root(), [&](KisNodeSP node) {
            if (KisShapeLayer *shapeLayer = dynamic_cast<KisShapeLayer *>(node.data())) {
                loaded = shapeLayer;
            }
        });
        QVERIFY(loaded);
        QCOMPARE(strokeShapes(loaded).size(), 2);
        QVERIFY(strokeShapes(loaded).first()->brushStroke());
        QCOMPARE(strokeShapes(loaded).first()->brushStroke()->preset->name(), basic->name());
        repaint(document->image(), loaded);
        QVERIFY(maxDifference(toImage(loaded->projection()), swapped) <= 2);
    }

    undoStep(d);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), before) <= 2);
}

/// While a transform is in progress the strokes show scaled pictures;
/// ending the preview draws them exactly
void KisVectorBrushStrokeTest::testTransformPreview()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisPaintOpPresetSP brush = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    const int count = 30;
    const QRect large(0, 0, imageBounds.width() * 2, imageBounds.height() * 2);

    auto drawLines = [&](Document &d) {
        for (int i = 0; i < count; i++) {
            paintLine(d.image,
                      d.layer,
                      brush,
                      KoColor(Qt::black, cs),
                      QPointF(10 + i * 6, 10),
                      QPointF(20 + i * 6, 90));
        }
        repaint(d.image, d.layer);
    };
    // returns how long the layer took to show the scaled strokes
    auto scaleTwice = [&](Document &d) {
        d.image->scaleImage(large.size(),
                            d.image->xRes(),
                            d.image->yRes(),
                            KisFilterStrategyRegistry::instance()->value(QStringLiteral("Bicubic")));
        QElapsedTimer timer;
        timer.start();
        qint64 busy = 0;
        // until the image has been idle for 30 ms
        for (int idle = 0; idle < 15 && timer.elapsed() < 120000;) {
            QApplication::processEvents();
            d.layer->forceUpdateTimedNode();
            QThread::msleep(2);
            if (d.image->isIdle()) {
                idle++;
            } else {
                idle = 0;
                busy = timer.elapsed();
            }
        }
        repaint(d.image, d.layer);
        return busy;
    };

    // the usual way: every stroke is drawn again
    Document exactDocument;
    drawLines(exactDocument);
    const qint64 exactTime = scaleTwice(exactDocument);
    const QImage exact = toImage(exactDocument.layer->projection(), large);

    // as a preview: scaled pictures
    Document d;
    drawLines(d);
    const QImage small = toImage(d.layer->projection());
    d.layer->setBrushStrokePreview(true);
    // nothing changes while every stroke has its picture
    d.layer->shapes().first()->update();
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), small) <= 2);

    const qint64 previewTime = scaleTwice(d);
    const QImage preview = toImage(d.layer->projection(), large);
    const qreal previewBlur = edgeBlur(preview);
    const qreal exactBlur = edgeBlur(exact);
    qInfo() << count << "strokes scaled by 2: preview" << previewTime << "ms (edge blur" << previewBlur << "), exact"
            << exactTime << "ms (edge blur" << exactBlur << ")";
    // the lines are where they belong, only softer
    QVERIFY(qAbs(coveredPixels(preview) - coveredPixels(exact)) < coveredPixels(exact) * 0.2);
    QVERIFY(previewBlur > exactBlur * 1.3);
    QVERIFY(maxDifference(preview, exact) > 16);
    // a forced update keeps the preview while it is on
    d.layer->forceUpdateTimedNode();
    waitForImage(d.image);
    QCOMPARE(toImage(d.layer->projection(), large), preview);

    // the end of the preview: drawn exactly, as without one
    d.layer->setBrushStrokePreview(false);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection(), large), exact) <= 2);
    QVERIFY(!d.layer->hasPendingTimedUpdates());

    // ending a preview that painted nothing asks for nothing
    d.layer->setBrushStrokePreview(true);
    d.layer->setBrushStrokePreview(false);
    QVERIFY(!d.layer->hasPendingTimedUpdates());

    // undo returns the small lines at once: their pictures are still there
    undoStep(d);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), small) <= 2);
}

/// Fewer control points for a line that stays where it was
void KisVectorBrushStrokeTest::testSimplify()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisPaintOpPresetSP ink = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    using Edit = KisBrushStrokePathEdit;
    using Job = KisRecordedBrushStroke::Job;
    Document d;
    paintStroke(d.image, d.layer, ink, KoColor(Qt::black, cs));
    repaint(d.image, d.layer);
    const QImage before = toImage(d.layer->projection());
    KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();
    QCOMPARE(stroke->jobs.size(), 62);
    QCOMPARE(Edit::pointCount(stroke), 62);

    /// how far the path of @p a leaves the path of @p b
    auto deviation = [](KisRecordedBrushStrokeSP a, KisRecordedBrushStrokeSP b) {
        qreal far = 0.0;
        for (const Job &job : a->jobs) {
            for (int step = 0; step <= 8; step++) {
                const qreal t = step / 8.0;
                const qreal u = 1.0 - t;
                QPointF point = job.pi1.pos() + (job.pi2.pos() - job.pi1.pos()) * t;
                if (job.type == int(FreehandStrokeStrategy::Data::CURVE)) {
                    point = u * u * u * job.pi1.pos() + 3 * u * u * t * job.control1 + 3 * u * t * t * job.control2
                        + t * t * t * job.pi2.pos();
                } else if (job.type == int(FreehandStrokeStrategy::Data::POINT)) {
                    point = job.pi1.pos();
                }
                far = qMax(far, Edit::nearestPoint(b, point).distance);
            }
        }
        return far;
    };
    auto connected = [](KisRecordedBrushStrokeSP changed) {
        for (int i = 1; i + 1 < changed->jobs.size(); i++) {
            const QPointF gap = changed->jobs[i].pi2.pos() - changed->jobs[i + 1].pi1.pos();
            if (qAbs(gap.x()) + qAbs(gap.y()) > 1e-6) {
                return false;
            }
        }
        return true;
    };

    // nothing asked, nothing done
    QCOMPARE(Edit::simplified(stroke, 0.0), stroke);
    QVERIFY(!Edit::simplified(KisRecordedBrushStrokeSP(), 1.0));

    // 60 short lines and a curve: a few curves, within a pixel of the path
    KisRecordedBrushStrokeSP simple = Edit::simplified(stroke, 1.0);
    QVERIFY(simple != stroke);
    qInfo() << "simplified at 1 px:" << stroke->jobs.size() << "jobs to" << simple->jobs.size() << "; path leaves by"
            << deviation(simple, stroke) << "and" << deviation(stroke, simple);
    QVERIFY(simple->jobs.size() <= 16);
    QVERIFY(simple->jobs.size() >= 4);
    QCOMPARE(Edit::pointCount(simple), simple->jobs.size());
    QCOMPARE(stroke->jobs.size(), 62);
    QVERIFY(deviation(simple, stroke) <= 1.05);
    QVERIFY(deviation(stroke, simple) <= 1.05);
    QVERIFY(connected(simple));
    // the starting point and the ends, with their pen values
    QVERIFY(simple->jobs.first().type == int(FreehandStrokeStrategy::Data::POINT));
    QCOMPARE(simple->jobs.first().pi1.pos(), QPointF(20, 50));
    QCOMPARE(simple->jobs[1].pi1.pos(), QPointF(20, 50));
    QCOMPARE(simple->jobs[1].pi1.pressure(), 0.2);
    QCOMPARE(simple->jobs.last().pi2.pos(), QPointF(180, 30));
    QCOMPARE(simple->jobs.last().pi2.pressure(), 0.5);
    QCOMPARE(simple->jobs.last().pi2.currentTime(), 300.0);
    // the pressure rises to the middle of the wave and falls again: no
    // curve skips that
    qreal highest = 0.0;
    for (const Job &job : simple->jobs) {
        highest = qMax(highest, job.pi2.pressure());
        QVERIFY(job.type != int(FreehandStrokeStrategy::Data::LINE));
    }
    QVERIFY(highest > 0.9);
    // the time runs on
    for (int i = 2; i < simple->jobs.size(); i++) {
        QVERIFY(simple->jobs[i].pi2.currentTime() > simple->jobs[i - 1].pi2.currentTime());
    }

    // a wider tolerance takes more; simplifying again does not add
    KisRecordedBrushStrokeSP rough = Edit::simplified(stroke, 4.0);
    QVERIFY(rough->jobs.size() < simple->jobs.size());
    QVERIFY(deviation(rough, stroke) <= 4.2);
    QVERIFY(Edit::simplified(simple, 1.0)->jobs.size() <= simple->jobs.size());
    // a fine tolerance still takes the points of the straight parts
    KisRecordedBrushStrokeSP fine = Edit::simplified(stroke, 0.1);
    QVERIFY(fine->jobs.size() > simple->jobs.size());
    qInfo() << "at 0.1 px:" << fine->jobs.size() << "jobs, path leaves by" << deviation(fine, stroke)
            << "; at 4 px:" << rough->jobs.size() << "jobs, by" << deviation(rough, stroke);
    // nearestPoint() measures a curve as 16 chords, which is 0.2 px off on
    // the stroke's closing curve
    QVERIFY(deviation(fine, stroke) <= 0.35);

    // a straight line of 20 parts: the point and one curve
    paintLine(d.image, d.layer, ink, KoColor(Qt::black, cs), QPointF(30, 90), QPointF(170, 90));
    KisRecordedBrushStrokeSP straight = strokeShapes(d.layer).last()->brushStroke();
    QCOMPARE(straight->jobs.size(), 21);
    KisRecordedBrushStrokeSP one = Edit::simplified(straight, 1.0);
    QCOMPARE(one->jobs.size(), 2);
    QCOMPARE(one->jobs.last().pi1.pos(), QPointF(30, 90));
    QCOMPARE(one->jobs.last().pi2.pos(), QPointF(170, 90));
    QVERIFY(qAbs(one->jobs.last().control1.y() - 90) < 1e-6);
    QVERIFY(deviation(one, straight) < 1e-6);
    // nothing more to take
    QCOMPARE(Edit::simplified(one, 1.0), one);

    // a corner stays a corner: two straight arms, two curves that meet in
    // the corner's point
    QSharedPointer<KisRecordedBrushStroke> angled(new KisRecordedBrushStroke(*straight));
    angled->jobs.clear();
    auto arm = [&](const QPointF &from, const QPointF &to, int id) {
        for (int i = 0; i < 20; i++) {
            Job job;
            job.type = FreehandStrokeStrategy::Data::LINE;
            job.strokeInfoId = id;
            job.pi1 = KisPaintInformation(from + (to - from) * (i / 20.0), 1.0);
            job.pi2 = KisPaintInformation(from + (to - from) * ((i + 1) / 20.0), 1.0);
            angled->jobs << job;
        }
    };
    arm(QPointF(20, 20), QPointF(100, 100), 0);
    arm(QPointF(100, 100), QPointF(180, 20), 0);
    KisRecordedBrushStrokeSP corner = Edit::simplified(angled, 1.0);
    QCOMPARE(corner->jobs.size(), 2);
    QCOMPARE(corner->jobs.first().pi2.pos(), QPointF(100, 100));
    QCOMPARE(corner->jobs.last().pi1.pos(), QPointF(100, 100));
    QVERIFY(deviation(corner, angled) < 1e-6);

    // mirrored copies are lines of their own and stay interleaved
    QSharedPointer<KisRecordedBrushStroke> mirrored(new KisRecordedBrushStroke(*stroke));
    mirrored->jobs.clear();
    mirrored->strokeInfoCount = 2;
    for (const Job &job : stroke->jobs) {
        mirrored->jobs << job;
        Job copy = job;
        copy.strokeInfoId = 1;
        auto flip = [](const QPointF &point) {
            return QPointF(200 - point.x(), point.y());
        };
        copy.pi1.setPos(flip(job.pi1.pos()));
        copy.pi2.setPos(flip(job.pi2.pos()));
        copy.control1 = flip(job.control1);
        copy.control2 = flip(job.control2);
        mirrored->jobs << copy;
    }
    KisRecordedBrushStrokeSP both = Edit::simplified(mirrored, 1.0);
    QCOMPARE(both->jobs.size(), simple->jobs.size() * 2);
    QCOMPARE(Edit::pointCount(both), Edit::pointCount(simple) * 2);
    // each copy is connected, and they are drawn in turns as before (the
    // two fits may split at neighbouring places)
    QPointF ends[2];
    int ahead = 0;
    for (int i = 0; i < both->jobs.size(); i++) {
        const Job &job = both->jobs[i];
        if (job.type != int(FreehandStrokeStrategy::Data::POINT)) {
            if (!ends[job.strokeInfoId].isNull()) {
                QCOMPARE(job.pi1.pos(), ends[job.strokeInfoId]);
            }
            ends[job.strokeInfoId] = job.pi2.pos();
        }
        ahead += job.strokeInfoId ? -1 : 1;
        QVERIFY(qAbs(ahead) <= 2);
    }
    QCOMPARE(ahead, 0);
    QVERIFY(deviation(both, mirrored) <= 1.05);

    // on the layer: one undo step, and the line looks as it did
    KisBrushStrokeShape *wave = strokeShapes(d.layer).first();
    repaint(d.image, d.layer);
    const QImage withLine = toImage(d.layer->projection());
    KUndo2Command *command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        {wave},
        [&](const KisBrushStrokeShape *shape) {
            return Edit::simplified(shape->brushStroke(), 1.0);
        },
        kundo2_noi18n("simplify"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage after = toImage(d.layer->projection());
    QCOMPARE(strokeShapes(d.layer).size(), 2);
    int changedPixels = 0;
    for (int y = 0; y < after.height(); y++) {
        for (int x = 0; x < after.width(); x++) {
            changedPixels += qAbs(qAlpha(after.pixel(x, y)) - qAlpha(withLine.pixel(x, y))) > 128;
        }
    }
    qInfo() << "ink:" << coveredPixels(withLine) << "pixels before," << coveredPixels(after) << "after;"
            << changedPixels << "changed by more than half";
    QVERIFY(qAbs(coveredPixels(after) - coveredPixels(withLine)) < coveredPixels(withLine) * 0.05);
    QVERIFY(changedPixels < coveredPixels(withLine) * 0.08);
    undoStep(d);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), withLine) <= 2);
    Q_UNUSED(before);
}

/// A control point more on a line, and one less
void KisVectorBrushStrokeTest::testAddDeletePoint()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisPaintOpPresetSP ink = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    using Edit = KisBrushStrokePathEdit;
    using Job = KisRecordedBrushStroke::Job;
    Document d;
    paintStroke(d.image, d.layer, ink, KoColor(Qt::black, cs));
    repaint(d.image, d.layer);
    const QImage drawn = toImage(d.layer->projection());
    // a point, 60 lines and a curve
    KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();
    QCOMPARE(stroke->jobs.size(), 62);
    const int points = Edit::pointCount(stroke);

    auto pathPoint = [](const Job &job, qreal t) {
        const qreal u = 1.0 - t;
        if (job.type == int(FreehandStrokeStrategy::Data::CURVE)) {
            return u * u * u * job.pi1.pos() + 3 * u * u * t * job.control1 + 3 * u * t * t * job.control2
                + t * t * t * job.pi2.pos();
        }
        return job.pi1.pos() + (job.pi2.pos() - job.pi1.pos()) * t;
    };
    auto near = [](const QPointF &a, const QPointF &b, qreal far) {
        return std::hypot(a.x() - b.x(), a.y() - b.y()) <= far;
    };
    auto connected = [&](KisRecordedBrushStrokeSP changed) {
        for (int i = 1; i + 1 < changed->jobs.size(); i++) {
            if (!near(changed->jobs[i].pi2.pos(), changed->jobs[i + 1].pi1.pos(), 1e-6)) {
                return false;
            }
        }
        return true;
    };

    // --- a point more, on the closing curve
    Edit::Hit onCurve;
    onCurve.valid = true;
    onCurve.jobIndex = 61;
    onCurve.t = 0.4;
    onCurve.position = pathPoint(stroke->jobs[61], 0.4);
    Edit::Hit added;
    KisRecordedBrushStrokeSP more = Edit::insertPoint(stroke, onCurve, &added);
    QVERIFY(more != stroke);
    QCOMPARE(stroke->jobs.size(), 62);
    QCOMPARE(more->jobs.size(), 63);
    QCOMPARE(Edit::pointCount(more), points + 1);
    QVERIFY(added.valid);
    QCOMPARE(added.jobIndex, 62);
    QCOMPARE(added.t, 0.0);
    QVERIFY(near(added.position, onCurve.position, 1e-9));
    QVERIFY(connected(more));
    QCOMPARE(more->jobs[61].pi1.pos(), stroke->jobs[61].pi1.pos());
    QCOMPARE(more->jobs[62].pi2.pos(), stroke->jobs[61].pi2.pos());
    QVERIFY(near(more->jobs[61].pi2.pos(), onCurve.position, 1e-9));
    // the two curves are the path of the one: its places are theirs
    for (int step = 0; step <= 10; step++) {
        const qreal t = step / 10.0;
        const QPointF expected = pathPoint(stroke->jobs[61], t);
        const QPointF actual =
            t <= 0.4 ? pathPoint(more->jobs[61], t / 0.4) : pathPoint(more->jobs[62], (t - 0.4) / 0.6);
        QVERIFY(near(actual, expected, 1e-9));
    }
    // the pen's values of that place, and the time
    const qreal pressure = stroke->jobs[61].pi1.pressure() * 0.6 + stroke->jobs[61].pi2.pressure() * 0.4;
    QVERIFY(qAbs(more->jobs[62].pi1.pressure() - pressure) < 1e-6);
    QVERIFY(more->jobs[62].pi1.currentTime() > stroke->jobs[61].pi1.currentTime());
    QVERIFY(more->jobs[62].pi1.currentTime() < stroke->jobs[61].pi2.currentTime());

    // the new point alone moves
    const KisRecordedBrushStrokeSP moved = Edit::movePoints(more, added, QPointF(0, 12), 0.0);
    QCOMPARE(moved->jobs[61].pi2.pos(), more->jobs[61].pi2.pos() + QPointF(0, 12));
    QCOMPARE(moved->jobs[62].pi1.pos(), more->jobs[62].pi1.pos() + QPointF(0, 12));
    QCOMPARE(moved->jobs[61].pi1.pos(), more->jobs[61].pi1.pos());
    QCOMPARE(moved->jobs[62].pi2.pos(), more->jobs[62].pi2.pos());
    QCOMPARE(moved->jobs[60].pi2.pos(), more->jobs[60].pi2.pos());

    // on a line
    Edit::Hit onLine;
    onLine.valid = true;
    onLine.jobIndex = 30;
    onLine.t = 0.5;
    onLine.position = pathPoint(stroke->jobs[30], 0.5);
    more = Edit::insertPoint(stroke, onLine, &added);
    QCOMPARE(more->jobs.size(), 63);
    QVERIFY(more->jobs[30].type == int(FreehandStrokeStrategy::Data::LINE));
    QVERIFY(more->jobs[31].type == int(FreehandStrokeStrategy::Data::LINE));
    QVERIFY(near(more->jobs[31].pi1.pos(), onLine.position, 1e-9));
    QCOMPARE(added.jobIndex, 31);

    // where a control point is already, nothing is added
    Edit::Hit atEnd = onLine;
    atEnd.t = 1.0;
    QCOMPARE(Edit::insertPoint(stroke, atEnd, &added), stroke);
    QVERIFY(added.valid);
    QCOMPARE(added.position, stroke->jobs[30].pi2.pos());
    QCOMPARE(added.jobIndex, 31);
    QCOMPARE(added.t, 0.0);
    QCOMPARE(Edit::insertPoint(stroke, Edit::Hit(), &added), stroke);
    QVERIFY(!added.valid);

    // --- the nearest control point: an end of the job
    Edit::Hit early = onLine;
    early.t = 0.3;
    QCOMPARE(Edit::nearestControlPoint(stroke, early).position, stroke->jobs[30].pi1.pos());
    Edit::Hit late = onLine;
    late.t = 0.7;
    QCOMPARE(Edit::nearestControlPoint(stroke, late).position, stroke->jobs[30].pi2.pos());
    Edit::Hit last = onCurve;
    last.t = 0.9;
    QCOMPARE(Edit::nearestControlPoint(stroke, last).position, QPointF(180, 30));
    QCOMPARE(Edit::nearestControlPoint(stroke, last).t, 1.0);
    // the dab at the start is the line's first control point
    Edit::Hit dab;
    dab.valid = true;
    dab.jobIndex = 0;
    QCOMPARE(Edit::nearestControlPoint(stroke, dab).position, QPointF(20, 50));
    QCOMPARE(Edit::nearestControlPoint(stroke, dab).jobIndex, 1);

    // --- a point less: two lines become a curve between their far ends
    KisRecordedBrushStrokeSP fewer = Edit::deletePoint(stroke, late);
    QVERIFY(fewer != stroke);
    QCOMPARE(stroke->jobs.size(), 62);
    QCOMPARE(fewer->jobs.size(), 61);
    QCOMPARE(Edit::pointCount(fewer), points - 1);
    QVERIFY(connected(fewer));
    QVERIFY(fewer->jobs[30].type == int(FreehandStrokeStrategy::Data::CURVE));
    QCOMPARE(fewer->jobs[30].pi1.pos(), stroke->jobs[30].pi1.pos());
    QCOMPARE(fewer->jobs[30].pi2.pos(), stroke->jobs[31].pi2.pos());
    QCOMPARE(fewer->jobs[30].pi2.pressure(), stroke->jobs[31].pi2.pressure());
    QCOMPARE(fewer->jobs[31].pi1.pos(), stroke->jobs[32].pi1.pos());
    // the curve passes near the point that went
    const QPointF gone = stroke->jobs[30].pi2.pos();
    qreal nearest = 1e9;
    for (int step = 0; step <= 100; step++) {
        const QPointF place = pathPoint(fewer->jobs[30], step / 100.0);
        nearest = qMin(nearest, std::hypot(place.x() - gone.x(), place.y() - gone.y()));
    }
    qInfo() << "the curve in place of two lines passes the deleted point at" << nearest << "px";
    QVERIFY(nearest < 0.5);
    // the same point from the job after it
    Edit::Hit fromNext = onLine;
    fromNext.jobIndex = 31;
    fromNext.t = 0.2;
    QCOMPARE(Edit::deletePoint(stroke, fromNext)->jobs[30].pi2.pos(), fewer->jobs[30].pi2.pos());

    // the line's first point: it starts a job later, and so does its dab
    Edit::Hit first;
    first.valid = true;
    first.jobIndex = 1;
    first.t = 0.1;
    fewer = Edit::deletePoint(stroke, first);
    QCOMPARE(fewer->jobs.size(), 61);
    QVERIFY(fewer->jobs[0].type == int(FreehandStrokeStrategy::Data::POINT));
    QCOMPARE(fewer->jobs[0].pi1.pos(), stroke->jobs[2].pi1.pos());
    QCOMPARE(fewer->jobs[1].pi1.pos(), stroke->jobs[2].pi1.pos());
    QCOMPARE(Edit::deletePoint(stroke, dab)->jobs[1].pi1.pos(), stroke->jobs[2].pi1.pos());
    // its last point: it ends a job earlier
    fewer = Edit::deletePoint(stroke, last);
    QCOMPARE(fewer->jobs.size(), 61);
    QCOMPARE(fewer->jobs.last().pi2.pos(), stroke->jobs[60].pi2.pos());

    // a line of one job keeps its two points
    QSharedPointer<KisRecordedBrushStroke> single(new KisRecordedBrushStroke(*stroke));
    single->jobs = {stroke->jobs[0], stroke->jobs[61]};
    Edit::Hit only;
    only.valid = true;
    only.jobIndex = 1;
    only.t = 0.9;
    QCOMPARE(Edit::deletePoint(single, only), KisRecordedBrushStrokeSP(single));
    QCOMPARE(Edit::deletePoint(stroke, Edit::Hit()), stroke);

    // a mirrored copy keeps its points
    QSharedPointer<KisRecordedBrushStroke> mirrored(new KisRecordedBrushStroke(*stroke));
    mirrored->jobs.clear();
    mirrored->strokeInfoCount = 2;
    for (const Job &job : stroke->jobs) {
        mirrored->jobs << job;
        Job copy = job;
        copy.strokeInfoId = 1;
        mirrored->jobs << copy;
    }
    Edit::Hit inCopy = late;
    inCopy.jobIndex = 30 * 2;
    fewer = Edit::deletePoint(mirrored, inCopy);
    int counts[2] = {0, 0};
    for (const Job &job : fewer->jobs) {
        counts[job.strokeInfoId]++;
    }
    QCOMPARE(counts[0], 61);
    QCOMPARE(counts[1], 62);
    more = Edit::insertPoint(mirrored, inCopy, &added);
    QCOMPARE(more->jobs.size(), 125);
    QCOMPARE(more->jobs[added.jobIndex].strokeInfoId, 0);
    QCOMPARE(more->jobs[added.jobIndex - 1].strokeInfoId, 0);

    // --- on the layer: a point added without moving it changes nothing to
    // see; deleting points is one undo step each
    KisBrushStrokeShape *shape = strokeShapes(d.layer).first();
    KUndo2Command *command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        {shape},
        [&](const KisBrushStrokeShape *edited) {
            return Edit::insertPoint(edited->brushStroke(), onCurve);
        },
        kundo2_noi18n("add point"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage withPoint = toImage(d.layer->projection());
    int changedPixels = 0;
    for (int y = 0; y < drawn.height(); y++) {
        for (int x = 0; x < drawn.width(); x++) {
            changedPixels += qAbs(qAlpha(withPoint.pixel(x, y)) - qAlpha(drawn.pixel(x, y))) > 128;
        }
    }
    qInfo() << "a point added on the curve:" << changedPixels << "pixels changed by more than half";
    QVERIFY(changedPixels <= 2);
    QCOMPARE(strokeShapes(d.layer).first()->brushStroke()->jobs.size(), 63);

    command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        strokeShapes(d.layer),
        [&](const KisBrushStrokeShape *edited) {
            return Edit::deletePoint(edited->brushStroke(), late);
        },
        kundo2_noi18n("delete point"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    QCOMPARE(strokeShapes(d.layer).first()->brushStroke()->jobs.size(), 62);
    QVERIFY(qAbs(coveredPixels(toImage(d.layer->projection())) - coveredPixels(drawn)) < coveredPixels(drawn) * 0.03);
    undoStep(d);
    undoStep(d);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), drawn) <= 2);
    QCOMPARE(strokeShapes(d.layer).first()->brushStroke()->jobs.size(), 62);
}

/// A control point switches between a corner and a curve
void KisVectorBrushStrokeTest::testSwitchCorner()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisPaintOpPresetSP ink = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    using Edit = KisBrushStrokePathEdit;
    using Job = KisRecordedBrushStroke::Job;
    const int LINE = int(FreehandStrokeStrategy::Data::LINE);
    const int CURVE = int(FreehandStrokeStrategy::Data::CURVE);
    Document d;
    paintStroke(d.image, d.layer, ink, KoColor(Qt::black, cs));
    repaint(d.image, d.layer);
    const QImage drawn = toImage(d.layer->projection());
    // a point, 60 lines and a curve
    KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();
    QCOMPARE(stroke->jobs.size(), 62);

    auto far = [](const QPointF &a, const QPointF &b) {
        return std::hypot(a.x() - b.x(), a.y() - b.y());
    };
    auto unit = [&](const QPointF &vector) {
        return vector / far(vector, QPointF());
    };
    auto hit = [](int jobIndex, qreal t) {
        Edit::Hit place;
        place.valid = true;
        place.jobIndex = jobIndex;
        place.t = t;
        return place;
    };

    // --- between two lines: a corner, however little the path turns
    const Edit::Hit between = hit(30, 0.7);
    QVERIFY(Edit::isCorner(stroke, between));
    KisRecordedBrushStrokeSP curved = Edit::switchCorner(stroke, between);
    QVERIFY(curved != stroke);
    QCOMPARE(int(stroke->jobs[30].type), LINE);
    QCOMPARE(curved->jobs.size(), 62);
    QCOMPARE(int(curved->jobs[30].type), CURVE);
    QCOMPARE(int(curved->jobs[31].type), CURVE);
    QCOMPARE(int(curved->jobs[29].type), LINE);
    QCOMPARE(int(curved->jobs[32].type), LINE);
    QVERIFY(!Edit::isCorner(curved, between));
    // the points and the pen's values stay
    for (int i = 0; i < 62; i++) {
        QCOMPARE(curved->jobs[i].pi1.pos(), stroke->jobs[i].pi1.pos());
        QCOMPARE(curved->jobs[i].pi2.pos(), stroke->jobs[i].pi2.pos());
        QCOMPARE(curved->jobs[i].pi2.pressure(), stroke->jobs[i].pi2.pressure());
        QCOMPARE(curved->jobs[i].pi2.currentTime(), stroke->jobs[i].pi2.currentTime());
    }
    {
        // both handles lie along the direction from the point before to
        // the point after, a third of the way to each
        const QPointF previous = stroke->jobs[30].pi1.pos();
        const QPointF point = stroke->jobs[30].pi2.pos();
        const QPointF next = stroke->jobs[31].pi2.pos();
        const QPointF direction = unit(next - previous);
        QVERIFY(far(curved->jobs[30].control2, point - direction * far(previous, point) / 3.0) < 1e-9);
        QVERIFY(far(curved->jobs[31].control1, point + direction * far(point, next) / 3.0) < 1e-9);
        // the far ends leave as the lines did
        QVERIFY(far(curved->jobs[30].control1, previous + (point - previous) / 3.0) < 1e-9);
        QVERIFY(far(curved->jobs[31].control2, next + (point - next) / 3.0) < 1e-9);
    }
    // the same point from the job after it
    QCOMPARE(Edit::switchCorner(stroke, hit(31, 0.2))->jobs[30].control2, curved->jobs[30].control2);

    // and back: the two lines as they were
    KisRecordedBrushStrokeSP back = Edit::switchCorner(curved, between);
    QVERIFY(back != curved);
    QCOMPARE(int(back->jobs[30].type), LINE);
    QCOMPARE(int(back->jobs[31].type), LINE);
    QVERIFY(Edit::isCorner(back, between));

    // --- a right angle of two long lines
    QSharedPointer<KisRecordedBrushStroke> angle(new KisRecordedBrushStroke(*stroke));
    {
        Job first = stroke->jobs[1];
        first.pi1.setPos(QPointF(20, 20));
        first.pi2.setPos(QPointF(80, 20));
        Job second = stroke->jobs[2];
        second.pi1.setPos(QPointF(80, 20));
        second.pi2.setPos(QPointF(80, 50));
        Job third = stroke->jobs[3];
        third.pi1.setPos(QPointF(80, 50));
        third.pi2.setPos(QPointF(140, 50));
        angle->jobs = {first, second, third};
    }
    const Edit::Hit firstTurn = hit(0, 0.9);
    const Edit::Hit secondTurn = hit(2, 0.1);
    QVERIFY(Edit::isCorner(angle, firstTurn));
    KisRecordedBrushStrokeSP round = Edit::switchCorner(angle, firstTurn);
    QVERIFY(!Edit::isCorner(round, firstTurn));
    QVERIFY(Edit::isCorner(round, secondTurn));
    {
        // the path passes through the point without a turn
        const Job &in = round->jobs[0];
        const Job &out = round->jobs[1];
        const QPointF direction = unit(QPointF(80, 50) - QPointF(20, 20));
        QVERIFY(far(unit(in.pi2.pos() - in.control2), direction) < 1e-9);
        QVERIFY(far(unit(out.control1 - out.pi1.pos()), direction) < 1e-9);
        QVERIFY(qAbs(far(in.control2, in.pi2.pos()) - 20.0) < 1e-9);
        QVERIFY(qAbs(far(out.control1, out.pi1.pos()) - 10.0) < 1e-9);
    }
    // both turns round: the job between them has a handle at each end
    round = Edit::switchCorner(round, secondTurn);
    QVERIFY(!Edit::isCorner(round, firstTurn));
    QVERIFY(!Edit::isCorner(round, secondTurn));
    QCOMPARE(int(round->jobs[1].type), CURVE);
    const QPointF keptHandle = round->jobs[1].control2;
    // the first a corner again: its handles go, the second turn's stay
    KisRecordedBrushStrokeSP mixed = Edit::switchCorner(round, firstTurn);
    QVERIFY(Edit::isCorner(mixed, firstTurn));
    QVERIFY(!Edit::isCorner(mixed, secondTurn));
    QCOMPARE(int(mixed->jobs[0].type), LINE);
    QCOMPARE(int(mixed->jobs[1].type), CURVE);
    QCOMPARE(mixed->jobs[1].control1, QPointF(80, 20));
    QCOMPARE(mixed->jobs[1].control2, keptHandle);
    // the flags of the line's control points
    QVector<bool> corners;
    QCOMPARE(Edit::controlPoints(mixed, 0, &corners).size(), 4);
    QCOMPARE(corners, QVector<bool>({false, true, false, false}));
    QCOMPARE(Edit::controlPoints(angle, 0, &corners).size(), 4);
    QCOMPARE(corners, QVector<bool>({false, true, true, false}));

    // a line that comes back on itself turns across
    QSharedPointer<KisRecordedBrushStroke> there(new KisRecordedBrushStroke(*angle));
    there->jobs.remove(2);
    there->jobs[1].pi2.setPos(QPointF(20, 20));
    round = Edit::switchCorner(there, hit(0, 0.9));
    QVERIFY(round != KisRecordedBrushStrokeSP(there));
    QVERIFY(qAbs(round->jobs[0].control2.x() - 80.0) < 1e-9);
    QVERIFY(qAbs(qAbs(round->jobs[0].control2.y() - 20.0) - 20.0) < 1e-9);

    // --- the line's ends, the dab at its start and no place: nothing
    QCOMPARE(Edit::switchCorner(stroke, hit(1, 0.1)), stroke);
    QCOMPARE(Edit::switchCorner(stroke, hit(61, 0.9)), stroke);
    QCOMPARE(Edit::switchCorner(stroke, hit(0, 0.0)), stroke);
    QCOMPARE(Edit::switchCorner(stroke, Edit::Hit()), stroke);
    QVERIFY(!Edit::isCorner(stroke, hit(1, 0.1)));
    QVERIFY(!Edit::isCorner(stroke, hit(61, 0.9)));
    QVERIFY(!Edit::switchCorner(KisRecordedBrushStrokeSP(), between));

    // the last line meets the curve's handle nearly straight on: a curve
    // point; all others of the drawn line are corners
    QCOMPARE(Edit::controlPoints(stroke, 1, &corners).size(), 62);
    QCOMPARE(corners.size(), 62);
    QVERIFY(!corners.first());
    QVERIFY(!corners.last());
    QVERIFY(!corners[60]);
    QCOMPARE(corners.count(true), 59);
    QVERIFY(!Edit::isCorner(stroke, hit(61, 0.1)));
    KisRecordedBrushStrokeSP sharp = Edit::switchCorner(stroke, hit(61, 0.1));
    QVERIFY(Edit::isCorner(sharp, hit(61, 0.1)));
    QCOMPARE(int(sharp->jobs[60].type), LINE);
    QCOMPARE(int(sharp->jobs[61].type), CURVE);
    QCOMPARE(sharp->jobs[61].control1, stroke->jobs[61].pi1.pos());
    QCOMPARE(sharp->jobs[61].control2, stroke->jobs[61].control2);

    // --- a simplified line: its inner points are curve points; one made
    // a corner loses its two handles and gets them again
    KisRecordedBrushStrokeSP simple = Edit::simplified(stroke, 1.0);
    QVERIFY(simple->jobs.size() >= 4);
    Edit::controlPoints(simple, 1, &corners);
    qInfo() << "simplified:" << simple->jobs.size() << "jobs," << corners.count(true) << "corners";
    QCOMPARE(corners.count(true), 0);
    const Edit::Hit inner = hit(2, 0.9);
    sharp = Edit::switchCorner(simple, inner);
    QVERIFY(Edit::isCorner(sharp, inner));
    QCOMPARE(sharp->jobs[2].control2, simple->jobs[2].pi2.pos());
    QCOMPARE(sharp->jobs[3].control1, simple->jobs[3].pi1.pos());
    QCOMPARE(sharp->jobs[2].control1, simple->jobs[2].control1);
    QCOMPARE(sharp->jobs[3].control2, simple->jobs[3].control2);
    back = Edit::switchCorner(sharp, inner);
    QVERIFY(!Edit::isCorner(back, inner));
    QCOMPARE(int(back->jobs[2].type), CURVE);
    QCOMPARE(int(back->jobs[3].type), CURVE);

    // --- a mirrored copy keeps its shape
    QSharedPointer<KisRecordedBrushStroke> mirrored(new KisRecordedBrushStroke(*stroke));
    mirrored->jobs.clear();
    mirrored->strokeInfoCount = 2;
    for (const Job &job : stroke->jobs) {
        mirrored->jobs << job;
        Job copy = job;
        copy.strokeInfoId = 1;
        mirrored->jobs << copy;
    }
    curved = Edit::switchCorner(mirrored, hit(30 * 2, 0.7));
    QCOMPARE(int(curved->jobs[60].type), CURVE);
    QCOMPARE(int(curved->jobs[62].type), CURVE);
    QCOMPARE(int(curved->jobs[61].type), LINE);
    QCOMPARE(int(curved->jobs[63].type), LINE);

    // --- on the layer: the simplified line with a corner looks different
    // around it only; undo gives the picture back
    KisBrushStrokeShape *shape = strokeShapes(d.layer).first();
    KUndo2Command *command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        {shape},
        [&](const KisBrushStrokeShape *edited) {
            return Edit::simplified(edited->brushStroke(), 1.0);
        },
        kundo2_noi18n("simplify"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage smooth = toImage(d.layer->projection());
    const QPointF corner = simple->jobs[2].pi2.pos();
    command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        strokeShapes(d.layer),
        [&](const KisBrushStrokeShape *edited) {
            return Edit::switchCorner(edited->brushStroke(), inner);
        },
        kundo2_noi18n("switch corner"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage cornered = toImage(d.layer->projection());
    QVERIFY(Edit::isCorner(strokeShapes(d.layer).first()->brushStroke(), inner));
    // changes lie between the control points before and after the corner
    // (their handles may lie a little further out)
    const qreal reach = 1.5 * qMax(far(simple->jobs[2].pi1.pos(), corner), far(simple->jobs[3].pi2.pos(), corner))
        + ink->settings()->paintOpSize();
    int changedNear = 0;
    int changedFar = 0;
    for (int y = 0; y < smooth.height(); y++) {
        for (int x = 0; x < smooth.width(); x++) {
            if (qAbs(qAlpha(cornered.pixel(x, y)) - qAlpha(smooth.pixel(x, y))) > 128) {
                (far(QPointF(x, y), corner) <= reach ? changedNear : changedFar)++;
            }
        }
    }
    qInfo() << "a corner at" << corner << ":" << changedNear << "pixels changed near it," << changedFar << "elsewhere";
    QVERIFY(changedNear > 0);
    QVERIFY(changedFar <= 2);
    undoStep(d);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), smooth) <= 2);
    undoStep(d);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), drawn) <= 2);
}

void KisVectorBrushStrokeTest::testDensity_data()
{
    QTest::addColumn<QString>("preset");
    QTest::newRow("ink, build up") << QStringLiteral("d)_Ink-3_Gpen.kpp");
    QTest::newRow("pressure size, wash") << QStringLiteral("b)_Basic-5_Size_default.kpp");
}

/// A line made lighter around a place: the density of its control points,
/// kept in the file and multiplied into the picture
void KisVectorBrushStrokeTest::testDensity()
{
    QFETCH(QString, preset);
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisPaintOpPresetSP brush = loadPreset(preset);
    using Edit = KisBrushStrokePathEdit;
    using Job = KisRecordedBrushStroke::Job;
    Document d;
    // a point and 20 lines of 8 pixels at full pressure
    paintLine(d.image, d.layer, brush, KoColor(Qt::black, cs), QPointF(20, 50), QPointF(180, 50));
    repaint(d.image, d.layer);
    const QImage drawn = toImage(d.layer->projection());
    KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();
    QCOMPARE(stroke->jobs.size(), 21);
    for (const Job &job : stroke->jobs) {
        QCOMPARE(job.pi1.densityFactor(), 1.0);
        QCOMPARE(job.pi2.densityFactor(), 1.0);
    }

    /// the density of the control point at @p x
    auto densityAt = [](KisRecordedBrushStrokeSP from, qreal x) {
        for (const Job &job : from->jobs) {
            if (qAbs(job.pi2.pos().x() - x) < 1e-6) {
                return job.pi2.densityFactor();
            }
        }
        return -1.0;
    };

    // --- the functions: half the density at x = 100, less of it up to 30
    // pixels along the line
    const Edit::Hit middle = Edit::nearestPoint(stroke, QPointF(100, 50));
    QVERIFY(middle.valid);
    KisRecordedBrushStrokeSP light = Edit::scaleDensity(stroke, middle, 0.5, 30.0);
    QVERIFY(light != stroke);
    QCOMPARE(densityAt(stroke, 100), 1.0);
    QVERIFY(qAbs(densityAt(light, 100) - 0.5) < 1e-9);
    // the cosine falloff: 8 pixels away, 24 pixels away, and beyond
    QVERIFY(qAbs(densityAt(light, 108) - (1.0 - 0.5 * 0.5 * (1.0 + qCos(M_PI * 8.0 / 30.0)))) < 1e-9);
    QVERIFY(qAbs(densityAt(light, 92) - densityAt(light, 108)) < 1e-9);
    QVERIFY(qAbs(densityAt(light, 76) - (1.0 - 0.5 * 0.5 * (1.0 + qCos(M_PI * 24.0 / 30.0)))) < 1e-9);
    QCOMPARE(densityAt(light, 132), 1.0);
    QCOMPARE(densityAt(light, 28), 1.0);
    // the two jobs that meet in a point agree, and nothing else changed
    for (int i = 0; i < light->jobs.size(); i++) {
        if (i > 1) {
            QCOMPARE(light->jobs[i].pi1.densityFactor(), light->jobs[i - 1].pi2.densityFactor());
        }
        QCOMPARE(light->jobs[i].pi2.pos(), stroke->jobs[i].pi2.pos());
        QCOMPARE(light->jobs[i].pi2.pressure(), stroke->jobs[i].pi2.pressure());
    }
    // between 0.01 and 1
    QVERIFY(qAbs(densityAt(Edit::scaleDensity(stroke, middle, 0.0001, 0.0), 100) - 0.01) < 1e-9);
    QCOMPARE(densityAt(Edit::scaleDensity(stroke, middle, 0.0001, 0.0), 108), 1.0);
    QCOMPARE(densityAt(Edit::scaleDensity(light, middle, 4.0, 30.0), 100), 1.0);
    QCOMPARE(densityAt(Edit::scaleDensity(stroke, middle, 3.0, 30.0), 100), 1.0);
    QCOMPARE(Edit::scaleDensity(stroke, Edit::Hit(), 0.5, 30.0), stroke);

    // the pen's values mix it, so a cut or a new point has it
    {
        KisPaintInformation full(QPointF(0, 0), 1.0);
        KisPaintInformation half(QPointF(10, 0), 1.0);
        half.setDensityFactor(0.5);
        QCOMPARE(KisPaintInformation(half).densityFactor(), 0.5);
        QVERIFY(qAbs(KisPaintInformation::mix(0.25, full, half).densityFactor() - 0.875) < 1e-9);
        half.setDensityFactor(7.0);
        QCOMPARE(half.densityFactor(), 1.0);
    }
    Edit::Hit inJob;
    inJob.valid = true;
    inJob.jobIndex = 11; // from x = 100 to 108
    inJob.t = 0.5;
    inJob.position = QPointF(104, 50);
    KisRecordedBrushStrokeSP withPoint = Edit::insertPoint(light, inJob);
    QVERIFY(qAbs(densityAt(withPoint, 104) - (densityAt(light, 100) + densityAt(light, 108)) / 2.0) < 1e-9);

    // --- the file: version 3 as before without a density, 4 with one
    auto saved = [](KisRecordedBrushStrokeSP from, quint32 *version) {
        QBuffer buffer;
        buffer.open(QIODevice::ReadWrite);
        if (!KisBrushStrokeLayer::saveStrokes({from}, &buffer)) {
            return KisRecordedBrushStrokeSP();
        }
        const QByteArray data = buffer.data();
        *version = quint32(quint8(data[4])) | quint32(quint8(data[5])) << 8 | quint32(quint8(data[6])) << 16
            | quint32(quint8(data[7])) << 24;
        buffer.seek(0);
        QVector<KisRecordedBrushStrokeSP> loaded;
        if (!KisBrushStrokeLayer::loadStrokes(&buffer, &loaded) || loaded.size() != 1) {
            return KisRecordedBrushStrokeSP();
        }
        return loaded.first();
    };
    quint32 version = 0;
    KisRecordedBrushStrokeSP loaded = saved(stroke, &version);
    QVERIFY(loaded);
    QCOMPARE(version, quint32(3));
    QCOMPARE(densityAt(loaded, 100), 1.0);
    loaded = saved(light, &version);
    QVERIFY(loaded);
    QCOMPARE(version, quint32(4));
    QCOMPARE(loaded->jobs.size(), light->jobs.size());
    for (int i = 0; i < light->jobs.size(); i++) {
        QCOMPARE(loaded->jobs[i].pi1.densityFactor(), light->jobs[i].pi1.densityFactor());
        QCOMPARE(loaded->jobs[i].pi2.densityFactor(), light->jobs[i].pi2.densityFactor());
        QCOMPARE(loaded->jobs[i].pi2.pos(), light->jobs[i].pi2.pos());
        QCOMPARE(loaded->jobs[i].pi2.pressure(), light->jobs[i].pi2.pressure());
        QCOMPARE(loaded->jobs[i].pi2.currentTime(), light->jobs[i].pi2.currentTime());
    }

    // --- on the layer: half the ink in the middle, the ends as drawn
    KisBrushStrokeShape *shape = strokeShapes(d.layer).first();
    KUndo2Command *command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        {shape},
        [&](const KisBrushStrokeShape *edited) {
            return Edit::scaleDensity(edited->brushStroke(),
                                      Edit::nearestPoint(edited->brushStroke(), QPointF(100, 50)),
                                      0.5,
                                      30.0);
        },
        kundo2_noi18n("density"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage lighter = toImage(d.layer->projection());
    auto alpha = [](const QImage &image, int x, int y) {
        return qAlpha(image.pixel(x, y));
    };
    qInfo() << "alpha along the line, drawn:" << alpha(drawn, 40, 50) << alpha(drawn, 76, 50) << alpha(drawn, 92, 50)
            << alpha(drawn, 100, 50) << "lighter:" << alpha(lighter, 40, 50) << alpha(lighter, 76, 50)
            << alpha(lighter, 92, 50) << alpha(lighter, 100, 50);
    QVERIFY(alpha(drawn, 100, 50) > 200);
    // the nearest place of the pixel (100, 50), whose center is at 100.5
    QVERIFY(qAbs(alpha(lighter, 100, 50) - alpha(drawn, 100, 50) * 0.5) <= 3.0);
    QVERIFY(qAbs(alpha(lighter, 100, 49) - alpha(drawn, 100, 49) * 0.5) <= 3.0);
    QVERIFY(qAbs(alpha(lighter, 92, 50) - alpha(drawn, 92, 50) * densityAt(light, 92)) <= 6.0);
    QVERIFY(alpha(lighter, 76, 50) > alpha(lighter, 92, 50));
    QVERIFY(alpha(lighter, 92, 50) > alpha(lighter, 100, 50));
    QVERIFY(alpha(lighter, 124, 50) > alpha(lighter, 108, 50));
    // beyond the reach nothing changed (the line is drawn again, which
    // may differ by a rounding step); within it nothing got denser
    int changedOutside = 0;
    int denserInside = 0;
    for (int y = 0; y < drawn.height(); y++) {
        for (int x = 0; x < drawn.width(); x++) {
            if (x < 68 || x > 131) {
                changedOutside += qAbs(alpha(lighter, x, y) - alpha(drawn, x, y)) > 2;
            } else {
                denserInside += alpha(lighter, x, y) > alpha(drawn, x, y) + 2;
            }
        }
    }
    QCOMPARE(changedOutside, 0);
    QCOMPARE(denserInside, 0);
    QCOMPARE(strokeShapes(d.layer).first()->brushStroke()->jobs.size(), 21);

    undoStep(d);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), drawn) <= 2);
    redoStep(d);
    repaint(d.image, d.layer);
    QVERIFY(maxDifference(toImage(d.layer->projection()), lighter) <= 2);

    // --- saved and loaded: the density is in the file, and the line drawn
    // again at twice the size is lighter at the same place
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("density.kra"));
    QVERIFY(d.document->exportDocumentSync(path, KisDocument::nativeFormatMimeType()));

    QScopedPointer<KisDocument> document(KisPart::instance()->createDocument());
    document->setFileBatchMode(true);
    QVERIFY(document->openPath(path, KisDocument::DontAddToRecent));
    KisImageSP image = document->image();
    waitForImage(image);
    KisShapeLayerSP layer;
    KisLayerUtils::recursiveApplyNodes(image->root(), [&](KisNodeSP node) {
        if (KisShapeLayer *shapeLayer = dynamic_cast<KisShapeLayer *>(node.data())) {
            layer = shapeLayer;
        }
    });
    QVERIFY(layer);
    QCOMPARE(strokeShapes(layer).size(), 1);
    QVERIFY(qAbs(densityAt(strokeShapes(layer).first()->brushStroke(), 100) - 0.5) < 1e-9);
    QVERIFY(qAbs(densityAt(strokeShapes(layer).first()->brushStroke(), 108) - densityAt(light, 108)) < 1e-9);
    repaint(image, layer);
    QVERIFY(maxDifference(toImage(layer->projection()), lighter) <= 2);

    image->scaleImage(QSize(imageBounds.width() * 2, imageBounds.height() * 2),
                      image->xRes(),
                      image->yRes(),
                      KisFilterStrategyRegistry::instance()->value(QStringLiteral("Bicubic")));
    repaint(image, layer);
    const QImage twice = toImage(layer->projection(), QRect(0, 0, imageBounds.width() * 2, imageBounds.height() * 2));
    qInfo() << "at twice the size, alpha at the line's start and middle:" << alpha(twice, 60, 100)
            << alpha(twice, 200, 100);
    QVERIFY(alpha(twice, 60, 100) > 200);
    QVERIFY(qAbs(alpha(twice, 200, 100) - alpha(twice, 60, 100) * 0.5) <= 4.0);

    // --- a line that crosses itself: along (20,50)-(180,50), up, back and
    // down through it at x = 100 in steps of 5 pixels. The part going down
    // is made lighter; where it crosses the dense part, that part keeps
    // its ink.
    auto crossing = [&](const KisBrushStrokeShape *edited) {
        QSharedPointer<KisRecordedBrushStroke> changed(new KisRecordedBrushStroke(*edited->brushStroke()));
        QVector<QPointF> corners{QPointF(20, 50), QPointF(180, 50), QPointF(180, 20), QPointF(100, 20)};
        for (int y = 25; y <= 95; y += 5) {
            corners << QPointF(100, y);
        }
        changed->jobs.clear();
        for (int i = 1; i < corners.size(); i++) {
            Job job = stroke->jobs[1];
            job.pi1.setPos(corners[i - 1]);
            job.pi2.setPos(corners[i]);
            changed->jobs << job;
        }
        return KisRecordedBrushStrokeSP(changed);
    };
    command =
        KisBrushStrokeShapeEdit::replaceStrokes(d.layer, strokeShapes(d.layer), crossing, kundo2_noi18n("crossing"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage crossed = toImage(d.layer->projection());
    command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        strokeShapes(d.layer),
        [&](const KisBrushStrokeShape *edited) {
            // 60 pixels along the line from (100, 85): past the crossing
            // at (100, 50), but not to the part that goes along y = 50
            return Edit::scaleDensity(edited->brushStroke(),
                                      Edit::nearestPoint(edited->brushStroke(), QPointF(100, 85)),
                                      0.25,
                                      60.0);
        },
        kundo2_noi18n("density at a crossing"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage crossedLight = toImage(d.layer->projection());
    const KisRecordedBrushStrokeSP crossedStroke = strokeShapes(d.layer).first()->brushStroke();
    qreal densityAtCrossing = 1.0;
    for (const Job &job : crossedStroke->jobs) {
        if (job.pi2.pos() == QPointF(100, 50)) {
            densityAtCrossing = job.pi2.densityFactor();
        }
    }
    qInfo() << "crossing: the light part's density there" << densityAtCrossing
            << "; alpha before and after: at the crossing" << alpha(crossed, 100, 50) << alpha(crossedLight, 100, 50)
            << "on the dense part beside it" << alpha(crossed, 96, 50) << alpha(crossedLight, 96, 50)
            << "on the light part's end" << alpha(crossed, 100, 88) << alpha(crossedLight, 100, 88);
    QVERIFY(densityAtCrossing < 0.8);
    QVERIFY(alpha(crossed, 100, 50) > 200);
    QVERIFY(qAbs(alpha(crossedLight, 100, 50) - alpha(crossed, 100, 50)) <= 2);
    QVERIFY(qAbs(alpha(crossedLight, 96, 50) - alpha(crossed, 96, 50)) <= 2);
    QVERIFY(qAbs(alpha(crossedLight, 60, 50) - alpha(crossed, 60, 50)) <= 2);
    QVERIFY(alpha(crossed, 100, 88) > 200);
    QVERIFY(alpha(crossedLight, 100, 88) < alpha(crossed, 100, 88) * 0.4);
}

/// The background color of a stroke, seen with a brush that mixes the two
/// colors
void KisVectorBrushStrokeTest::testStrokeBackgroundColor()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    using Edit = KisBrushStrokePathEdit;
    KisPaintOpPresetSP brush = loadPreset(QStringLiteral("b)_Basic-5_Size_default.kpp"));
    QVERIFY(brush);
    // the Mix option: from the foreground to the background color with
    // the pressure
    brush = brush->clone().dynamicCast<KisPaintOpPreset>();
    brush->settings()->setProperty("PressureMix", true);
    brush->settings()->setPaintOpSize(20.0);

    Document d;
    paintLine(d.image, d.layer, brush, KoColor(Qt::black, cs), QPointF(20, 50), QPointF(180, 50));
    repaint(d.image, d.layer);
    const QImage drawn = toImage(d.layer->projection());
    const KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();

    // the function leaves the stroke itself alone
    const KoColor red(Qt::red, cs);
    QVERIFY(stroke->bgColor != red);
    const KoColor background = stroke->bgColor;
    KisRecordedBrushStrokeSP changed = Edit::withBackgroundColor(stroke, red);
    QVERIFY(changed != stroke);
    QCOMPARE(changed->bgColor, red);
    QCOMPARE(changed->fgColor, stroke->fgColor);
    QCOMPARE(stroke->bgColor, background);
    QCOMPARE(Edit::withBackgroundColor(changed, red), changed);
    QCOMPARE(Edit::withBackgroundColor(KisRecordedBrushStrokeSP(), red), KisRecordedBrushStrokeSP());

    // on the layer, at half pressure: the line loses its green to the red
    // background color and keeps its shape
    KUndo2Command *command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        {strokeShapes(d.layer).first()},
        [&](const KisBrushStrokeShape *edited) {
            QSharedPointer<KisRecordedBrushStroke> half(new KisRecordedBrushStroke(*edited->brushStroke()));
            for (KisRecordedBrushStroke::Job &job : half->jobs) {
                job.pi1.setPressure(0.5);
                job.pi2.setPressure(0.5);
            }
            return KisRecordedBrushStrokeSP(half);
        },
        kundo2_noi18n("half pressure"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage before = toImage(d.layer->projection());
    command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        {strokeShapes(d.layer).first()},
        [&](const KisBrushStrokeShape *edited) {
            return Edit::withBackgroundColor(edited->brushStroke(), red);
        },
        kundo2_noi18n("background"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage after = toImage(d.layer->projection());
    QCOMPARE(strokeShapes(d.layer).first()->brushStroke()->bgColor, red);
    auto greenOf = [](const QImage &image) {
        qint64 sum = 0;
        qint64 count = 0;
        for (int y = 0; y < image.height(); y++) {
            for (int x = 0; x < image.width(); x++) {
                const QRgb pixel = image.pixel(x, y);
                if (qAlpha(pixel) > 200) {
                    sum += qGreen(pixel);
                    count++;
                }
            }
        }
        return count ? qreal(sum) / count : -1.0;
    };
    // the background was white: the mix with black was gray, with red
    // it has no green
    qInfo() << "mean green of the opaque pixels, before:" << greenOf(before) << "after:" << greenOf(after);
    QVERIFY(greenOf(before) > 60.0);
    QVERIFY(greenOf(after) < greenOf(before) - 40.0);
    QCOMPARE(after.convertToFormat(QImage::Format_Alpha8), before.convertToFormat(QImage::Format_Alpha8));

    d.document->undoStack()->undo();
    repaint(d.image, d.layer);
    QCOMPARE(toImage(d.layer->projection()), before);
    d.document->undoStack()->redo();
    repaint(d.image, d.layer);
    QCOMPARE(toImage(d.layer->projection()), after);
    Q_UNUSED(drawn);
}

/// A line made wider or thinner by a fixed amount: through the pressures
/// of a brush whose size follows them, through the brush size otherwise
void KisVectorBrushStrokeTest::testAddWidth()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    using Edit = KisBrushStrokePathEdit;
    using Job = KisRecordedBrushStroke::Job;
    KisPaintOpPresetSP brush = loadPreset(QStringLiteral("b)_Basic-5_Size_default.kpp"));
    QVERIFY(brush);
    brush = brush->clone().dynamicCast<KisPaintOpPreset>();
    brush->settings()->setPaintOpSize(12.0);

    Document d;
    // a point and 20 lines of 8 pixels at full pressure
    paintLine(d.image, d.layer, brush, KoColor(Qt::black, cs), QPointF(20, 50), QPointF(180, 50));
    repaint(d.image, d.layer);
    const QImage drawn = toImage(d.layer->projection());
    const KisRecordedBrushStrokeSP full = strokeShapes(d.layer).first()->brushStroke();
    QCOMPARE(full->jobs.size(), 21);
    QVERIFY(Edit::sizeFollowsPressure(full));
    const qreal size = full->preset->settings()->paintOpSize();
    QCOMPARE(size, 12.0);

    auto pressureAt = [](KisRecordedBrushStrokeSP from, qreal x) {
        for (const Job &job : from->jobs) {
            if (qAbs(job.pi2.pos().x() - x) < 1e-6) {
                return job.pi2.pressure();
            }
        }
        return -1.0;
    };
    auto sizeOf = [](KisRecordedBrushStrokeSP from) {
        return from->preset->settings()->paintOpSize();
    };
    /// the line's width at the control point at @p x, by the brush's curve
    auto lineWidth = [&](KisRecordedBrushStrokeSP from, qreal x) {
        return sizeOf(from) * Edit::widthShare(from, pressureAt(from, x));
    };
    const qreal top = Edit::widthShare(full, 1.0);
    qInfo() << "width share at pressure 0.25, 0.5, 0.75, 1:" << Edit::widthShare(full, 0.25)
            << Edit::widthShare(full, 0.5) << Edit::widthShare(full, 0.75) << top;
    QVERIFY(top > 0.9);
    QVERIFY(Edit::widthShare(full, 0.25) < Edit::widthShare(full, 0.75));

    // --- the functions, on the line at a pressure of 0.7
    QSharedPointer<KisRecordedBrushStroke> lowPressure(new KisRecordedBrushStroke(*full));
    for (Job &job : lowPressure->jobs) {
        job.pi1.setPressure(0.7);
        job.pi2.setPressure(0.7);
    }
    const KisRecordedBrushStrokeSP stroke = lowPressure;
    const qreal width = lineWidth(stroke, 100);
    QVERIFY(width > 1.0);
    QVERIFY(width < size * top - 1.0);
    const Edit::Hit middle = Edit::nearestPoint(stroke, QPointF(100, 50));
    QVERIFY(middle.valid);

    // wider at x = 100, within the brush: only pressures change
    const qreal more = (size * top - width) / 2.0;
    KisRecordedBrushStrokeSP wider = Edit::addWidth(stroke, middle, more, 30.0);
    QVERIFY(wider != stroke);
    QCOMPARE(wider->preset, stroke->preset);
    QVERIFY(qAbs(lineWidth(wider, 100) - (width + more)) < 1e-6);
    const qreal near = 0.5 * (1.0 + qCos(M_PI * 8.0 / 30.0));
    QVERIFY(qAbs(lineWidth(wider, 108) - (width + more * near)) < 1e-6);
    QVERIFY(qAbs(pressureAt(wider, 92) - pressureAt(wider, 108)) < 1e-9);
    QCOMPARE(pressureAt(wider, 132), 0.7);
    QCOMPARE(pressureAt(wider, 28), 0.7);
    QCOMPARE(pressureAt(stroke, 100), 0.7);
    for (int i = 0; i < wider->jobs.size(); i++) {
        if (i > 1) {
            QCOMPARE(wider->jobs[i].pi1.pressure(), wider->jobs[i - 1].pi2.pressure());
        }
        QCOMPARE(wider->jobs[i].pi2.pos(), stroke->jobs[i].pi2.pos());
    }
    // the same amount at a thick and a thin place: not a scale
    {
        QSharedPointer<KisRecordedBrushStroke> uneven(new KisRecordedBrushStroke(*stroke));
        for (Job &job : uneven->jobs) {
            if (job.pi1.pos().x() >= 100 - 1e-6) {
                job.pi1.setPressure(0.5);
            }
            if (job.pi2.pos().x() >= 100 - 1e-6) {
                job.pi2.setPressure(0.5);
            }
        }
        const qreal thick = lineWidth(uneven, 60);
        const qreal thin = lineWidth(uneven, 140);
        QVERIFY(thick > thin + 0.5);
        // a reach far longer than the line: (nearly) all of it
        KisRecordedBrushStrokeSP both = Edit::addWidth(uneven, Edit::nearestPoint(uneven, QPointF(96, 50)), 1.0, 1e7);
        QVERIFY(qAbs(lineWidth(both, 60) - (thick + 1.0)) < 1e-4);
        QVERIFY(qAbs(lineWidth(both, 140) - (thin + 1.0)) < 1e-4);
    }

    // thinner, and never below a pressure of 0.01
    KisRecordedBrushStrokeSP thinner = Edit::addWidth(stroke, middle, -width / 2.0, 30.0);
    QVERIFY(qAbs(lineWidth(thinner, 100) - width / 2.0) < 1e-6);
    QCOMPARE(pressureAt(thinner, 132), 0.7);
    QCOMPARE(thinner->preset, stroke->preset);
    QVERIFY(qAbs(pressureAt(Edit::addWidth(stroke, middle, -50.0, 0.0), 100) - 0.01) < 1e-9);
    QCOMPARE(pressureAt(Edit::addWidth(stroke, middle, -50.0, 0.0), 108), 0.7);

    // wider than the brush: it grows to that width, and the other places
    // keep theirs
    KisRecordedBrushStrokeSP grown = Edit::addWidth(stroke, middle, 12.0, 30.0);
    QVERIFY(grown->preset != stroke->preset);
    qInfo() << "grown brush:" << sizeOf(grown) << "for a width of" << width + 12.0;
    QVERIFY(qAbs(sizeOf(grown) * top - (width + 12.0)) < 1e-3);
    QCOMPARE(sizeOf(stroke), 12.0);
    QVERIFY(qAbs(pressureAt(grown, 100) - 1.0) < 1e-6);
    QVERIFY(qAbs(lineWidth(grown, 132) - width) < 1e-3);
    QVERIFY(qAbs(lineWidth(grown, 108) - (width + 12.0 * near)) < 1e-3);
    QVERIFY(Edit::sizeFollowsPressure(grown));

    QCOMPARE(Edit::addWidth(stroke, middle, 0.0, 30.0), stroke);
    QCOMPARE(Edit::addWidth(stroke, Edit::Hit(), 3.0, 30.0), stroke);

    // --- a brush whose size does not follow the pressure: its size, for
    // the whole stroke
    {
        QSharedPointer<KisRecordedBrushStroke> even(new KisRecordedBrushStroke(*stroke));
        even->preset = stroke->preset->clone().dynamicCast<KisPaintOpPreset>();
        even->preset->settings()->setProperty("PressureSize", false);
        QVERIFY(!Edit::sizeFollowsPressure(even));
        QCOMPARE(Edit::widthShare(even, 0.3), 1.0);
        const Edit::Hit place = Edit::nearestPoint(even, QPointF(100, 50));
        KisRecordedBrushStrokeSP evenWider = Edit::addWidth(even, place, 3.0, 30.0);
        QVERIFY(qAbs(sizeOf(evenWider) - 15.0) < 1e-9);
        QCOMPARE(sizeOf(even), 12.0);
        for (int i = 0; i < even->jobs.size(); i++) {
            QCOMPARE(evenWider->jobs[i].pi2.pressure(), even->jobs[i].pi2.pressure());
        }
        QVERIFY(qAbs(sizeOf(Edit::addWidth(even, place, -4.0, 0.0)) - 8.0) < 1e-9);
        QVERIFY(qAbs(sizeOf(Edit::addWidth(even, place, -40.0, 0.0)) - 1.0) < 1e-9);
        QVERIFY(!Edit::sizeFollowsPressure(KisRecordedBrushStrokeSP()));
    }

    // --- on the layer, the line drawn at full pressure: 6 pixels wider in
    // the middle (the brush grows), the ends as drawn
    auto widthAt = [](const QImage &image, int x) {
        int count = 0;
        for (int y = 0; y < image.height(); y++) {
            count += qAlpha(image.pixel(x, y)) > 127 ? 1 : 0;
        }
        return count;
    };
    KisBrushStrokeShape *shape = strokeShapes(d.layer).first();
    KUndo2Command *command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        {shape},
        [&](const KisBrushStrokeShape *edited) {
            return Edit::addWidth(edited->brushStroke(),
                                  Edit::nearestPoint(edited->brushStroke(), QPointF(100, 50)),
                                  6.0,
                                  30.0);
        },
        kundo2_noi18n("add width"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage thick = toImage(d.layer->projection());
    qInfo() << "width at x = 40, 100, 150, drawn:" << widthAt(drawn, 40) << widthAt(drawn, 100) << widthAt(drawn, 150)
            << "wider:" << widthAt(thick, 40) << widthAt(thick, 100) << widthAt(thick, 150);
    QVERIFY(qAbs(widthAt(drawn, 100) - 12) <= 2);
    // the dabs do not fall on the control point: the widest column near it
    int widest = 0;
    int thinnest = thick.height();
    for (int x = 98; x <= 102; x++) {
        widest = qMax(widest, widthAt(thick, x));
    }
    QVERIFY(qAbs(widest - widthAt(drawn, 100) - 6) <= 2);
    QVERIFY(qAbs(widthAt(thick, 40) - widthAt(drawn, 40)) <= 1);
    QVERIFY(qAbs(widthAt(thick, 150) - widthAt(drawn, 150)) <= 1);

    // thinner by 6 pixels there, from the drawn line
    d.document->undoStack()->undo();
    repaint(d.image, d.layer);
    QCOMPARE(toImage(d.layer->projection()), drawn);
    command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        {strokeShapes(d.layer).first()},
        [&](const KisBrushStrokeShape *edited) {
            return Edit::addWidth(edited->brushStroke(),
                                  Edit::nearestPoint(edited->brushStroke(), QPointF(100, 50)),
                                  -6.0,
                                  30.0);
        },
        kundo2_noi18n("add width"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage thin = toImage(d.layer->projection());
    qInfo() << "thinner:" << widthAt(thin, 40) << widthAt(thin, 100) << widthAt(thin, 150);
    for (int x = 98; x <= 102; x++) {
        thinnest = qMin(thinnest, widthAt(thin, x));
    }
    QVERIFY(qAbs(widthAt(drawn, 100) - thinnest - 6) <= 2);
    QVERIFY(qAbs(widthAt(thin, 40) - widthAt(drawn, 40)) <= 1);
    d.document->undoStack()->undo();
    repaint(d.image, d.layer);
    QCOMPARE(toImage(d.layer->projection()), drawn);
    d.document->undoStack()->redo();
    repaint(d.image, d.layer);
    QCOMPARE(toImage(d.layer->projection()), thin);
}

/// A part of a line with the widths a pen gives it that traces the line
void KisVectorBrushStrokeTest::testRedrawWidth()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    using Edit = KisBrushStrokePathEdit;
    using Job = KisRecordedBrushStroke::Job;
    KisPaintOpPresetSP brush = loadPreset(QStringLiteral("b)_Basic-5_Size_default.kpp"));
    QVERIFY(brush);
    brush = brush->clone().dynamicCast<KisPaintOpPreset>();
    brush->settings()->setPaintOpSize(12.0);

    Document d;
    // a point and 20 lines of 8 pixels at full pressure
    paintLine(d.image, d.layer, brush, KoColor(Qt::black, cs), QPointF(20, 50), QPointF(180, 50));
    repaint(d.image, d.layer);
    const QImage drawn = toImage(d.layer->projection());
    const KisRecordedBrushStrokeSP full = strokeShapes(d.layer).first()->brushStroke();
    QCOMPARE(full->jobs.size(), 21);
    QVERIFY(Edit::sizeFollowsPressure(full));
    const qreal top = Edit::widthShare(full, 1.0);
    QCOMPARE(Edit::widthShare(full->preset, 0.4), Edit::widthShare(full, 0.4));
    QCOMPARE(Edit::widthShare(KisPaintOpPresetSP(), 0.4), 1.0);

    auto sizeOf = [](KisRecordedBrushStrokeSP from) {
        return from->preset->settings()->paintOpSize();
    };
    /// the line's width at the control point at @p x, by the brush's
    /// curve; negative without a control point there
    auto lineWidth = [&](KisRecordedBrushStrokeSP from, qreal x) {
        for (const Job &job : from->jobs) {
            if (qAbs(job.pi2.pos().x() - x) < 1e-6) {
                return sizeOf(from) * Edit::widthShare(from, job.pi2.pressure());
            }
        }
        return -1.0;
    };
    /// the jobs meet with the same pen values, on the line as drawn
    auto isSameLine = [](KisRecordedBrushStrokeSP on) {
        const Job *previous = nullptr;
        for (const Job &job : on->jobs) {
            if (qAbs(job.pi1.pos().y() - 50.0) > 1e-6) {
                return false;
            }
            if (job.type == FreehandStrokeStrategy::Data::POINT) {
                continue;
            }
            if (qAbs(job.pi2.pos().y() - 50.0) > 1e-6) {
                return false;
            }
            if (previous
                && (qAbs(job.pi1.pos().x() - previous->pi2.pos().x()) > 1e-6
                    || qAbs(job.pi1.pressure() - previous->pi2.pressure()) > 1e-9
                    || job.pi1.currentTime() + 1e-9 < previous->pi2.currentTime())) {
                return false;
            }
            previous = &job;
        }
        return previous && qAbs(previous->pi2.pos().x() - 180.0) < 1e-6;
    };
    auto placesAt = [](KisRecordedBrushStrokeSP on, const QVector<qreal> &xs) {
        QVector<Edit::Hit> places;
        for (const qreal x : xs) {
            places << Edit::nearestPoint(on, QPointF(x, 50));
        }
        return places;
    };

    // --- the function, on the line at a pressure of 0.7
    QSharedPointer<KisRecordedBrushStroke> lowPressure(new KisRecordedBrushStroke(*full));
    for (Job &job : lowPressure->jobs) {
        job.pi1.setPressure(0.7);
        job.pi2.setPressure(0.7);
    }
    const KisRecordedBrushStrokeSP stroke = lowPressure;
    const qreal width = lineWidth(stroke, 100);
    QVERIFY(width > 5.0);
    QVERIFY(width < 12.0 * top - 1.0);

    // one width from x = 60 to 140: control points every 4 pixels there
    KisRecordedBrushStrokeSP changed = Edit::redrawnWidth(stroke, placesAt(stroke, {60, 100, 140}), {4.0, 4.0, 4.0});
    QVERIFY(changed != stroke);
    QCOMPARE(changed->preset, stroke->preset);
    QVERIFY(isSameLine(changed));
    QCOMPARE(changed->jobs.size(), 31);
    QCOMPARE(stroke->jobs.size(), 21);
    for (qreal x = 64; x <= 136; x += 4) {
        QVERIFY(qAbs(lineWidth(changed, x) - 4.0) < 1e-6);
    }
    // the part's ends and the rest keep their width
    for (const qreal x : {28.0, 52.0, 60.0, 140.0, 148.0, 180.0}) {
        QVERIFY(qAbs(lineWidth(changed, x) - width) < 1e-9);
    }
    QVERIFY(lineWidth(changed, 56) < 0.0);
    QCOMPARE(changed->jobs.first().pi1.pressure(), 0.7);
    QCOMPARE(changed->jobs.last().pi2.currentTime(), stroke->jobs.last().pi2.currentTime());
    QCOMPARE(lineWidth(stroke, 100), width);

    // widths that change along the part, given in any order
    changed = Edit::redrawnWidth(stroke, placesAt(stroke, {140, 60, 100}), {2.0, 2.0, 8.0});
    QVERIFY(isSameLine(changed));
    QVERIFY(qAbs(lineWidth(changed, 100) - 8.0) < 1e-6);
    QVERIFY(qAbs(lineWidth(changed, 80) - 5.0) < 1e-6);
    QVERIFY(qAbs(lineWidth(changed, 120) - 5.0) < 1e-6);
    QVERIFY(qAbs(lineWidth(changed, 64) - 2.6) < 1e-6);
    QVERIFY(qAbs(lineWidth(changed, 60) - width) < 1e-9);

    // a part inside one job: points at its ends and one between
    changed = Edit::redrawnWidth(stroke, placesAt(stroke, {61, 67}), {3.0, 3.0});
    QVERIFY(isSameLine(changed));
    QCOMPARE(changed->jobs.size(), 24);
    QVERIFY(qAbs(lineWidth(changed, 64) - 3.0) < 1e-6);
    QVERIFY(qAbs(lineWidth(changed, 61) - width) < 1e-9);
    QVERIFY(qAbs(lineWidth(changed, 67) - width) < 1e-9);
    QVERIFY(qAbs(lineWidth(changed, 68) - width) < 1e-9);

    // up to the line's ends: they take the width, the first dab too
    changed = Edit::redrawnWidth(stroke, placesAt(stroke, {140, 180}), {4.0, 4.0});
    QVERIFY(isSameLine(changed));
    QVERIFY(qAbs(lineWidth(changed, 180) - 4.0) < 1e-6);
    QVERIFY(qAbs(lineWidth(changed, 140) - width) < 1e-9);
    changed = Edit::redrawnWidth(stroke, placesAt(stroke, {20, 60}), {4.0, 4.0});
    QVERIFY(isSameLine(changed));
    QCOMPARE(int(changed->jobs.first().type), int(FreehandStrokeStrategy::Data::POINT));
    QVERIFY(qAbs(sizeOf(changed) * Edit::widthShare(changed, changed->jobs.first().pi1.pressure()) - 4.0) < 1e-6);
    QVERIFY(qAbs(sizeOf(changed) * Edit::widthShare(changed, changed->jobs[1].pi1.pressure()) - 4.0) < 1e-6);
    QVERIFY(qAbs(lineWidth(changed, 60) - width) < 1e-9);

    // wider than the brush: it grows, and the rest keeps its width
    changed = Edit::redrawnWidth(stroke, placesAt(stroke, {60, 140}), {20.0, 20.0});
    QVERIFY(changed->preset != stroke->preset);
    QVERIFY(qAbs(sizeOf(changed) * top - 20.0) < 1e-3);
    QVERIFY(qAbs(lineWidth(changed, 100) - 20.0) < 1e-3);
    QVERIFY(qAbs(lineWidth(changed, 28) - width) < 1e-3);
    QVERIFY(qAbs(lineWidth(changed, 140) - width) < 1e-3);
    QCOMPARE(sizeOf(stroke), 12.0);

    // nothing to do
    QCOMPARE(Edit::redrawnWidth(stroke, {}, {}), stroke);
    QCOMPARE(Edit::redrawnWidth(stroke, placesAt(stroke, {100}), {4.0}), stroke);
    QCOMPARE(Edit::redrawnWidth(stroke, placesAt(stroke, {60, 140}), {4.0}), stroke);
    QCOMPARE(Edit::redrawnWidth(stroke, placesAt(stroke, {60, 140}), {0.0, 0.0}), stroke);
    QCOMPARE(Edit::redrawnWidth(stroke, {Edit::Hit(), Edit::Hit()}, {4.0, 4.0}), stroke);

    // --- a brush whose size does not follow the pressure: the mean of the
    // widths as its size, for the whole stroke
    {
        QSharedPointer<KisRecordedBrushStroke> even(new KisRecordedBrushStroke(*stroke));
        even->preset = stroke->preset->clone().dynamicCast<KisPaintOpPreset>();
        even->preset->settings()->setProperty("PressureSize", false);
        const KisRecordedBrushStrokeSP evenChanged = Edit::redrawnWidth(even, placesAt(even, {60, 140}), {4.0, 8.0});
        QVERIFY(qAbs(sizeOf(evenChanged) - 6.0) < 1e-3);
        QCOMPARE(evenChanged->jobs.size(), even->jobs.size());
        QCOMPARE(sizeOf(even), 12.0);
    }

    // --- on the layer, the line drawn at full pressure: 5 pixels wide in
    // the middle, the ends as drawn
    auto widthAt = [](const QImage &image, int x) {
        int count = 0;
        for (int y = 0; y < image.height(); y++) {
            count += qAlpha(image.pixel(x, y)) > 127 ? 1 : 0;
        }
        return count;
    };
    KUndo2Command *command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        {strokeShapes(d.layer).first()},
        [&](const KisBrushStrokeShape *edited) {
            const KisRecordedBrushStrokeSP on = edited->brushStroke();
            return Edit::redrawnWidth(on, placesAt(on, {60, 100, 140}), {5.0, 5.0, 5.0});
        },
        kundo2_noi18n("redraw width"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage thin = toImage(d.layer->projection());
    qInfo() << "width at x = 40, 80, 100, 120, 160, drawn:" << widthAt(drawn, 40) << widthAt(drawn, 80)
            << widthAt(drawn, 100) << widthAt(drawn, 120) << widthAt(drawn, 160) << "traced:" << widthAt(thin, 40)
            << widthAt(thin, 80) << widthAt(thin, 100) << widthAt(thin, 120) << widthAt(thin, 160);
    QVERIFY(qAbs(widthAt(drawn, 100) - 12) <= 2);
    for (const int x : {80, 100, 120}) {
        QVERIFY(qAbs(widthAt(thin, x) - 5) <= 2);
    }
    QVERIFY(qAbs(widthAt(thin, 40) - widthAt(drawn, 40)) <= 1);
    QVERIFY(qAbs(widthAt(thin, 160) - widthAt(drawn, 160)) <= 1);
    d.document->undoStack()->undo();
    repaint(d.image, d.layer);
    QCOMPARE(toImage(d.layer->projection()), drawn);
    d.document->undoStack()->redo();
    repaint(d.image, d.layer);
    QCOMPARE(toImage(d.layer->projection()), thin);
}

/// The paths of a stroke's lines as few pieces, for assistants along them
void KisVectorBrushStrokeTest::testPathPieces()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    using Edit = KisBrushStrokePathEdit;
    using Job = KisRecordedBrushStroke::Job;
    KisPaintOpPresetSP ink = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    QVERIFY(ink);

    // --- a wave with a pressure that rises and falls: 62 jobs
    {
        Document d;
        paintStroke(d.image, d.layer, ink, KoColor(Qt::black, cs));
        const KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();
        QCOMPARE(stroke->jobs.size(), 62);
        const Job *firstPath = nullptr;
        for (const Job &job : stroke->jobs) {
            if (job.type != FreehandStrokeStrategy::Data::POINT) {
                firstPath = &job;
                break;
            }
        }
        QVERIFY(firstPath);

        const QVector<Edit::PathPiece> pieces = Edit::pathPieces(stroke, 1.0);
        const int simplifiedJobs = Edit::simplified(stroke, 1.0)->jobs.size();
        qInfo() << "pieces at 1 px:" << pieces.size() << "; jobs simplified with the pressure:" << simplifiedJobs;
        QVERIFY(!pieces.isEmpty());
        QVERIFY(pieces.size() <= simplifiedJobs);
        QVERIFY(pieces.size() <= 10);
        // one path from the line's start to its end, within the tolerance
        QVERIFY(std::hypot(pieces.first().start.x() - firstPath->pi1.pos().x(),
                           pieces.first().start.y() - firstPath->pi1.pos().y())
                < 1e-6);
        QVERIFY(std::hypot(pieces.last().end.x() - stroke->jobs.last().pi2.pos().x(),
                           pieces.last().end.y() - stroke->jobs.last().pi2.pos().y())
                < 1e-6);
        qreal far = 0.0;
        int curves = 0;
        for (int i = 0; i < pieces.size(); i++) {
            const Edit::PathPiece &piece = pieces[i];
            if (i > 0) {
                QCOMPARE(piece.start, pieces[i - 1].end);
            }
            curves += piece.curve ? 1 : 0;
            if (!piece.curve) {
                QCOMPARE(piece.control1, piece.start);
                QCOMPARE(piece.control2, piece.end);
            }
            for (int step = 0; step <= 16; step++) {
                const qreal t = step / 16.0;
                const qreal u = 1.0 - t;
                const QPointF point = u * u * u * piece.start + 3 * u * u * t * piece.control1
                    + 3 * u * t * t * piece.control2 + t * t * t * piece.end;
                far = qMax(far, Edit::nearestPoint(stroke, point).distance);
            }
        }
        qInfo() << "curved pieces:" << curves << "; the pieces leave the path by" << far;
        QVERIFY(curves > 0);
        QVERIFY(far < 1.5);
        // the stroke is as it was
        QCOMPARE(stroke->jobs.size(), 62);
        QVERIFY(stroke->jobs.last().pi2.pressure() < 1.0);

        // without simplifying: a piece for every job of the path
        QCOMPARE(Edit::pathPieces(stroke, 0.0).size(), 61);
        // a looser tolerance does not need more pieces
        QVERIFY(Edit::pathPieces(stroke, 4.0).size() <= pieces.size());
    }

    // --- a straight line: one straight piece
    {
        Document d;
        paintLine(d.image, d.layer, ink, KoColor(Qt::black, cs), QPointF(20, 50), QPointF(180, 50));
        const KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();
        const QVector<Edit::PathPiece> pieces = Edit::pathPieces(stroke, 1.0);
        QCOMPARE(pieces.size(), 1);
        QVERIFY(!pieces.first().curve);
        QVERIFY(std::hypot(pieces.first().start.x() - 20.0, pieces.first().start.y() - 50.0) < 1e-6);
        QVERIFY(std::hypot(pieces.first().end.x() - 180.0, pieces.first().end.y() - 50.0) < 1e-6);
    }

    QVERIFY(Edit::pathPieces(KisRecordedBrushStrokeSP(), 1.0).isEmpty());

    // --- what a line encloses: the wave closed from its end to its start
    {
        Document d;
        paintStroke(d.image, d.layer, ink, KoColor(Qt::black, cs));
        const KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();
        const QPainterPath closed = Edit::closedPath(stroke);
        QCOMPARE(closed.fillRule(), Qt::WindingFill);
        // the wave goes down to y = 80 at x = 47 and up to y = 20 at x =
        // 102; the closing piece runs from (180, 30) to (20, 50)
        QVERIFY(closed.contains(QPointF(47, 65)));
        QVERIFY(closed.contains(QPointF(102, 30)));
        QVERIFY(!closed.contains(QPointF(47, 30)));
        QVERIFY(!closed.contains(QPointF(102, 60)));
        QVERIFY(!closed.contains(QPointF(190, 50)));
        QVERIFY(closed.boundingRect().contains(QPointF(20, 50)));
        QVERIFY(closed.boundingRect().contains(QPointF(180, 30)));
    }
    {
        Document d;
        paintLine(d.image, d.layer, ink, KoColor(Qt::black, cs), QPointF(20, 50), QPointF(180, 50));
        const QPainterPath flat = Edit::closedPath(strokeShapes(d.layer).first()->brushStroke());
        QVERIFY(flat.boundingRect().height() < 1e-6);
        QVERIFY(!flat.contains(QPointF(100, 51)));
    }
    QVERIFY(Edit::closedPath(KisRecordedBrushStrokeSP()).isEmpty());
}

/// The points of a stroke's path moved by a map, as Free Transform does
/// with the frame of the selected lines
void KisVectorBrushStrokeTest::testMapPoints()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    using Edit = KisBrushStrokePathEdit;
    using Job = KisRecordedBrushStroke::Job;
    KisPaintOpPresetSP brush = loadPreset(QStringLiteral("b)_Basic-5_Size_default.kpp"));
    QVERIFY(brush);

    // --- the function, on a stroke with a point, lines and a curve
    {
        Document d;
        paintStroke(d.image, d.layer, brush, KoColor(Qt::black, cs));
        const KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();
        bool hasCurve = false;
        bool hasPoint = false;
        for (const Job &job : stroke->jobs) {
            hasCurve = hasCurve || job.type == FreehandStrokeStrategy::Data::CURVE;
            hasPoint = hasPoint || job.type == FreehandStrokeStrategy::Data::POINT;
        }
        QVERIFY(hasCurve);
        QVERIFY(hasPoint);
        QVERIFY(Edit::canMapPoints(stroke));

        // a frame whose right side is pulled down and made shorter
        QTransform transform;
        QVERIFY(QTransform::quadToQuad(QPolygonF({QPointF(0, 0), QPointF(200, 0), QPointF(200, 100), QPointF(0, 100)}),
                                       QPolygonF({QPointF(0, 0), QPointF(180, 30), QPointF(180, 90), QPointF(0, 100)}),
                                       transform));
        QVERIFY(!transform.isAffine());
        const KisRecordedBrushStrokeSP moved = Edit::mapped(stroke, [&](const QPointF &point) {
            return transform.map(point);
        });
        QVERIFY(moved != stroke);
        QCOMPARE(moved->jobs.size(), stroke->jobs.size());
        QCOMPARE(moved->preset, stroke->preset);
        for (int i = 0; i < stroke->jobs.size(); i++) {
            const Job &before = stroke->jobs[i];
            const Job &after = moved->jobs[i];
            QCOMPARE(int(after.type), int(before.type));
            QCOMPARE(after.pi1.pos(), transform.map(before.pi1.pos()));
            QCOMPARE(after.pi1.pressure(), before.pi1.pressure());
            QCOMPARE(after.pi1.currentTime(), before.pi1.currentTime());
            if (before.type != FreehandStrokeStrategy::Data::POINT) {
                QCOMPARE(after.pi2.pos(), transform.map(before.pi2.pos()));
                QCOMPARE(after.pi2.pressure(), before.pi2.pressure());
            }
            if (before.type == FreehandStrokeStrategy::Data::CURVE) {
                QCOMPARE(after.control1, transform.map(before.control1));
                QCOMPARE(after.control2, transform.map(before.control2));
            }
            // the jobs still meet
            if (i > 1 && before.pi1.pos() == stroke->jobs[i - 1].pi2.pos()) {
                QCOMPARE(after.pi1.pos(), moved->jobs[i - 1].pi2.pos());
            }
        }
        QCOMPARE(stroke->jobs.first().pi1.pos(), strokeShapes(d.layer).first()->brushStroke()->jobs.first().pi1.pos());

        // not for the canvas mirror's copies, and not to places that are
        // not numbers
        QSharedPointer<KisRecordedBrushStroke> mirrored(new KisRecordedBrushStroke(*stroke));
        mirrored->mirrorHorizontal = true;
        QVERIFY(!Edit::canMapPoints(mirrored));
        QCOMPARE(Edit::mapped(mirrored,
                              [](const QPointF &point) {
                                  return point + QPointF(5, 5);
                              }),
                 KisRecordedBrushStrokeSP(mirrored));
        QCOMPARE(Edit::mapped(stroke,
                              [](const QPointF &point) {
                                  return point / 0.0;
                              }),
                 stroke);
        QVERIFY(!Edit::canMapPoints(KisRecordedBrushStrokeSP()));
    }

    // --- on the layer: a line moved down by 20 pixels and made half as
    // long towards its start, one undo step
    Document d;
    paintLine(d.image, d.layer, brush, KoColor(Qt::black, cs), QPointF(20, 50), QPointF(180, 50));
    repaint(d.image, d.layer);
    const QImage drawn = toImage(d.layer->projection());
    auto inkBounds = [](const QImage &image) {
        QRect bounds;
        for (int y = 0; y < image.height(); y++) {
            for (int x = 0; x < image.width(); x++) {
                if (qAlpha(image.pixel(x, y)) > 127) {
                    bounds |= QRect(x, y, 1, 1);
                }
            }
        }
        return bounds;
    };
    const QRect before = inkBounds(drawn);
    QVERIFY(before.width() > 150);
    KisBrushStrokeShape *shape = strokeShapes(d.layer).first();
    const QTransform shapeToImage = shape->expectedTransform();
    KUndo2Command *command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        {shape},
        [&](const KisBrushStrokeShape *edited) {
            return Edit::mapped(edited->brushStroke(), [&](const QPointF &point) {
                const QPointF image = shapeToImage.map(point);
                return shapeToImage.inverted().map(QPointF(20.0 + (image.x() - 20.0) / 2.0, image.y() + 20.0));
            });
        },
        kundo2_noi18n("map"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage movedImage = toImage(d.layer->projection());
    const QRect after = inkBounds(movedImage);
    qInfo() << "ink before" << before << "after" << after;
    QVERIFY(qAbs(after.top() - before.top() - 20) <= 1);
    QVERIFY(qAbs(after.bottom() - before.bottom() - 20) <= 1);
    QVERIFY(qAbs(after.left() - before.left()) <= 2);
    QVERIFY(qAbs(after.width() - (before.width() - 80)) <= 3);
    d.document->undoStack()->undo();
    repaint(d.image, d.layer);
    QCOMPARE(toImage(d.layer->projection()), drawn);
    d.document->undoStack()->redo();
    repaint(d.image, d.layer);
    QCOMPARE(toImage(d.layer->projection()), movedImage);
}

/// A part of a line drawn again along another path
void KisVectorBrushStrokeTest::testRedraw()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    using Edit = KisBrushStrokePathEdit;
    using Job = KisRecordedBrushStroke::Job;
    KisPaintOpPresetSP brush = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    QVERIFY(brush);
    brush = brush->clone().dynamicCast<KisPaintOpPreset>();
    brush->settings()->setPaintOpSize(8.0);

    Document d;
    // a point and 20 lines of 8 pixels, from x = 20 to 180 at y = 70
    paintLine(d.image, d.layer, brush, KoColor(Qt::black, cs), QPointF(20, 70), QPointF(180, 70));
    repaint(d.image, d.layer);
    const QImage drawn = toImage(d.layer->projection());
    const KisRecordedBrushStrokeSP stroke = strokeShapes(d.layer).first()->brushStroke();
    QCOMPARE(stroke->jobs.size(), 21);

    auto hitAt = [](KisRecordedBrushStrokeSP on, const QPointF &point) {
        return Edit::nearestPoint(on, point);
    };
    auto farFrom = [](KisRecordedBrushStrokeSP on, const QPointF &point) {
        return Edit::nearestPoint(on, point).distance;
    };
    /// the jobs meet, the pen's time does not run back, the first dab is
    /// where the first job starts
    auto isOneLine = [](KisRecordedBrushStrokeSP on) {
        const Job *previous = nullptr;
        for (const Job &job : on->jobs) {
            if (job.type == FreehandStrokeStrategy::Data::POINT) {
                continue;
            }
            if (previous) {
                const QPointF gap = job.pi1.pos() - previous->pi2.pos();
                if (std::hypot(gap.x(), gap.y()) > 1e-6 || job.pi1.currentTime() + 1e-9 < previous->pi2.currentTime()) {
                    return false;
                }
            }
            if (job.pi2.currentTime() + 1e-9 < job.pi1.currentTime()) {
                return false;
            }
            previous = &job;
        }
        return previous != nullptr;
    };
    auto firstPathJob = [](KisRecordedBrushStrokeSP on) {
        for (const Job &job : on->jobs) {
            if (job.type != FreehandStrokeStrategy::Data::POINT) {
                return job;
            }
        }
        return Job();
    };
    // an arc over the line, from x = 60 to x = 140, 30 pixels high
    QPolygonF arc;
    for (int i = 0; i <= 40; i++) {
        const qreal t = i / 40.0;
        arc << QPointF(60.0 + 80.0 * t, 70.0 - 30.0 * qSin(M_PI * t));
    }

    // --- between two places of the line
    const Edit::Hit from = hitAt(stroke, QPointF(60, 70));
    const Edit::Hit to = hitAt(stroke, QPointF(140, 70));
    QVERIFY(from.valid && to.valid);
    KisRecordedBrushStrokeSP changed = Edit::redrawn(stroke, from, to, arc);
    QVERIFY(changed != stroke);
    QVERIFY(isOneLine(changed));
    QVERIFY(farFrom(changed, QPointF(100, 40)) < 1.0);
    QVERIFY(farFrom(changed, QPointF(100, 70)) > 25.0);
    // the line outside the part is as it was, point for point
    QVERIFY(farFrom(changed, QPointF(40, 70)) < 1e-6);
    QVERIFY(farFrom(changed, QPointF(160, 70)) < 1e-6);
    QCOMPARE(changed->jobs.first().pi1.pos(), stroke->jobs.first().pi1.pos());
    QCOMPARE(changed->jobs.last().pi2.pos(), stroke->jobs.last().pi2.pos());
    QCOMPARE(changed->jobs.last().pi2.currentTime(), stroke->jobs.last().pi2.currentTime());
    QCOMPARE(changed->preset, stroke->preset);
    for (const Job &job : changed->jobs) {
        QCOMPARE(job.strokeInfoId, stroke->jobs.first().strokeInfoId);
        QVERIFY(job.pi2.pressure() > 0.0 && job.pi2.pressure() <= 1.0);
    }
    // jobs about 3 pixels long along the arc of about 105 pixels
    QVERIFY(changed->jobs.size() > stroke->jobs.size() + 10);
    QCOMPARE(stroke->jobs.size(), 21);

    // the same with the path drawn the other way, and with a path whose
    // ends are beside the line: it is moved onto it
    QPolygonF back = arc;
    std::reverse(back.begin(), back.end());
    KisRecordedBrushStrokeSP other = Edit::redrawn(stroke, to, from, back);
    QVERIFY(isOneLine(other));
    QVERIFY(farFrom(other, QPointF(100, 40)) < 1.0);
    QCOMPARE(other->jobs.size(), changed->jobs.size());
    QPolygonF beside = arc;
    beside.translate(0, 3);
    other = Edit::redrawn(stroke, from, to, beside);
    QVERIFY(isOneLine(other));
    QVERIFY(farFrom(other, QPointF(60, 70)) < 1e-6);
    QVERIFY(farFrom(other, QPointF(140, 70)) < 1e-6);
    QVERIFY(farFrom(other, QPointF(100, 40)) < 1.0);

    // --- a path that leaves the line: the rest of the line follows it
    QPolygonF away;
    for (int i = 0; i <= 30; i++) {
        away << QPointF(100.0 + 3.0 * i, 70.0 - 1.5 * i);
    }
    const Edit::Hit middle = hitAt(stroke, QPointF(100, 70));
    changed = Edit::redrawn(stroke, middle, Edit::Hit(), away);
    QVERIFY(changed != stroke);
    QVERIFY(isOneLine(changed));
    QCOMPARE(changed->jobs.last().pi2.pos(), QPointF(190, 25));
    QVERIFY(farFrom(changed, QPointF(160, 70)) > 20.0);
    QVERIFY(farFrom(changed, QPointF(60, 70)) < 1e-6);
    QCOMPARE(changed->jobs.first().pi1.pos(), stroke->jobs.first().pi1.pos());
    QCOMPARE(changed->jobs.last().pi2.currentTime(), stroke->jobs.last().pi2.currentTime());

    // towards the line's start: its first dab goes along
    QPolygonF awayBack;
    for (int i = 0; i <= 30; i++) {
        awayBack << QPointF(100.0 - 3.0 * i, 70.0 - 1.5 * i);
    }
    changed = Edit::redrawn(stroke, middle, Edit::Hit(), awayBack);
    QVERIFY(changed != stroke);
    QVERIFY(isOneLine(changed));
    QCOMPARE(firstPathJob(changed).pi1.pos(), QPointF(10, 25));
    QCOMPARE(int(changed->jobs.first().type), int(FreehandStrokeStrategy::Data::POINT));
    QCOMPARE(changed->jobs.first().pi1.pos(), QPointF(10, 25));
    QVERIFY(farFrom(changed, QPointF(40, 70)) > 20.0);
    QVERIFY(farFrom(changed, QPointF(160, 70)) < 1e-6);
    QCOMPARE(changed->jobs.last().pi2.pos(), stroke->jobs.last().pi2.pos());

    // nothing to do
    QCOMPARE(Edit::redrawn(stroke, Edit::Hit(), to, arc), stroke);
    QCOMPARE(Edit::redrawn(stroke, from, to, QPolygonF({QPointF(60, 70)})), stroke);
    QCOMPARE(Edit::redrawn(stroke, from, from, arc), stroke);

    // --- on the layer: ink on the arc, none where the line was, one undo
    // step
    KUndo2Command *command = KisBrushStrokeShapeEdit::replaceStrokes(
        d.layer,
        {strokeShapes(d.layer).first()},
        [&](const KisBrushStrokeShape *edited) {
            const KisRecordedBrushStrokeSP on = edited->brushStroke();
            return Edit::redrawn(on, hitAt(on, QPointF(60, 70)), hitAt(on, QPointF(140, 70)), arc);
        },
        kundo2_noi18n("redraw"));
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage redrawnImage = toImage(d.layer->projection());
    QVERIFY(qAlpha(drawn.pixel(100, 70)) > 200);
    QVERIFY(qAlpha(drawn.pixel(100, 40)) == 0);
    QVERIFY(qAlpha(redrawnImage.pixel(100, 40)) > 200);
    QVERIFY(qAlpha(redrawnImage.pixel(100, 70)) == 0);
    QVERIFY(qAlpha(redrawnImage.pixel(40, 70)) > 200);
    QVERIFY(qAlpha(redrawnImage.pixel(160, 70)) > 200);
    d.document->undoStack()->undo();
    repaint(d.image, d.layer);
    QCOMPARE(toImage(d.layer->projection()), drawn);
    d.document->undoStack()->redo();
    repaint(d.image, d.layer);
    QCOMPARE(toImage(d.layer->projection()), redrawnImage);
}

/// Two or more lines become one, connected at their nearest ends
void KisVectorBrushStrokeTest::testJoin()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisPaintOpPresetSP ink = loadPreset(QStringLiteral("d)_Ink-3_Gpen.kpp"));
    using Edit = KisBrushStrokePathEdit;
    using Job = KisRecordedBrushStroke::Job;
    Document d;
    paintLine(d.image, d.layer, ink, KoColor(Qt::black, cs), QPointF(30, 90), QPointF(100, 90));
    paintLine(d.image, d.layer, ink, KoColor(Qt::red, cs), QPointF(120, 90), QPointF(170, 90));
    repaint(d.image, d.layer);
    const QImage drawn = toImage(d.layer->projection());
    QCOMPARE(strokeShapes(d.layer).size(), 2);
    const KisRecordedBrushStrokeSP model = strokeShapes(d.layer).first()->brushStroke();

    /// a line of ten parts from @p from to @p to, the pressure and the
    /// time rising along it
    auto line = [&](const QPointF &from, const QPointF &to, qreal startPressure = 0.2, qreal endPressure = 1.0) {
        QSharedPointer<KisRecordedBrushStroke> stroke(new KisRecordedBrushStroke(*model));
        stroke->jobs.clear();
        for (int i = 0; i < 10; i++) {
            Job job;
            job.type = FreehandStrokeStrategy::Data::LINE;
            const qreal t1 = i / 10.0;
            const qreal t2 = (i + 1) / 10.0;
            job.pi1 = KisPaintInformation(from + (to - from) * t1, startPressure + (endPressure - startPressure) * t1);
            job.pi2 = KisPaintInformation(from + (to - from) * t2, startPressure + (endPressure - startPressure) * t2);
            job.pi1.setCurrentTime(100 + i * 5);
            job.pi2.setCurrentTime(100 + (i + 1) * 5);
            stroke->jobs << job;
        }
        if (!stroke->starts.isEmpty()) {
            stroke->starts[0].hasLastDab = true;
            stroke->starts[0].dabSeqNo = 3;
        }
        return KisRecordedBrushStrokeSP(stroke);
    };
    auto part = [](KisRecordedBrushStrokeSP stroke, const QTransform &toFirst = QTransform()) {
        return Edit::JoinPart{stroke, toFirst};
    };
    /// one path without gaps, the time never running back
    auto sound = [](KisRecordedBrushStrokeSP stroke) {
        qreal time = -1.0;
        for (int i = 0; i < stroke->jobs.size(); i++) {
            const Job &job = stroke->jobs[i];
            if (job.pi1.currentTime() < time || job.pi2.currentTime() < job.pi1.currentTime()) {
                return false;
            }
            time = job.pi2.currentTime();
            if (i > 0 && job.type != int(FreehandStrokeStrategy::Data::POINT)
                && stroke->jobs[i - 1].type != int(FreehandStrokeStrategy::Data::POINT)
                && job.pi1.pos() != stroke->jobs[i - 1].pi2.pos()) {
                return false;
            }
        }
        return true;
    };

    // --- end to start: the lines as they are, and a straight connection
    const KisRecordedBrushStrokeSP a = line(QPointF(20, 20), QPointF(80, 20));
    const KisRecordedBrushStrokeSP b = line(QPointF(100, 20), QPointF(160, 20));
    QVERIFY(Edit::canJoin(a));
    KisRecordedBrushStrokeSP joined = Edit::joined({part(a), part(b)});
    QVERIFY(joined);
    QCOMPARE(joined->jobs.size(), 21);
    QCOMPARE(a->jobs.size(), 10);
    QCOMPARE(joined->jobs.first().pi1.pos(), QPointF(20, 20));
    QCOMPARE(joined->jobs.last().pi2.pos(), QPointF(160, 20));
    QCOMPARE(joined->jobs[10].pi1.pos(), QPointF(80, 20));
    QCOMPARE(joined->jobs[10].pi2.pos(), QPointF(100, 20));
    QCOMPARE(joined->jobs[10].pi1.pressure(), 1.0);
    QCOMPARE(joined->jobs[10].pi2.pressure(), 0.2);
    QVERIFY(sound(joined));
    // the first line's brush and start
    QCOMPARE(joined->preset, a->preset);
    QVERIFY(joined->starts.isEmpty() || joined->starts[0].hasLastDab);
    QCOMPARE(Edit::pointCount(joined), 22);

    // --- end to end: the second line is turned around
    const KisRecordedBrushStrokeSP backwards = line(QPointF(160, 20), QPointF(100, 20));
    joined = Edit::joined({part(a), part(backwards)});
    QCOMPARE(joined->jobs.size(), 21);
    QCOMPARE(joined->jobs.first().pi1.pos(), QPointF(20, 20));
    QCOMPARE(joined->jobs.last().pi2.pos(), QPointF(160, 20));
    QCOMPARE(joined->jobs[11].pi1.pos(), QPointF(100, 20));
    // its pen values go with its points: full pressure at (100, 20)
    QCOMPARE(joined->jobs[11].pi1.pressure(), 1.0);
    QCOMPARE(joined->jobs.last().pi2.pressure(), 0.2);
    QVERIFY(sound(joined));

    // --- start to start: the second turned around and put first; the
    // first line keeps its direction, the spacing starts anew
    const KisRecordedBrushStrokeSP leftwards = line(QPointF(80, 20), QPointF(20, 20));
    joined = Edit::joined({part(leftwards), part(b)});
    QCOMPARE(joined->jobs.size(), 21);
    QCOMPARE(joined->jobs.first().pi1.pos(), QPointF(160, 20));
    QCOMPARE(joined->jobs.last().pi2.pos(), QPointF(20, 20));
    QVERIFY(sound(joined));
    QVERIFY(joined->starts.isEmpty() || !joined->starts[0].hasLastDab);

    // --- the other's end to the first's start: the other comes first
    joined = Edit::joined({part(b), part(a)});
    QCOMPARE(joined->jobs.first().pi1.pos(), QPointF(20, 20));
    QCOMPARE(joined->jobs.last().pi2.pos(), QPointF(160, 20));
    QCOMPARE(joined->preset, b->preset);
    QVERIFY(sound(joined));

    // --- ends that touch: no connection needed
    const KisRecordedBrushStrokeSP touching = line(QPointF(80, 20), QPointF(140, 60));
    joined = Edit::joined({part(a), part(touching)});
    QCOMPARE(joined->jobs.size(), 20);
    QVERIFY(sound(joined));

    // --- three lines, given out of order
    const KisRecordedBrushStrokeSP c = line(QPointF(180, 20), QPointF(240, 20));
    joined = Edit::joined({part(a), part(c), part(b)});
    QCOMPARE(joined->jobs.size(), 32);
    QCOMPARE(joined->jobs.first().pi1.pos(), QPointF(20, 20));
    QCOMPARE(joined->jobs.last().pi2.pos(), QPointF(240, 20));
    QVERIFY(sound(joined));
    for (int i = 1; i < joined->jobs.size(); i++) {
        QVERIFY(joined->jobs[i].pi2.pos().x() > joined->jobs[i - 1].pi2.pos().x());
    }

    // --- a line recorded in other pixels: twice as large and shifted
    QSharedPointer<KisRecordedBrushStroke> curved(new KisRecordedBrushStroke(*model));
    curved->jobs.clear();
    {
        Job job;
        job.type = FreehandStrokeStrategy::Data::CURVE;
        job.pi1 = KisPaintInformation(QPointF(0, 0), 0.5);
        job.pi2 = KisPaintInformation(QPointF(30, 0), 0.5);
        job.control1 = QPointF(10, 10);
        job.control2 = QPointF(20, -10);
        curved->jobs << job;
    }
    QTransform toFirst;
    toFirst.translate(100, 20);
    toFirst.scale(2, 2);
    joined = Edit::joined({part(a), part(curved, toFirst)});
    QCOMPARE(joined->jobs.size(), 12);
    QCOMPARE(joined->jobs.last().pi1.pos(), QPointF(100, 20));
    QCOMPARE(joined->jobs.last().pi2.pos(), QPointF(160, 20));
    QCOMPARE(joined->jobs.last().control1, QPointF(120, 40));
    QCOMPARE(joined->jobs.last().control2, QPointF(140, 0));
    QCOMPARE(curved->jobs.first().pi2.pos(), QPointF(30, 0));

    // --- the first line's starting dab stays while it starts the result;
    // other dabs go
    QSharedPointer<KisRecordedBrushStroke> withDab(new KisRecordedBrushStroke(*a));
    {
        Job dab;
        dab.type = FreehandStrokeStrategy::Data::POINT;
        dab.pi1 = a->jobs.first().pi1;
        withDab->jobs.prepend(dab);
    }
    QSharedPointer<KisRecordedBrushStroke> otherWithDab(new KisRecordedBrushStroke(*b));
    otherWithDab->jobs.prepend(withDab->jobs.first());
    joined = Edit::joined({part(withDab), part(otherWithDab)});
    QCOMPARE(joined->jobs.size(), 22);
    QVERIFY(joined->jobs.first().type == int(FreehandStrokeStrategy::Data::POINT));
    QVERIFY(joined->jobs[12].type == int(FreehandStrokeStrategy::Data::LINE));
    joined = Edit::joined({part(otherWithDab), part(withDab)});
    QCOMPARE(joined->jobs.size(), 21);
    QVERIFY(joined->jobs.first().type == int(FreehandStrokeStrategy::Data::LINE));

    // --- what cannot be joined
    QSharedPointer<KisRecordedBrushStroke> mirrored(new KisRecordedBrushStroke(*a));
    mirrored->jobs.last().strokeInfoId = 1;
    QVERIFY(!Edit::canJoin(mirrored));
    QSharedPointer<KisRecordedBrushStroke> dot(new KisRecordedBrushStroke(*a));
    dot->jobs = {withDab->jobs.first()};
    QVERIFY(!Edit::canJoin(dot));
    QVERIFY(!Edit::canJoin(KisRecordedBrushStrokeSP()));
    QVERIFY(!Edit::joined({part(a)}));
    QVERIFY(!Edit::joined({part(a), part(mirrored)}));
    QVERIFY(!Edit::joined({part(mirrored), part(a), part(b)}));
    // left out, the rest joined
    joined = Edit::joined({part(a), part(dot), part(b)});
    QCOMPARE(joined->jobs.size(), 21);

    // --- on the layer: the black and the red line become one black line,
    // ink where the gap was; one undo step
    QVERIFY(qAlpha(drawn.pixel(110, 90)) < 20);
    QVERIFY(qRed(drawn.pixel(150, 90)) > 150);
    QList<KisBrushStrokeShape *> shapes = strokeShapes(d.layer);
    KisBrushStrokeShape *joinedShape = nullptr;
    QVERIFY(!KisBrushStrokeShapeEdit::joinStrokes(d.layer, {shapes.first()}, kundo2_noi18n("join"), &joinedShape));
    QVERIFY(!joinedShape);
    KUndo2Command *command = KisBrushStrokeShapeEdit::joinStrokes(d.layer, shapes, kundo2_noi18n("join"), &joinedShape);
    QVERIFY(command);
    QVERIFY(joinedShape);
    d.document->undoStack()->push(command);
    QCOMPARE(strokeShapes(d.layer).size(), 1);
    QCOMPARE(strokeShapes(d.layer).first(), joinedShape);
    QCOMPARE(joinedShape->brushStroke()->jobs.size(), 20 + 1 + 20 + 1);
    repaint(d.image, d.layer);
    const QImage one = toImage(d.layer->projection());
    QVERIFY(qAlpha(one.pixel(110, 90)) > 200);
    QVERIFY(qAlpha(one.pixel(60, 90)) > 200);
    QVERIFY(qAlpha(one.pixel(150, 90)) > 200);
    QVERIFY(qRed(one.pixel(150, 90)) < 60);
    QVERIFY(qAlpha(one.pixel(20, 90)) < 20);
    QVERIFY(qAlpha(one.pixel(180, 90)) < 20);
    undoStep(d);
    repaint(d.image, d.layer);
    QCOMPARE(strokeShapes(d.layer).size(), 2);
    QVERIFY(maxDifference(toImage(d.layer->projection()), drawn) <= 2);
    redoStep(d);
    repaint(d.image, d.layer);
    QCOMPARE(strokeShapes(d.layer).size(), 1);
    QVERIFY(maxDifference(toImage(d.layer->projection()), one) <= 2);
    undoStep(d);
    repaint(d.image, d.layer);

    // a line that was moved is joined where it lies now; the result is the
    // second line's when that one is given first
    shapes = strokeShapes(d.layer);
    QCOMPARE(shapes.size(), 2);
    shapes.last()->setTransformation(shapes.last()->transformation() * QTransform::fromTranslate(0, -30));
    repaint(d.image, d.layer);
    command = KisBrushStrokeShapeEdit::joinStrokes(d.layer,
                                                   {shapes.last(), shapes.first()},
                                                   kundo2_noi18n("join"),
                                                   &joinedShape);
    QVERIFY(command);
    d.document->undoStack()->push(command);
    repaint(d.image, d.layer);
    const QImage moved = toImage(d.layer->projection());
    // the red line at its new place, the black one red now, and the
    // connection between (100, 90) and (120, 60)
    QVERIFY(qRed(moved.pixel(150, 60)) > 150);
    QVERIFY(qAlpha(moved.pixel(150, 90)) < 20);
    QVERIFY(qRed(moved.pixel(60, 90)) > 150);
    QVERIFY(qAlpha(moved.pixel(110, 75)) > 200);
    QCOMPARE(strokeShapes(d.layer).size(), 1);
}

SOLSTICE_BRUSH_TEST_MAIN_WITH_BUNDLES(KisVectorBrushStrokeTest, QStringLiteral("Krita_4_Default_Resources.bundle"))

#include "KisVectorBrushStrokeTest.moc"
