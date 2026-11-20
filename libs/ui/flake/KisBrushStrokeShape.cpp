/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisBrushStrokeShape.h"

#include <mutex>
#include <utility>

#include <QBuffer>
#include <QLineF>
#include <QMap>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPair>
#include <QRegularExpression>
#include <QtMath>

#include <klocalizedstring.h>

#include <KisMimeDatabase.h>
#include <KisQPainterStateSaver.h>
#include <KoColorSpaceRegistry.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeRegistry.h>
#include <KoXmlNS.h>
#include <KoXmlWriter.h>
#include <SvgLoadingContext.h>
#include <SvgSavingContext.h>
#include <SvgStyleWriter.h>
#include <SvgUtil.h>
#include <kis_assert.h>
#include <kis_dom_utils.h>
#include <kis_paint_device.h>
#include <kis_paintop_preset.h>
#include <kis_paintop_settings.h>
#include <strokes/freehand_stroke.h>

namespace
{
const char *const RectAttribute = "data-solstice-brush-stroke-rect";
const char *const IndexAttribute = "data-solstice-brush-stroke-index";
const char *const InlineAttribute = "data-solstice-brush-stroke";
const char *const ExactAttribute = "data-solstice-brush-stroke-exact";

/// a redrawn picture larger than this is not made; the old one is scaled
const int MaximumRenderSide = 16384;
/// the largest picture written to a file
const int MaximumSavedSide = 8192;

thread_local KisBrushStrokeShape::SavingScope *s_savingScope = nullptr;
thread_local KisBrushStrokeShape::LoadingScope *s_loadingScope = nullptr;
thread_local KisBrushStrokeShape::PreviewPaintScope *s_previewScope = nullptr;

/// The scale a transform applies to lengths (the square root of its area
/// scale)
qreal lengthScale(const QTransform &transform)
{
    return qSqrt(qAbs(transform.determinant()));
}

/**
 * Whether a picture made for @p render (recorded pixels to its pixels) can
 * be shown under @p full (recorded pixels to device pixels) without
 * resampling: the two differ by a move only. @p shift is that move in
 * whole pixels; a brush stroke is not drawn again for the part of a pixel
 * that is left, so moving a shape keeps its picture.
 */
bool pixelAligned(const QTransform &render, const QTransform &full, const QSize &size, QPoint *shift)
{
    if (!render.isInvertible() || !render.isAffine() || !full.isAffine()) {
        return false;
    }
    const QTransform difference = render.inverted() * full;
    // less than a twentieth of a pixel across the picture
    const qreal epsilon = 0.05 / qMax(1, qMax(size.width(), size.height()));
    if (qAbs(difference.m11() - 1.0) > epsilon || qAbs(difference.m22() - 1.0) > epsilon
        || qAbs(difference.m12()) > epsilon || qAbs(difference.m21()) > epsilon) {
        return false;
    }
    // rounded the same way wherever the painter's own origin lies (the
    // vector layer paints in patches)
    *shift = QPoint(qFloor(difference.dx() + 0.5 + 1e-6), qFloor(difference.dy() + 0.5 + 1e-6));
    return true;
}

/// how many drawings of a stroke are kept (copies of a shape share them)
const int MaximumPictures = 4;

/// The area the stroke's line covers, for picking the shape: the path the
/// brush followed, as wide as the brush. Empty when it cannot be told (the
/// canvas mirror paints copies the jobs do not hold).
QPainterPath strokeOutline(const KisRecordedBrushStroke &stroke, qreal brushSize)
{
    if (stroke.mirrorHorizontal || stroke.mirrorVertical) {
        return QPainterPath();
    }
    const qreal width = qMax(brushSize, 6.0);
    // one line for each copy the tool painted (Multibrush)
    QMap<int, QPainterPath> lines;
    QPainterPath result;
    result.setFillRule(Qt::WindingFill);

    auto continueAt = [](QPainterPath &line, const QPointF &point) {
        if (line.isEmpty() || line.currentPosition() != point) {
            line.moveTo(point);
        }
    };

    for (const KisRecordedBrushStroke::Job &job : stroke.jobs) {
        QPainterPath &line = lines[job.strokeInfoId];
        switch (job.type) {
        case FreehandStrokeStrategy::Data::POINT:
            result.addEllipse(job.pi1.pos(), width / 2, width / 2);
            break;
        case FreehandStrokeStrategy::Data::LINE:
            continueAt(line, job.pi1.pos());
            line.lineTo(job.pi2.pos());
            break;
        case FreehandStrokeStrategy::Data::CURVE:
            continueAt(line, job.pi1.pos());
            line.cubicTo(job.control1, job.control2, job.pi2.pos());
            break;
        default:
            break;
        }
    }

    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    for (const QPainterPath &line : lines) {
        if (!line.isEmpty()) {
            result.addPath(stroker.createStroke(line));
        }
    }
    return result;
}

QString rectToString(const QRectF &rect)
{
    return QStringLiteral("%1 %2 %3 %4")
        .arg(KisDomUtils::toString(rect.x()),
             KisDomUtils::toString(rect.y()),
             KisDomUtils::toString(rect.width()),
             KisDomUtils::toString(rect.height()));
}

bool rectFromString(const QString &string, QRectF *rect)
{
    const QStringList parts = string.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() != 4) {
        return false;
    }
    qreal values[4];
    for (int i = 0; i < 4; i++) {
        bool ok = false;
        values[i] = parts[i].toDouble(&ok);
        if (!ok || !qIsFinite(values[i]) || qAbs(values[i]) > 1e7) {
            return false;
        }
    }
    if (values[2] <= 0.0 || values[3] <= 0.0) {
        return false;
    }
    *rect = QRectF(values[0], values[1], values[2], values[3]);
    return true;
}

