/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisBrushStrokeLayer.h"

#include <QMutexLocker>
#include <QtMath>

#include <KisAsynchronousStrokeUpdateHelper.h>
#include <KoCanvasResourceProvider.h>
#include <kis_icon_utils.h>
#include <kis_image.h>
#include <kis_paint_device.h>
#include <kis_paintop_preset.h>
#include <kis_paintop_settings.h>
#include <kis_resources_snapshot.h>
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
