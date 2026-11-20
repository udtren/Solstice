/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISBRUSHSTROKEPATHEDIT_H
#define KISBRUSHSTROKEPATHEDIT_H

#include <functional>

#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <QVector>

#include <KoShape.h>
#include <kis_types.h>
#include <kundo2magicstring.h>

#include "KisBrushStrokeLayer.h"
#include "kritaui_export.h"

class KUndo2Command;
class KisBrushStrokeShape;

/**
 * Editing the path of a recorded brush stroke (Solstice): its jobs' end
 * points are the path's control points, each with the pen's values. The
 * functions work in the stroke's recorded pixels and return a changed copy.
 * See docs/agent/vector-brush-strokes.md ("Editing the path").
 */
class KRITAUI_EXPORT KisBrushStrokePathEdit
{
public:
    /// A place on a stroke's path
    struct Hit {
        bool valid = false;
        /// the job and the parameter in it
        int jobIndex = -1;
        qreal t = 0.0;
        QPointF position;
        /// how far the asked point is from @p position
        qreal distance = 0.0;
    };

    /// The place on @p stroke's path that is nearest to @p point
    static Hit nearestPoint(KisRecordedBrushStrokeSP stroke, const QPointF &point);

    /// The control points of the line that @p jobIndex belongs to (the end
    /// points of its jobs), in drawing order. @p corners gets, for each of
    /// them, whether it is a corner (isCorner(); false for the line's ends).
    static QPolygonF controlPoints(KisRecordedBrushStrokeSP stroke, int jobIndex, QVector<bool> *corners = nullptr);

    /**
     * @p stroke with the control points around @p anchor moved by @p
     * offset: fully at the anchor, less with the distance along the line,
     * not at all from @p reach on. With a reach of 0 only the control point
     * nearest to the anchor moves.
     */
    static KisRecordedBrushStrokeSP
    movePoints(KisRecordedBrushStrokeSP stroke, const Hit &anchor, const QPointF &offset, qreal reach);

    /**
     * @p stroke with the pen pressure around @p anchor multiplied by @p
     * factor, with the same falloff as movePoints(). The pressure stays
     * between 0.01 and 1. It changes the line's width for brushes whose
     * size follows the pressure.
     */
    static KisRecordedBrushStrokeSP
    scaleWidth(KisRecordedBrushStrokeSP stroke, const Hit &anchor, qreal factor, qreal reach);

    /**
     * Whether the size of @p stroke's brush follows the pen pressure: its
     * Size option is on, with a curve, and the pressure is one of its
     * sensors. Read from the preset's settings (the option's own classes
     * are not reachable from here); a brush engine without that option
     * counts as not following.
     */
    static bool sizeFollowsPressure(KisRecordedBrushStrokeSP stroke);

    /**
     * The width of @p stroke's line at @p pressure, as a share of its brush
     * size: the Size option's strength times its pressure curve; 1 for a
     * brush whose size does not follow the pressure. Other sensors of the
     * option (speed, fade, ...) are not in it.
     */
    static qreal widthShare(KisRecordedBrushStrokeSP stroke, qreal pressure);
    /// The same for a brush that has not drawn yet
    static qreal widthShare(KisPaintOpPresetSP preset, qreal pressure);

    /**
     * @p stroke with its line @p amount wider around @p anchor (recorded
     * pixels of the brush's diameter; negative for thinner), with the same
     * falloff as movePoints().
     *
     * If the brush's size follows the pressure (sizeFollowsPressure()), the
     * width at a control point is the brush size times widthShare() of its
     * pressure, and the points get the pressures of their new widths.
     * Where a width would exceed what the brush has at full pressure, the
     * brush grows to it and every point of the stroke gets the pressure
     * that keeps its width. A pressure stays at least 0.01.
     *
     * Otherwise the line has the brush's size everywhere and nothing can
     * change it in one place: the brush size of the whole stroke changes by
     * @p amount (at least 1 pixel, or the size it has if that is less).
     */
    static KisRecordedBrushStrokeSP
    addWidth(KisRecordedBrushStrokeSP stroke, const Hit &anchor, qreal amount, qreal reach);

    /**
     * @p stroke with the density around @p anchor multiplied by @p factor,
     * with the same falloff as movePoints(). The density
     * (KisPaintInformation::densityFactor()) is a factor for the opacity
     * of the stroke's picture around that place of the path
     * (KisBrushStrokeShape applies it); it stays between 0.01 and 1, where
     * 1 is the line as it was drawn.
     */
    static KisRecordedBrushStrokeSP
    scaleDensity(KisRecordedBrushStrokeSP stroke, const Hit &anchor, qreal factor, qreal reach);

