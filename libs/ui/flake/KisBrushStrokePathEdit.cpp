/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisBrushStrokePathEdit.h"

#include <QDomDocument>
#include <QDomElement>
#include <QHash>
#include <QMap>
#include <QtMath>

#include <algorithm>
#include <limits>

#include <kundo2command.h>

#include <KoColor.h>
#include <KoShape.h>
#include <KoShapeContainer.h>
#include <kis_cubic_curve.h>
#include <kis_paintop_preset.h>
#include <kis_paintop_settings.h>
#include <strokes/freehand_stroke.h>

#include "KisBrushStrokeShape.h"
#include "KisBrushStrokeShapeEraser.h"
#include "kis_shape_layer.h"

namespace
{
using Job = KisRecordedBrushStroke::Job;
using Hit = KisBrushStrokePathEdit::Hit;

/// how many pieces a curve is measured in
const int CurveSteps = 16;

QPointF jobPosition(const Job &job, qreal t)
{
    switch (job.type) {
    case FreehandStrokeStrategy::Data::LINE:
        return job.pi1.pos() + (job.pi2.pos() - job.pi1.pos()) * t;
    case FreehandStrokeStrategy::Data::CURVE: {
        const qreal u = 1.0 - t;
        return u * u * u * job.pi1.pos() + 3 * u * u * t * job.control1 + 3 * u * t * t * job.control2
            + t * t * t * job.pi2.pos();
    }
    default:
        return job.pi1.pos();
    }
}

qreal distance(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

qreal jobLength(const Job &job)
{
    switch (job.type) {
    case FreehandStrokeStrategy::Data::LINE:
        return distance(job.pi1.pos(), job.pi2.pos());
    case FreehandStrokeStrategy::Data::CURVE: {
        qreal length = 0.0;
        QPointF previous = job.pi1.pos();
        for (int i = 1; i <= CurveSteps; i++) {
            const QPointF next = jobPosition(job, qreal(i) / CurveSteps);
            length += distance(previous, next);
            previous = next;
        }
        return length;
    }
    default:
        return 0.0;
    }
}

/// The line a job belongs to: the jobs of its copy of the stroke, with the
/// distance along the line at which each starts
struct Line {
    QVector<int> jobs;
    QVector<qreal> starts;
    QVector<qreal> lengths;
};

Line lineOf(const KisRecordedBrushStroke &stroke, int jobIndex)
{
    Line line;
    if (jobIndex < 0 || jobIndex >= stroke.jobs.size()) {
        return line;
    }
    const int id = stroke.jobs[jobIndex].strokeInfoId;
    qreal position = 0.0;
    for (int i = 0; i < stroke.jobs.size(); i++) {
        if (stroke.jobs[i].strokeInfoId != id) {
            continue;
        }
        const qreal length = jobLength(stroke.jobs[i]);
        line.jobs << i;
        line.starts << position;
        line.lengths << length;
        position += length;
    }
    return line;
}

/**
 * How much of an edit at the distance @p anchor along the line reaches the
 * distance @p position: 1 at the anchor, falling smoothly to 0 at @p reach.
 * Without a reach, only the control point at @p nearest takes it.
 */
qreal weight(qreal position, qreal anchor, qreal reach, qreal nearest)
{
    if (reach <= 0.0) {
        return qAbs(position - nearest) < 1e-6 ? 1.0 : 0.0;
    }
    const qreal far = qAbs(position - anchor) / reach;
    return far >= 1.0 ? 0.0 : 0.5 * (1.0 + qCos(M_PI * far));
}

/// Calls @p change for every control point of the line around @p anchor
/// with its weight; returns the changed stroke
template<typename Change>
KisRecordedBrushStrokeSP changePoints(KisRecordedBrushStrokeSP stroke, const Hit &anchor, qreal reach, Change change)
{
    if (!stroke || !anchor.valid) {
        return stroke;
    }
    const Line line = lineOf(*stroke, anchor.jobIndex);
    const int ordinal = line.jobs.indexOf(anchor.jobIndex);
    if (ordinal < 0) {
        return stroke;
    }
    const qreal anchorPosition = line.starts[ordinal] + qBound(0.0, anchor.t, 1.0) * line.lengths[ordinal];
    // the control point nearest to the anchor: an end of its job
    const qreal nearest = line.starts[ordinal] + (anchor.t < 0.5 ? 0.0 : line.lengths[ordinal]);

    QSharedPointer<KisRecordedBrushStroke> changed(new KisRecordedBrushStroke(*stroke));
    for (int i = 0; i < line.jobs.size(); i++) {
        Job &job = changed->jobs[line.jobs[i]];
        const qreal start = line.starts[i];
        const qreal length = line.lengths[i];
        // 0: the start point, 1 and 2: a curve's handles, 3: the end point
        change(job, 0, weight(start, anchorPosition, reach, nearest));
        if (job.type == FreehandStrokeStrategy::Data::POINT) {
            continue;
        }
        if (job.type == FreehandStrokeStrategy::Data::CURVE) {
            // the handles go with the points they belong to
            change(job, 1, weight(start, anchorPosition, reach, nearest));
            change(job, 2, weight(start + length, anchorPosition, reach, nearest));
        }
        change(job, 3, weight(start + length, anchorPosition, reach, nearest));
    }
    return changed;
}

// --- simplifying -----------------------------------------------------------

/// how far apart a line's path is sampled at most, recorded pixels; a
/// tolerance below it is sampled as finely as it asks, down to a quarter
const qreal SampleSpacing = 2.0;
const int MaximumJobSamples = 256;
/// a turn at a control point sharper than this (60 degrees) stays a corner
const qreal CornerCosine = 0.5;
/// the pen pressure counts as the width of a brush at least this wide
const qreal MinimumPressureWidth = 20.0;

/// A place on a line's path that the simplified path is measured against
struct Sample {
    QPointF position;
    qreal pressure = 0.0;
    /// the distance along the run
    qreal along = 0.0;
    /// the job (its number in the run) and the parameter in it
    int ordinal = 0;
    qreal t = 0.0;
};

QPointF normalized(const QPointF &vector)
{
    const qreal length = std::hypot(vector.x(), vector.y());
    return length > 0.0 ? vector / length : QPointF();
}

/// A job of the simplified stroke, with the place in its line where it
/// ends: the number of the line's job plus the parameter in it. Mirrored
/// copies are drawn job by job in turns; sorting by the place keeps that.
struct OrderedJob {
    Job job;
    qreal order = 0.0;
};

/**
 * Fits cubic curves to a run of samples, after Schneider ("An Algorithm for
 * Automatically Fitting Digitized Curves", Graphics Gems, 1990): a least
 * squares fit with the end tangents kept, improved by moving the samples'
 * parameters, and split at the worst sample while it is too far. The pen
 * pressure, which a curve changes evenly from end to end, is part of the
 * distance.
 */
class CurveFit
{
public:
    struct Curve {
        int first = 0;
        int last = 0;
        QPointF control1;
        QPointF control2;
    };

    CurveFit(const QVector<Sample> &samples, qreal tolerance, qreal pressureScale)
        : m_samples(samples)
        , m_tolerance(tolerance)
        , m_pressureScale(pressureScale)
    {
    }

    /// Fits the samples @p first to @p last; the tangents point into the
    /// section at both ends
    void fit(int first, int last, const QPointF &tangent1, const QPointF &tangent2)
    {
        const QPointF start = m_samples[first].position;
        const QPointF end = m_samples[last].position;
        if (last - first < 2) {
            const qreal third = distance(start, end) / 3.0;
            m_curves << Curve{first, last, start + tangent1 * third, end + tangent2 * third};
            return;
        }

        // the parameters: the share of the path walked
        QVector<qreal> parameters(last - first + 1);
        const qreal length = m_samples[last].along - m_samples[first].along;
        for (int i = first; i <= last; i++) {
            parameters[i - first] = length > 0.0 ? (m_samples[i].along - m_samples[first].along) / length : 0.0;
        }
        parameters.last() = 1.0;

        Curve curve = generate(first, last, parameters, tangent1, tangent2);
        int worst = first + 1;
        qreal error = maximumError(curve, parameters, &worst);
        if (error > m_tolerance && error < m_tolerance * 2.0) {
            for (int round = 0; round < 4 && error > m_tolerance; round++) {
                reparameterize(curve, &parameters);
                curve = generate(first, last, parameters, tangent1, tangent2);
                error = maximumError(curve, parameters, &worst);
            }
        }
        if (error <= m_tolerance) {
            m_curves << curve;
            return;
        }

        QPointF center = normalized(m_samples[worst - 1].position - m_samples[worst + 1].position);
        if (center.isNull()) {
            center = normalized(m_samples[worst - 1].position - m_samples[worst].position);
        }
        fit(first, worst, tangent1, center);
        fit(worst, last, -center, tangent2);
    }

    const QVector<Curve> &curves() const
    {
        return m_curves;
    }

private:
    static QPointF pointAt(const QPointF points[4], qreal t)
    {
        const qreal u = 1.0 - t;
        return u * u * u * points[0] + 3 * u * u * t * points[1] + 3 * u * t * t * points[2] + t * t * t * points[3];
    }

    Curve generate(int first,
                   int last,
                   const QVector<qreal> &parameters,
                   const QPointF &tangent1,
                   const QPointF &tangent2) const
    {
        const QPointF start = m_samples[first].position;
        const QPointF end = m_samples[last].position;
        qreal c00 = 0.0;
        qreal c01 = 0.0;
        qreal c11 = 0.0;
        qreal x0 = 0.0;
        qreal x1 = 0.0;
        for (int i = first; i <= last; i++) {
            const qreal t = parameters[i - first];
            const qreal u = 1.0 - t;
            const QPointF a0 = tangent1 * (3 * u * u * t);
            const QPointF a1 = tangent2 * (3 * u * t * t);
            c00 += QPointF::dotProduct(a0, a0);
            c01 += QPointF::dotProduct(a0, a1);
            c11 += QPointF::dotProduct(a1, a1);
            const QPointF rest =
                m_samples[i].position - (start * (u * u * u + 3 * u * u * t) + end * (3 * u * t * t + t * t * t));
            x0 += QPointF::dotProduct(a0, rest);
            x1 += QPointF::dotProduct(a1, rest);
        }
        const qreal determinant = c00 * c11 - c01 * c01;
        const qreal alpha1 = determinant != 0.0 ? (x0 * c11 - x1 * c01) / determinant : 0.0;
        const qreal alpha2 = determinant != 0.0 ? (c00 * x1 - c01 * x0) / determinant : 0.0;
        const qreal chord = distance(start, end);
        const qreal epsilon = 1e-6 * chord;
        if (alpha1 < epsilon || alpha2 < epsilon) {
            // no good fit: handles a third of the way, as for two points
            return Curve{first, last, start + tangent1 * (chord / 3.0), end + tangent2 * (chord / 3.0)};
        }
        return Curve{first, last, start + tangent1 * alpha1, end + tangent2 * alpha2};
    }

    /// How far the samples are from @p curve at their parameters, with the
    /// pen pressure as part of it; @p worst gets the farthest inner sample
    qreal maximumError(const Curve &curve, const QVector<qreal> &parameters, int *worst) const
    {
        const QPointF points[4] = {m_samples[curve.first].position,
                                   curve.control1,
                                   curve.control2,
                                   m_samples[curve.last].position};
        const qreal pressure1 = m_samples[curve.first].pressure;
        const qreal pressure2 = m_samples[curve.last].pressure;
        qreal maximum = 0.0;
        for (int i = curve.first + 1; i < curve.last; i++) {
            const qreal t = parameters[i - curve.first];
            const qreal place = distance(pointAt(points, t), m_samples[i].position);
            const qreal pressure =
                qAbs(m_samples[i].pressure - (pressure1 + (pressure2 - pressure1) * t)) * m_pressureScale;
            const qreal error = qMax(place, pressure);
            if (error >= maximum) {
                maximum = error;
                *worst = i;
            }
        }
        return maximum;
    }

    /// One Newton step for each sample's parameter towards the place on
    /// @p curve nearest to the sample
    void reparameterize(const Curve &curve, QVector<qreal> *parameters) const
    {
        const QPointF points[4] = {m_samples[curve.first].position,
                                   curve.control1,
                                   curve.control2,
                                   m_samples[curve.last].position};
        const QPointF first[3] = {(points[1] - points[0]) * 3.0,
                                  (points[2] - points[1]) * 3.0,
                                  (points[3] - points[2]) * 3.0};
        const QPointF second[2] = {(first[1] - first[0]) * 2.0, (first[2] - first[1]) * 2.0};
        for (int i = curve.first + 1; i < curve.last; i++) {
            qreal &t = (*parameters)[i - curve.first];
            const qreal u = 1.0 - t;
            const QPointF offset = pointAt(points, t) - m_samples[i].position;
            const QPointF speed = u * u * first[0] + 2 * u * t * first[1] + t * t * first[2];
            const QPointF bend = u * second[0] + t * second[1];
            const qreal denominator = QPointF::dotProduct(speed, speed) + QPointF::dotProduct(offset, bend);
            if (denominator != 0.0) {
                t = qBound(0.0, t - QPointF::dotProduct(offset, speed) / denominator, 1.0);
            }
        }
    }

    const QVector<Sample> &m_samples;
    const qreal m_tolerance;
    const qreal m_pressureScale;
    QVector<Curve> m_curves;
};

/// The pen's values at a sample of @p run: of the job that ends there, or
/// with @p outgoing of the job that starts there
KisPaintInformation
sampleInformation(const KisRecordedBrushStroke &stroke, const QVector<int> &run, const Sample &sample, bool outgoing)
{
    const Job &job = stroke.jobs[run[sample.ordinal]];
    if (sample.t <= 0.0) {
        return job.pi1;
    }
    if (sample.t >= 1.0) {
        return outgoing && sample.ordinal + 1 < run.size() ? stroke.jobs[run[sample.ordinal + 1]].pi1 : job.pi2;
    }
    return KisPaintInformation::mix(sample.position, sample.t, job.pi1, job.pi2);
}

/// The sample that lies @p far along the run from the sample @p from,
/// backwards or forwards
int sampleAway(const QVector<Sample> &samples, int from, qreal far, bool forwards)
{
    int index = from;
    while (forwards ? index + 1 < samples.size() : index > 0) {
        index += forwards ? 1 : -1;
        if (qAbs(samples[index].along - samples[from].along) >= far) {
            break;
        }
    }
    return index;
}

/// The path of the connected jobs @p run, sampled every @p spacing; @p
/// controlPoints gets the samples that are the control points between jobs
QVector<Sample>
sampleRun(const KisRecordedBrushStroke &stroke, const QVector<int> &run, qreal spacing, QVector<int> *controlPoints)
{
    QVector<Sample> samples;
    {
        Sample first;
        first.position = stroke.jobs[run.first()].pi1.pos();
        first.pressure = stroke.jobs[run.first()].pi1.pressure();
        samples << first;
    }
    for (int ordinal = 0; ordinal < run.size(); ordinal++) {
        const Job &job = stroke.jobs[run[ordinal]];
        const int steps = qBound(1, qCeil(jobLength(job) / spacing), MaximumJobSamples);
        for (int step = 1; step <= steps; step++) {
            Sample sample;
            sample.ordinal = ordinal;
            sample.t = qreal(step) / steps;
            sample.position = jobPosition(job, sample.t);
            sample.pressure = job.pi1.pressure() + (job.pi2.pressure() - job.pi1.pressure()) * sample.t;
            const qreal far = distance(samples.last().position, sample.position);
            if (far < 1e-9) {
                // nothing to measure; the place belongs to the later job
                sample.along = samples.last().along;
                samples.last() = sample;
            } else {
                sample.along = samples.last().along + far;
                samples << sample;
            }
        }
        if (controlPoints && ordinal + 1 < run.size() && samples.size() > 1
            && controlPoints->value(controlPoints->size() - 1, -1) != samples.size() - 1) {
            *controlPoints << samples.size() - 1;
        }
    }
    return samples;
}

/// A control point of a line: the place between two of its LINE and CURVE
/// jobs, or one of its ends
struct ControlPoint {
    bool valid = false;
    /// the line's LINE and CURVE jobs, by their numbers in the stroke
    QVector<int> path;
    /// the point lies before the job path[boundary]; path.size() is the
    /// line's end
    int boundary = 0;
};

ControlPoint controlPointAt(const KisRecordedBrushStroke &stroke, const Hit &place)
{
    ControlPoint point;
    if (!place.valid || place.jobIndex < 0 || place.jobIndex >= stroke.jobs.size()) {
        return point;
    }
    const int id = stroke.jobs[place.jobIndex].strokeInfoId;
    int ordinal = -1;
    bool after = place.t >= 0.5;
    for (int i = 0; i < stroke.jobs.size(); i++) {
        const Job &job = stroke.jobs[i];
        if (job.strokeInfoId != id) {
            continue;
        }
        const bool onPath =
            job.type == FreehandStrokeStrategy::Data::LINE || job.type == FreehandStrokeStrategy::Data::CURVE;
        if (i == place.jobIndex && !onPath) {
            // a dab of its own (the line's starting point): the control
            // point where the path goes on, or where it ended
            ordinal = point.path.size();
            after = false;
        }
        if (onPath) {
            if (i == place.jobIndex) {
                ordinal = point.path.size();
            }
            point.path << i;
        }
    }
    if (ordinal < 0 || point.path.isEmpty()) {
        return point;
    }
    point.boundary = qMin(ordinal + (after ? 1 : 0), point.path.size());
    point.valid = true;
    return point;
}

QPointF lerp(const QPointF &a, const QPointF &b, qreal t)
{
    return a + (b - a) * t;
}

/**
 * Fits the connected jobs @p run (LINE and CURVE jobs of one line, by their
 * numbers in @p stroke) again and adds the result to @p result: fewer
 * curves, or the jobs as they are if that gains nothing. @p firstInLine is
 * the number of the run's first job in its line.
 */
void simplifyRun(const KisRecordedBrushStroke &stroke,
                 const QVector<int> &run,
                 int firstInLine,
                 qreal tolerance,
                 qreal pressureScale,
                 QVector<OrderedJob> *result)
{
    auto keep = [&]() {
        for (int ordinal = 0; ordinal < run.size(); ordinal++) {
            *result << OrderedJob{stroke.jobs[run[ordinal]], firstInLine + ordinal + 1.0};
        }
    };
    if (run.size() < 2) {
        keep();
        return;
    }

    // the path, sampled; the control points between jobs are samples
    QVector<int> controlPoints;
    const QVector<Sample> samples = sampleRun(stroke, run, qBound(0.25, tolerance, SampleSpacing), &controlPoints);
    if (samples.size() < 3) {
        keep();
        return;
    }

    // corners: control points where the path turns sharply, measured over
    // a stretch that hides what is smaller than the tolerance; of
    // neighbours that close, the sharpest
    const qreal stretch = qMax(2.0, tolerance * 2.0);
    QVector<int> corners;
    qreal cornerCosine = 1.0;
    for (int index : controlPoints) {
        const QPointF point = samples[index].position;
        const QPointF in = normalized(point - samples[sampleAway(samples, index, stretch, false)].position);
        const QPointF out = normalized(samples[sampleAway(samples, index, stretch, true)].position - point);
        if (in.isNull() || out.isNull()) {
            continue;
        }
        const qreal cosine = QPointF::dotProduct(in, out);
        if (cosine >= CornerCosine) {
            continue;
        }
        if (!corners.isEmpty() && samples[index].along - samples[corners.last()].along < stretch) {
            if (cosine < cornerCosine) {
                corners.last() = index;
                cornerCosine = cosine;
            }
            continue;
        }
        corners << index;
        cornerCosine = cosine;
    }

    CurveFit fit(samples, tolerance, pressureScale);
    int start = 0;
    corners << samples.size() - 1;
    for (int end : corners) {
        if (end <= start) {
            continue;
        }
        fit.fit(start,
                end,
                normalized(samples[start + 1].position - samples[start].position),
                normalized(samples[end - 1].position - samples[end].position));
        start = end;
    }
    if (fit.curves().size() >= run.size()) {
        keep();
        return;
    }

    for (const CurveFit::Curve &curve : fit.curves()) {
        OrderedJob made;
        made.job.type = FreehandStrokeStrategy::Data::CURVE;
        made.job.strokeInfoId = stroke.jobs[run.first()].strokeInfoId;
        made.job.pi1 = sampleInformation(stroke, run, samples[curve.first], true);
        made.job.pi2 = sampleInformation(stroke, run, samples[curve.last], false);
        made.job.control1 = curve.control1;
        made.job.control2 = curve.control2;
        made.order = firstInLine + samples[curve.last].ordinal + samples[curve.last].t;
        *result << made;
    }
}

// --- corners ---------------------------------------------------------------

/// a control point where the path turns by less than this (10 degrees) is
/// a curve passing through, if a handle shapes it
const qreal SmoothCosine = 0.985;
/// a handle nearer to its point than this is none
const qreal HandleLength = 1e-3;

bool hasHandleAtEnd(const Job &job)
{
    return job.type == FreehandStrokeStrategy::Data::CURVE && distance(job.control2, job.pi2.pos()) > HandleLength;
}

bool hasHandleAtStart(const Job &job)
{
    return job.type == FreehandStrokeStrategy::Data::CURVE && distance(job.control1, job.pi1.pos()) > HandleLength;
}

/// The direction in which the path of @p job arrives at its end
QPointF directionIn(const Job &job)
{
    if (hasHandleAtEnd(job)) {
        return normalized(job.pi2.pos() - job.control2);
    }
    if (hasHandleAtStart(job)) {
        return normalized(job.pi2.pos() - job.control1);
    }
    return normalized(job.pi2.pos() - job.pi1.pos());
}

/// The direction in which the path of @p job leaves its start
QPointF directionOut(const Job &job)
{
    if (hasHandleAtStart(job)) {
        return normalized(job.control1 - job.pi1.pos());
    }
    if (hasHandleAtEnd(job)) {
        return normalized(job.control2 - job.pi1.pos());
    }
    return normalized(job.pi2.pos() - job.pi1.pos());
}

/// Whether the path has a corner where @p before ends and @p after starts
bool isCornerBetween(const Job &before, const Job &after)
{
    if (!hasHandleAtEnd(before) && !hasHandleAtStart(after)) {
        return true;
    }
    const QPointF in = directionIn(before);
    const QPointF out = directionOut(after);
    return in.isNull() || out.isNull() || QPointF::dotProduct(in, out) < SmoothCosine;
}

/// Whether @p handle lies on the straight line between the ends of @p job
bool isOnChord(const Job &job, const QPointF &handle)
{
    const QPointF start = job.pi1.pos();
    const QPointF chord = job.pi2.pos() - start;
    const qreal lengthSquared = QPointF::dotProduct(chord, chord);
    if (lengthSquared <= 0.0) {
        return distance(handle, start) <= HandleLength;
    }
    const qreal along = QPointF::dotProduct(handle - start, chord) / lengthSquared;
    return along > -1e-6 && along < 1.0 + 1e-6 && distance(start + chord * along, handle) <= HandleLength;
}

/// A control point between two connected jobs of a line
struct Joint {
    bool valid = false;
    int before = -1;
    int after = -1;
};

Joint jointAt(const KisRecordedBrushStroke &stroke, const Hit &place)
{
    Joint joint;
    const ControlPoint point = controlPointAt(stroke, place);
    if (!point.valid || point.boundary <= 0 || point.boundary >= point.path.size()) {
        return joint;
    }
    joint.before = point.path[point.boundary - 1];
    joint.after = point.path[point.boundary];
    joint.valid = distance(stroke.jobs[joint.before].pi2.pos(), stroke.jobs[joint.after].pi1.pos()) <= 1e-3;
    return joint;
}
} // namespace

KisRecordedBrushStrokeSP KisBrushStrokePathEdit::simplified(KisRecordedBrushStrokeSP stroke, qreal tolerance)
{
    if (!stroke || tolerance <= 0.0 || stroke->jobs.size() < 2) {
        return stroke;
    }
    const qreal brushSize =
        stroke->preset && stroke->preset->settings() ? stroke->preset->settings()->paintOpSize() : 0.0;
    const qreal pressureScale = qMax(brushSize, MinimumPressureWidth) / 2.0;

    // the lines (mirrored copies), each as runs of connected jobs
    QMap<int, QVector<int>> lines;
    for (int i = 0; i < stroke->jobs.size(); i++) {
        lines[stroke->jobs[i].strokeInfoId] << i;
    }
    QVector<OrderedJob> result;
    for (const QVector<int> &line : std::as_const(lines)) {
        QVector<int> run;
        int runStart = 0;
        auto finishRun = [&]() {
            if (!run.isEmpty()) {
                simplifyRun(*stroke, run, runStart, tolerance, pressureScale, &result);
                run.clear();
            }
        };
        for (int inLine = 0; inLine < line.size(); inLine++) {
            const Job &job = stroke->jobs[line[inLine]];
            if (job.type != FreehandStrokeStrategy::Data::LINE && job.type != FreehandStrokeStrategy::Data::CURVE) {
                finishRun();
                result << OrderedJob{job, inLine + 1.0};
                continue;
            }
            if (!run.isEmpty() && distance(stroke->jobs[run.last()].pi2.pos(), job.pi1.pos()) > 1e-3) {
                finishRun();
            }
            if (run.isEmpty()) {
                runStart = inLine;
            }
            run << line[inLine];
        }
        finishRun();
    }
    if (result.size() >= stroke->jobs.size()) {
        return stroke;
    }

    // the copies' jobs in turns, as they were drawn (the lines were
    // gathered in the order of their numbers, which a stable sort keeps)
    std::stable_sort(result.begin(), result.end(), [](const OrderedJob &a, const OrderedJob &b) {
        return a.order < b.order;
    });
    QSharedPointer<KisRecordedBrushStroke> changed(new KisRecordedBrushStroke(*stroke));
    changed->jobs.clear();
    changed->jobs.reserve(result.size());
    for (const OrderedJob &made : std::as_const(result)) {
        changed->jobs << made.job;
    }
    return changed;
}

KisRecordedBrushStrokeSP
KisBrushStrokePathEdit::insertPoint(KisRecordedBrushStrokeSP stroke, const Hit &place, Hit *added)
{
    if (added) {
        *added = Hit();
    }
    if (!stroke || !place.valid || place.jobIndex < 0 || place.jobIndex >= stroke->jobs.size()) {
        return stroke;
    }
    const Job &job = stroke->jobs[place.jobIndex];
    const bool onPath =
        job.type == FreehandStrokeStrategy::Data::LINE || job.type == FreehandStrokeStrategy::Data::CURVE;
    const qreal t = qBound(0.0, place.t, 1.0);
    if (!onPath || t < 1e-3 || t > 1.0 - 1e-3) {
        // a control point is there already
        if (added) {
            *added = nearestControlPoint(stroke, place);
        }
        return stroke;
    }

    Job first = job;
    Job second = job;
    const KisPaintInformation middle = KisPaintInformation::mix(jobPosition(job, t), t, job.pi1, job.pi2);
    first.pi2 = middle;
    second.pi1 = middle;
    if (job.type == FreehandStrokeStrategy::Data::CURVE) {
        // de Casteljau: the two curves are the same path
        const QPointF a = lerp(job.pi1.pos(), job.control1, t);
        const QPointF b = lerp(job.control1, job.control2, t);
        const QPointF c = lerp(job.control2, job.pi2.pos(), t);
        const QPointF d = lerp(a, b, t);
        const QPointF e = lerp(b, c, t);
        first.control1 = a;
        first.control2 = d;
        second.control1 = e;
        second.control2 = c;
    }
    QSharedPointer<KisRecordedBrushStroke> changed(new KisRecordedBrushStroke(*stroke));
    changed->jobs[place.jobIndex] = first;
    changed->jobs.insert(place.jobIndex + 1, second);
    if (added) {
        added->valid = true;
        added->jobIndex = place.jobIndex + 1;
        added->t = 0.0;
        added->position = middle.pos();
    }
    return changed;
}

KisBrushStrokePathEdit::Hit KisBrushStrokePathEdit::nearestControlPoint(KisRecordedBrushStrokeSP stroke,
                                                                        const Hit &place)
{
    Hit hit;
    if (!stroke) {
        return hit;
    }
    const ControlPoint point = controlPointAt(*stroke, place);
    if (!point.valid) {
        return hit;
    }
    hit.valid = true;
    if (point.boundary < point.path.size()) {
        hit.jobIndex = point.path[point.boundary];
        hit.t = 0.0;
        hit.position = stroke->jobs[hit.jobIndex].pi1.pos();
    } else {
        hit.jobIndex = point.path.last();
        hit.t = 1.0;
        hit.position = stroke->jobs[hit.jobIndex].pi2.pos();
    }
    return hit;
}

KisRecordedBrushStrokeSP KisBrushStrokePathEdit::deletePoint(KisRecordedBrushStrokeSP stroke, const Hit &place)
{
    if (!stroke) {
        return stroke;
    }
    const ControlPoint point = controlPointAt(*stroke, place);
    if (!point.valid || point.path.size() < 2) {
        return stroke;
    }
    QSharedPointer<KisRecordedBrushStroke> changed(new KisRecordedBrushStroke(*stroke));

    if (point.boundary == 0) {
        // the line starts a job later; the dab at its start goes along
        const int removed = point.path.first();
        const KisPaintInformation start = stroke->jobs[point.path[1]].pi1;
        for (int i = 0; i < removed; i++) {
            Job &job = changed->jobs[i];
            if (job.strokeInfoId == stroke->jobs[removed].strokeInfoId
                && job.type == FreehandStrokeStrategy::Data::POINT
                && distance(job.pi1.pos(), stroke->jobs[removed].pi1.pos()) < 1e-3) {
                job.pi1 = start;
            }
        }
        changed->jobs.remove(removed);
        return changed;
    }
    if (point.boundary == point.path.size()) {
        changed->jobs.remove(point.path.last());
        return changed;
    }

    // one curve in place of the two jobs that met in the point
    const int before = point.path[point.boundary - 1];
    const int after = point.path[point.boundary];
    const QVector<Sample> samples = sampleRun(*stroke, {before, after}, 1.0, nullptr);
    Job merged = stroke->jobs[before];
    merged.type = FreehandStrokeStrategy::Data::CURVE;
    merged.pi2 = stroke->jobs[after].pi2;
    if (samples.size() >= 2) {
        // the least squares fit, however far it is
        CurveFit fit(samples, std::numeric_limits<qreal>::max(), 0.0);
        fit.fit(0,
                samples.size() - 1,
                normalized(samples[1].position - samples[0].position),
                normalized(samples[samples.size() - 2].position - samples.last().position));
        merged.control1 = fit.curves().first().control1;
        merged.control2 = fit.curves().first().control2;
    } else {
        merged.control1 = merged.pi1.pos();
        merged.control2 = merged.pi2.pos();
    }
    changed->jobs[before] = merged;
    changed->jobs.remove(after);
    return changed;
}

bool KisBrushStrokePathEdit::isCorner(KisRecordedBrushStrokeSP stroke, const Hit &place)
{
    if (!stroke) {
        return false;
    }
    const Joint joint = jointAt(*stroke, place);
    return joint.valid && isCornerBetween(stroke->jobs[joint.before], stroke->jobs[joint.after]);
}

KisRecordedBrushStrokeSP KisBrushStrokePathEdit::switchCorner(KisRecordedBrushStrokeSP stroke, const Hit &place)
{
    if (!stroke) {
        return stroke;
    }
    const Joint joint = jointAt(*stroke, place);
    if (!joint.valid) {
        return stroke;
    }
    QSharedPointer<KisRecordedBrushStroke> changed(new KisRecordedBrushStroke(*stroke));
    Job &before = changed->jobs[joint.before];
    Job &after = changed->jobs[joint.after];
    const QPointF point = before.pi2.pos();

    if (!isCornerBetween(before, after)) {
        // a corner: the handles at the point go; what is left of the two
        // jobs' shape comes from the handles at their other ends
        if (before.type == FreehandStrokeStrategy::Data::CURVE) {
            before.control2 = point;
        }
        if (after.type == FreehandStrokeStrategy::Data::CURVE) {
            after.control1 = after.pi1.pos();
        }
        for (Job *job : {&before, &after}) {
            // a straight path is a line again (a line that was made a
            // curve on the way to a curve point comes back as it was)
            if (job->type == FreehandStrokeStrategy::Data::CURVE && isOnChord(*job, job->control1)
                && isOnChord(*job, job->control2)) {
                job->type = FreehandStrokeStrategy::Data::LINE;
            }
        }
        return changed;
    }

    // a curve through the point, in the direction from the control point
    // before it to the one after it
    const QPointF previous = before.pi1.pos();
    const QPointF next = after.pi2.pos();
    QPointF direction = normalized(next - previous);
    if (direction.isNull()) {
        // the line comes back to where it was: it turns across
        const QPointF there = normalized(point - previous);
        direction = QPointF(-there.y(), there.x());
    }
    if (direction.isNull()) {
        return stroke;
    }
    if (before.type == FreehandStrokeStrategy::Data::LINE) {
        // leaves its start as the line did
        before.type = FreehandStrokeStrategy::Data::CURVE;
        before.control1 = lerp(previous, point, 1.0 / 3.0);
    }
    if (after.type == FreehandStrokeStrategy::Data::LINE) {
        after.type = FreehandStrokeStrategy::Data::CURVE;
        after.control2 = lerp(next, after.pi1.pos(), 1.0 / 3.0);
    }
    before.control2 = point - direction * (distance(previous, point) / 3.0);
    after.control1 = after.pi1.pos() + direction * (distance(after.pi1.pos(), next) / 3.0);
    return changed;
}

bool KisBrushStrokePathEdit::canJoin(KisRecordedBrushStrokeSP stroke)
{
    if (!stroke || stroke->jobs.isEmpty()) {
        return false;
    }
    bool hasPath = false;
    for (const Job &job : stroke->jobs) {
        if (job.strokeInfoId != stroke->jobs.first().strokeInfoId) {
            return false;
        }
        hasPath |= job.type == FreehandStrokeStrategy::Data::LINE || job.type == FreehandStrokeStrategy::Data::CURVE;
    }
    return hasPath;
}

KisRecordedBrushStrokeSP KisBrushStrokePathEdit::joined(const QVector<JoinPart> &parts)
{
    if (parts.isEmpty() || !canJoin(parts.first().stroke)) {
        return KisRecordedBrushStrokeSP();
    }
    const KisRecordedBrushStrokeSP base = parts.first().stroke;
    const int id = base->jobs.first().strokeInfoId;

    // the lines' paths in the first stroke's pixels; the dabs of their own
    // (a line's starting point) stay only at the start of the first
    struct Chain {
        QVector<Job> jobs;
        bool startsWithBase = false;
    };
    QVector<Chain> chains;
    QVector<Job> baseDabs;
    for (int i = 0; i < parts.size(); i++) {
        if (!canJoin(parts[i].stroke)) {
            continue;
        }
        Chain chain;
        chain.startsWithBase = i == 0;
        for (Job job : parts[i].stroke->jobs) {
            const bool onPath =
                job.type == FreehandStrokeStrategy::Data::LINE || job.type == FreehandStrokeStrategy::Data::CURVE;
            if (!onPath) {
                if (i == 0 && chain.jobs.isEmpty()) {
                    baseDabs << job;
                }
                continue;
            }
            job.strokeInfoId = id;
            if (i > 0) {
                const QTransform &toFirst = parts[i].toFirst;
                job.pi1.setPos(toFirst.map(job.pi1.pos()));
                job.pi2.setPos(toFirst.map(job.pi2.pos()));
                job.control1 = toFirst.map(job.control1);
                job.control2 = toFirst.map(job.control2);
            }
            chain.jobs << job;
        }
        chains << chain;
    }
    if (chains.size() < 2) {
        return KisRecordedBrushStrokeSP();
    }

    auto turnedAround = [](QVector<Job> jobs) {
        std::reverse(jobs.begin(), jobs.end());
        for (Job &job : jobs) {
            std::swap(job.pi1, job.pi2);
            std::swap(job.control1, job.control2);
        }
        return jobs;
    };

    while (chains.size() > 1) {
        // the nearest two ends of different lines. Of a pair, the earlier
        // line keeps its direction: 0 its end to the other's start, 1 the
        // other's end to its start, 2 end to end, 3 start to start
        int bestA = 0;
        int bestB = 1;
        int bestWay = 0;
        qreal bestDistance = -1.0;
        for (int a = 0; a < chains.size(); a++) {
            for (int b = a + 1; b < chains.size(); b++) {
                const QPointF aStart = chains[a].jobs.first().pi1.pos();
                const QPointF aEnd = chains[a].jobs.last().pi2.pos();
                const QPointF bStart = chains[b].jobs.first().pi1.pos();
                const QPointF bEnd = chains[b].jobs.last().pi2.pos();
                const qreal ways[4] = {distance(aEnd, bStart),
                                       distance(bEnd, aStart),
                                       distance(aEnd, bEnd),
                                       distance(aStart, bStart)};
                for (int way = 0; way < 4; way++) {
                    if (bestDistance < 0.0 || ways[way] < bestDistance) {
                        bestDistance = ways[way];
                        bestA = a;
                        bestB = b;
                        bestWay = way;
                    }
                }
            }
        }

        Chain &kept = chains[bestA];
        QVector<Job> first = kept.jobs;
        QVector<Job> second = chains[bestB].jobs;
        if (bestWay == 1) {
            first.swap(second);
        } else if (bestWay == 2) {
            second = turnedAround(second);
        } else if (bestWay == 3) {
            second = turnedAround(second);
            first.swap(second);
        }
        // only a line that still begins the result begins with the base
        kept.startsWithBase = kept.startsWithBase && (bestWay == 0 || bestWay == 2);
        if (bestDistance > 1e-3) {
            // the connection: straight, with the pen's values of both ends
            Job bridge;
            bridge.type = FreehandStrokeStrategy::Data::LINE;
            bridge.strokeInfoId = id;
            bridge.pi1 = first.last().pi2;
            bridge.pi2 = second.first().pi1;
            // it takes no time (see below)
            bridge.pi2.setCurrentTime(bridge.pi1.currentTime());
            first << bridge;
        }
        kept.jobs = first + second;
        chains.remove(bestB);
    }

    // the pen's time runs on from job to job, whichever line and direction
    // a job came from
    QVector<Job> &jobs = chains.first().jobs;
    qreal clock = jobs.first().pi1.currentTime();
    for (Job &job : jobs) {
        const qreal duration = qAbs(job.pi2.currentTime() - job.pi1.currentTime());
        job.pi1.setCurrentTime(clock);
        clock += duration;
        job.pi2.setCurrentTime(clock);
    }

    QSharedPointer<KisRecordedBrushStroke> result(new KisRecordedBrushStroke(*base));
    if (chains.first().startsWithBase) {
        result->jobs = baseDabs + jobs;
    } else {
        result->jobs = jobs;
        if (id >= 0 && id < result->starts.size()) {
            // another start: the dab spacing starts anew, as at a cut
            result->starts[id].hasLastDab = false;
            result->starts[id].dabSeqNo = 0;
        }
    }
    return result;
}

QVector<KisBrushStrokePathEdit::PathPiece> KisBrushStrokePathEdit::pathPieces(KisRecordedBrushStrokeSP stroke,
                                                                              qreal tolerance)
{
    QVector<PathPiece> pieces;
    if (!stroke) {
        return pieces;
    }
    // one pressure everywhere: simplified() keeps points for a width that
    // changes, and only the path is wanted
    QSharedPointer<KisRecordedBrushStroke> even(new KisRecordedBrushStroke(*stroke));
    for (Job &job : even->jobs) {
        job.pi1.setPressure(1.0);
        job.pi2.setPressure(1.0);
    }
    const KisRecordedBrushStrokeSP simple = simplified(even, tolerance);
    for (const Job &job : simple->jobs) {
        const bool isCurve = job.type == FreehandStrokeStrategy::Data::CURVE;
        if (!isCurve && job.type != FreehandStrokeStrategy::Data::LINE) {
            continue;
        }
        if (jobLength(job) < 1e-3) {
            continue;
        }
        PathPiece piece;
        piece.start = job.pi1.pos();
        piece.end = job.pi2.pos();
        piece.control1 = isCurve ? job.control1 : piece.start;
        piece.control2 = isCurve ? job.control2 : piece.end;
        // a fitted curve of a straight path has its handles on the chord
        piece.curve = isCurve && !(isOnChord(job, job.control1) && isOnChord(job, job.control2));
        pieces << piece;
    }
    return pieces;
}

QPainterPath KisBrushStrokePathEdit::closedPath(KisRecordedBrushStrokeSP stroke)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    if (!stroke) {
        return path;
    }
    for (const QPolygonF &line : KisBrushStrokeShapeEraser::strokeLines(stroke)) {
        if (line.size() < 3) {
            continue;
        }
        path.addPolygon(line);
        path.closeSubpath();
    }
    return path;
}

