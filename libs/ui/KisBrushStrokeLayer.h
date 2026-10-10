/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISBRUSHSTROKELAYER_H
#define KISBRUSHSTROKELAYER_H

#include <QMutex>
#include <QPoint>
#include <QSharedPointer>
#include <QTransform>
#include <QVector>

#include <KisRedrawableLayerInterface.h>
#include <KoColor.h>
#include <kis_paint_information.h>
#include <kis_paint_layer.h>
#include <kis_types.h>
#include <kundo2command.h>
#include <resources/KoAbstractGradient.h>
#include <resources/KoPattern.h>

#include "kritaui_export.h"

class QIODevice;

/**
 * One brush stroke as the brush tool drew it on a KisBrushStrokeLayer: the
 * stroke's jobs and the state they were drawn with, enough to draw it again
 * at another resolution. See docs/agent/brush-stroke-layer-plan.md.
 */
struct KRITAUI_EXPORT KisRecordedBrushStroke {
    /// A FreehandStrokeStrategy::Data job of type POINT, LINE or CURVE
    struct Job {
        int type = 0;
        int strokeInfoId = 0;
        KisPaintInformation pi1;
        KisPaintInformation pi2;
        QPointF control1;
        QPointF control2;
    };

    KisPaintOpPresetSP preset;
    KoColor fgColor;
    KoColor bgColor;
    QString compositeOpId;
    qreal opacity = 1.0;
    qreal effectiveZoom = 1.0;
    KoPatternSP pattern;
    KoAbstractGradientSP gradient;
    int seed = 0;
    /// the number of mirrored copies the tool painted (KisFreehandStrokeInfo)
    int strokeInfoCount = 1;
    /// the offset of the layer's paint device when the stroke was drawn
    QPoint deviceOffset;
    QVector<Job> jobs;
};

using KisRecordedBrushStrokeSP = QSharedPointer<const KisRecordedBrushStroke>;

/**
 * A paint layer that keeps the brush strokes drawn on it, so that they can
 * be drawn again at a new resolution (Solstice). Only the brush tool paints
 * on it. See docs/agent/brush-stroke-layer-plan.md.
 */
class KRITAUI_EXPORT KisBrushStrokeLayer : public KisPaintLayer, public KisRedrawableLayerInterface
{
    Q_OBJECT
public:
    KisBrushStrokeLayer(KisImageWSP image, const QString &name, quint8 opacity, const KoColorSpace *colorSpace);
    KisBrushStrokeLayer(const KisBrushStrokeLayer &rhs);
    ~KisBrushStrokeLayer() override;

    KisNodeSP clone() const override;
    QIcon icon() const override;

    QVector<KisRecordedBrushStrokeSP> strokes() const;
    void addStroke(KisRecordedBrushStrokeSP stroke);
    void removeStroke(KisRecordedBrushStrokeSP stroke);
    void setStrokes(const QVector<KisRecordedBrushStrokeSP> &strokes);

    /**
     * Draws the strokes again with @p transform applied (scaling and moving
     * only) into the paint device, replacing what it holds, if @p original
     * (the content before the transform) is just the recorded strokes:
     * drawing the record at the current size must give its pixels.
     * Otherwise (another tool or a filter changed them, a selection clipped
     * a stroke, rotation, mirroring), nullptr: the pixels are transformed as
     * on a paint layer.
     */
    KUndo2Command *createTransformRedrawCommand(const QTransform &transform, KisPaintDeviceSP original) override;

    /// @p strokes with their positions mapped by @p transform from the
    /// device at @p deviceOffset, and their brush sizes scaled; their
    /// recorded offset becomes @p deviceOffset
    static QVector<KisRecordedBrushStrokeSP> transformedStrokes(const QVector<KisRecordedBrushStrokeSP> &strokes,
                                                                const QPoint &deviceOffset,
                                                                const QTransform &transform);

    /**
     * Draws @p strokes again into a new paint device in @p colorSpace, through
     * the brush tool's stroke path with each stroke's seed. The positions are
     * moved by the difference between @p deviceOffset and each stroke's
     * recorded offset, then mapped by @p transform; the brush sizes are
     * scaled by the transform's scale. Blocks until done.
     */
    static KisPaintDeviceSP renderStrokes(const QVector<KisRecordedBrushStrokeSP> &strokes,
                                          const KoColorSpace *colorSpace,
                                          const QRect &imageBounds,
                                          const QPoint &deviceOffset,
                                          const QTransform &transform = QTransform());

    /**
     * Writes @p strokes to @p device in Solstice's brush stroke format (the
     * layer's file in a .kra, KisBrushStrokeLayerIO.cpp). The brush tips
     * are not embedded; patterns and gradients are.
     */
    static bool saveStrokes(const QVector<KisRecordedBrushStrokeSP> &strokes, QIODevice *device);

    /// Reads strokes written by saveStrokes(); false for damaged or newer
    /// data. Strokes whose brush engine is missing are left out.
    static bool loadStrokes(QIODevice *device, QVector<KisRecordedBrushStrokeSP> *strokes);

private:
    mutable QMutex m_mutex;
    QVector<KisRecordedBrushStrokeSP> m_strokes;
};

using KisBrushStrokeLayerSP = KisSharedPtr<KisBrushStrokeLayer>;

/// Adds a recorded stroke to its layer on redo and removes it on undo; part
/// of the stroke's undo command
class KRITAUI_EXPORT KisRecordBrushStrokeCommand : public KUndo2Command
{
public:
    KisRecordBrushStrokeCommand(KisBrushStrokeLayer *layer, KisRecordedBrushStrokeSP stroke);
    void redo() override;
    void undo() override;

private:
    KisBrushStrokeLayerSP m_layer;
    KisRecordedBrushStrokeSP m_stroke;
};

#endif // KISBRUSHSTROKELAYER_H