    /// Whether mapped() can be used on @p stroke: not on one drawn with the
    /// canvas mirror, whose copies are painted by mirroring every dab
    /// around the axes and cannot follow the path
    static bool canMapPoints(KisRecordedBrushStrokeSP stroke);

    /**
     * @p stroke with every control point and handle of its path at the
     * place @p map gives it (recorded pixels). The pen's values stay. A
     * map that is not affine bends a curve only through its four points,
     * which is close for jobs as short as a brush stroke's. The stroke
     * itself is returned if it cannot be mapped (canMapPoints()) or @p map
     * gives a place that is not finite.
     */
    static KisRecordedBrushStrokeSP mapped(KisRecordedBrushStrokeSP stroke,
                                           const std::function<QPointF(const QPointF &)> &map);

    /**
     * @p stroke with a part of one line drawn along @p path (recorded
     * pixels) in place of the path it had: the part between @p from and
     * @p to if @p to is a place on the same line, and otherwise from @p
     * from to that end of the line towards which @p path leads (the end
     * on the side of the line's place nearest to the path's last point).
     * The path may run either way along the line. It is moved so that it
     * starts and ends exactly on the line where the part is cut out (by
     * what each end is off, changing evenly along the path); an end of
     * the line itself goes where the path ends. The new part consists of straight jobs
     * about @p spacing long, with the pen's values the old part had at the
     * same share of its length. The stroke itself is returned if there is
     * nothing to replace.
     */
    static KisRecordedBrushStrokeSP redrawn(KisRecordedBrushStrokeSP stroke,
                                            const Hit &from,
                                            const Hit &to,
                                            const QPolygonF &path,
                                            qreal spacing = 3.0);

    /**
     * @p stroke with the widths @p widths (recorded pixels of the brush's
     * diameter) at @p places of one line, as a pen gives them that traces
     * the line: the part between the first and the last of the places
     * along the line gets them, changing evenly from one place to the
     * next; the rest keeps its width. Places on another line than the
     * first valid one, and widths that are not above 0, are left out. The
     * part gets control points at most @p spacing apart (on the path as it
     * is), so that the widths have points to be at and the change ends
     * within that distance of the part's ends; an end of the line itself
     * takes its width.
     *
     * The widths become pen pressures as in addWidth(): a width beyond
     * what the brush has at full pressure makes the brush grow and gives
     * every point of the stroke the pressure that keeps its width. If the
     * brush's size does not follow the pressure, the whole stroke gets the
     * mean of the widths as its brush size (at least 1 pixel, or the size
     * it has if that is less). The stroke itself is returned if the places
     * do not span a part.
     */
    static KisRecordedBrushStrokeSP redrawnWidth(KisRecordedBrushStrokeSP stroke,
                                                 const QVector<Hit> &places,
                                                 const QVector<qreal> &widths,
                                                 qreal spacing = 4.0);

    /// The two parts of @p stroke on each side of @p place (one part, or
    /// none, if the place is at an end)
    static QVector<KisRecordedBrushStrokeSP> splitAt(KisRecordedBrushStrokeSP stroke, const Hit &place);

    /// @p stroke drawn with @p color as its foreground color
    static KisRecordedBrushStrokeSP withColor(KisRecordedBrushStrokeSP stroke, const KoColor &color);

    /// @p stroke drawn with @p color as its background color, which brushes
    /// use that mix the two colors or paint a gradient between them
    static KisRecordedBrushStrokeSP withBackgroundColor(KisRecordedBrushStrokeSP stroke, const KoColor &color);

    /**
     * Whether the background color shows in the picture of @p stroke: the
     * stroke is drawn twice within @p recordRect (recorded pixels), with a
     * black and with a white background color, and the pictures are
     * compared. That holds for every brush engine and option, at the cost
     * of two drawings; it blocks until they are done. A brush that mixes
     * the colors by pressure shows none of it in a line drawn at full
     * pressure.
     */
    static bool usesBackgroundColor(KisRecordedBrushStrokeSP stroke, const QRectF &recordRect);

    /// @p stroke drawn with a brush of @p size (its diameter, in recorded
    /// pixels)
    static KisRecordedBrushStrokeSP withBrushSize(KisRecordedBrushStrokeSP stroke, qreal size);