class KisBrushStrokeShapeFactory : public KoShapeFactoryBase
{
public:
    KisBrushStrokeShapeFactory()
        : KoShapeFactoryBase(KisBrushStrokeShapeId, i18n("Brush Stroke"))
    {
        // tried before the image shape, which reads the element when it is
        // not one of ours (loadSvg() says so)
        setXmlElementNames(KoXmlNS::svg, QStringList("image"));
        setLoadingPriority(2);
        setHidden(true);
    }

    KoShape *createDefaultShape(KoDocumentResourceManager * /*documentResources*/ = 0) const override
    {
        KisBrushStrokeShape *shape = new KisBrushStrokeShape();
        shape->setShapeId(KisBrushStrokeShapeId);
        return shape;
    }

    bool supports(const QDomElement & /*e*/, KoShapeLoadingContext & /*context*/) const override
    {
        return false;
    }

    QList<KoShapeConfigWidgetBase *> createShapeOptionPanels() override
    {
        return QList<KoShapeConfigWidgetBase *>();
    }
};
} // namespace

namespace
{
bool hasDensity(const KisRecordedBrushStroke &stroke)
{
    for (const KisRecordedBrushStroke::Job &job : stroke.jobs) {
        if (job.pi1.densityFactor() < 1.0 || job.pi2.densityFactor() < 1.0) {
            return true;
        }
    }
    return false;
}

/**
 * Multiplies the picture of @p stroke by the density of its path
 * (KisPaintInformation::densityFactor()): every pixel within @p radius of
 * the path takes the density of the nearest place on it. It works on the
 * finished picture, so it is the same for every brush engine and for
 * Build-up and Wash strokes alike; where dabs overlap, a factor for each
 * dab would add up again.
 *
 * Where two parts of the path that are far from each other along it (a
 * line crossing itself, mirrored copies) both lie within @p halfBrush of a
 * pixel, the denser part wins: a light line that crosses a dense one does
 * not take ink out of it.
 *
 * @p toPicture maps recorded pixels to the picture's, @p offset is the
 * place of @p image in them.
 */
void applyDensity(const KisRecordedBrushStroke &stroke,
                  const QTransform &toPicture,
                  qreal halfBrush,
                  qreal radius,
                  QImage *image,
                  const QPoint &offset)
{
    struct Segment {
        QPointF a;
        QPointF b;
        qreal densityA = 1.0;
        qreal densityB = 1.0;
        /// the distance along the path at which it starts
        qreal along = 0.0;
    };
    QVector<Segment> segments;
    // short pieces: a piece is tested against every pixel of its bounding
    // box, which for a long slanted one is far more than its surroundings
    const qreal pieceLength = qMax(4.0, radius);
    // places further apart than this along the path are different parts
    const qreal samePart = 4.0 * halfBrush + 4.0;

    // the canvas mirror paints copies of every dab around its axes
    QVector<QPair<bool, bool>> mirrors{{false, false}};
    if (stroke.mirrorHorizontal) {
        mirrors << QPair<bool, bool>(true, false);
    }
    if (stroke.mirrorVertical) {
        mirrors << QPair<bool, bool>(false, true);
    }
    if (stroke.mirrorHorizontal && stroke.mirrorVertical) {
        mirrors << QPair<bool, bool>(true, true);
    }
    auto place = [&](const QPointF &point, const QPair<bool, bool> &mirror) {
        const QPointF mirrored(mirror.first ? 2 * stroke.mirrorAxesCenter.x() - point.x() : point.x(),
                               mirror.second ? 2 * stroke.mirrorAxesCenter.y() - point.y() : point.y());
        return toPicture.map(mirrored) - QPointF(offset);
    };

    // every copy of every line (Multibrush, canvas mirror) is a path of
    // its own: its distances start far from the others'
    QMap<QPair<int, int>, qreal> lengths;
    for (const KisRecordedBrushStroke::Job &job : stroke.jobs) {
        const qreal densityA = job.pi1.densityFactor();
        const qreal densityB = job.pi2.densityFactor();
        for (int mirror = 0; mirror < mirrors.size(); mirror++) {
            const QPair<int, int> line(job.strokeInfoId, mirror);
            if (!lengths.contains(line)) {
                lengths.insert(line, lengths.size() * 1e9);
            }
            qreal &along = lengths[line];

            const QPointF start = place(job.pi1.pos(), mirrors[mirror]);
            if (job.type == FreehandStrokeStrategy::Data::POINT) {
                segments << Segment{start, start, densityA, densityA, along};
                continue;
            }
            const QPointF end = place(job.pi2.pos(), mirrors[mirror]);
            const bool curve = job.type == FreehandStrokeStrategy::Data::CURVE;
            const QPointF control1 = curve ? place(job.control1, mirrors[mirror]) : start;
            const QPointF control2 = curve ? place(job.control2, mirrors[mirror]) : end;
            const qreal length = curve ? QLineF(start, control1).length() + QLineF(control1, control2).length()
                    + QLineF(control2, end).length()
                                       : QLineF(start, end).length();
            const int pieces = qBound(curve ? 4 : 1, qCeil(length / pieceLength), 1024);
            QPointF previous = start;
            for (int i = 1; i <= pieces; i++) {
                const qreal t = qreal(i) / pieces;
                const qreal u = 1.0 - t;
                const QPointF next = curve
                    ? u * u * u * start + 3 * u * u * t * control1 + 3 * u * t * t * control2 + t * t * t * end
                    : start + (end - start) * t;
                // a curve changes the pen's values evenly with its parameter
                const qreal before = densityA + (densityB - densityA) * (qreal(i - 1) / pieces);
                const qreal after = densityA + (densityB - densityA) * t;
                segments << Segment{previous, next, before, after, along};
                along += QLineF(previous, next).length();
                previous = next;
            }
        }
    }

    // only around the lighter parts; the others still compete for the
    // pixels there
    QRectF lighter;
    for (const Segment &segment : std::as_const(segments)) {
        if (segment.densityA < 1.0 || segment.densityB < 1.0) {
            lighter |= QRectF(segment.a, segment.b).normalized().adjusted(-radius, -radius, radius, radius);
        }
    }
    const QRect area = lighter.toAlignedRect() & image->rect();
    if (area.isEmpty()) {
        return;
    }
    const int width = area.width();
    const float outside = float(radius * radius);
    const float inBrush = float(halfBrush * halfBrush);
    QVector<float> nearest(width * area.height(), outside);
    QVector<float> density(width * area.height(), 1.0f);
    QVector<double> position(width * area.height(), 0.0);

    for (const Segment &segment : std::as_const(segments)) {
        const QRect box =
            QRectF(segment.a, segment.b).normalized().adjusted(-radius, -radius, radius, radius).toAlignedRect() & area;
        if (box.isEmpty()) {
            continue;
        }
        const QPointF direction = segment.b - segment.a;
        const qreal lengthSquared = QPointF::dotProduct(direction, direction);
        const qreal length = qSqrt(lengthSquared);
        for (int y = box.top(); y <= box.bottom(); y++) {
            const int row = (y - area.top()) * width - area.left();
            for (int x = box.left(); x <= box.right(); x++) {
                // the pixel's center
                const QPointF point(x + 0.5, y + 0.5);
                const qreal t = lengthSquared > 0.0
                    ? qBound(0.0, QPointF::dotProduct(point - segment.a, direction) / lengthSquared, 1.0)
                    : 0.0;
                const QPointF away = point - (segment.a + direction * t);
                const float far = float(QPointF::dotProduct(away, away));
                const int index = row + x;
                if (far >= outside) {
                    continue;
                }
                const float value = float(segment.densityA + (segment.densityB - segment.densityA) * t);
                const double along = segment.along + length * t;
                bool take = false;
                if (nearest[index] >= outside) {
                    take = true;
                } else if (qAbs(along - position[index]) > samePart && far <= inBrush && nearest[index] <= inBrush) {
                    // two parts of the path under the brush: the denser
                    take = value > density[index] || (value == density[index] && far < nearest[index]);
                } else {
                    take = far < nearest[index];
                }
                if (take) {
                    nearest[index] = far;
                    density[index] = value;
                    position[index] = along;
                }
            }
        }
    }

    KIS_SAFE_ASSERT_RECOVER_RETURN(image->format() == QImage::Format_ARGB32_Premultiplied);
    for (int y = area.top(); y <= area.bottom(); y++) {
        QRgb *line = reinterpret_cast<QRgb *>(image->scanLine(y));
        const int row = (y - area.top()) * width - area.left();
        for (int x = area.left(); x <= area.right(); x++) {
            const float factor = density[row + x];
            if (factor < 1.0f && line[x]) {
                const QRgb pixel = line[x];
                line[x] = qRgba(qRound(qRed(pixel) * factor),
                                qRound(qGreen(pixel) * factor),
                                qRound(qBlue(pixel) * factor),
                                qRound(qAlpha(pixel) * factor));
            }
        }
    }
}
} // namespace

