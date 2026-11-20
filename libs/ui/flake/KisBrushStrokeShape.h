/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISBRUSHSTROKESHAPE_H
#define KISBRUSHSTROKESHAPE_H

#include <QImage>
#include <QRectF>
#include <QSharedPointer>
#include <QVector>

#include <KoShape.h>
#include <SvgShape.h>

#include "KisBrushStrokeLayer.h"
#include "kritaui_export.h"

#define KisBrushStrokeShapeId "KisBrushStrokeShape"

/**
 * One brush stroke on a vector layer (Solstice): a shape that keeps the
 * stroke the brush tool drew and draws it again with the same brush
 * whenever its size, rotation or the image resolution changes. See
 * docs/agent/vector-brush-strokes.md.
 *
 * The shape's rectangle is the stroke's pixel bounds when it was drawn
 * (recordRect(), in the pixels the stroke was recorded in). Its data is
 * shared with its clones and never changes after the shape is set up, so
 * the vector layer may paint clones in another thread.
 */
class KRITAUI_EXPORT KisBrushStrokeShape : public KoShape, public SvgShape
{
public:
    KisBrushStrokeShape();
    ~KisBrushStrokeShape() override;

    /**
     * The shape of @p stroke, whose positions are image pixels. @p image is
     * what the stroke painted, with its top left pixel at @p imageOffset;
     * the shape shows it until it has to draw the stroke again. @p xRes and
     * @p yRes are the image's pixels per point. The shape's transformation
     * places it in the document; nullptr for an empty image.
     */
    static KisBrushStrokeShape *
    fromStroke(KisRecordedBrushStrokeSP stroke, const QImage &image, const QPoint &imageOffset, qreal xRes, qreal yRes);

    /// The recorded stroke; null when the file's stroke data could not be
    /// read (the shape then shows its saved picture, scaled)
    KisRecordedBrushStrokeSP brushStroke() const;
    /// The stroke's bounds in the pixels it was recorded in
    QRectF recordRect() const;
    /// The picture the shape currently shows for a painter that maps a
    /// recorded pixel to one device pixel (tests)
    QImage staticImage() const;

    /// Recorded pixels to image pixels, as the shape's layer paints it
    QTransform expectedTransform() const;

    /**
     * A shape for @p piece, a part of this shape's stroke in the same
     * recorded coordinates (what the vector eraser leaves of it). It lies
     * where this one does, with this one's stacking order, and carries its
     * transformation in document coordinates: add it to this shape's
     * layer. nullptr for an empty piece. With @p withinThis the piece's
     * rectangle stays inside this shape's (a part cut from it); without,
     * it follows the piece's path (points that were moved).
     */
    KisBrushStrokeShape *createPiece(KisRecordedBrushStrokeSP piece, bool withinThis = true) const;

    KoShape *cloneShape() const override;
    void paint(QPainter &painter) const override;
    QPainterPath outline() const override;
    QRectF boundingRect() const override;
    void setResolution(qreal xRes, qreal yRes) override;

    bool saveSvg(SvgSavingContext &context) override;
    bool loadSvg(const QDomElement &element, SvgLoadingContext &context) override;

    /// Registers the shape's factory with the shape registry, once
    static void registerFactory();

    /**
     * While one exists in a thread, the shapes saved there write their
     * strokes into it and only an index into the SVG, so that the strokes
     * of a layer share one file with their brushes stored once
     * (KisShapeLayer::saveLayer()). Without one a shape embeds its stroke
     * in the SVG (clipboard, export).
     */
    class KRITAUI_EXPORT SavingScope
    {
    public:
        SavingScope();
        ~SavingScope();
        const QVector<KisRecordedBrushStrokeSP> &strokes() const;

    private:
        Q_DISABLE_COPY(SavingScope)
        friend class KisBrushStrokeShape;
        QVector<KisRecordedBrushStrokeSP> m_strokes;
        SavingScope *m_previous;
    };

    /**
     * While one exists in a thread with @p enabled, a shape painted there
     * that has no picture for the painter's transform shows its latest
     * picture scaled instead of drawing its stroke again: for the steps of
     * a transform in progress. usedPreview() says whether any did; the
     * layer then has to paint those shapes again without a scope
     * (KisShapeLayerCanvas::setBrushStrokePreview()).
     */
    class KRITAUI_EXPORT PreviewPaintScope
    {
    public:
        PreviewPaintScope(bool enabled);
        ~PreviewPaintScope();
        bool usedPreview() const;

    private:
        Q_DISABLE_COPY(PreviewPaintScope)
        friend class KisBrushStrokeShape;
        bool m_enabled;
        bool m_used = false;
        PreviewPaintScope *m_previous;
    };

    /// While one exists in a thread, the shapes loaded there take the
    /// strokes their index names from it
    class KRITAUI_EXPORT LoadingScope
    {
    public:
        LoadingScope(const QVector<KisRecordedBrushStrokeSP> &strokes);
        ~LoadingScope();

    private:
        Q_DISABLE_COPY(LoadingScope)
        friend class KisBrushStrokeShape;
        QVector<KisRecordedBrushStrokeSP> m_strokes;
        LoadingScope *m_previous;
    };

private:
    KisBrushStrokeShape(const KisBrushStrokeShape &rhs);

    /// recorded pixels to the shape's own coordinates (points)
    QTransform recordToShape() const;
    /// how far a redrawn stroke may reach outside the shape's rectangle,
    /// in the shape's coordinates
    qreal paintMargin() const;

    struct Data;
    QSharedPointer<Data> m_d;
    /// the image's pixels per inch (setResolution())
    qreal m_xRes = 72.0;
    qreal m_yRes = 72.0;
};

#endif // KISBRUSHSTROKESHAPE_H