int KisBrushStrokePathEdit::pointCount(KisRecordedBrushStrokeSP stroke)
{
    if (!stroke) {
        return 0;
    }
    int count = 0;
    QVector<int> seen;
    for (int i = 0; i < stroke->jobs.size(); i++) {
        const int id = stroke->jobs[i].strokeInfoId;
        if (!seen.contains(id)) {
            seen << id;
            count += controlPoints(stroke, i).size();
        }
    }
    return count;
}

KisBrushStrokePathEdit::Hit KisBrushStrokePathEdit::nearestPoint(KisRecordedBrushStrokeSP stroke, const QPointF &point)
{
    Hit best;
    if (!stroke) {
        return best;
    }
    for (int i = 0; i < stroke->jobs.size(); i++) {
        const Job &job = stroke->jobs[i];
        // a polyline close to the job's path, then the nearest place on it
        const int steps = job.type == FreehandStrokeStrategy::Data::CURVE
            ? CurveSteps
            : (job.type == FreehandStrokeStrategy::Data::LINE ? 1 : 0);
        QPointF previous = job.pi1.pos();
        for (int step = 0; step <= steps; step++) {
            const qreal t0 = steps ? qreal(qMax(0, step - 1)) / steps : 0.0;
            const qreal t1 = steps ? qreal(step) / steps : 0.0;
            const QPointF next = jobPosition(job, t1);
            const QPointF direction = next - previous;
            const qreal lengthSquared = QPointF::dotProduct(direction, direction);
            const qreal along = lengthSquared > 0.0
                ? qBound(0.0, QPointF::dotProduct(point - previous, direction) / lengthSquared, 1.0)
                : 0.0;
            const QPointF nearest = previous + direction * along;
            const qreal far = distance(nearest, point);
            if (!best.valid || far < best.distance) {
                best.valid = true;
                best.jobIndex = i;
                best.t = t0 + (t1 - t0) * along;
                best.position = nearest;
                best.distance = far;
            }
            previous = next;
        }
    }
    return best;
}