struct KisBrushStrokeShape::Data {
    KisRecordedBrushStrokeSP stroke;
    QRectF recordRect;
    /// strokeOutline(), in recorded pixels
    QPainterPath recordOutline;
    /// the brush's diameter in recorded pixels
    qreal brushSize = 1.0;

    /// The stroke as drawn for @p renderTransform (recorded pixels to the
    /// picture's pixels), with its top left pixel at @p offset
    struct Picture {
        QImage image;
        QPoint offset;
        QTransform renderTransform;
        /// the brush's own drawing for its transform, not a resampled copy
        /// read from a file
        bool exact = true;
    };

    /// guards the pictures
    QMutex mutex;
    /// the last drawings, the latest first; a shape and its copies at other
    /// sizes or angles each find their own
    QList<Picture> pictures;

    /// The index of the picture that shows the stroke under @p full
    /// without resampling, moved by @p shift; -1 if none does
    int pictureFor(const QTransform &full, QPoint *shift) const
    {
        for (int i = 0; i < pictures.size(); i++) {
            if (pixelAligned(pictures[i].renderTransform, full, pictures[i].image.size(), shift)) {
                return i;
            }
        }
        return -1;
    }

    void addPicture(const Picture &picture)
    {
        pictures.prepend(picture);
        while (pictures.size() > MaximumPictures) {
            pictures.removeLast();
        }
    }