    /**
     * @p stroke drawn with @p preset in place of its brush, at the size it
     * has now. The preset has to carry its resources (the preset of a
     * KisResourcesSnapshot does).
     */
    static KisRecordedBrushStrokeSP withBrush(KisRecordedBrushStrokeSP stroke, KisPaintOpPresetSP preset);

    /**
     * @p stroke with fewer control points: every line is fitted again with
     * curves that stay within @p tolerance (recorded pixels) of its path.
     * The pen pressure counts as width: it may differ by the tolerance over
     * half the brush size (of a brush at least 20 pixels wide). Sharp turns
     * at control points stay corners. The kept points keep their pen
     * values; between them the values change evenly. Returns @p stroke
     * itself if no point can go.
     */
    static KisRecordedBrushStrokeSP simplified(KisRecordedBrushStrokeSP stroke, qreal tolerance);

    /// A piece of a line's path between two control points: straight, or
    /// a cubic curve with two handles
    struct PathPiece {
        bool curve = false;
        QPointF start;
        QPointF control1;
        QPointF control2;
        QPointF end;
    };

    /**
     * The paths of @p stroke's lines as few pieces, in drawing order: the
     * jobs of the stroke simplified with @p tolerance (simplified(); as
     * they are with a tolerance of 0 or less). The pen's values do not
     * count: a line whose width changes has no more pieces for it. Single
     * dabs and pieces without length are left out.
     */
    static QVector<PathPiece> pathPieces(KisRecordedBrushStrokeSP stroke, qreal tolerance);

    /**
     * The pieces of @p path (a vector shape's outline), in its own
     * coordinates: one for every line and curve with a length, also the
     * line that closes a subpath.
     */
    static QVector<PathPiece> pathPieces(const QPainterPath &path);

    /**
     * The ellipse inscribed in @p rect as @p transform shows it: the two
     * ends of its longer axis and one end of the shorter one, which is
     * what an Ellipse assistant takes as handles. A transform with shear
     * still gives an ellipse, with other axes than the mapped ones. False
     * for an empty rectangle or a transform that flattens it.
     */
    static bool
    ellipseAxes(const QRectF &rect, const QTransform &transform, QPointF *major1, QPointF *major2, QPointF *minor);

    /**
     * The areas that the lines of @p stroke enclose, in recorded pixels:
     * the path of each line, closed by a straight piece from its end to
     * its start. The fill rule is Qt::WindingFill, so where a line crosses
     * itself all its loops are inside. Lines of fewer than three points
     * are left out; a straight line gives a path without area.
     */
    static QPainterPath closedPath(KisRecordedBrushStrokeSP stroke);

    /// The number of control points of all lines of @p stroke
    static int pointCount(KisRecordedBrushStrokeSP stroke);

    /**
     * @p stroke with a control point at @p place: the job there becomes two
     * that follow the same path, with the pen's values of that place. @p
     * added gets the new point as the start of its second job, ready for
     * movePoints() with a reach of 0. At a job's end, where a control point
     * already is, the stroke itself is returned and @p added is that point.
     */
    static KisRecordedBrushStrokeSP
    insertPoint(KisRecordedBrushStrokeSP stroke, const Hit &place, Hit *added = nullptr);

    /// The control point that is nearest to @p place along its job: an end
    /// of the job, with its position. Invalid without a line there.
    static Hit nearestControlPoint(KisRecordedBrushStrokeSP stroke, const Hit &place);

    /**
     * @p stroke without the control point nearest to @p place
     * (nearestControlPoint()). Between two jobs, they become one curve
     * fitted to the path of both, from the first one's start to the second
     * one's end. At a line's end, the line loses its first or last job. A
     * line keeps at least one job: the stroke itself is returned for a line
     * of one.
     */
    static KisRecordedBrushStrokeSP deletePoint(KisRecordedBrushStrokeSP stroke, const Hit &place);

    /**
     * Whether the control point nearest to @p place (nearestControlPoint())
     * is a corner: a point between two jobs where neither has a handle, or
     * where the path turns by more than 10 degrees. Otherwise the path
     * passes through it as a curve. False for a line's ends, which are
     * neither.
     */
    static bool isCorner(KisRecordedBrushStrokeSP stroke, const Hit &place);