QPolygonF KisBrushStrokePathEdit::controlPoints(KisRecordedBrushStrokeSP stroke, int jobIndex, QVector<bool> *corners)
{
    QPolygonF points;
    if (corners) {
        corners->clear();
    }
    if (!stroke) {
        return points;
    }
    const Line line = lineOf(*stroke, jobIndex);
    for (int inLine = 0; inLine < line.jobs.size(); inLine++) {
        const Job &job = stroke->jobs[line.jobs[inLine]];
        if (points.isEmpty() || points.last() != job.pi1.pos()) {
            points << job.pi1.pos();
            if (corners) {
                *corners << false;
            }
        }
        if (job.type != FreehandStrokeStrategy::Data::POINT && points.last() != job.pi2.pos()) {
            points << job.pi2.pos();
            if (corners) {
                // with the job that goes on from here
                bool corner = false;
                if (inLine + 1 < line.jobs.size()) {
                    const Job &next = stroke->jobs[line.jobs[inLine + 1]];
                    corner = next.type != FreehandStrokeStrategy::Data::POINT
                        && distance(job.pi2.pos(), next.pi1.pos()) <= 1e-3 && isCornerBetween(job, next);
                }
                *corners << corner;
            }
        }
    }
    return points;
}

KisRecordedBrushStrokeSP KisBrushStrokePathEdit::movePoints(KisRecordedBrushStrokeSP stroke,
                                                            const Hit &anchor,
                                                            const QPointF &offset,
                                                            qreal reach)
{
    return changePoints(stroke, anchor, reach, [&](Job &job, int which, qreal weight) {
        if (weight <= 0.0) {
            return;
        }
        const QPointF shift = offset * weight;
        switch (which) {
        case 0:
            job.pi1.setPos(job.pi1.pos() + shift);
            break;
        case 1:
            job.control1 += shift;
            break;
        case 2:
            job.control2 += shift;
            break;
        default:
            job.pi2.setPos(job.pi2.pos() + shift);
            break;
        }
    });
}