    void setStroke(KisRecordedBrushStrokeSP value)
    {
        stroke = value;
        if (stroke && stroke->preset && stroke->preset->settings()) {
            brushSize = qMax(1.0, stroke->preset->settings()->paintOpSize());
            recordOutline = strokeOutline(*stroke, brushSize);
        }
    }

    /// Draws the stroke again for @p full (recorded pixels to device
    /// pixels); the mutex is held. False if it cannot be drawn for it.
    bool render(const QTransform &full)
    {
        if (!full.isInvertible() || !full.isAffine()) {
            return false;
        }
        const qreal margin = brushSize * lengthScale(full) + 2.0;
        const QRectF mapped = full.mapRect(recordRect).adjusted(-margin, -margin, margin, margin);
        if (mapped.isEmpty() || mapped.width() > MaximumRenderSide || mapped.height() > MaximumRenderSide) {
            return false;
        }
        // the picture's own pixels: the device's, moved by whole pixels so
        // that the stroke lies at positive coordinates
        const QPoint origin(qFloor(mapped.left()), qFloor(mapped.top()));
        const QTransform transform = full * QTransform::fromTranslate(-origin.x(), -origin.y());
        const QRectF pictureRect(0, 0, mapped.width() + 1, mapped.height() + 1);
        const QRect recordBounds = transform.inverted().mapRect(pictureRect).toAlignedRect();

        KisPaintDeviceSP device = KisBrushStrokeLayer::renderStrokes({stroke},
                                                                     KoColorSpaceRegistry::instance()->rgb8(),
                                                                     recordBounds,
                                                                     stroke->deviceOffset,
                                                                     transform);
        const QRect bounds = device ? device->exactBounds() : QRect();
        Picture picture;
        if (bounds.isEmpty()) {
            // nothing to show at this size (a stroke scaled to nothing)
            picture.image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
            picture.image.fill(0);
        } else {
            picture.image =
                device->convertToQImage(nullptr, bounds).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            picture.offset = bounds.topLeft();
            if (hasDensity(*stroke)) {
                applyDensity(*stroke,
                             transform,
                             brushSize * lengthScale(full) / 2.0,
                             margin,
                             &picture.image,
                             picture.offset);
            }
        }
        picture.renderTransform = transform;
        // a picture read from a file for the same transform is replaced
        QPoint shift;
        const int replaced = pictureFor(transform, &shift);
        if (replaced >= 0) {
            pictures.removeAt(replaced);
        }
        addPicture(picture);
        return true;
    }

