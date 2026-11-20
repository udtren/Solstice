/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISBRUSHSTROKESHAPEERASER_H
#define KISBRUSHSTROKESHAPEERASER_H

#include <QHash>
#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSet>
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
 * The vector eraser for brush strokes on a vector layer (Solstice): it cuts
 * the strokes' recorded paths where the eraser touches them, and each part
 * that is left becomes a stroke shape of its own. See
 * docs/agent/vector-brush-strokes.md.
 *
 * One object erases for one drag, in the GUI thread: erase() changes the
 * layer's shapes at once, finish() returns the undo command for all of it.
 */
class KRITAUI_EXPORT KisBrushStrokeShapeEraser
{
public:
    enum Mode {
        /// removes the part of a line under the eraser
        TouchedPart,
        /// removes every line the eraser touches
        WholeLine,
        /// removes a touched line as far as the next line crossing it on
        /// each side, or its end
        UpToIntersection,
    };

    KisBrushStrokeShapeEraser(KisShapeLayerSP layer, Mode mode);
    /// cancels what was erased unless finish() was called
    ~KisBrushStrokeShapeEraser();

    /// Erases along the line from @p from to @p to, @p radius wide on each
    /// side, in image pixels
    void erase(const QPointF &from, const QPointF &to, qreal radius);

    /// Whether anything was erased so far
    bool hasChanges() const;

    /// The undo command of the drag, already applied (its first redo does
    /// nothing); nullptr if nothing was erased
    KUndo2Command *finish();

    /// Puts the layer's shapes back as they were
    void cancel();

    /// The mode the brush tools use (Tool Options of the Freehand Brush)
    static Mode configuredMode();
    static void setConfiguredMode(Mode mode);

    /**
     * The parts of @p stroke that are left when everything within @p radius
     * of the line from @p from to @p to is removed, in the stroke's recorded
     * pixels and in the order they were drawn. The path is cut where the
     * line drawn along it (half the brush's size times the pen pressure
     * wide on each side) reaches the eraser, so that no ink stays inside
     * it. A cut puts a new point there, with the pen values in between its
     * neighbours'. @p touched says whether the eraser reached the line; if
     * not, the result is @p stroke alone.
     */
    static QVector<KisRecordedBrushStrokeSP>
    cutStroke(KisRecordedBrushStrokeSP stroke, const QPointF &from, const QPointF &to, qreal radius, bool *touched);

    /// The path of another line, and half the width it is drawn with, in
    /// the recorded pixels of the stroke that is cut
    struct OtherLine {
        QPolygonF points;
        qreal halfWidth = 0.0;
    };

    /**
     * As cutStroke(), but each touched stretch of a line is removed as far
     * as the nearest place on each side where the line crosses one of
     * @p others, another copy of the same stroke or itself; without a
     * crossing, up to the line's end. A line of @p others that ends at the
     * stroke's line (within half of both widths) counts as crossing it.
     */
    static QVector<KisRecordedBrushStrokeSP> cutStrokeUpToIntersections(KisRecordedBrushStrokeSP stroke,
                                                                        const QPointF &from,
                                                                        const QPointF &to,
                                                                        qreal radius,
                                                                        const QVector<OtherLine> &others,
                                                                        bool *touched);

    /// The paths of @p stroke as polylines, one for each copy the tool
    /// painted, in recorded pixels
    static QVector<QPolygonF> strokeLines(KisRecordedBrushStrokeSP stroke);

    /// @p stroke cut in two at the parameter @p t of its job @p jobIndex,
    /// without removing anything (one stroke, or none, at a line's end)
    static QVector<KisRecordedBrushStrokeSP> splitStroke(KisRecordedBrushStrokeSP stroke, int jobIndex, qreal t);

    /**
     * An undo command that exchanges @p removed and @p added on @p layer.
     * The exchange is already done: the first redo does nothing. The
     * command owns the shapes that are not in the layer.
     */
    static KUndo2Command *createReplaceCommand(KisShapeLayerSP layer,
                                               const QList<KoShape *> &removed,
                                               const QList<KoShape *> &added,
                                               const KUndo2MagicString &text);

private:
    Q_DISABLE_COPY(KisBrushStrokeShapeEraser)

    /// The lines of the layer's other strokes near @p shape, mapped from
    /// image pixels by @p toRecord
    QVector<OtherLine> otherLines(const KisBrushStrokeShape *shape, const QTransform &toRecord);

    struct ImageLines {
        QVector<QPolygonF> lines;
        QRectF bounds;
        qreal halfWidth = 0.0;
    };
    /// the lines of the layer's strokes in image pixels, as far as asked for
    QHash<KoShape *, ImageLines> m_lines;

    KisShapeLayerSP m_layer;
    Mode m_mode;
    /// shapes the drag took out of the layer that were there before it
    QList<KoShape *> m_removed;
    /// shapes the drag made that are in the layer now
    QSet<KoShape *> m_created;
    bool m_finished = false;
};

#endif // KISBRUSHSTROKESHAPEERASER_H