KisRecordedBrushStrokeSP
KisBrushStrokePathEdit::scaleWidth(KisRecordedBrushStrokeSP stroke, const Hit &anchor, qreal factor, qreal reach)
{
    return changePoints(stroke, anchor, reach, [&](Job &job, int which, qreal weight) {
        if (weight <= 0.0 || which == 1 || which == 2) {
            return;
        }
        KisPaintInformation &info = which == 0 ? job.pi1 : job.pi2;
        info.setPressure(qBound(0.01, info.pressure() * (1.0 + (factor - 1.0) * weight), 1.0));
    });
}

namespace
{
/// How the size of a brush follows the pen pressure
struct SizeResponse {
    bool follows = false;
    KisCubicCurve curve;
    qreal strength = 1.0;

    /// the width at @p pressure as a share of the brush size
    qreal share(qreal pressure) const
    {
        return follows ? strength * curve.value(qBound(0.0, pressure, 1.0)) : 1.0;
    }

    /// the lowest pressure that gives at least @p share (the curve is
    /// taken as rising); 1 if none does
    qreal pressure(qreal wanted) const
    {
        if (share(1.0) <= wanted) {
            return 1.0;
        }
        qreal low = 0.0;
        qreal high = 1.0;
        for (int i = 0; i < 48; i++) {
            const qreal middle = (low + high) / 2.0;
            (share(middle) < wanted ? low : high) = middle;
        }
        return high;
    }
};

/**
 * The Size option of @p stroke's brush, read as KisKritaSensorPack writes
 * it (plugins/paintops/libpaintop; not reachable from this library):
 * "PressureSize" is the option's checkbox, "SizeUseCurve" its curve
 * switch, "SizeValue" its strength, and "SizeSensor" one sensor element or
 * a list of <ChildSensor> elements, each with an id and, unless it is the
 * straight default, a <curve>. With "SizeUseSameCurve" (the default) all
 * sensors share "SizecommonCurve". Presets from before the sensors kept
 * their curves have "CustomSize" and "CurveSize".
 */
SizeResponse sizeResponse(KisPaintOpPresetSP preset)
{
    SizeResponse response;
    if (!preset || !preset->settings()) {
        return response;
    }
    const KisPaintOpSettingsSP settings = preset->settings();
    if (!settings->getBool("PressureSize", false) || !settings->getBool("SizeUseCurve", true)) {
        return response;
    }

    const QString pressureId = QStringLiteral("pressure");
    const QString definition = settings->getString("SizeSensor");
    QString curve = DEFAULT_CURVE_STRING;
    // without the entry, or without an active sensor, the option has its
    // default sensor: the pressure
    bool hasPressure = definition.isEmpty();
    bool hasSensor = false;
    if (!definition.isEmpty()) {
        QDomDocument document;
        document.setContent(definition);
        const QDomElement root = document.documentElement();
        QList<QDomElement> sensors;
        if (definition.contains(QLatin1String("sensorslist"))) {
            for (QDomElement child = root.firstChildElement(QStringLiteral("ChildSensor")); !child.isNull();
                 child = child.nextSiblingElement(QStringLiteral("ChildSensor"))) {
                sensors << child;
            }
        } else {
            sensors << root;
        }
        for (const QDomElement &sensor : std::as_const(sensors)) {
            hasSensor = true;
            if (sensor.attribute(QStringLiteral("id")) == pressureId) {
                hasPressure = true;
                const QDomElement own = sensor.firstChildElement(QStringLiteral("curve"));
                if (!own.isNull()) {
                    curve = own.text();
                }
            }
        }
        if (!hasSensor) {
            hasPressure = true;
        }
    }
    if (!hasPressure) {
        return response;
    }
    if (!definition.contains(QLatin1String("curve")) && settings->getBool("CustomSize", false)) {
        curve = settings->getString("CurveSize", DEFAULT_CURVE_STRING);
    }
    if (settings->getBool("SizeUseSameCurve", true)) {
        curve = settings->getString("SizecommonCurve", curve);
    }
    if (curve.isEmpty()) {
        curve = DEFAULT_CURVE_STRING;
    }

    response.follows = true;
    response.curve = KisCubicCurve(curve);
    response.strength = qBound(0.0, settings->getDouble("SizeValue", 1.0), 1.0);
    // a curve that gives no width at full pressure cannot be worked with
    if (response.share(1.0) <= 1e-6) {
        response.follows = false;
    }
    return response;
}

SizeResponse sizeResponse(KisRecordedBrushStrokeSP stroke)
{
    return sizeResponse(stroke ? stroke->preset : KisPaintOpPresetSP());
}

/**
 * @p stroke with the widths @p shares (of its brush size) at its control
 * points; the key is twice the job's index, plus 1 for its end point. A
 * share beyond what the brush has at full pressure makes the brush grow;
 * then every point of the stroke gets the pressure of the width it has.
 */
KisRecordedBrushStrokeSP
withShares(KisRecordedBrushStrokeSP stroke, const SizeResponse &response, const QHash<int, qreal> &shares)
{
    const qreal top = response.share(1.0);
    qreal widest = top;
    for (const qreal share : shares) {
        widest = qMax(widest, share);
    }
    const qreal grow = widest / top;
    const bool grows = grow > 1.0 + 1e-9;
    QSharedPointer<KisRecordedBrushStroke> result(new KisRecordedBrushStroke(*stroke));
    auto set = [&](int key, KisPaintInformation &info) {
        const auto it = shares.constFind(key);
        if (it == shares.constEnd() && !grows) {
            return;
        }
        const qreal share = it != shares.constEnd() ? it.value() : response.share(info.pressure());
        info.setPressure(qMax(0.01, response.pressure(grows ? share / grow : share)));
    };
    for (int i = 0; i < result->jobs.size(); i++) {
        set(i * 2, result->jobs[i].pi1);
        set(i * 2 + 1, result->jobs[i].pi2);
    }
    if (grows) {
        result->preset = stroke->preset->clone().dynamicCast<KisPaintOpPreset>();
        result->preset->settings()->setPaintOpSize(stroke->preset->settings()->paintOpSize() * grow);
    }
    return result;
}
} // namespace