    /**
     * @p stroke with the control point nearest to @p place switched between
     * a corner and a curve. To a curve: the two jobs that meet there get
     * handles at the point, along the direction from the control point
     * before to the one after, a third of the way to each (a LINE job
     * becomes a CURVE job that leaves its other end as before). To a
     * corner: the handles at the point are pulled into it; a job without
     * handles at both ends becomes a LINE job. The pen's values stay. The
     * stroke itself is returned for a line's ends.
     */
    static KisRecordedBrushStrokeSP switchCorner(KisRecordedBrushStrokeSP stroke, const Hit &place);

    /// A stroke to join, and the transformation from its recorded pixels to
    /// those of the first stroke (identity for the first)
    struct JoinPart {
        KisRecordedBrushStrokeSP stroke;
        QTransform toFirst;
    };

    /// Whether @p stroke can be joined: one line (no mirrored copies) with
    /// at least one LINE or CURVE job
    static bool canJoin(KisRecordedBrushStrokeSP stroke);

    /**
     * One stroke of the lines of @p parts: the two nearest ends of
     * different lines are connected by a straight job, again and again
     * until one line is left. A line is turned around where its end has to
     * meet an end; the first stroke's line never is. The result is in the
     * first stroke's recorded pixels and has its brush, colors and
     * settings; the pen's time runs on through all of it. Parts that
     * cannot be joined are left out. Null unless the first part and at
     * least one more can be joined.
     */
    static KisRecordedBrushStrokeSP joined(const QVector<JoinPart> &parts);
};

/**
 * Shows edited versions of a stroke shape's stroke on its layer while a
 * drag lasts, and makes the undo command at its end (Solstice). GUI thread.
 */
class KRITAUI_EXPORT KisBrushStrokeShapeEdit
{
public:
    /// @p shape lies directly on @p layer
    KisBrushStrokeShapeEdit(KisShapeLayerSP layer, KisBrushStrokeShape *shape);
    /// cancels unless finish() was called
    ~KisBrushStrokeShapeEdit();

    /// The stroke as it was when the edit began
    KisRecordedBrushStrokeSP originalStroke() const;
    /// The shape whose stroke is edited (out of the layer once setStrokes()
    /// was called); for its place on the canvas
    KisBrushStrokeShape *originalShape() const;

    /// Replaces what the layer shows of the shape by shapes of @p strokes
    void setStrokes(const QVector<KisRecordedBrushStrokeSP> &strokes);
    /// The shapes that show the strokes now (the original shape before the
    /// first setStrokes())
    QList<KisBrushStrokeShape *> shapes() const;

    /// The undo command, already applied; nullptr if nothing changed
    KUndo2Command *finish(const KUndo2MagicString &text);
    void cancel();
    /// Whether setStrokes() was called
    bool hasChanges() const;

    /**
     * Replaces each of @p shapes (stroke shapes directly on @p layer) by a
     * shape of the stroke @p change returns for it, at once; a null or
     * unchanged stroke leaves the shape. Returns the undo command of all of
     * it, already applied, or nullptr if nothing changed. @p replacements
     * gets the shapes that are there afterwards, in the order of @p shapes.
     */
    static KUndo2Command *
    replaceStrokes(KisShapeLayerSP layer,
                   const QList<KisBrushStrokeShape *> &shapes,
                   const std::function<KisRecordedBrushStrokeSP(const KisBrushStrokeShape *)> &change,
                   const KUndo2MagicString &text,
                   QList<KisBrushStrokeShape *> *replacements = nullptr);

    /**
     * Replaces those of @p shapes that can be joined (stroke shapes
     * directly on @p layer, KisBrushStrokePathEdit::canJoin()) by one shape
     * of their joined stroke (KisBrushStrokePathEdit::joined()), at once.
     * It lies where the first of them did and has its stacking order.
     * Returns the undo command, already applied, or nullptr if fewer than
     * two can be joined. @p joined gets the new shape.
     */
    static KUndo2Command *joinStrokes(KisShapeLayerSP layer,
                                      const QList<KisBrushStrokeShape *> &shapes,
                                      const KUndo2MagicString &text,
                                      KisBrushStrokeShape **joined = nullptr);

private:
    Q_DISABLE_COPY(KisBrushStrokeShapeEdit)

    KisShapeLayerSP m_layer;
    KisBrushStrokeShape *m_original;
    QList<KisBrushStrokeShape *> m_current;
    bool m_changed = false;
    bool m_finished = false;
};

#endif // KISBRUSHSTROKEPATHEDIT_H
