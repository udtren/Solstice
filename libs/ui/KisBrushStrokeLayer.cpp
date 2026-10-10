/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisBrushStrokeLayer.h"

#include <QMutexLocker>
#include <QtMath>

#include <KisAsynchronousStrokeUpdateHelper.h>
#include <KisUsageLogger.h>
#include <KoCanvasResourceProvider.h>
#include <kis_command_utils.h>
#include <kis_debug.h>
#include <kis_icon_utils.h>
#include <kis_image.h>
#include <kis_paint_device.h>
#include <kis_painter.h>
#include <kis_paintop_preset.h>
#include <kis_paintop_settings.h>
#include <kis_resources_snapshot.h>
#include <kis_transaction.h>
#include <strokes/KisFreehandStrokeInfo.h>
#include <strokes/freehand_stroke.h>

KisBrushStrokeLayer::KisBrushStrokeLayer(KisImageWSP image,
                                         const QString &name,
                                         quint8 opacity,
                                         const KoColorSpace *colorSpace)
    : KisPaintLayer(image, name, opacity, colorSpace)
{
}

KisBrushStrokeLayer::KisBrushStrokeLayer(const KisBrushStrokeLayer &rhs)
    : KisPaintLayer(rhs)
    , m_strokes(rhs.strokes())
{
}

KisBrushStrokeLayer::~KisBrushStrokeLayer()
{
}

KisNodeSP KisBrushStrokeLayer::clone() const
{
    return KisNodeSP(new KisBrushStrokeLayer(*this));
}

QIcon KisBrushStrokeLayer::icon() const
{
    return KisIconUtils::loadIcon("krita_tool_freehand");
}

QVector<KisRecordedBrushStrokeSP> KisBrushStrokeLayer::strokes() const
{
    QMutexLocker locker(&m_mutex);
    return m_strokes;
}

void KisBrushStrokeLayer::addStroke(KisRecordedBrushStrokeSP stroke)
{
    QMutexLocker locker(&m_mutex);
    m_strokes.append(stroke);
}

void KisBrushStrokeLayer::removeStroke(KisRecordedBrushStrokeSP stroke)
{
    QMutexLocker locker(&m_mutex);
    m_strokes.removeOne(stroke);
}

void KisBrushStrokeLayer::setStrokes(const QVector<KisRecordedBrushStrokeSP> &strokes)
{
    QMutexLocker locker(&m_mutex);
    m_strokes = strokes;
}