    /// The picture as a file keeps it: covering exactly the stroke's
    /// rectangle; the mutex is held. @p expected is the transform the
    /// shape's layer paints it with (recorded pixels to image pixels).
    /// @p exact says whether the picture read back from the file is the
    /// brush's drawing pixel for pixel.
    QImage savedPicture(const QTransform &expected, bool *exact = nullptr) const
    {
        if (exact) {
            *exact = false;
        }
        QPoint unused;
        const int index = qMax(0, pictureFor(expected, &unused));
        if (index >= pictures.size() || !pictures[index].renderTransform.isInvertible() || recordRect.isEmpty()) {
            QImage empty(1, 1, QImage::Format_ARGB32_Premultiplied);
            empty.fill(0);
            return empty;
        }
        const QImage &image = pictures[index].image;
        const QPoint imageOffset = pictures[index].offset;
        const QTransform renderTransform = pictures[index].renderTransform;
        const bool imageExact = pictures[index].exact;
        const qreal xScale =
            qSqrt(renderTransform.m11() * renderTransform.m11() + renderTransform.m12() * renderTransform.m12());
        const qreal yScale =
            qSqrt(renderTransform.m21() * renderTransform.m21() + renderTransform.m22() * renderTransform.m22());
        const int width = qBound(1, qCeil(recordRect.width() * xScale), MaximumSavedSide);
        const int height = qBound(1, qCeil(recordRect.height() * yScale), MaximumSavedSide);

        QImage result(width, height, QImage::Format_ARGB32_Premultiplied);
        result.fill(0);
        QPainter painter(&result);
        // what loadSvg() takes the file's picture to be drawn for
        const QTransform saved = QTransform::fromTranslate(-recordRect.x(), -recordRect.y())
            * QTransform::fromScale(width / recordRect.width(), height / recordRect.height());
        // Both pictures are placed against the transform the layer paints
        // with, each rounded to whole pixels as paint() rounds it. Rounding
        // the difference between the two instead put a picture whose
        // rectangle starts on a half pixel one pixel off after loading.
        QPoint drawnShift;
        QPoint savedShift;
        if (imageExact && pixelAligned(renderTransform, expected, image.size(), &drawnShift)
            && pixelAligned(saved, expected, result.size(), &savedShift)) {
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(imageOffset + drawnShift - savedShift, image);
            if (exact) {
                *exact = true;
            }
        } else {
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setTransform(renderTransform.inverted() * saved);
            painter.drawImage(imageOffset, image);
        }
        return result;
    }
};

KisBrushStrokeShape::KisBrushStrokeShape()
    : m_d(new Data())
{
}

KisBrushStrokeShape::KisBrushStrokeShape(const KisBrushStrokeShape &rhs)
    : KoShape(rhs)
    , m_d(rhs.m_d)
    , m_xRes(rhs.m_xRes)
    , m_yRes(rhs.m_yRes)
{
}

KisBrushStrokeShape::~KisBrushStrokeShape()
{
}

KisBrushStrokeShape *KisBrushStrokeShape::fromStroke(KisRecordedBrushStrokeSP stroke,
                                                     const QImage &image,
                                                     const QPoint &imageOffset,
                                                     qreal xRes,
                                                     qreal yRes)
{
    if (!stroke || image.isNull() || xRes <= 0.0 || yRes <= 0.0) {
        return nullptr;
    }
    KisBrushStrokeShape *shape = new KisBrushStrokeShape();
    shape->setShapeId(KisBrushStrokeShapeId);

    Data &data = *shape->m_d;
    data.recordRect = QRectF(imageOffset, image.size());
    Data::Picture picture;
    picture.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    picture.offset = imageOffset;
    data.addPicture(picture);
    data.setStroke(stroke);
    shape->m_xRes = xRes * 72.0;
    shape->m_yRes = yRes * 72.0;

    shape->setSize(QSizeF(image.width() / xRes, image.height() / yRes));
    shape->setTransformation(QTransform::fromTranslate(imageOffset.x() / xRes, imageOffset.y() / yRes));
    return shape;
}

