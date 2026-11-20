/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisBrushStrokeShapeEraser.h"

#include <algorithm>

#include <QMap>
#include <QtMath>

#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>
#include <kundo2command.h>

#include <KoShape.h>
#include <kis_paintop_preset.h>
#include <kis_paintop_settings.h>
#include <strokes/freehand_stroke.h>

#include "KisBrushStrokeShape.h"
#include "kis_shape_layer.h"

namespace
{
using Job = KisRecordedBrushStroke::Job;

const char *const ConfigGroup = "VectorBrushStrokes";
const char *const ConfigModeKey = "eraserMode";

/// a part of a path shorter than this (recorded pixels) is not kept
const qreal MinimumPartLength = 0.5;

QPointF lerp(const QPointF &a, const QPointF &b, qreal t)
{
    return a + (b - a) * t;
}

QPointF jobPosition(const Job &job, qreal t)
{
    switch (job.type) {
    case FreehandStrokeStrategy::Data::LINE:
        return lerp(job.pi1.pos(), job.pi2.pos(), t);
    case FreehandStrokeStrategy::Data::CURVE: {
        const qreal u = 1.0 - t;
        return u * u * u * job.pi1.pos() + 3 * u * u * t * job.control1 + 3 * u * t * t * job.control2
            + t * t * t * job.pi2.pos();
    }
    default:
        return job.pi1.pos();
    }
}

/// An upper bound of the job's length
qreal jobLength(const Job &job)
{
    auto distance = [](const QPointF &a, const QPointF &b) {
        return std::hypot(a.x() - b.x(), a.y() - b.y());
    };
    switch (job.type) {
    case FreehandStrokeStrategy::Data::LINE:
        return distance(job.pi1.pos(), job.pi2.pos());
    case FreehandStrokeStrategy::Data::CURVE:
        return distance(job.pi1.pos(), job.control1) + distance(job.control1, job.control2)
            + distance(job.control2, job.pi2.pos());
    default:
        return 0.0;
    }
}

/// The eraser's sweep: everything within the radius of a line segment
struct Eraser {
    QPointF from;
    QPointF to;
    qreal radius;
    /// half the brush's diameter: a point of the path is erased when the
    /// line drawn there reaches into the sweep, so that no ink is left
    /// inside it. The line's width is taken to follow the pen pressure.
    qreal halfBrush;

    bool contains(const QPointF &point, qreal pressure) const
    {
        const QPointF direction = to - from;
        const qreal lengthSquared = QPointF::dotProduct(direction, direction);
        const qreal t =
            lengthSquared > 0.0 ? qBound(0.0, QPointF::dotProduct(point - from, direction) / lengthSquared, 1.0) : 0.0;
        const QPointF offset = from + direction * t - point;
        const qreal reach = radius + halfBrush * qBound(0.0, pressure, 1.0);
        return QPointF::dotProduct(offset, offset) <= reach * reach;
    }