namespace
{
/// The scale a transform applies to lengths (the square root of its area
/// scale)
qreal lengthScale(const QTransform &transform)
{
    return qSqrt(qAbs(transform.determinant()));
}

void drawStroke(KisImageSP image,
                KisPaintLayerSP layer,
                const KisRecordedBrushStroke &stroke,
                const QPointF &offset,
                const QTransform &transform)
{
    const qreal scale = lengthScale(transform);
    auto map = [&](const QPointF &point) {
        return transform.map(point + offset);
    };

    KisPaintOpPresetSP preset = stroke.preset->clone().dynamicCast<KisPaintOpPreset>();
    KisPaintOpSettingsSP settings = preset->settings();
    if (!qFuzzyCompare(scale, 1.0)) {
        settings->setPaintOpSize(settings->paintOpSize() * scale);
        if (settings->hasProperty("Texture/Pattern/Scale")) {
            settings->setProperty("Texture/Pattern/Scale", settings->getDouble("Texture/Pattern/Scale") * scale);
        }
    }

    KoCanvasResourceProvider provider;
    provider.setResource(KoCanvasResource::ForegroundColor, QVariant::fromValue(stroke.fgColor));
    provider.setResource(KoCanvasResource::BackgroundColor, QVariant::fromValue(stroke.bgColor));
    provider.setResource(KoCanvasResource::CurrentPaintOpPreset, QVariant::fromValue(preset));
    provider.setResource(KoCanvasResource::Opacity, stroke.opacity);
    provider.setResource(KoCanvasResource::CurrentCompositeOp, stroke.compositeOpId);
    provider.setResource(KoCanvasResource::CurrentEffectiveCompositeOp, stroke.compositeOpId);
    provider.setResource(KoCanvasResource::EffectiveZoom, stroke.effectiveZoom);
    provider.setResource(KoCanvasResource::HdrExposure, 0.0);
    provider.setResource(KoCanvasResource::EraserMode, false);
    provider.setResource(KoCanvasResource::GlobalAlphaLock, false);
    provider.setResource(KoCanvasResource::MirrorHorizontal, false);
    provider.setResource(KoCanvasResource::MirrorVertical, false);
    provider.setResource(KoCanvasResource::EffectiveLodAvailability, false);
    provider.setResource(KoCanvasResource::Size, settings->paintOpSize());
    if (stroke.pattern) {
        provider.setResource(KoCanvasResource::CurrentPattern, QVariant::fromValue(stroke.pattern));
    }
    if (stroke.gradient) {
        provider.setResource(KoCanvasResource::CurrentGradient, QVariant::fromValue(stroke.gradient));
    }

    KisResourcesSnapshotSP resources = new KisResourcesSnapshot(image, layer, &provider, nullptr, {}, preset);
    resources->setOpacity(stroke.opacity);
    resources->setMirroring(false, false);
    resources->setFGColorOverride(stroke.fgColor);
    resources->setBGColorOverride(stroke.bgColor);

    QVector<KisFreehandStrokeInfo *> strokeInfos;
    for (int i = 0; i < qMax(1, stroke.strokeInfoCount); i++) {
        strokeInfos << new KisFreehandStrokeInfo();
    }
    FreehandStrokeStrategy *strategy =
        new FreehandStrokeStrategy(resources, strokeInfos, kundo2_noi18n("redraw brush stroke"));
    strategy->setPreviewRandomSeed(stroke.seed);
    KisStrokeId id = image->startStroke(strategy);

    for (const KisRecordedBrushStroke::Job &job : stroke.jobs) {
        KisPaintInformation pi1 = job.pi1;
        KisPaintInformation pi2 = job.pi2;
        pi1.setPos(map(pi1.pos()));
        pi2.setPos(map(pi2.pos()));
        switch (job.type) {
        case FreehandStrokeStrategy::Data::POINT:
            image->addJob(id, new FreehandStrokeStrategy::Data(job.strokeInfoId, pi1));
            break;
        case FreehandStrokeStrategy::Data::LINE:
            image->addJob(id, new FreehandStrokeStrategy::Data(job.strokeInfoId, pi1, pi2));
            break;
        case FreehandStrokeStrategy::Data::CURVE:
            image->addJob(
                id,
                new FreehandStrokeStrategy::Data(job.strokeInfoId, pi1, map(job.control1), map(job.control2), pi2));
            break;
        default:
            break;
        }
    }
    // the dabs are rendered asynchronously; an update job flushes them, as
    // the brush tool's update timer does
    image->addJob(id, new KisAsynchronousStrokeUpdateHelper::UpdateData(true));
    image->endStroke(id);
    image->waitForDone();
}
} // namespace

KisPaintDeviceSP KisBrushStrokeLayer::renderStrokes(const QVector<KisRecordedBrushStrokeSP> &strokes,
                                                    const KoColorSpace *colorSpace,
                                                    const QRect &imageBounds,
                                                    const QPoint &deviceOffset,
                                                    const QTransform &transform)
{
    const QRect bounds = transform.mapRect(QRectF(imageBounds)).toAlignedRect();
    KisImageSP image = new KisImage(nullptr,
                                    qMax(1, bounds.right() + 1),
                                    qMax(1, bounds.bottom() + 1),
                                    colorSpace,
                                    "brush stroke layer redraw");
    KisPaintLayerSP layer = new KisPaintLayer(image, "redraw", OPACITY_OPAQUE_U8, colorSpace);

    for (const KisRecordedBrushStrokeSP &stroke : strokes) {
        if (stroke && stroke->preset) {
            drawStroke(image, layer, *stroke, QPointF(deviceOffset - stroke->deviceOffset), transform);
        }
    }
    return layer->paintDevice();
}