KisRecordedBrushStrokeSP KisBrushStrokeShape::brushStroke() const
{
    return m_d->stroke;
}

QRectF KisBrushStrokeShape::recordRect() const
{
    return m_d->recordRect;
}

QImage KisBrushStrokeShape::staticImage() const
{
    QMutexLocker locker(&m_d->mutex);
    return m_d->savedPicture(expectedTransform());
}

void KisBrushStrokeShape::setResolution(qreal xRes, qreal yRes)
{
    m_xRes = xRes;
    m_yRes = yRes;
}

QTransform KisBrushStrokeShape::expectedTransform() const
{
    return recordToShape() * absoluteTransformation() * QTransform::fromScale(m_xRes / 72.0, m_yRes / 72.0);
}

KisBrushStrokeShape *KisBrushStrokeShape::createPiece(KisRecordedBrushStrokeSP piece, bool withinThis) const
{
    const QRectF whole = m_d->recordRect;
    if (!piece || piece->jobs.isEmpty() || whole.isEmpty()) {
        return nullptr;
    }

    // the piece's path, as wide as the brush; never outside the stroke it
    // was cut from
    QPolygonF points;
    for (const KisRecordedBrushStroke::Job &job : piece->jobs) {
        points << job.pi1.pos();
        if (job.type != FreehandStrokeStrategy::Data::POINT) {
            points << job.pi2.pos();
        }
        if (job.type == FreehandStrokeStrategy::Data::CURVE) {
            points << job.control1 << job.control2;
        }
    }
    // the canvas mirror paints copies of every dab around its axes: they
    // are part of the picture, though no job holds them
    if (piece->mirrorHorizontal || piece->mirrorVertical) {
        const QRectF own = points.boundingRect();
        const QPointF center = piece->mirrorAxesCenter;
        const qreal left = 2 * center.x() - own.right();
        const qreal top = 2 * center.y() - own.bottom();
        if (piece->mirrorHorizontal) {
            points << QPointF(left, own.top()) << QPointF(left + own.width(), own.bottom());
        }
        if (piece->mirrorVertical) {
            points << QPointF(own.left(), top) << QPointF(own.right(), top + own.height());
        }
        if (piece->mirrorHorizontal && piece->mirrorVertical) {
            points << QPointF(left, top) << QPointF(left + own.width(), top + own.height());
        }
    }

    // this shape's own properties (stacking order, transparency, name), a
    // stroke and pictures of its own
    KisBrushStrokeShape *shape = new KisBrushStrokeShape(*this);
    shape->m_d.reset(new Data());
    shape->m_d->setStroke(piece);

    // on whole pixels, like the rectangle of a stroke as it was painted
    const qreal margin = shape->m_d->brushSize * 0.5 + 2.0;
    QRectF rect = QRectF(points.boundingRect().adjusted(-margin, -margin, margin, margin).toAlignedRect());
    if (withinThis && rect.intersects(whole)) {
        rect &= whole;
    }
    shape->m_d->recordRect = rect;

    const QSizeF size = this->size();
    const qreal xScale = size.width() / whole.width();
    const qreal yScale = size.height() / whole.height();
    // The transformation first: KoShape::setSize() writes through the data a
    // copy still shares with its source (setSizeImpl() is const and does
    // not detach), which resized this shape too.
    shape->setTransformation(QTransform::fromTranslate((rect.x() - whole.x()) * xScale, (rect.y() - whole.y()) * yScale)
                             * absoluteTransformation());
    shape->setSize(QSizeF(rect.width() * xScale, rect.height() * yScale));
    return shape;
}

KoShape *KisBrushStrokeShape::cloneShape() const
{
    return new KisBrushStrokeShape(*this);
}

QTransform KisBrushStrokeShape::recordToShape() const
{
    const QRectF rect = m_d->recordRect;
    if (rect.isEmpty()) {
        return QTransform();
    }
    const QSizeF size = this->size();
    return QTransform::fromTranslate(-rect.x(), -rect.y())
        * QTransform::fromScale(size.width() / rect.width(), size.height() / rect.height());
}

