/* SPDX-FileCopyrightText: 2017 Scott Petrovic <scottpetrovic@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "KisBrushStrokePreviewRenderer.h"
#include <KisAsynchronousStrokeUpdateHelper.h>
#include <KisGlobalResourcesInterface.h>
#include <KoCanvasResourceProvider.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOpRegistry.h>
#include <kis_brush.h>
#include <kis_canvas_resource_provider.h>
#include <kis_image.h>
#include <kis_paint_layer.h>
#include <kis_paintop_preset.h>
#include <kis_paintop_settings.h>
#include <kis_transaction.h>
#include <resources/KoPattern.h>
#include <resources/KoStopGradient.h>
#include <strokes/KisFreehandStrokeInfo.h>
#include <strokes/freehand_stroke.h>

bool KisBrushStrokePreviewRenderer::supported(const QString &id)
{
    return id != "roundmarker" && id != "experimentbrush" && id != "duplicate";
}
bool KisBrushStrokePreviewRenderer::striped(const QString &id)
{
    return id == "colorsmudge" || id == "deformbrush" || id == "filter";
}
void KisBrushStrokePreviewRenderer::paintBackground(KisPaintDeviceSP device,
                                                    const QRect &bounds,
                                                    const QColor &background,
                                                    bool stripes)
{
    KisTransaction transaction(device);
    if (stripes) {
        // Preserve the editor's overlapping stripe rectangles exactly.
        for (int i = 0; i < 20; ++i) {
            const float section = 1.0f / 20;
            const QRect rect(bounds.width() * section * i,
                             0,
                             bounds.width() * (section * i + section),
                             bounds.height());
            device->fill(rect, KoColor(i % 2 ? QColor(80, 80, 80) : QColor(140, 140, 140), device->colorSpace()));
        }
    } else {
        device->fill(bounds, KoColor(background, device->colorSpace()));
    }
    transaction.end();
}

KisStrokeId KisBrushStrokePreviewRenderer::enqueue(KisImageSP image,
                                                   KisLayerSP layer,
                                                   KisPaintOpPresetSP preset,
                                                   KoCanvasResourceProvider *provider,
                                                   const KoColor &color,
                                                   const QSize &size,
                                                   KisPaintInformation &first,
                                                   KisPaintInformation &last,
                                                   qreal scale,
                                                   std::optional<int> seed)
{
    const QPointF center(size.width() * 0.5, size.height() * 0.5);
    // limit the brush stroke size. larger brush strokes just don't look good and are CPU intensive
    // we are making a proxy preset and setting it to the painter...otherwise setting the brush size of the original
    // preset will fire off signals that make this run in an infinite loop
    qreal previewSize = qBound(3.0, preset->settings()->paintOpSize(), 25.0); // constrain live preview brush size
    // Except for the sketchbrush where it determines the history.
    if (preset->paintOp().id() == "sketchbrush" || preset->paintOp().id() == "spraybrush") {
        previewSize = qMax(3.0, preset->settings()->paintOpSize());
    }

    KisPaintOpPresetSP proxy_preset = preset->clone().dynamicCast<KisPaintOpPreset>();
    KisPaintOpSettingsSP settings = proxy_preset->settings();
    if (seed)
        settings->setProperty("Solstice/StrokePreviewSeed", *seed);
    settings->setPaintOpSize(previewSize * scale);

    int maxTextureSize = qRound(200 * scale);
    int textureOffsetX = settings->getInt("Texture/Pattern/MaximumOffsetX") * 2;
    int textureOffsetY = settings->getInt("Texture/Pattern/MaximumOffsetY") * 2;
    double textureScale = settings->getDouble("Texture/Pattern/Scale");
    if (textureOffsetX * textureScale > maxTextureSize || textureOffsetY * textureScale > maxTextureSize) {
        int maxSize = qMax(textureOffsetX, textureOffsetY);
        double result = qreal(maxTextureSize) / maxSize;
        settings->setProperty("Texture/Pattern/Scale", result);
    }
    if (proxy_preset->paintOp().id() == "spraybrush") {
        QDomElement element;
        QDomDocument d;
        QString brushDefinition = settings->getString("brush_definition");
        if (!brushDefinition.isEmpty()) {
            d.setContent(brushDefinition);
            element = d.firstChildElement("Brush");

            KisBrushSP brush = KisBrush::fromXML(element, KisGlobalResourcesInterface::instance());

            if (!brush)
                return {};
            qreal width = brush->image().width();
            qreal brushScale = brush->scale();
            qreal diameterToBrushRatio = 1.0;
            qreal diameter = settings->getInt("Spray/diameter");
            // hack, 1000 being the maximum possible brushsize.
            if (brush->filename().endsWith(".svg", Qt::CaseInsensitive)) {
                diameterToBrushRatio = diameter / (1000.0 * brushScale);
                brushScale = (25.0 * scale) / 1000.0;
            } else {
                if (width * brushScale > (25.0 * scale)) {
                    diameterToBrushRatio = diameter / (width * brushScale);
                    brushScale = (25.0 * scale) / width;

                    if (!settings->getBool("SprayShape/proportional")) {
                        settings->setProperty("SprayShape/width",
                                              qRound(brushScale * settings->getInt("SprayShape/width")));
                        settings->setProperty("SprayShape/height",
                                              qRound(brushScale * settings->getInt("SprayShape/height")));
                    }
                }
            }
            settings->setProperty("Spray/diameter", int((25.0 * scale) * diameterToBrushRatio));

            brush->setScale(brushScale);
            d.clear();
            element = d.createElement("Brush");
            brush->toXML(d, element);
            d.appendChild(element);
            settings->setProperty("brush_definition", d.toString());
        }
    }

    proxy_preset->setSettings(settings);
    if (seed) {
        // Canvas-dependent inputs come from the adjusted saved proxy, never
        // from the user's active brush or toolbar state.
        provider->setResource(KoCanvasResource::CurrentPaintOpPreset, QVariant::fromValue(proxy_preset));
        provider->setResource(KoCanvasResource::CurrentCompositeOp, settings->paintOpCompositeOp());
        provider->setResource(KoCanvasResource::CurrentEffectiveCompositeOp, settings->effectivePaintOpCompositeOp());
        provider->setResource(KoCanvasResource::EraserMode, settings->eraserMode());
        provider->setResource(KoCanvasResource::Opacity, settings->paintOpOpacity());
        provider->setResource(KoCanvasResource::Flow, settings->paintOpFlow());
        provider->setResource(KoCanvasResource::Size, settings->paintOpSize());
        provider->setResource(KoCanvasResource::BrushRotation, settings->paintOpAngle());
        provider->setResource(KoCanvasResource::PatternSize, settings->paintOpPatternSize());
    }

    KisResourcesSnapshotSP resources = new KisResourcesSnapshot(image, layer, provider, 0, {}, proxy_preset);
    resources->setOpacity(settings->paintOpOpacity());
    resources->setMirroring(false, false); // ignore mirroring in toolbar

    resources->setFGColorOverride(color);
    KisFreehandStrokeInfo *strokeInfo = new KisFreehandStrokeInfo();

    FreehandStrokeStrategy *stroke = new FreehandStrokeStrategy(resources, strokeInfo, kundo2_noi18n("temp_stroke"));

    if (seed)
        stroke->setPreviewRandomSeed(*seed);
    KisStrokeId strokeId = image->startStroke(stroke);

    if (proxy_preset->paintOp().id() == "mypaintbrush") {
        first.setCurrentTime(123);
        last.setCurrentTime(1230);
    }

    // paint the stroke. The sketchbrush gets a different shape than the others to show how it works
    if (proxy_preset->paintOp().id() == "sketchbrush" || proxy_preset->paintOp().id() == "curvebrush"
        || proxy_preset->paintOp().id() == "particlebrush") {
        qreal startX = center.x() - (size.width() * 0.4);
        qreal endX = center.x() + (size.width() * 0.4);
        qreal middle = center.y();
        KisPaintInformation pointOne;
        pointOne.setPressure(0.0);
        pointOne.setPos(QPointF(startX, middle));
        KisPaintInformation pointTwo;
        pointTwo.setPressure(0.0);
        pointTwo.setPos(QPointF(startX, middle));
        int repeats = 8;

        for (int i = 0; i < repeats; i++) {
            pointOne.setPos(pointTwo.pos());
            pointOne.setPressure(pointTwo.pressure());

            pointTwo.setPressure((1.0 / repeats) * (i + 1));
            qreal xPos = ((1.0 / repeats) * (i + 1) * (endX - startX)) + startX;
            pointTwo.setPos(QPointF(xPos, middle));

            qreal offset = (size.height() / (repeats * 1.5)) * (i + 1);
            qreal handleY = middle + offset;
            if (i % 2 == 0) {
                handleY = middle - offset;
            }

            image->addJob(strokeId,
                          new FreehandStrokeStrategy::Data(0,
                                                           pointOne,
                                                           QPointF(pointOne.pos().x(), handleY),
                                                           QPointF(pointTwo.pos().x(), handleY),
                                                           pointTwo));
            image->addJob(strokeId, new KisAsynchronousStrokeUpdateHelper::UpdateData(true));
        }

    } else {
        // paint an S curve
        first.setPos(QPointF(center.x() - (size.width() * 0.45), center.y() + (size.height() * 0.2)));
        first.setPressure(0.0);

        last.setPos(QPointF(center.x() + (size.width() * 0.4), center.y() - (size.height() * 0.2)));

        last.setPressure(1.0);

        image->addJob(strokeId,
                      new FreehandStrokeStrategy::Data(0,
                                                       first,
                                                       QPointF(center.x(), center.y() - size.height()),
                                                       QPointF(center.x(), center.y() + size.height()),
                                                       last));
        image->addJob(strokeId, new KisAsynchronousStrokeUpdateHelper::UpdateData(true));
    }
    image->endStroke(strokeId);

    return strokeId;
}

KisBrushStrokePreviewRenderer::KisBrushStrokePreviewRenderer(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(15);
    connect(&m_poll, &QTimer::timeout, this, &KisBrushStrokePreviewRenderer::poll);
}
KisBrushStrokePreviewRenderer::~KisBrushStrokePreviewRenderer()
{
    // The cache keeps this object alive until cancellation has drained.
    cancel();
}
bool KisBrushStrokePreviewRenderer::isRunning() const
{
    return bool(m_image);
}

void KisBrushStrokePreviewRenderer::start(KisPaintOpPresetSP preset)
{
    KIS_SAFE_ASSERT_RECOVER_RETURN(!isRunning());
    if (!preset || !supported(preset->paintOp().id())) {
        Q_EMIT finished(QImage(), false);
        return;
    }
    m_cancelled = m_timeout = false;
    const QSize size(480, 160);
    const auto *cs = KoColorSpaceRegistry::instance()->rgb8();
    m_image = new KisImage(nullptr, size.width(), size.height(), cs, "Brush stroke preview");
    m_layer = new KisPaintLayer(m_image, "Preview", OPACITY_OPAQUE_U8, cs);
    const bool stripes = striped(preset->paintOp().id());
    paintBackground(m_layer->paintDevice(), m_image->bounds(), Qt::transparent, stripes);

    KoCanvasResourceProvider provider;
    const KoColor fg(QColor("#E8E8E8"), cs), bg(Qt::black, cs);
    provider.setResource(KoCanvasResource::ForegroundColor, QVariant::fromValue(fg));
    provider.setResource(KoCanvasResource::BackgroundColor, QVariant::fromValue(bg));
    provider.setResource(KoCanvasResource::CurrentPaintOpPreset, QVariant::fromValue(preset));
    provider.setResource(KoCanvasResource::Opacity, 1.0);
    provider.setResource(KoCanvasResource::CurrentEffectiveCompositeOp, COMPOSITE_OVER);
    provider.setResource(KoCanvasResource::EffectiveZoom, 1.0);
    provider.setResource(KoCanvasResource::HdrExposure, 0.0);
    provider.setResource(KoCanvasResource::EraserMode, false);
    provider.setResource(KoCanvasResource::GlobalAlphaLock, false);
    provider.setResource(KoCanvasResource::MirrorHorizontal, false);
    provider.setResource(KoCanvasResource::MirrorVertical, false);
    provider.setResource(KoCanvasResource::EffectiveLodAvailability, false);
    QLinearGradient gradient;
    gradient.setColorAt(0, fg.toQColor());
    gradient.setColorAt(1, bg.toQColor());
    KoAbstractGradientSP fixedGradient(KoStopGradient::fromQGradient(&gradient));
    provider.setResource(KoCanvasResource::CurrentGradient, QVariant::fromValue(fixedGradient));
    QImage pattern(2, 2, QImage::Format_RGB32);
    pattern.fill(QColor("#808080"));
    KoPatternSP fixedPattern(new KoPattern(pattern, "Preview pattern", "preview.pat"));
    provider.setResource(KoCanvasResource::CurrentPattern, QVariant::fromValue(fixedPattern));

    KisPaintInformation first, last;
    m_elapsed.start();
    m_stroke = enqueue(m_image,
                       m_layer,
                       preset,
                       &provider,
                       stripes ? KoColor(Qt::white, cs) : fg,
                       size,
                       first,
                       last,
                       size.height() / 60.0,
                       271828);
    m_poll.start();
}
void KisBrushStrokePreviewRenderer::cancel()
{
    if (!m_image || m_cancelled)
        return;
    m_cancelled = true;
    m_image->cancelStroke(m_stroke);
}
void KisBrushStrokePreviewRenderer::poll()
{
    if (!m_image->isIdle() && !m_cancelled && m_elapsed.elapsed() >= 5000) {
        m_timeout = true;
        cancel();
    }
    if (!m_image->isIdle())
        return;
    m_poll.stop();
    QImage result = m_cancelled
        ? QImage()
        : m_layer->paintDevice()->convertToQImage(nullptr, m_image->bounds()).convertToFormat(QImage::Format_ARGB32);
    // Engines may leave arbitrary RGB under zero alpha. Canonicalize invisible
    // pixels for reproducible cache files without changing any visible color.
    for (int y = 0; y < result.height(); ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            if (!qAlpha(pixels[x]))
                pixels[x] = 0;
        }
    }
    const bool interrupted = m_cancelled && !m_timeout;
    m_stroke.clear();
    m_layer.clear();
    m_image.clear();
    Q_EMIT finished(result, interrupted);
}