namespace
{
/// Whether two devices hold the same pixels in @p rect, allowing a
/// difference of 3 in each 8-bit channel; otherwise @p difference describes
/// how they differ. The colors are compared premultiplied by alpha: the
/// color of a transparent pixel does not show, and smudging brushes leave
/// it different from what drawing again gives.
bool sameContent(KisPaintDeviceSP a, KisPaintDeviceSP b, const QRect &rect, QString *difference)
{
    if (rect.isEmpty()) {
        *difference = QStringLiteral("the layer is empty, the redrawn strokes are not");
        return b->exactBounds().isEmpty();
    }
    const QImage first = a->convertToQImage(nullptr, rect).convertToFormat(QImage::Format_ARGB32);
    const QImage second = b->convertToQImage(nullptr, rect).convertToFormat(QImage::Format_ARGB32);
    int differing = 0;
    int largest = 0;
    QRect area;
    for (int y = 0; y < first.height(); y++) {
        const QRgb *lineA = reinterpret_cast<const QRgb *>(first.constScanLine(y));
        const QRgb *lineB = reinterpret_cast<const QRgb *>(second.constScanLine(y));
        for (int x = 0; x < first.width(); x++) {
            const QRgb p = lineA[x];
            const QRgb q = lineB[x];
            const int alphaP = qAlpha(p);
            const int alphaQ = qAlpha(q);
            auto premultiplied = [](int channel, int alpha) {
                return channel * alpha / 255;
            };
            const int channel =
                qMax(qMax(qAbs(alphaP - alphaQ), qAbs(premultiplied(qRed(p), alphaP) - premultiplied(qRed(q), alphaQ))),
                     qMax(qAbs(premultiplied(qGreen(p), alphaP) - premultiplied(qGreen(q), alphaQ)),
                          qAbs(premultiplied(qBlue(p), alphaP) - premultiplied(qBlue(q), alphaQ))));
            if (channel > 3) {
                differing++;
                largest = qMax(largest, channel);
                area |= QRect(rect.x() + x, rect.y() + y, 1, 1);
            }
        }
    }
    *difference = QStringLiteral("%1 of %2 pixels differ, by up to %3, in %4,%5 %6x%7")
                      .arg(differing)
                      .arg(rect.width() * rect.height())
                      .arg(largest)
                      .arg(area.x())
                      .arg(area.y())
                      .arg(area.width())
                      .arg(area.height());
    return differing == 0;
}

/// Replaces the layer's strokes; undo restores the previous ones
class SetStrokesCommand : public KUndo2Command
{
public:
    SetStrokesCommand(KisBrushStrokeLayer *layer,
                      const QVector<KisRecordedBrushStrokeSP> &before,
                      const QVector<KisRecordedBrushStrokeSP> &after)
        : m_layer(layer)
        , m_before(before)
        , m_after(after)
    {
    }

    void redo() override
    {
        m_layer->setStrokes(m_after);
    }

    void undo() override
    {
        m_layer->setStrokes(m_before);
    }

private:
    KisBrushStrokeLayerSP m_layer;
    QVector<KisRecordedBrushStrokeSP> m_before;
    QVector<KisRecordedBrushStrokeSP> m_after;
};
} // namespace