qreal KisBrushStrokeShape::paintMargin() const
{
    const QRectF rect = m_d->recordRect;
    if (!m_d->stroke || rect.isEmpty()) {
        return 0.0;
    }
    const QSizeF size = this->size();
    const qreal scale = qMax(size.width() / rect.width(), size.height() / rect.height());
    return (m_d->brushSize * 0.5 + 2.0) * scale;
}

void KisBrushStrokeShape::paint(QPainter &painter) const
{
    const QTransform full = recordToShape() * painter.transform();

    QImage image;
    QPoint offset;
    QTransform render;
    QPoint shift;
    bool aligned = false;
    {
        QMutexLocker locker(&m_d->mutex);
        int index = m_d->pictureFor(full, &shift);
        // only a painter on an image gets its own drawing of the stroke
        // (the vector layer's); anything else shows the last one, scaled
        const bool exactDevice = painter.device() && painter.device()->devType() == QInternal::Image;
        if (m_d->stroke && exactDevice && (index < 0 || !m_d->pictures[index].exact)) {
            if (s_previewScope && s_previewScope->m_enabled && !m_d->pictures.isEmpty()) {
                // a preview: the latest picture, scaled; the layer paints
                // the shape again when the preview ends
                s_previewScope->m_used = true;
            } else if (m_d->render(full)) {
                index = m_d->pictureFor(full, &shift);
            }
        }
        aligned = index >= 0;
        if (index < 0 && !m_d->pictures.isEmpty()) {
            index = 0;
        }
        if (index < 0) {
            return;
        }
        if (index > 0) {
            m_d->pictures.move(index, 0);
        }
        image = m_d->pictures.first().image;
        offset = m_d->pictures.first().offset;
        render = m_d->pictures.first().renderTransform;
    }
    if (image.isNull() || !render.isInvertible()) {
        return;
    }

    KisQPainterStateSaver saver(&painter);
    const qreal margin = paintMargin();
    painter.setClipRect(QRectF(QPointF(), size()).adjusted(-margin, -margin, margin, margin), Qt::IntersectClip);
    if (aligned) {
        painter.setTransform(QTransform::fromTranslate(shift.x(), shift.y()));
    } else {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setTransform(render.inverted() * full);
    }
    painter.drawImage(offset, image);
}

QPainterPath KisBrushStrokeShape::outline() const
{
    if (m_d->recordOutline.isEmpty()) {
        return KoShape::outline();
    }
    return recordToShape().map(m_d->recordOutline);
}

QRectF KisBrushStrokeShape::boundingRect() const
{
    const qreal margin = paintMargin();
    const QRectF rect = QRectF(QPointF(), size()).adjusted(-margin, -margin, margin, margin);
    return absoluteTransformation().mapRect(rect) | KoShape::boundingRect();
}

bool KisBrushStrokeShape::saveSvg(SvgSavingContext &context)
{
    QImage picture;
    bool exact = false;
    {
        QMutexLocker locker(&m_d->mutex);
        picture = m_d->savedPicture(expectedTransform(), &exact);
    }

    // an image element: an application that does not know the stroke shows
    // its picture
    context.shapeWriter().startElement("image");
    context.shapeWriter().addAttribute("id", context.createUID("image"));
    SvgUtil::writeTransformAttributeLazy("transform", transformation(), context.shapeWriter());
    context.shapeWriter().addAttribute("width", QString("%1px").arg(KisDomUtils::toString(size().width())));
    context.shapeWriter().addAttribute("height", QString("%1px").arg(KisDomUtils::toString(size().height())));
    context.shapeWriter().addAttribute("preserveAspectRatio", "none");
    context.shapeWriter().addAttribute(RectAttribute, rectToString(m_d->recordRect));
    if (exact) {
        context.shapeWriter().addAttribute(ExactAttribute, "1");
    }

    if (m_d->stroke) {
        if (s_savingScope) {
            context.shapeWriter().addAttribute(IndexAttribute, QString::number(s_savingScope->m_strokes.size()));
            s_savingScope->m_strokes << m_d->stroke;
        } else {
            QBuffer buffer;
            buffer.open(QIODevice::WriteOnly);
            if (KisBrushStrokeLayer::saveStrokes({m_d->stroke}, &buffer)) {
                context.shapeWriter().addAttribute(InlineAttribute,
                                                   QString::fromLatin1(qCompress(buffer.data()).toBase64()));
            }
        }
    }

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    if (picture.save(&buffer, "PNG")) {
        const QString mimeType = KisMimeDatabase::mimeTypeForSuffix("*.png");
        context.shapeWriter().addAttribute("xlink:href", "data:" + mimeType + ";base64," + buffer.data().toBase64());
    }
    SvgStyleWriter::saveMetadata(this, context);

    context.shapeWriter().endElement(); // image
    return true;
}