    /// Whether the path of @p job at @p t is erased
    bool erases(const Job &job, qreal t) const
    {
        const qreal pressure = job.type == FreehandStrokeStrategy::Data::POINT
            ? job.pi1.pressure()
            : job.pi1.pressure() + (job.pi2.pressure() - job.pi1.pressure()) * t;
        return contains(jobPosition(job, t), pressure);
    }
};

/// A part of a job that is kept: its parameter range
struct Range {
    qreal start;
    qreal end;
};

/// The parts of @p job outside the eraser, in order. @p touched is set when
/// any of it is inside.
QVector<Range> keptRanges(const Job &job, const Eraser &eraser, bool *touched)
{
    if (job.type == FreehandStrokeStrategy::Data::POINT) {
        if (eraser.erases(job, 0.0)) {
            *touched = true;
            return {};
        }
        return {Range{0.0, 1.0}};
    }

    // fine enough to find a crossing of the eraser's width
    const qreal length = jobLength(job);
    const qreal step = qMax(0.5, (eraser.radius + eraser.halfBrush * 0.25) * 0.5);
    const int count = qBound(job.type == FreehandStrokeStrategy::Data::CURVE ? 4 : 1, qCeil(length / step), 512);

    // the parameter where the path crosses the eraser's edge between an
    // inside and an outside sample
    auto crossing = [&](qreal inside, qreal outside) {
        for (int i = 0; i < 16; i++) {
            const qreal middle = 0.5 * (inside + outside);
            (eraser.erases(job, middle) ? inside : outside) = middle;
        }
        return outside;
    };

    QVector<Range> ranges;
    bool previousInside = eraser.erases(job, 0.0);
    qreal rangeStart = 0.0;
    *touched |= previousInside;
    for (int i = 1; i <= count; i++) {
        const qreal t = qreal(i) / count;
        const bool inside = eraser.erases(job, t);
        if (inside != previousInside) {
            *touched = true;
            const qreal previous = qreal(i - 1) / count;
            if (inside) {
                ranges << Range{rangeStart, crossing(t, previous)};
            } else {
                rangeStart = crossing(previous, t);
            }
        }
        previousInside = inside;
    }
    if (!previousInside) {
        ranges << Range{rangeStart, 1.0};
    }
    return ranges;
}

/// The part of @p job in @p range, drawn the same way
Job partOfJob(const Job &job, const Range &range)
{
    if (range.start <= 0.0 && range.end >= 1.0) {
        return job;
    }
    Job part = job;
    part.pi1 = KisPaintInformation::mix(jobPosition(job, range.start), range.start, job.pi1, job.pi2);
    part.pi2 = KisPaintInformation::mix(jobPosition(job, range.end), range.end, job.pi1, job.pi2);
    if (job.type == FreehandStrokeStrategy::Data::CURVE) {
        // de Casteljau: the curve up to the range's end, then from its start
        QPointF p0 = job.pi1.pos();
        QPointF p1 = job.control1;
        QPointF p2 = job.control2;
        QPointF p3 = job.pi2.pos();
        auto split = [&](qreal t, bool keepFirst) {
            const QPointF a = lerp(p0, p1, t);
            const QPointF b = lerp(p1, p2, t);
            const QPointF c = lerp(p2, p3, t);
            const QPointF d = lerp(a, b, t);
            const QPointF e = lerp(b, c, t);
            const QPointF f = lerp(d, e, t);
            if (keepFirst) {
                p1 = a;
                p2 = d;
                p3 = f;
            } else {
                p0 = f;
                p1 = e;
                p2 = c;
            }
        };
        if (range.end < 1.0) {
            split(range.end, true);
        }
        if (range.start > 0.0) {
            split(range.end > 0.0 ? range.start / range.end : 0.0, false);
        }
        part.control1 = p1;
        part.control2 = p2;
    }
    return part;
}

/// The strokes of the kept ranges of each job of @p stroke
QVector<KisRecordedBrushStrokeSP> piecesFromRanges(KisRecordedBrushStrokeSP stroke,
                                                   const QVector<QVector<Range>> &ranges)
{
    QVector<KisRecordedBrushStrokeSP> result;

    // The jobs of each copy the tool painted (Multibrush) form a line of
    // their own. A line's parts: runs of jobs that are kept without a gap.
    struct Part {
        QVector<Job> jobs;
        qreal length = 0.0;
        bool hasPoint = false;
        /// starts where the line started, with the line's first dab state
        bool fromLineStart = false;
        int strokeInfoId = 0;
    };
    QVector<Part> parts;
    // per copy: the index of the part that can still grow, or -1
    QMap<int, int> openPart;
    QMap<int, bool> lineStarted;

    for (int i = 0; i < stroke->jobs.size(); i++) {
        const Job &job = stroke->jobs[i];
        const int id = job.strokeInfoId;
        const bool firstOfLine = !lineStarted.value(id, false);
        lineStarted[id] = true;

        int open = openPart.value(id, -1);
        for (const Range &range : ranges[i]) {
            const bool continues = open >= 0 && range.start <= 0.0;
            if (!continues) {
                Part part;
                part.strokeInfoId = id;
                part.fromLineStart = firstOfLine && range.start <= 0.0;
                parts << part;
                open = parts.size() - 1;
            }
            Part &part = parts[open];
            part.jobs << partOfJob(job, range);
            part.length += jobLength(part.jobs.last());
            part.hasPoint |= job.type == FreehandStrokeStrategy::Data::POINT;
            // a part that ends inside the job cannot grow
            if (range.end < 1.0) {
                open = -1;
            }
        }
        if (ranges[i].isEmpty()) {
            open = -1;
        }
        openPart[id] = open;
    }

    for (const Part &part : parts) {
        // crumbs left between two passes of the eraser
        if (part.length < MinimumPartLength && !(part.hasPoint && part.fromLineStart)) {
            continue;
        }
        QSharedPointer<KisRecordedBrushStroke> piece(new KisRecordedBrushStroke(*stroke));
        piece->jobs = part.jobs;
        if (!part.fromLineStart && part.strokeInfoId >= 0 && part.strokeInfoId < piece->starts.size()) {
            // the dab spacing starts anew at a cut
            KisRecordedBrushStroke::Start &start = piece->starts[part.strokeInfoId];
            start.hasLastDab = false;
            start.dabSeqNo = 0;
        }
        result << piece;
    }
    return result;
}

/// A vertex of a line's path with its place along the line: the number of
/// the job in the line plus the parameter in the job
struct Vertex {
    QPointF point;
    qreal place;
};

/// The path of the jobs @p indices of @p stroke (one line), as a polyline
QVector<Vertex> flattenLine(const KisRecordedBrushStroke &stroke, const QVector<int> &indices)
{
    QVector<Vertex> vertices;
    for (int ordinal = 0; ordinal < indices.size(); ordinal++) {
        const Job &job = stroke.jobs[indices[ordinal]];
        int count = 0;
        if (job.type == FreehandStrokeStrategy::Data::LINE) {
            count = 1;
        } else if (job.type == FreehandStrokeStrategy::Data::CURVE) {
            count = qBound(2, qCeil(jobLength(job) / 2.0), 64);
        }
        for (int i = 0; i <= count; i++) {
            const qreal t = count ? qreal(i) / count : 0.0;
            vertices << Vertex{jobPosition(job, t), ordinal + t};
        }
    }
    return vertices;
}

qreal cross(const QPointF &a, const QPointF &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

/// Where the segment from @p p0 to @p p1 meets the segment from @p q0 to
/// @p q1: the factor along the first one, or -1
qreal segmentCrossing(const QPointF &p0, const QPointF &p1, const QPointF &q0, const QPointF &q1)
{
    const QPointF r = p1 - p0;
    const QPointF d = q1 - q0;
    const qreal denominator = cross(r, d);
    if (qAbs(denominator) < 1e-12) {
        return -1.0;
    }
    const qreal a = cross(q0 - p0, d) / denominator;
    const qreal b = cross(q0 - p0, r) / denominator;
    const qreal tolerance = 1e-9;
    return a >= -tolerance && a <= 1.0 + tolerance && b >= -tolerance && b <= 1.0 + tolerance ? qBound(0.0, a, 1.0)
                                                                                              : -1.0;
}

/// @p line with both ends moved outwards by @p length
QPolygonF extendedEnds(const QPolygonF &line, qreal length)
{
    QPolygonF result = line;
    auto extend = [&](int end, int step) {
        for (int i = end + step; i >= 0 && i < result.size(); i += step) {
            const QPointF direction = result[end] - result[i];
            const qreal distance = std::hypot(direction.x(), direction.y());
            if (distance > 1e-6) {
                result[end] += direction * (length / distance);
                return;
            }
        }
    };
    if (result.size() >= 2) {
        // the end first: moving the start does not change it
        extend(result.size() - 1, -1);
        extend(0, 1);
    }
    return result;
}

/// The places along @p line where it crosses @p other. With @p self, @p
/// other is the line itself: neighbouring segments do not count.
void collectCrossings(const QVector<Vertex> &line, const QPolygonF &other, bool self, QVector<qreal> *crossings)
{
    const QRectF otherBounds = other.boundingRect().adjusted(-1, -1, 1, 1);
    for (int i = 0; i + 1 < line.size(); i++) {
        const QPointF p0 = line[i].point;
        const QPointF p1 = line[i + 1].point;
        if (p0 == p1) {
            continue;
        }
        const QRectF bounds = QRectF(p0, p1).normalized().adjusted(-1, -1, 1, 1);
        if (!bounds.intersects(otherBounds)) {
            continue;
        }
        for (int k = 0; k + 1 < other.size(); k++) {
            if (self && qAbs(k - i) <= 2) {
                continue;
            }
            if (other[k] == other[k + 1]
                || !bounds.intersects(QRectF(other[k], other[k + 1]).normalized().adjusted(-1, -1, 1, 1))) {
                continue;
            }
            const qreal factor = segmentCrossing(p0, p1, other[k], other[k + 1]);
            if (factor >= 0.0) {
                *crossings << line[i].place + factor * (line[i + 1].place - line[i].place);
            }
        }
    }
}

/// Replaces the shapes of an erasing drag on undo and redo
class EraseStrokesCommand : public KUndo2Command
{
public:
    EraseStrokesCommand(KisShapeLayerSP layer,
                        const QList<KoShape *> &removed,
                        const QList<KoShape *> &added,
                        const KUndo2MagicString &text)
        : KUndo2Command(text)
        , m_layer(layer)
        , m_removed(removed)
        , m_added(added)
    {
    }

    ~EraseStrokesCommand() override
    {
        // the shapes that are not in the layer belong to the command
        qDeleteAll(m_erased ? m_removed : m_added);
    }

    void redo() override
    {
        // the drag has erased already
        if (m_firstRedo) {
            m_firstRedo = false;
            return;
        }
        exchange(m_removed, m_added);
        m_erased = true;
    }

    void undo() override
    {
        exchange(m_added, m_removed);
        m_erased = false;
    }

private:
    void exchange(const QList<KoShape *> &out, const QList<KoShape *> &in)
    {
        Q_FOREACH (KoShape *shape, out) {
            m_layer->removeShape(shape);
        }
        Q_FOREACH (KoShape *shape, in) {
            m_layer->addShape(shape);
        }
    }

    KisShapeLayerSP m_layer;
    QList<KoShape *> m_removed;
    QList<KoShape *> m_added;
    bool m_firstRedo = true;
    bool m_erased = true;
};
} // namespace

QVector<KisRecordedBrushStrokeSP> KisBrushStrokeShapeEraser::cutStroke(KisRecordedBrushStrokeSP stroke,
                                                                       const QPointF &from,
                                                                       const QPointF &to,
                                                                       qreal radius,
                                                                       bool *touched)
{
    bool reached = false;
    QVector<KisRecordedBrushStrokeSP> result;
    if (!stroke) {
        if (touched) {
            *touched = false;
        }
        return result;
    }

    const qreal brushSize =
        stroke->preset && stroke->preset->settings() ? stroke->preset->settings()->paintOpSize() : 0.0;
    const Eraser eraser{from, to, qMax(radius, 0.0), qMax(brushSize, 0.0) * 0.5};
    QVector<QVector<Range>> ranges;
    ranges.reserve(stroke->jobs.size());
    for (const Job &job : stroke->jobs) {
        ranges << keptRanges(job, eraser, &reached);
    }
    if (touched) {
        *touched = reached;
    }
    if (!reached) {
        result << stroke;
        return result;
    }

    return piecesFromRanges(stroke, ranges);
}

KUndo2Command *KisBrushStrokeShapeEraser::createReplaceCommand(KisShapeLayerSP layer,
                                                               const QList<KoShape *> &removed,
                                                               const QList<KoShape *> &added,
                                                               const KUndo2MagicString &text)
{
    return new EraseStrokesCommand(layer, removed, added, text);
}

QVector<KisRecordedBrushStrokeSP>
KisBrushStrokeShapeEraser::splitStroke(KisRecordedBrushStrokeSP stroke, int jobIndex, qreal t)
{
    if (!stroke || jobIndex < 0 || jobIndex >= stroke->jobs.size()) {
        return {stroke};
    }
    QVector<QVector<Range>> ranges(stroke->jobs.size(), QVector<Range>{Range{0.0, 1.0}});
    const qreal place = qBound(0.0, t, 1.0);
    if (stroke->jobs[jobIndex].type == FreehandStrokeStrategy::Data::POINT) {
        return {stroke};
    }
    // both parts of the job; an empty one is dropped as a crumb
    ranges[jobIndex] = QVector<Range>{Range{0.0, place}, Range{place, 1.0}};
    return piecesFromRanges(stroke, ranges);
}

QVector<QPolygonF> KisBrushStrokeShapeEraser::strokeLines(KisRecordedBrushStrokeSP stroke)
{
    QVector<QPolygonF> lines;
    if (!stroke) {
        return lines;
    }
    QMap<int, QVector<int>> indices;
    for (int i = 0; i < stroke->jobs.size(); i++) {
        indices[stroke->jobs[i].strokeInfoId] << i;
    }
    for (const QVector<int> &line : indices) {
        QPolygonF points;
        for (const Vertex &vertex : flattenLine(*stroke, line)) {
            points << vertex.point;
        }
        lines << points;
    }
    return lines;
}

QVector<KisRecordedBrushStrokeSP>
KisBrushStrokeShapeEraser::cutStrokeUpToIntersections(KisRecordedBrushStrokeSP stroke,
                                                      const QPointF &from,
                                                      const QPointF &to,
                                                      qreal radius,
                                                      const QVector<OtherLine> &others,
                                                      bool *touched)
{
    bool reached = false;
    if (!stroke) {
        if (touched) {
            *touched = false;
        }
        return {};
    }

    // where the eraser touches the stroke
    const qreal brushSize =
        stroke->preset && stroke->preset->settings() ? stroke->preset->settings()->paintOpSize() : 0.0;
    const qreal halfBrush = qMax(brushSize, 0.0) * 0.5;
    const Eraser eraser{from, to, qMax(radius, 0.0), halfBrush};
    QVector<QVector<Range>> ranges;
    ranges.reserve(stroke->jobs.size());
    for (const Job &job : stroke->jobs) {
        ranges << keptRanges(job, eraser, &reached);
    }
    if (touched) {
        *touched = reached;
    }
    if (!reached) {
        return {stroke};
    }

    // the stroke's lines (one for each copy the tool painted)
    QMap<int, QVector<int>> indices;
    for (int i = 0; i < stroke->jobs.size(); i++) {
        indices[stroke->jobs[i].strokeInfoId] << i;
    }
    QMap<int, QVector<Vertex>> lines;
    for (auto it = indices.constBegin(); it != indices.constEnd(); ++it) {
        lines[it.key()] = flattenLine(*stroke, it.value());
    }

    for (auto it = indices.constBegin(); it != indices.constEnd(); ++it) {
        const QVector<int> &jobs = it.value();
        const QVector<Vertex> &line = lines[it.key()];

        // the touched stretches, as places along the line
        struct Stretch {
            qreal start;
            qreal end;
        };
        QVector<Stretch> erased;
        for (int ordinal = 0; ordinal < jobs.size(); ordinal++) {
            qreal position = 0.0;
            for (const Range &range : ranges[jobs[ordinal]]) {
                if (range.start > position) {
                    erased << Stretch{ordinal + position, ordinal + range.start};
                }
                position = range.end;
            }
            if (ranges[jobs[ordinal]].isEmpty()) {
                erased << Stretch{qreal(ordinal), ordinal + 1.0};
            } else if (position < 1.0) {
                erased << Stretch{ordinal + position, ordinal + 1.0};
            }
        }
        if (erased.isEmpty()) {
            continue;
        }

        // where the line crosses the other lines, the stroke's other
        // copies and itself. A line that ends at another one touches it:
        // the other lines' ends reach half of both widths further.
        QVector<qreal> crossings;
        for (const OtherLine &other : others) {
            collectCrossings(line, extendedEnds(other.points, other.halfWidth + halfBrush + 1.0), false, &crossings);
        }
        QPolygonF own;
        for (const Vertex &vertex : line) {
            own << vertex.point;
        }
        collectCrossings(line, own, true, &crossings);
        for (auto other = lines.constBegin(); other != lines.constEnd(); ++other) {
            if (other.key() == it.key()) {
                continue;
            }
            QPolygonF points;
            for (const Vertex &vertex : other.value()) {
                points << vertex.point;
            }
            collectCrossings(line, extendedEnds(points, 2.0 * halfBrush + 1.0), false, &crossings);
        }
        std::sort(crossings.begin(), crossings.end());

        // each stretch grows to the nearest crossing on both sides, or to
        // the line's end
        const qreal lineEnd = jobs.size();
        for (Stretch &stretch : erased) {
            qreal start = 0.0;
            qreal end = lineEnd;
            for (qreal crossing : crossings) {
                if (crossing <= stretch.start) {
                    start = crossing;
                } else if (crossing >= stretch.end) {
                    end = crossing;
                    break;
                }
            }
            stretch.start = start;
            stretch.end = end;
        }

        // back to kept ranges of the jobs
        for (int ordinal = 0; ordinal < jobs.size(); ordinal++) {
            const Job &job = stroke->jobs[jobs[ordinal]];
            QVector<Range> kept{Range{0.0, 1.0}};
            for (const Stretch &stretch : erased) {
                const qreal start = stretch.start - ordinal;
                const qreal end = stretch.end - ordinal;
                if (job.type == FreehandStrokeStrategy::Data::POINT) {
                    // a dab: at the place where its job starts
                    if (start <= 0.0 && end >= 0.0 && (end > 0.0 || start < 0.0 || stretch.start <= 0.0)) {
                        kept.clear();
                    }
                    continue;
                }
                QVector<Range> next;
                for (const Range &range : kept) {
                    if (end <= range.start || start >= range.end) {
                        next << range;
                        continue;
                    }
                    if (start > range.start) {
                        next << Range{range.start, start};
                    }
                    if (end < range.end) {
                        next << Range{end, range.end};
                    }
                }
                kept = next;
            }
            ranges[jobs[ordinal]] = kept;
        }
    }
    return piecesFromRanges(stroke, ranges);
}

KisBrushStrokeShapeEraser::KisBrushStrokeShapeEraser(KisShapeLayerSP layer, Mode mode)
    : m_layer(layer)
    , m_mode(mode)
{
}

KisBrushStrokeShapeEraser::~KisBrushStrokeShapeEraser()
{
    if (!m_finished) {
        cancel();
    }
}

void KisBrushStrokeShapeEraser::erase(const QPointF &from, const QPointF &to, qreal radius)
{
    if (!m_layer || m_finished) {
        return;
    }
    const QRectF reach = QRectF(from, to).normalized().adjusted(-radius, -radius, radius, radius);

    // strokes in groups keep their place; only the layer's own are cut
    Q_FOREACH (KoShape *candidate, m_layer->shapes()) {
        KisBrushStrokeShape *shape = dynamic_cast<KisBrushStrokeShape *>(candidate);
        if (!shape || !shape->brushStroke() || !shape->isVisible() || shape->isGeometryProtected()) {
            continue;
        }
        const QTransform toImage = shape->expectedTransform();
        if (!toImage.isInvertible() || !toImage.isAffine()) {
            continue;
        }
        const qreal scale = qSqrt(qAbs(toImage.determinant()));
        const qreal brush = shape->brushStroke()->preset && shape->brushStroke()->preset->settings()
            ? shape->brushStroke()->preset->settings()->paintOpSize()
            : 0.0;
        const qreal margin = brush * scale + 2.0;
        if (!toImage.mapRect(shape->recordRect()).adjusted(-margin, -margin, margin, margin).intersects(reach)) {
            continue;
        }

        const QTransform toRecord = toImage.inverted();
        const qreal recordRadius = scale > 0.0 ? radius / scale : radius;
        bool touched = false;
        QVector<KisRecordedBrushStrokeSP> pieces;
        if (m_mode == UpToIntersection) {
            // cheap test first: most lines are not touched
            cutStroke(shape->brushStroke(), toRecord.map(from), toRecord.map(to), recordRadius, &touched);
            if (touched) {
                pieces = cutStrokeUpToIntersections(shape->brushStroke(),
                                                    toRecord.map(from),
                                                    toRecord.map(to),
                                                    recordRadius,
                                                    otherLines(shape, toRecord),
                                                    &touched);
            }
        } else {
            pieces = cutStroke(shape->brushStroke(), toRecord.map(from), toRecord.map(to), recordRadius, &touched);
        }
        if (!touched) {
            continue;
        }
        if (m_mode == WholeLine) {
            pieces.clear();
        }

        // the pieces take the shape's place (made while it is in the layer)
        QList<KoShape *> replacements;
        for (const KisRecordedBrushStrokeSP &piece : pieces) {
            if (KoShape *replacement = shape->createPiece(piece)) {
                replacements << replacement;
            }
        }
        m_layer->removeShape(shape);
        m_lines.remove(shape);
        if (m_created.remove(shape)) {
            delete shape;
        } else {
            m_removed << shape;
        }
        Q_FOREACH (KoShape *replacement, replacements) {
            m_layer->addShape(replacement);
            m_created << replacement;
        }
    }
}

QVector<KisBrushStrokeShapeEraser::OtherLine> KisBrushStrokeShapeEraser::otherLines(const KisBrushStrokeShape *shape,
                                                                                    const QTransform &toRecord)
{
    const QRectF reach = shape->expectedTransform().mapRect(shape->recordRect());
    const qreal scale = qSqrt(qAbs(toRecord.determinant()));

    QVector<OtherLine> result;
    Q_FOREACH (KoShape *candidate, m_layer->shapes()) {
        KisBrushStrokeShape *other = dynamic_cast<KisBrushStrokeShape *>(candidate);
        if (!other || other == shape || !other->brushStroke() || !other->isVisible()) {
            continue;
        }
        // the lines of a stroke in image pixels, kept for the drag
        auto it = m_lines.find(other);
        if (it == m_lines.end()) {
            const QTransform toImage = other->expectedTransform();
            ImageLines lines;
            for (const QPolygonF &line : strokeLines(other->brushStroke())) {
                lines.lines << toImage.map(line);
                lines.bounds |= lines.lines.last().boundingRect();
            }
            const KisPaintOpPresetSP preset = other->brushStroke()->preset;
            lines.halfWidth = (preset && preset->settings() ? preset->settings()->paintOpSize() * 0.5 : 0.0)
                * qSqrt(qAbs(toImage.determinant()));
            it = m_lines.insert(other, lines);
        }
        const qreal margin = it->halfWidth + 2.0;
        if (!it->bounds.adjusted(-margin, -margin, margin, margin).intersects(reach)) {
            continue;
        }
        for (const QPolygonF &line : it->lines) {
            result << OtherLine{toRecord.map(line), it->halfWidth * scale};
        }
    }
    return result;
}

bool KisBrushStrokeShapeEraser::hasChanges() const
{
    return !m_removed.isEmpty() || !m_created.isEmpty();
}

KUndo2Command *KisBrushStrokeShapeEraser::finish()
{
    if (m_finished) {
        return nullptr;
    }
    m_finished = true;
    if (!hasChanges()) {
        return nullptr;
    }
    QList<KoShape *> added = m_created.values();
    std::sort(added.begin(), added.end(), KoShape::compareShapeZIndex);
    KUndo2Command *command = createReplaceCommand(m_layer, m_removed, added, kundo2_i18n("Erase Brush Strokes"));
    m_removed.clear();
    m_created.clear();
    return command;
}

void KisBrushStrokeShapeEraser::cancel()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    Q_FOREACH (KoShape *shape, m_created) {
        m_layer->removeShape(shape);
        delete shape;
    }
    Q_FOREACH (KoShape *shape, m_removed) {
        m_layer->addShape(shape);
    }
    m_created.clear();
    m_removed.clear();
}

KisBrushStrokeShapeEraser::Mode KisBrushStrokeShapeEraser::configuredMode()
{
    const KConfigGroup group = KSharedConfig::openConfig()->group(ConfigGroup);
    const int mode = group.readEntry(ConfigModeKey, int(TouchedPart));
    return mode == int(WholeLine) || mode == int(UpToIntersection) ? Mode(mode) : TouchedPart;
}

void KisBrushStrokeShapeEraser::setConfiguredMode(Mode mode)
{
    KConfigGroup group = KSharedConfig::openConfig()->group(ConfigGroup);
    group.writeEntry(ConfigModeKey, int(mode));
}