bool KisBrushStrokePathEdit::sizeFollowsPressure(KisRecordedBrushStrokeSP stroke)
{
    return sizeResponse(stroke).follows;
}

qreal KisBrushStrokePathEdit::widthShare(KisRecordedBrushStrokeSP stroke, qreal pressure)
{
    return sizeResponse(stroke).share(pressure);
}

qreal KisBrushStrokePathEdit::widthShare(KisPaintOpPresetSP preset, qreal pressure)
{
    return sizeResponse(preset).share(pressure);
}

KisRecordedBrushStrokeSP
KisBrushStrokePathEdit::addWidth(KisRecordedBrushStrokeSP stroke, const Hit &anchor, qreal amount, qreal reach)
{
    if (!stroke || !anchor.valid || !stroke->preset || !stroke->preset->settings() || qFuzzyIsNull(amount)) {
        return stroke;
    }
    const qreal size = stroke->preset->settings()->paintOpSize();
    if (size <= 0.0) {
        return stroke;
    }
    const SizeResponse response = sizeResponse(stroke);
    if (!response.follows) {
        return withBrushSize(stroke, qMax(qMin(size, 1.0), size + amount));
    }

    // the new widths of the points around the anchor, as shares of the
    // brush size; they may exceed what the brush has
    QHash<const KisPaintInformation *, qreal> wanted;
    const KisRecordedBrushStrokeSP changed =
        changePoints(stroke, anchor, reach, [&](Job &job, int which, qreal weight) {
            if (weight <= 0.0 || which == 1 || which == 2) {
                return;
            }
            const KisPaintInformation &info = which == 0 ? job.pi1 : job.pi2;
            wanted.insert(&info, qMax(0.0, response.share(info.pressure()) + amount * weight / size));
        });
    if (changed == stroke || wanted.isEmpty()) {
        return stroke;
    }
    QHash<int, qreal> shares;
    for (int i = 0; i < changed->jobs.size(); i++) {
        const auto first = wanted.constFind(&changed->jobs[i].pi1);
        if (first != wanted.constEnd()) {
            shares.insert(i * 2, first.value());
        }
        const auto second = wanted.constFind(&changed->jobs[i].pi2);
        if (second != wanted.constEnd()) {
            shares.insert(i * 2 + 1, second.value());
        }
    }
    return withShares(stroke, response, shares);
}