bool KisBrushStrokeShape::loadSvg(const QDomElement &element, SvgLoadingContext &context)
{
    // any other image belongs to the image shape
    QRectF recordRect;
    if (!rectFromString(element.attribute(RectAttribute), &recordRect)) {
        return false;
    }

    QImage image;
    const QString href = element.attribute("xlink:href");
    if (href.startsWith("data:")) {
        const QRegularExpressionMatch match = QRegularExpression("data:(.+?);base64,(.+)").match(href);
        QByteArray data = QByteArray::fromBase64(match.captured(2).toLatin1());
        QBuffer buffer(&data);
        image.load(&buffer, "");
    }

    KisRecordedBrushStrokeSP stroke;
    if (element.hasAttribute(IndexAttribute)) {
        bool ok = false;
        const int index = element.attribute(IndexAttribute).toInt(&ok);
        if (ok && s_loadingScope && index >= 0 && index < s_loadingScope->m_strokes.size()) {
            stroke = s_loadingScope->m_strokes[index];
        }
    } else if (element.hasAttribute(InlineAttribute)) {
        QByteArray data = qUncompress(QByteArray::fromBase64(element.attribute(InlineAttribute).toLatin1()));
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        QVector<KisRecordedBrushStrokeSP> strokes;
        if (!data.isEmpty() && KisBrushStrokeLayer::loadStrokes(&buffer, &strokes) && !strokes.isEmpty()) {
            stroke = strokes.first();
        }
    }
    if (!stroke && image.isNull()) {
        return false;
    }

    const qreal x = SvgUtil::parseUnitX(context.currentGC(), context.resolvedProperties(), element.attribute("x"));
    const qreal y = SvgUtil::parseUnitY(context.currentGC(), context.resolvedProperties(), element.attribute("y"));
    const qreal w = SvgUtil::parseUnitX(context.currentGC(), context.resolvedProperties(), element.attribute("width"));
    const qreal h = SvgUtil::parseUnitY(context.currentGC(), context.resolvedProperties(), element.attribute("height"));
    setSize(QSizeF(w, h));
    setPosition(QPointF(x, y));
    if (w == 0.0 || h == 0.0) {
        setVisible(false);
    }

    m_d.reset(new Data());
    m_d->recordRect = recordRect;
    if (!image.isNull()) {
        // the saved picture covers the stroke's rectangle
        Data::Picture picture;
        picture.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        picture.renderTransform = QTransform::fromTranslate(-recordRect.x(), -recordRect.y())
            * QTransform::fromScale(image.width() / recordRect.width(), image.height() / recordRect.height());
        // a resampled picture is drawn again when the layer first paints it
        picture.exact = element.attribute(ExactAttribute) == QLatin1String("1");
        m_d->addPicture(picture);
    }
    m_d->setStroke(stroke);
    return true;
}

void KisBrushStrokeShape::registerFactory()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        KoShapeRegistry::instance()->addFactory(new KisBrushStrokeShapeFactory());
    });
}

KisBrushStrokeShape::SavingScope::SavingScope()
    : m_previous(s_savingScope)
{
    s_savingScope = this;
}

KisBrushStrokeShape::SavingScope::~SavingScope()
{
    s_savingScope = m_previous;
}

const QVector<KisRecordedBrushStrokeSP> &KisBrushStrokeShape::SavingScope::strokes() const
{
    return m_strokes;
}

KisBrushStrokeShape::PreviewPaintScope::PreviewPaintScope(bool enabled)
    : m_enabled(enabled)
    , m_previous(s_previewScope)
{
    s_previewScope = this;
}

KisBrushStrokeShape::PreviewPaintScope::~PreviewPaintScope()
{
    s_previewScope = m_previous;
}

bool KisBrushStrokeShape::PreviewPaintScope::usedPreview() const
{
    return m_used;
}

KisBrushStrokeShape::LoadingScope::LoadingScope(const QVector<KisRecordedBrushStrokeSP> &strokes)
    : m_strokes(strokes)
    , m_previous(s_loadingScope)
{
    s_loadingScope = this;
}

KisBrushStrokeShape::LoadingScope::~LoadingScope()
{
    s_loadingScope = m_previous;
}