QVector<KisRecordedBrushStrokeSP>
KisBrushStrokeLayer::transformedStrokes(const QVector<KisRecordedBrushStrokeSP> &strokes,
                                        const QPoint &deviceOffset,
                                        const QTransform &transform)
{
    const qreal scale = lengthScale(transform);
    QVector<KisRecordedBrushStrokeSP> result;
    for (const KisRecordedBrushStrokeSP &stroke : strokes) {
        QSharedPointer<KisRecordedBrushStroke> mapped(new KisRecordedBrushStroke(*stroke));
        const QPointF shift(deviceOffset - stroke->deviceOffset);
        auto map = [&](const QPointF &point) {
            return transform.map(point + shift);
        };
        for (KisRecordedBrushStroke::Job &job : mapped->jobs) {
            job.pi1.setPos(map(job.pi1.pos()));
            job.pi2.setPos(map(job.pi2.pos()));
            job.control1 = map(job.control1);
            job.control2 = map(job.control2);
        }
        mapped->deviceOffset = deviceOffset;

        mapped->preset = stroke->preset->clone().dynamicCast<KisPaintOpPreset>();
        KisPaintOpSettingsSP settings = mapped->preset->settings();
        settings->setPaintOpSize(settings->paintOpSize() * scale);
        if (settings->hasProperty("Texture/Pattern/Scale")) {
            settings->setProperty("Texture/Pattern/Scale", settings->getDouble("Texture/Pattern/Scale") * scale);
        }
        result << mapped;
    }
    return result;
}

KUndo2Command *KisBrushStrokeLayer::createTransformRedrawCommand(const QTransform &transform, KisPaintDeviceSP original)
{
    const QVector<KisRecordedBrushStrokeSP> recorded = strokes();
    KisPaintDeviceSP device = paintDevice();
    if (recorded.isEmpty() || !device || !original || device->keyframeChannel()) {
        return nullptr;
    }
    // scaling and moving only: a rotated or mirrored brush tip would not
    // match the transformed pixels
    if (transform.type() > QTransform::TxScale || transform.m11() <= 0.0 || transform.m22() <= 0.0) {
        return nullptr;
    }

    const QPoint offset(original->x(), original->y());
    const QRect bounds = original->exactBounds();
    const KoColorSpace *colorSpace = device->colorSpace();

    // the layer must hold just its recorded strokes
    KisPaintDeviceSP current = renderStrokes(recorded, colorSpace, bounds | QRect(0, 0, 1, 1), offset);
    QString difference;
    if (!sameContent(original, current, bounds, &difference)) {
        const QString message = QStringLiteral(
                                    "Brush stroke layer \"%1\": its pixels are not its %2 recorded strokes (%3); "
                                    "transforming the pixels instead of drawing again")
                                    .arg(name())
                                    .arg(recorded.size())
                                    .arg(difference);
        warnKrita << message;
        KisUsageLogger::log(message);
        return nullptr;
    }

    const QVector<KisRecordedBrushStrokeSP> mapped = transformedStrokes(recorded, offset, transform);
    const QRect mappedBounds = transform.mapRect(QRectF(bounds)).toAlignedRect();
    KisPaintDeviceSP redrawn = renderStrokes(mapped, colorSpace, mappedBounds | QRect(0, 0, 1, 1), offset);

    KisCommandUtils::CompositeCommand *command = new KisCommandUtils::CompositeCommand();
    command->setText(kundo2_i18n("Redraw Brush Strokes"));
    command->addCommand(new SetStrokesCommand(this, recorded, mapped));

    KisTransaction transaction(device);
    device->clear();
    const QRect redrawnRect = redrawn->extent();
    if (!redrawnRect.isEmpty()) {
        KisPainter::copyAreaOptimized(redrawnRect.topLeft(), redrawn, device, redrawnRect);
    }
    command->addCommand(transaction.endAndTake());
    setDirty();

    return command;
}

KisRecordBrushStrokeCommand::KisRecordBrushStrokeCommand(KisBrushStrokeLayer *layer, KisRecordedBrushStrokeSP stroke)
    : m_layer(layer)
    , m_stroke(stroke)
{
}

void KisRecordBrushStrokeCommand::redo()
{
    m_layer->addStroke(m_stroke);
}

void KisRecordBrushStrokeCommand::undo()
{
    m_layer->removeStroke(m_stroke);
}