KisRecordedBrushStrokeSP
KisBrushStrokePathEdit::scaleDensity(KisRecordedBrushStrokeSP stroke, const Hit &anchor, qreal factor, qreal reach)
{
    return changePoints(stroke, anchor, reach, [&](Job &job, int which, qreal weight) {
        if (weight <= 0.0 || which == 1 || which == 2) {
            return;
        }
        KisPaintInformation &info = which == 0 ? job.pi1 : job.pi2;
        info.setDensityFactor(qBound(0.01, info.densityFactor() * (1.0 + (factor - 1.0) * weight), 1.0));
    });
}

bool KisBrushStrokePathEdit::canMapPoints(KisRecordedBrushStrokeSP stroke)
{
    return stroke && !stroke->mirrorHorizontal && !stroke->mirrorVertical;
}

KisRecordedBrushStrokeSP KisBrushStrokePathEdit::mapped(KisRecordedBrushStrokeSP stroke,
                                                        const std::function<QPointF(const QPointF &)> &map)
{
    if (!canMapPoints(stroke) || !map) {
        return stroke;
    }
    bool finite = true;
    auto place = [&](const QPointF &point) {
        const QPointF result = map(point);
        finite = finite && qIsFinite(result.x()) && qIsFinite(result.y());
        return result;
    };
    QSharedPointer<KisRecordedBrushStroke> changed(new KisRecordedBrushStroke(*stroke));
    for (Job &job : changed->jobs) {
        job.pi1.setPos(place(job.pi1.pos()));
        if (job.type == FreehandStrokeStrategy::Data::POINT) {
            continue;
        }
        if (job.type == FreehandStrokeStrategy::Data::CURVE) {
            job.control1 = place(job.control1);
            job.control2 = place(job.control2);
        }
        job.pi2.setPos(place(job.pi2.pos()));
    }
    if (!finite) {
        return stroke;
    }
    return changed;
}

namespace
{
/// The two parts of @p job on each side of @p t, following the same path
void splitJob(const Job &job, qreal t, Job *first, Job *second)
{
    *first = job;
    *second = job;
    const KisPaintInformation middle = KisPaintInformation::mix(jobPosition(job, t), t, job.pi1, job.pi2);
    first->pi2 = middle;
    second->pi1 = middle;
    if (job.type == FreehandStrokeStrategy::Data::CURVE) {
        const QPointF a = lerp(job.pi1.pos(), job.control1, t);
        const QPointF b = lerp(job.control1, job.control2, t);
        const QPointF c = lerp(job.control2, job.pi2.pos(), t);
        first->control1 = a;
        first->control2 = lerp(a, b, t);
        second->control1 = lerp(b, c, t);
        second->control2 = c;
    }
}

bool isPathJob(const Job &job)
{
    return job.type == FreehandStrokeStrategy::Data::LINE || job.type == FreehandStrokeStrategy::Data::CURVE;
}
} // namespace

KisRecordedBrushStrokeSP KisBrushStrokePathEdit::redrawn(KisRecordedBrushStrokeSP stroke,
                                                         const Hit &from,
                                                         const Hit &to,
                                                         const QPolygonF &path,
                                                         qreal spacing)
{
    if (!stroke || !from.valid || path.size() < 2) {
        return stroke;
    }
    const Line line = lineOf(*stroke, from.jobIndex);
    const int fromOrdinal = line.jobs.indexOf(from.jobIndex);
    if (fromOrdinal < 0 || line.jobs.isEmpty()) {
        return stroke;
    }
    const qreal total = line.starts.last() + line.lengths.last();
    auto alongOf = [&](const Hit &hit) {
        const int ordinal = line.jobs.indexOf(hit.jobIndex);
        return ordinal < 0 ? -1.0 : line.starts[ordinal] + qBound(0.0, hit.t, 1.0) * line.lengths[ordinal];
    };
    /// the pen's values on the line at the distance @p along
    auto infoAt = [&](qreal along) {
        int ordinal = -1;
        for (int i = 0; i < line.jobs.size(); i++) {
            if (!isPathJob(stroke->jobs[line.jobs[i]]) || line.lengths[i] <= 0.0) {
                continue;
            }
            ordinal = i;
            if (along <= line.starts[i] + line.lengths[i]) {
                break;
            }
        }
        if (ordinal < 0) {
            return stroke->jobs[line.jobs.first()].pi1;
        }
        const Job &job = stroke->jobs[line.jobs[ordinal]];
        const qreal t = qBound(0.0, (along - line.starts[ordinal]) / line.lengths[ordinal], 1.0);
        return KisPaintInformation::mix(jobPosition(job, t), t, job.pi1, job.pi2);
    };

    // the part to replace, and which of its ends are places on the line
    qreal start = alongOf(from);
    qreal end = to.valid ? alongOf(to) : -1.0;
    bool startOnLine = true;
    bool endOnLine = end >= 0.0;
    if (!endOnLine) {
        const Hit last = nearestPoint(stroke, path.last());
        const qreal lastAlong = last.valid ? alongOf(last) : -1.0;
        end = lastAlong >= 0.0 && lastAlong < start ? 0.0 : total;
    }
    QPolygonF points = path;
    if (end < start) {
        std::swap(start, end);
        std::swap(startOnLine, endOnLine);
        std::reverse(points.begin(), points.end());
    }
    if (end - start < 1e-6) {
        return stroke;
    }

    // the path's ends onto the line
    QVector<qreal> shares(points.size(), 0.0);
    for (int i = 1; i < points.size(); i++) {
        shares[i] = shares[i - 1] + distance(points[i - 1], points[i]);
    }
    const qreal pathLength = shares.last();
    if (pathLength < 1e-6) {
        return stroke;
    }
    const QPointF startShift = startOnLine ? infoAt(start).pos() - points.first() : QPointF();
    const QPointF endShift = endOnLine ? infoAt(end).pos() - points.last() : QPointF();
    for (int i = 0; i < points.size(); i++) {
        shares[i] /= pathLength;
        points[i] += startShift * (1.0 - shares[i]) + endShift * shares[i];
    }

    // thinned out to the spacing; the ends stay
    QPolygonF kept;
    QVector<qreal> keptShares;
    kept << points.first();
    keptShares << 0.0;
    for (int i = 1; i < points.size() - 1; i++) {
        if (distance(points[i], kept.last()) >= spacing && distance(points[i], points.last()) >= spacing * 0.5) {
            kept << points[i];
            keptShares << shares[i];
        }
    }
    kept << points.last();
    keptShares << 1.0;

    auto infoOnPath = [&](int index) {
        KisPaintInformation info = infoAt(start + keptShares[index] * (end - start));
        info.setPos(kept[index]);
        return info;
    };

    QSharedPointer<KisRecordedBrushStroke> changed(new KisRecordedBrushStroke(*stroke));
    changed->jobs.clear();
    bool added = false;
    auto addPath = [&](const Job &like) {
        if (added) {
            return;
        }
        added = true;
        for (int i = 1; i < kept.size(); i++) {
            Job job = like;
            job.type = FreehandStrokeStrategy::Data::LINE;
            job.pi1 = infoOnPath(i - 1);
            job.pi2 = infoOnPath(i);
            job.control1 = job.pi1.pos();
            job.control2 = job.pi2.pos();
            changed->jobs << job;
        }
    };
    const qreal epsilon = 1e-6;
    for (int index = 0; index < stroke->jobs.size(); index++) {
        const Job &job = stroke->jobs[index];
        const int ordinal = line.jobs.indexOf(index);
        if (ordinal < 0) {
            changed->jobs << job;
            continue;
        }
        const qreal jobStart = line.starts[ordinal];
        const qreal jobEnd = jobStart + line.lengths[ordinal];
        if (!isPathJob(job) || line.lengths[ordinal] <= 0.0) {
            // a single dab: it goes with the part it lies in, and with an
            // end of the line that moves
            const bool inside = jobStart > start + epsilon && jobStart < end - epsilon;
            const bool atMovedStart = !startOnLine && jobStart <= start + epsilon;
            const bool atMovedEnd = !endOnLine && jobStart >= end - epsilon;
            Job moved = job;
            if (inside || atMovedStart || atMovedEnd) {
                const qreal share = qBound(0.0, (jobStart - start) / (end - start), 1.0);
                int nearest = 0;
                for (int i = 1; i < kept.size(); i++) {
                    if (qAbs(keptShares[i] - share) < qAbs(keptShares[nearest] - share)) {
                        nearest = i;
                    }
                }
                moved.pi1.setPos(kept[nearest]);
                moved.pi2.setPos(kept[nearest]);
            }
            changed->jobs << moved;
            continue;
        }
        if (jobEnd <= start + epsilon || jobStart >= end - epsilon) {
            if (jobStart >= end - epsilon) {
                // the part was cut out at a control point, or at the
                // line's start
                addPath(job);
            }
            changed->jobs << job;
            continue;
        }
        Job first;
        Job second;
        if (jobStart < start - epsilon) {
            splitJob(job, (start - jobStart) / line.lengths[ordinal], &first, &second);
            changed->jobs << first;
        }
        addPath(job);
        if (jobEnd > end + epsilon) {
            splitJob(job, (end - jobStart) / line.lengths[ordinal], &first, &second);
            changed->jobs << second;
        }
    }
    if (!added) {
        return stroke;
    }
    return changed;
}

KisRecordedBrushStrokeSP KisBrushStrokePathEdit::redrawnWidth(KisRecordedBrushStrokeSP stroke,
                                                              const QVector<Hit> &places,
                                                              const QVector<qreal> &widths,
                                                              qreal spacing)
{
    if (!stroke || !stroke->preset || !stroke->preset->settings() || places.size() != widths.size()) {
        return stroke;
    }
    const qreal size = stroke->preset->settings()->paintOpSize();
    if (size <= 0.0) {
        return stroke;
    }

    // the widths by their distance along the line
    struct Width {
        qreal along;
        qreal width;
    };
    Line line;
    QVector<Width> traced;
    for (int i = 0; i < places.size(); i++) {
        if (!places[i].valid || !(widths[i] > 0.0)) {
            continue;
        }
        if (line.jobs.isEmpty()) {
            line = lineOf(*stroke, places[i].jobIndex);
        }
        const int ordinal = line.jobs.indexOf(places[i].jobIndex);
        if (ordinal < 0) {
            continue;
        }
        traced << Width{line.starts[ordinal] + qBound(0.0, places[i].t, 1.0) * line.lengths[ordinal], widths[i]};
    }
    if (traced.isEmpty()) {
        return stroke;
    }
    std::stable_sort(traced.begin(), traced.end(), [](const Width &a, const Width &b) {
        return a.along < b.along;
    });
    const qreal epsilon = 1e-6;
    const qreal start = traced.first().along;
    const qreal end = traced.last().along;
    if (end - start < epsilon) {
        return stroke;
    }
    const qreal total = line.starts.last() + line.lengths.last();

    const SizeResponse response = sizeResponse(stroke);
    if (!response.follows) {
        qreal sum = 0.0;
        for (const Width &width : std::as_const(traced)) {
            sum += width.width;
        }
        return withBrushSize(stroke, qMax(qMin(size, 1.0), sum / traced.size()));
    }

    auto widthAt = [&](qreal along) {
        int next = 0;
        while (next < traced.size() && traced[next].along < along) {
            next++;
        }
        if (next == 0) {
            return traced.first().width;
        }
        if (next == traced.size()) {
            return traced.last().width;
        }
        const Width &a = traced[next - 1];
        const Width &b = traced[next];
        return b.along - a.along < epsilon ? b.width
                                           : a.width + (b.width - a.width) * (along - a.along) / (b.along - a.along);
    };
    // the points of the part take the widths; its two ends keep theirs,
    // unless they are the line's
    auto takes = [&](qreal along) {
        return (along > start + epsilon && along < end - epsilon) || (along <= start + epsilon && start <= epsilon)
            || (along >= end - epsilon && end >= total - epsilon);
    };

    QSharedPointer<KisRecordedBrushStroke> divided(new KisRecordedBrushStroke(*stroke));
    divided->jobs.clear();
    QHash<int, qreal> shares;
    spacing = qMax(spacing, 0.5);
    for (int index = 0; index < stroke->jobs.size(); index++) {
        const Job &job = stroke->jobs[index];
        const int ordinal = line.jobs.indexOf(index);
        if (ordinal < 0) {
            divided->jobs << job;
            continue;
        }
        const qreal jobStart = line.starts[ordinal];
        const qreal length = line.lengths[ordinal];
        const qreal jobEnd = jobStart + length;
        if (!isPathJob(job) || length <= 0.0) {
            // a single dab
            if (takes(jobStart)) {
                shares.insert(divided->jobs.size() * 2, widthAt(jobStart) / size);
                shares.insert(divided->jobs.size() * 2 + 1, widthAt(jobStart) / size);
            }
            divided->jobs << job;
            continue;
        }

        // where the job gets control points: at the part's ends and along
        // what it has of the part
        QVector<qreal> cuts;
        const qreal from = qMax(jobStart, start);
        const qreal to = qMin(jobEnd, end);
        if (to - from > epsilon) {
            if (from > jobStart + epsilon) {
                cuts << from;
            }
            int pieces = qCeil((to - from) / spacing);
            if (jobStart <= start + epsilon && jobEnd >= end - epsilon) {
                // the whole part is in this job: a point inside it
                pieces = qMax(pieces, 2);
            }
            for (int i = 1; i < pieces; i++) {
                cuts << from + (to - from) * i / pieces;
            }
            if (to < jobEnd - epsilon) {
                cuts << to;
            }
        }

        Job rest = job;
        qreal restStart = jobStart;
        auto add = [&](const Job &piece, qreal pieceStart, qreal pieceEnd) {
            if (takes(pieceStart)) {
                shares.insert(divided->jobs.size() * 2, widthAt(pieceStart) / size);
            }
            if (takes(pieceEnd)) {
                shares.insert(divided->jobs.size() * 2 + 1, widthAt(pieceEnd) / size);
            }
            divided->jobs << piece;
        };
        for (const qreal cut : std::as_const(cuts)) {
            Job first;
            Job second;
            splitJob(rest, qBound(0.0, (cut - restStart) / (jobEnd - restStart), 1.0), &first, &second);
            add(first, restStart, cut);
            rest = second;
            restStart = cut;
        }
        add(rest, restStart, jobEnd);
    }
    if (shares.isEmpty()) {
        return stroke;
    }
    return withShares(divided, response, shares);
}

QVector<KisRecordedBrushStrokeSP> KisBrushStrokePathEdit::splitAt(KisRecordedBrushStrokeSP stroke, const Hit &place)
{
    if (!stroke || !place.valid) {
        return {stroke};
    }
    return KisBrushStrokeShapeEraser::splitStroke(stroke, place.jobIndex, place.t);
}

KisRecordedBrushStrokeSP KisBrushStrokePathEdit::withColor(KisRecordedBrushStrokeSP stroke, const KoColor &color)
{
    if (!stroke || stroke->fgColor == color) {
        return stroke;
    }
    QSharedPointer<KisRecordedBrushStroke> changed(new KisRecordedBrushStroke(*stroke));
    changed->fgColor = color;
    return changed;
}

KisRecordedBrushStrokeSP KisBrushStrokePathEdit::withBackgroundColor(KisRecordedBrushStrokeSP stroke,
                                                                     const KoColor &color)
{
    if (!stroke || stroke->bgColor == color) {
        return stroke;
    }
    QSharedPointer<KisRecordedBrushStroke> changed(new KisRecordedBrushStroke(*stroke));
    changed->bgColor = color;
    return changed;
}

KisRecordedBrushStrokeSP KisBrushStrokePathEdit::withBrushSize(KisRecordedBrushStrokeSP stroke, qreal size)
{
    if (!stroke || !stroke->preset || !stroke->preset->settings() || size <= 0.0
        || qFuzzyCompare(stroke->preset->settings()->paintOpSize(), size)) {
        return stroke;
    }
    QSharedPointer<KisRecordedBrushStroke> changed(new KisRecordedBrushStroke(*stroke));
    changed->preset = stroke->preset->clone().dynamicCast<KisPaintOpPreset>();
    changed->preset->settings()->setPaintOpSize(size);
    return changed;
}

KisRecordedBrushStrokeSP KisBrushStrokePathEdit::withBrush(KisRecordedBrushStrokeSP stroke, KisPaintOpPresetSP preset)
{
    if (!stroke || !preset || !preset->settings()) {
        return stroke;
    }
    QSharedPointer<KisRecordedBrushStroke> changed(new KisRecordedBrushStroke(*stroke));
    changed->preset = preset->clone().dynamicCast<KisPaintOpPreset>();
    if (stroke->preset && stroke->preset->settings()) {
        // the line keeps its width; only the brush changes
        changed->preset->settings()->setPaintOpSize(stroke->preset->settings()->paintOpSize());
    }
    return changed;
}

bool KisBrushStrokeShapeEdit::hasChanges() const
{
    return m_changed;
}

KUndo2Command *KisBrushStrokeShapeEdit::replaceStrokes(
    KisShapeLayerSP layer,
    const QList<KisBrushStrokeShape *> &shapes,
    const std::function<KisRecordedBrushStrokeSP(const KisBrushStrokeShape *)> &change,
    const KUndo2MagicString &text,
    QList<KisBrushStrokeShape *> *replacements)
{
    QList<KoShape *> removed;
    QList<KoShape *> added;
    Q_FOREACH (KisBrushStrokeShape *shape, shapes) {
        KisBrushStrokeShape *replacement = nullptr;
        if (shape && shape->parent() == layer.data() && shape->brushStroke()) {
            const KisRecordedBrushStrokeSP stroke = change(shape);
            if (stroke && stroke != shape->brushStroke()) {
                // made while the shape is in the layer, where it knows its place
                replacement = shape->createPiece(stroke, false);
            }
        }
        if (replacement) {
            layer->removeShape(shape);
            layer->addShape(replacement);
            removed << shape;
            added << replacement;
        }
        if (replacements) {
            *replacements << (replacement ? replacement : shape);
        }
    }
    return removed.isEmpty() ? nullptr : KisBrushStrokeShapeEraser::createReplaceCommand(layer, removed, added, text);
}

KUndo2Command *KisBrushStrokeShapeEdit::joinStrokes(KisShapeLayerSP layer,
                                                    const QList<KisBrushStrokeShape *> &shapes,
                                                    const KUndo2MagicString &text,
                                                    KisBrushStrokeShape **joined)
{
    if (joined) {
        *joined = nullptr;
    }
    QList<KisBrushStrokeShape *> usable;
    QVector<KisBrushStrokePathEdit::JoinPart> parts;
    QTransform fromImage;
    Q_FOREACH (KisBrushStrokeShape *shape, shapes) {
        if (!shape || shape->parent() != layer.data() || !KisBrushStrokePathEdit::canJoin(shape->brushStroke())
            || usable.contains(shape)) {
            continue;
        }
        // through image pixels into the first shape's recorded pixels
        const QTransform toImage = shape->expectedTransform();
        if (!toImage.isInvertible()) {
            continue;
        }
        if (usable.isEmpty()) {
            fromImage = toImage.inverted();
        }
        usable << shape;
        parts << KisBrushStrokePathEdit::JoinPart{shape->brushStroke(),
                                                  parts.isEmpty() ? QTransform() : toImage * fromImage};
    }
    if (usable.size() < 2) {
        return nullptr;
    }
    const KisRecordedBrushStrokeSP stroke = KisBrushStrokePathEdit::joined(parts);
    // made while the first shape is in the layer, where it knows its place
    KisBrushStrokeShape *replacement = stroke ? usable.first()->createPiece(stroke, false) : nullptr;
    if (!replacement) {
        return nullptr;
    }
    QList<KoShape *> removed;
    Q_FOREACH (KisBrushStrokeShape *shape, usable) {
        layer->removeShape(shape);
        removed << shape;
    }
    layer->addShape(replacement);
    if (joined) {
        *joined = replacement;
    }
    return KisBrushStrokeShapeEraser::createReplaceCommand(layer, removed, {replacement}, text);
}

KisBrushStrokeShapeEdit::KisBrushStrokeShapeEdit(KisShapeLayerSP layer, KisBrushStrokeShape *shape)
    : m_layer(layer)
    , m_original(shape)
{
    m_current << shape;
}

KisBrushStrokeShapeEdit::~KisBrushStrokeShapeEdit()
{
    if (!m_finished) {
        cancel();
    }
}

KisRecordedBrushStrokeSP KisBrushStrokeShapeEdit::originalStroke() const
{
    return m_original->brushStroke();
}

KisBrushStrokeShape *KisBrushStrokeShapeEdit::originalShape() const
{
    return m_original;
}

QList<KisBrushStrokeShape *> KisBrushStrokeShapeEdit::shapes() const
{
    return m_current;
}

void KisBrushStrokeShapeEdit::setStrokes(const QVector<KisRecordedBrushStrokeSP> &strokes)
{
    if (m_finished) {
        return;
    }
    // made from the original, which keeps its place whether it is in the
    // layer or not
    QList<KisBrushStrokeShape *> next;
    for (const KisRecordedBrushStrokeSP &stroke : strokes) {
        if (KisBrushStrokeShape *shape = m_original->createPiece(stroke, false)) {
            next << shape;
        }
    }
    Q_FOREACH (KisBrushStrokeShape *shape, m_current) {
        m_layer->removeShape(shape);
        if (shape != m_original) {
            delete shape;
        }
    }
    Q_FOREACH (KisBrushStrokeShape *shape, next) {
        m_layer->addShape(shape);
    }
    m_current = next;
    m_changed = true;
}

KUndo2Command *KisBrushStrokeShapeEdit::finish(const KUndo2MagicString &text)
{
    if (m_finished) {
        return nullptr;
    }
    m_finished = true;
    if (!m_changed) {
        return nullptr;
    }
    QList<KoShape *> added;
    Q_FOREACH (KisBrushStrokeShape *shape, m_current) {
        added << shape;
    }
    return KisBrushStrokeShapeEraser::createReplaceCommand(m_layer, {m_original}, added, text);
}

void KisBrushStrokeShapeEdit::cancel()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    if (!m_changed) {
        return;
    }
    Q_FOREACH (KisBrushStrokeShape *shape, m_current) {
        m_layer->removeShape(shape);
        delete shape;
    }
    m_current.clear();
    m_layer->addShape(m_original);
    m_current << m_original;
}
