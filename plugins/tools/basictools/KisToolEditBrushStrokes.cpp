/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisToolEditBrushStrokes.h"

#include <QComboBox>
#include <QFormLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>

#include <KConfigGroup>
#include <KSharedConfig>

#include <KisBrushStrokeShape.h>
#include <KisBrushStrokeShapeEraser.h>
#include <KisViewManager.h>
#include <KoCanvasBase.h>
#include <KoCanvasResourceProvider.h>
#include <KoColor.h>
#include <KoPointerEvent.h>
#include <KoSelectedShapesProxy.h>
#include <KoSelection.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_settings.h>
#include <canvas/kis_canvas2.h>
#include <kis_cursor.h>
#include <kis_default_bounds.h>
#include <kis_icon.h>
#include <kis_image.h>
#include <kis_painter.h>
#include <kis_painting_assistant.h>
#include <kis_painting_assistants_decoration.h>
#include <kis_pixel_selection.h>
#include <kis_resources_snapshot.h>
#include <kis_selection_tool_helper.h>
#include <kis_shape_layer.h>
#include <kis_slider_spin_box.h>
#include <kundo2command.h>

namespace
{
const char *const ConfigGroup = "KisToolEditBrushStrokes";

/// how near the cursor has to be to a line, in view pixels (or within the
/// line itself, whichever is more)
const qreal PickDistance = 10.0;
/// a drag this far to the right doubles the width, in view pixels
const qreal WidthDragDistance = 80.0;
/// in Add Width, the line gets one image pixel wider for a drag of as many
/// image pixels
const qreal AddWidthDragRatio = 4.0;

qreal lengthScale(const QTransform &transform)
{
    return qSqrt(qAbs(transform.determinant()));
}

/// Whether @p frame (four corners in order) can be the target of a
/// transformation: convex, and not folded or flat
bool isUsableFrame(const QPolygonF &frame)
{
    if (frame.size() != 4) {
        return false;
    }
    int sign = 0;
    for (int i = 0; i < 4; i++) {
        const QPointF a = frame[(i + 1) % 4] - frame[i];
        const QPointF b = frame[(i + 2) % 4] - frame[(i + 1) % 4];
        const qreal cross = a.x() * b.y() - a.y() * b.x();
        if (qAbs(cross) < 1e-3) {
            return false;
        }
        const int turn = cross > 0.0 ? 1 : -1;
        if (sign && turn != sign) {
            return false;
        }
        sign = turn;
    }
    return true;
}

/// @p frame with its part @p handle (KisToolEditBrushStrokes::
/// frameHandleAt()) moved by @p offset
QPolygonF movedFrame(const QPolygonF &frame, int handle, const QPointF &offset)
{
    QPolygonF moved = frame;
    if (moved.size() != 4 || handle < 0) {
        return moved;
    }
    if (handle < 4) {
        moved[handle] += offset;
    } else if (handle < 8) {
        moved[handle - 4] += offset;
        moved[(handle - 3) % 4] += offset;
    } else {
        moved.translate(offset);
    }
    return moved;
}

/// The assistants of a canvas's document replaced by other ones
class AssistantsCommand : public KUndo2Command
{
public:
    AssistantsCommand(KisCanvas2 *canvas,
                      const QList<KisPaintingAssistantSP> &before,
                      const QList<KisPaintingAssistantSP> &after,
                      const KUndo2MagicString &text)
        : KUndo2Command(text)
        , m_canvas(canvas)
        , m_before(before)
        , m_after(after)
    {
    }

    void redo() override
    {
        set(m_after);
    }

    void undo() override
    {
        set(m_before);
    }

private:
    void set(const QList<KisPaintingAssistantSP> &assistants)
    {
        if (!m_canvas || !m_canvas->paintingAssistantsDecoration()) {
            return;
        }
        m_canvas->paintingAssistantsDecoration()->setAssistants(assistants);
        m_canvas->updateCanvas();
    }

    QPointer<KisCanvas2> m_canvas;
    QList<KisPaintingAssistantSP> m_before;
    QList<KisPaintingAssistantSP> m_after;
};
} // namespace

KisToolEditBrushStrokes::KisToolEditBrushStrokes(KoCanvasBase *canvas)
    : KisTool(canvas, KisCursor::arrowCursor())
{
    setObjectName("tool_edit_brush_strokes");
}

KisToolEditBrushStrokes::~KisToolEditBrushStrokes()
{
}

void KisToolEditBrushStrokes::activate(const QSet<KoShape *> &shapes)
{
    KisTool::activate(shapes);
}

void KisToolEditBrushStrokes::deactivate()
{
    finishEdit();
    clearPath();
    m_shownSelection.clear();
    m_selectionLines.clear();
    m_frameShapes.clear();
    m_frame.clear();
    m_frameHandle = -1;
    m_redrawPath.clear();
    KisTool::deactivate();
}

KisToolEditBrushStrokes::Mode KisToolEditBrushStrokes::editMode() const
{
    if (m_cmbMode) {
        return Mode(qBound(0, m_cmbMode->currentIndex(), int(RedrawWidth)));
    }
    return Mode(qBound(0, KSharedConfig::openConfig()->group(ConfigGroup).readEntry("mode", 0), int(RedrawWidth)));
}

qreal KisToolEditBrushStrokes::simplifyTolerance() const
{
    if (m_sliderSimplify) {
        return m_sliderSimplify->value();
    }
    return qBound(0.1, KSharedConfig::openConfig()->group(ConfigGroup).readEntry("simplifyTolerance", 1.0), 20.0);
}

int KisToolEditBrushStrokes::reach() const
{
    if (m_sliderReach) {
        return m_sliderReach->value();
    }
    return KSharedConfig::openConfig()->group(ConfigGroup).readEntry("reach", 60);
}

KisShapeLayerSP KisToolEditBrushStrokes::vectorLayer() const
{
    KisNodeSP node = const_cast<KisToolEditBrushStrokes *>(this)->currentNode();
    return dynamic_cast<KisShapeLayer *>(node.data());
}

qreal KisToolEditBrushStrokes::imagePixelsPerViewPixel() const
{
    const QPointF span = pixelToView(QPointF(1, 0)) - pixelToView(QPointF(0, 0));
    const qreal viewPixels = std::hypot(span.x(), span.y());
    return viewPixels > 0.0 ? 1.0 / viewPixels : 1.0;
}

KisToolEditBrushStrokes::Target KisToolEditBrushStrokes::findTarget(const QPointF &imagePoint) const
{
    Target best;
    KisShapeLayerSP layer = vectorLayer();
    if (!layer) {
        return best;
    }
    const qreal pick = PickDistance * imagePixelsPerViewPixel();
    qreal bestDistance = 0.0;

    // strokes in groups keep their place; only the layer's own are edited
    Q_FOREACH (KoShape *candidate, layer->shapes()) {
        KisBrushStrokeShape *shape = dynamic_cast<KisBrushStrokeShape *>(candidate);
        if (!shape || !shape->brushStroke() || !shape->isVisible() || shape->isGeometryProtected()) {
            continue;
        }
        const QTransform toImage = shape->expectedTransform();
        if (!toImage.isInvertible() || !toImage.isAffine()) {
            continue;
        }
        const qreal scale = lengthScale(toImage);
        const KisPaintOpPresetSP preset = shape->brushStroke()->preset;
        const qreal halfBrush = preset && preset->settings() ? preset->settings()->paintOpSize() * 0.5 * scale : 0.0;
        const qreal tolerance = qMax(pick, halfBrush);
        if (!toImage.mapRect(shape->recordRect())
                 .adjusted(-tolerance, -tolerance, tolerance, tolerance)
                 .contains(imagePoint)) {
            continue;
        }
        const KisBrushStrokePathEdit::Hit hit =
            KisBrushStrokePathEdit::nearestPoint(shape->brushStroke(), toImage.inverted().map(imagePoint));
        if (!hit.valid) {
            continue;
        }
        const qreal distance = hit.distance * scale;
        if (distance <= tolerance && (!best.shape || distance < bestDistance)) {
            best.shape = shape;
            best.hit = hit;
            bestDistance = distance;
        }
    }
    return best;
}

void KisToolEditBrushStrokes::showPath(const KisBrushStrokeShape *shape, const KisBrushStrokePathEdit::Hit &place)
{
    m_lines.clear();
    m_points.clear();
    m_pointCorners.clear();
    m_hasMarker = false;
    if (shape && shape->brushStroke()) {
        const QTransform toImage = shape->expectedTransform();
        for (const QPolygonF &line : KisBrushStrokeShapeEraser::strokeLines(shape->brushStroke())) {
            m_lines << toImage.map(line);
        }
        if (place.valid) {
            // Switch Corner shows which points are corners
            m_points = toImage.map(
                KisBrushStrokePathEdit::controlPoints(shape->brushStroke(),
                                                      place.jobIndex,
                                                      editMode() == SwitchCorner ? &m_pointCorners : nullptr));
            m_marker = toImage.map(place.position);
            m_hasMarker = true;
        }
    }
    updateCanvasPixelRect(image() ? QRectF(image()->bounds()) : QRectF());
}

void KisToolEditBrushStrokes::clearPath()
{
    if (m_lines.isEmpty() && !m_hasMarker) {
        return;
    }
    showPath(nullptr, KisBrushStrokePathEdit::Hit());
}

void KisToolEditBrushStrokes::mouseMoveEvent(KoPointerEvent *event)
{
    KisTool::mouseMoveEvent(event);
    if (m_edit) {
        return;
    }
    // the line under the cursor shows its path
    const Target target = findTarget(convertToPixelCoord(event));
    if (target.shape) {
        // Delete Point and Switch Corner mark the control point a click
        // would change
        const Mode mode = editMode();
        const KisBrushStrokePathEdit::Hit point = mode == DeletePoint || mode == SwitchCorner
            ? KisBrushStrokePathEdit::nearestControlPoint(target.shape->brushStroke(), target.hit)
            : KisBrushStrokePathEdit::Hit();
        showPath(target.shape, point.valid ? point : target.hit);
    } else {
        clearPath();
    }
}

void KisToolEditBrushStrokes::beginPrimaryAction(KoPointerEvent *event)
{
    KisShapeLayerSP layer = vectorLayer();
    if (!layer || !nodeEditable()) {
        if (!layer) {
            KisCanvas2 *kiscanvas = static_cast<KisCanvas2 *>(canvas());
            kiscanvas->viewManager()->showFloatingMessage(
                i18n("Select a vector layer with brush strokes to edit their lines."),
                koIcon("object-locked"));
        }
        event->ignore();
        return;
    }

    const QPointF point = convertToPixelCoord(event);
    if (editMode() == FreeTransform) {
        // the frame of the selected lines comes before the lines
        m_frameHandle = frameHandleAt(point);
        if (m_frameHandle >= 0) {
            finishEdit();
            clearPath();
            m_dragFrame = m_frame;
            m_startPoint = point;
            m_startViewPoint = pixelToView(point);
            m_pressMode = FreeTransform;
            setMode(KisTool::PAINT_MODE);
            return;
        }
    }
    const Target target = findTarget(point);
    if (!target.shape) {
        // a click beside the lines selects nothing
        clearSelection();
        event->ignore();
        return;
    }
    m_editedShapeWasSelected = selectedStrokes().contains(target.shape);

    m_toRecord = target.shape->expectedTransform().inverted();
    m_recordScale = lengthScale(m_toRecord);
    m_anchor = target.hit;
    m_startPoint = point;
    m_startViewPoint = pixelToView(point);
    m_edit.reset(new KisBrushStrokeShapeEdit(layer, target.shape));
    m_pressMode = editMode();
    m_strokeWithPoint.clear();
    m_redrawPath.clear();
    m_redrawPressures.clear();
    if (m_pressMode == Redraw || m_pressMode == RedrawWidth) {
        m_redrawPath << point;
        m_redrawPressures << event->pressure();
    }
    setMode(KisTool::PAINT_MODE);

    if (m_pressMode == Cut) {
        // done at the press: the line is two lines now
        const QVector<KisRecordedBrushStrokeSP> parts =
            KisBrushStrokePathEdit::splitAt(m_edit->originalStroke(), m_anchor);
        if (parts.size() == 2) {
            m_edit->setStrokes(parts);
        }
        clearPath();
    } else if (m_pressMode == AddPoint) {
        // the point is there at the press; a drag moves it
        KisBrushStrokePathEdit::Hit added;
        m_strokeWithPoint = KisBrushStrokePathEdit::insertPoint(m_edit->originalStroke(), m_anchor, &added);
        if (added.valid) {
            m_anchor = added;
        }
        if (m_strokeWithPoint != m_edit->originalStroke()) {
            m_edit->setStrokes({m_strokeWithPoint});
            showPath(m_edit->shapes().value(0), m_anchor);
        }
    } else if (m_pressMode == DeletePoint) {
        const KisRecordedBrushStrokeSP remaining =
            KisBrushStrokePathEdit::deletePoint(m_edit->originalStroke(), m_anchor);
        if (remaining != m_edit->originalStroke()) {
            m_edit->setStrokes({remaining});
        } else {
            KisCanvas2 *kiscanvas = static_cast<KisCanvas2 *>(canvas());
            kiscanvas->viewManager()->showFloatingMessage(i18n("A line keeps at least two control points."), QIcon());
        }
        clearPath();
    } else if (m_pressMode == SwitchCorner) {
        const KisRecordedBrushStrokeSP switched =
            KisBrushStrokePathEdit::switchCorner(m_edit->originalStroke(), m_anchor);
        if (switched != m_edit->originalStroke()) {
            m_edit->setStrokes({switched});
        } else {
            KisCanvas2 *kiscanvas = static_cast<KisCanvas2 *>(canvas());
            kiscanvas->viewManager()->showFloatingMessage(
                i18n("Only control points between a line's ends can be switched."),
                QIcon());
        }
        clearPath();
    }
}

void KisToolEditBrushStrokes::continuePrimaryAction(KoPointerEvent *event)
{
    CHECK_MODE_SANITY_OR_RETURN(KisTool::PAINT_MODE);
    if (m_pressMode == FreeTransform) {
        if (m_frameHandle >= 0) {
            // the frame follows; the lines are drawn again at the release
            m_dragFrame = movedFrame(m_frame, m_frameHandle, convertToPixelCoord(event) - m_startPoint);
            updateCanvasPixelRect(image() ? QRectF(image()->bounds()) : QRectF());
        }
        return;
    }
    if (m_pressMode == Redraw || m_pressMode == RedrawWidth) {
        if (m_edit) {
            // the new path is drawn; the line takes it at the release
            m_redrawPath << convertToPixelCoord(event);
            m_redrawPressures << event->pressure();
            updateCanvasPixelRect(image() ? QRectF(image()->bounds()) : QRectF());
        }
        return;
    }
    if (!m_edit || m_pressMode == Cut || m_pressMode == DeletePoint || m_pressMode == SwitchCorner) {
        return;
    }

    const QPointF point = convertToPixelCoord(event);
    const qreal recordReach = reach() * m_recordScale;
    KisRecordedBrushStrokeSP edited;
    if (m_pressMode == AddPoint) {
        // only the new point follows the cursor
        edited = KisBrushStrokePathEdit::movePoints(m_strokeWithPoint,
                                                    m_anchor,
                                                    m_toRecord.map(point) - m_toRecord.map(m_startPoint),
                                                    0.0);
    } else if (m_pressMode == ChangeWidth) {
        // to the right: wider, to the left: thinner
        const qreal distance = pixelToView(point).x() - m_startViewPoint.x();
        edited = KisBrushStrokePathEdit::scaleWidth(m_edit->originalStroke(),
                                                    m_anchor,
                                                    qPow(2.0, distance / WidthDragDistance),
                                                    recordReach);
    } else if (m_pressMode == ChangeDensity) {
        // to the right: denser (up to the line as drawn), to the left:
        // lighter
        const qreal distance = pixelToView(point).x() - m_startViewPoint.x();
        edited = KisBrushStrokePathEdit::scaleDensity(m_edit->originalStroke(),
                                                      m_anchor,
                                                      qPow(2.0, distance / WidthDragDistance),
                                                      recordReach);
    } else if (m_pressMode == AddWidth) {
        // to the right: wider by a fixed amount, to the left: thinner
        const qreal distance = pixelToView(point).x() - m_startViewPoint.x();
        edited =
            KisBrushStrokePathEdit::addWidth(m_edit->originalStroke(),
                                             m_anchor,
                                             distance * imagePixelsPerViewPixel() / AddWidthDragRatio * m_recordScale,
                                             recordReach);
    } else {
        edited = KisBrushStrokePathEdit::movePoints(m_edit->originalStroke(),
                                                    m_anchor,
                                                    m_toRecord.map(point) - m_toRecord.map(m_startPoint),
                                                    recordReach);
    }
    m_edit->setStrokes({edited});

    // the path follows the edit; the marker stays on the dragged place
    const QList<KisBrushStrokeShape *> shapes = m_edit->shapes();
    if (!shapes.isEmpty()) {
        KisBrushStrokePathEdit::Hit place = m_anchor;
        place.position = KisBrushStrokePathEdit::nearestPoint(shapes.first()->brushStroke(),
                                                              m_pressMode == ChangeWidth || m_pressMode == ChangeDensity
                                                                      || m_pressMode == AddWidth
                                                                  ? m_anchor.position
                                                                  : m_toRecord.map(point))
                             .position;
        showPath(shapes.first(), place);
    }
}

void KisToolEditBrushStrokes::endPrimaryAction(KoPointerEvent *event)
{
    CHECK_MODE_SANITY_OR_RETURN(KisTool::PAINT_MODE);
    if (m_pressMode == FreeTransform && m_frameHandle >= 0) {
        const int handle = m_frameHandle;
        // where the button is released, whatever the last move said
        const QPolygonF target = movedFrame(m_frame, handle, convertToPixelCoord(event) - m_startPoint);
        m_frameHandle = -1;
        m_dragFrame.clear();
        const QPointF moved = pixelToView(convertToPixelCoord(event)) - m_startViewPoint;
        if (std::hypot(moved.x(), moved.y()) < 2.0) {
            // a click: inside the frame, the line there joins or leaves
            // the selection
            if (handle == 8) {
                toggleSelected(findTarget(m_startPoint).shape);
            }
        } else if (isUsableFrame(target)) {
            transformSelected(target);
        } else {
            KisCanvas2 *kiscanvas = static_cast<KisCanvas2 *>(canvas());
            kiscanvas->viewManager()->showFloatingMessage(i18n("The frame cannot be folded or flattened."), QIcon());
        }
        updateCanvasPixelRect(image() ? QRectF(image()->bounds()) : QRectF());
        setMode(KisTool::HOVER_MODE);
        return;
    }
    if (m_pressMode == Redraw && m_edit) {
        m_redrawPath << convertToPixelCoord(event);
        qreal viewLength = 0.0;
        for (int i = 1; i < m_redrawPath.size(); i++) {
            const QPointF step = pixelToView(m_redrawPath[i]) - pixelToView(m_redrawPath[i - 1]);
            viewLength += std::hypot(step.x(), step.y());
        }
        const QPolygonF path = m_toRecord.map(m_redrawPath);
        m_redrawPath.clear();
        // a click selects, as below; a drag gives its path to the line
        if (viewLength >= 3.0) {
            const KisRecordedBrushStrokeSP stroke = m_edit->originalStroke();
            // the path ends on the line if it ends as near to it as a
            // press has to be
            const KisPaintOpPresetSP preset = stroke->preset;
            const qreal tolerance = qMax(PickDistance * imagePixelsPerViewPixel() * m_recordScale,
                                         preset && preset->settings() ? preset->settings()->paintOpSize() * 0.5 : 0.0);
            KisBrushStrokePathEdit::Hit end = KisBrushStrokePathEdit::nearestPoint(stroke, path.last());
            if (end.valid && end.distance > tolerance) {
                end = KisBrushStrokePathEdit::Hit();
            }
            const KisRecordedBrushStrokeSP edited = KisBrushStrokePathEdit::redrawn(stroke, m_anchor, end, path);
            if (edited != stroke) {
                m_edit->setStrokes({edited});
            } else {
                // nothing to replace: no selection change either
                m_edit.reset();
                clearPath();
                updateCanvasPixelRect(image() ? QRectF(image()->bounds()) : QRectF());
                setMode(KisTool::HOVER_MODE);
                return;
            }
        }
        clearPath();
    }
    if (m_pressMode == RedrawWidth && m_edit) {
        // the release itself has no pressure to give
        qreal viewLength = 0.0;
        for (int i = 1; i < m_redrawPath.size(); i++) {
            const QPointF step = pixelToView(m_redrawPath[i]) - pixelToView(m_redrawPath[i - 1]);
            viewLength += std::hypot(step.x(), step.y());
        }
        const QPolygonF path = m_toRecord.map(m_redrawPath);
        const QVector<qreal> pressures = m_redrawPressures;
        m_redrawPath.clear();
        m_redrawPressures.clear();
        const KisPaintOpPresetSP brush = currentPaintOpPreset();
        // a click selects, as below; a drag gives the line the width the
        // current brush has along it
        if (viewLength >= 3.0 && brush && brush->settings()) {
            const KisRecordedBrushStrokeSP stroke = m_edit->originalStroke();
            const qreal brushSize = brush->settings()->paintOpSize() * m_recordScale;
            const KisPaintOpPresetSP preset = stroke->preset;
            // the pen is on the line where its dab touches the line's
            const qreal tolerance =
                qMax(PickDistance * imagePixelsPerViewPixel() * m_recordScale,
                     (brushSize + (preset && preset->settings() ? preset->settings()->paintOpSize() : 0.0)) * 0.5);
            QVector<KisBrushStrokePathEdit::Hit> places;
            QVector<qreal> widths;
            for (int i = 0; i < path.size(); i++) {
                const KisBrushStrokePathEdit::Hit place = KisBrushStrokePathEdit::nearestPoint(stroke, path[i]);
                if (place.valid && place.distance <= tolerance) {
                    places << place;
                    widths << brushSize * KisBrushStrokePathEdit::widthShare(brush, pressures.value(i, 1.0));
                }
            }
            const KisRecordedBrushStrokeSP edited = KisBrushStrokePathEdit::redrawnWidth(stroke, places, widths);
            if (edited != stroke) {
                m_edit->setStrokes({edited});
            } else {
                // nothing traced: no selection change either
                m_edit.reset();
                clearPath();
                updateCanvasPixelRect(image() ? QRectF(image()->bounds()) : QRectF());
                setMode(KisTool::HOVER_MODE);
                return;
            }
        }
        clearPath();
        updateCanvasPixelRect(image() ? QRectF(image()->bounds()) : QRectF());
    }
    if (m_edit && !m_edit->hasChanges()) {
        // a click without a drag: the line joins or leaves the selection;
        // in the modes that act at the press, nothing was there to do
        KisBrushStrokeShape *clicked = m_edit->originalShape();
        m_edit.reset();
        if (m_pressMode == MovePoints || m_pressMode == ChangeWidth || m_pressMode == ChangeDensity
            || m_pressMode == AddWidth || m_pressMode == FreeTransform || m_pressMode == Redraw
            || m_pressMode == RedrawWidth) {
            toggleSelected(clicked);
        }
    } else {
        finishEdit();
    }
    setMode(KisTool::HOVER_MODE);
}

QList<KisBrushStrokeShape *> KisToolEditBrushStrokes::selectedStrokes() const
{
    QList<KisBrushStrokeShape *> result;
    KisShapeLayerSP layer = vectorLayer();
    KoSelection *selection = canvas()->selectedShapesProxy() ? canvas()->selectedShapesProxy()->selection() : nullptr;
    if (!layer || !selection) {
        return result;
    }
    Q_FOREACH (KoShape *shape, selection->selectedEditableShapes()) {
        KisBrushStrokeShape *stroke = dynamic_cast<KisBrushStrokeShape *>(shape);
        // strokes in groups keep their place; only the layer's own are edited
        if (stroke && stroke->brushStroke() && stroke->parent() == layer.data()) {
            result << stroke;
        }
    }
    return result;
}

void KisToolEditBrushStrokes::toggleSelected(KisBrushStrokeShape *shape)
{
    KoSelection *selection = canvas()->selectedShapesProxy() ? canvas()->selectedShapesProxy()->selection() : nullptr;
    if (!selection || !shape) {
        return;
    }
    if (selection->isSelected(shape)) {
        selection->deselect(shape);
    } else {
        selection->select(shape);
    }
    updateCanvasPixelRect(image() ? QRectF(image()->bounds()) : QRectF());
}

void KisToolEditBrushStrokes::clearSelection()
{
    KoSelection *selection = canvas()->selectedShapesProxy() ? canvas()->selectedShapesProxy()->selection() : nullptr;
    if (selection && !selectedStrokes().isEmpty()) {
        selection->deselectAll();
        updateCanvasPixelRect(image() ? QRectF(image()->bounds()) : QRectF());
    }
}

void KisToolEditBrushStrokes::changeSelectedStrokes(
    const std::function<KisRecordedBrushStrokeSP(const KisBrushStrokeShape *)> &change,
    const KUndo2MagicString &text)
{
    KisShapeLayerSP layer = vectorLayer();
    const QList<KisBrushStrokeShape *> shapes = selectedStrokes();
    if (!layer || shapes.isEmpty() || !nodeEditable()) {
        if (layer && shapes.isEmpty()) {
            KisCanvas2 *kiscanvas = static_cast<KisCanvas2 *>(canvas());
            kiscanvas->viewManager()->showFloatingMessage(
                i18n("No lines are selected. Click lines with this tool to select them."),
                koIcon("object-locked"));
        }
        return;
    }
    finishEdit();
    clearPath();
    // the selection's shapes are replaced: its paths are flattened again
    m_shownSelection.clear();
    m_selectionLines.clear();

    QList<KisBrushStrokeShape *> replacements;
    // already applied; the command only undoes and redoes it
    KUndo2Command *command = KisBrushStrokeShapeEdit::replaceStrokes(layer, shapes, change, text, &replacements);
    if (!command) {
        return;
    }
    canvas()->addCommand(command);
    if (KoSelection *selection = canvas()->selectedShapesProxy()->selection()) {
        Q_FOREACH (KisBrushStrokeShape *shape, replacements) {
            selection->select(shape);
        }
    }
    updateCanvasPixelRect(image() ? QRectF(image()->bounds()) : QRectF());
}

QPolygonF KisToolEditBrushStrokes::frame()
{
    const QList<KisBrushStrokeShape *> selected = selectedStrokes();
    if (selected.isEmpty()) {
        m_frameShapes.clear();
        m_frame.clear();
        return m_frame;
    }
    if (selected == m_frameShapes && m_frame.size() == 4) {
        return m_frame;
    }
    // around the lines' paths, with room for the widest brush
    QRectF bounds;
    qreal margin = 0.0;
    Q_FOREACH (const KisBrushStrokeShape *shape, selected) {
        const QTransform toImage = shape->expectedTransform();
        for (const QPolygonF &line : KisBrushStrokeShapeEraser::strokeLines(shape->brushStroke())) {
            const QRectF rect = toImage.map(line).boundingRect();
            // a point's or a straight line's rectangle is empty but valid
            bounds = bounds.isNull() ? rect : bounds.united(rect.adjusted(0, 0, 1e-6, 1e-6));
        }
        const KisPaintOpPresetSP preset = shape->brushStroke()->preset;
        if (preset && preset->settings()) {
            margin = qMax(margin, preset->settings()->paintOpSize() * 0.5 * lengthScale(toImage));
        }
    }
    margin = qMax(margin, 2.0);
    bounds.adjust(-margin, -margin, margin, margin);
    m_frame = QPolygonF({bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()});
    m_frameShapes = selected;
    return m_frame;
}

int KisToolEditBrushStrokes::frameHandleAt(const QPointF &imagePoint)
{
    const QPolygonF corners = frame();
    if (corners.size() != 4) {
        return -1;
    }
    const QPointF view = pixelToView(imagePoint);
    int best = -1;
    qreal bestDistance = PickDistance;
    for (int i = 0; i < 8; i++) {
        const QPointF handle = pixelToView(i < 4 ? corners[i] : (corners[i - 4] + corners[(i - 3) % 4]) / 2.0);
        const qreal distance = std::hypot(handle.x() - view.x(), handle.y() - view.y());
        if (distance <= bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    if (best < 0 && corners.containsPoint(imagePoint, Qt::OddEvenFill)) {
        best = 8;
    }
    return best;
}

void KisToolEditBrushStrokes::transformSelected(const QPolygonF &target)
{
    const QPolygonF source = frame();
    QTransform transform;
    if (source.size() != 4 || target == source || !QTransform::quadToQuad(source, target, transform)) {
        return;
    }
    bool mirrored = false;
    bool changed = false;
    changeSelectedStrokes(
        [&](const KisBrushStrokeShape *shape) {
            const KisRecordedBrushStrokeSP stroke = shape->brushStroke();
            const QTransform toImage = shape->expectedTransform();
            if (!KisBrushStrokePathEdit::canMapPoints(stroke)) {
                mirrored = true;
                return KisRecordedBrushStrokeSP();
            }
            if (!toImage.isInvertible()) {
                return KisRecordedBrushStrokeSP();
            }
            const QTransform toRecord = toImage.inverted();
            KisRecordedBrushStrokeSP moved = KisBrushStrokePathEdit::mapped(stroke, [&](const QPointF &point) {
                return toRecord.map(transform.map(toImage.map(point)));
            });
            if (moved == stroke) {
                return KisRecordedBrushStrokeSP();
            }
            // the brush follows the size the lines get where they are
            const QPointF center = toImage.mapRect(shape->recordRect()).center();
            const QPointF origin = transform.map(center);
            const QPointF alongX = transform.map(center + QPointF(1, 0)) - origin;
            const QPointF alongY = transform.map(center + QPointF(0, 1)) - origin;
            const qreal scale = qSqrt(qAbs(alongX.x() * alongY.y() - alongX.y() * alongY.x()));
            if (stroke->preset && stroke->preset->settings() && qIsFinite(scale) && scale > 1e-3
                && qAbs(scale - 1.0) > 1e-6) {
                moved = KisBrushStrokePathEdit::withBrushSize(moved, stroke->preset->settings()->paintOpSize() * scale);
            }
            changed = true;
            return moved;
        },
        kundo2_i18n("Transform Brush Strokes"));
    if (changed) {
        // the frame stays as it was dragged, around the new shapes
        m_frame = target;
        m_frameShapes = selectedStrokes();
    }
    if (mirrored) {
        KisCanvas2 *kiscanvas = static_cast<KisCanvas2 *>(canvas());
        kiscanvas->viewManager()->showFloatingMessage(
            i18n("Lines drawn with the canvas mirror cannot be transformed here."),
            koIcon("object-locked"));
    }
}

void KisToolEditBrushStrokes::applyColor()
{
    const KoColor color = canvas()->resourceManager()->foregroundColor();
    changeSelectedStrokes(
        [&](const KisBrushStrokeShape *shape) {
            return KisBrushStrokePathEdit::withColor(shape->brushStroke(), color);
        },
        kundo2_i18n("Change Brush Stroke Color"));
}

void KisToolEditBrushStrokes::applyBackgroundColor()
{
    const KoColor color = canvas()->resourceManager()->backgroundColor();
    changeSelectedStrokes(
        [&](const KisBrushStrokeShape *shape) {
            return KisBrushStrokePathEdit::withBackgroundColor(shape->brushStroke(), color);
        },
        kundo2_i18n("Change Brush Stroke Background Color"));
}

void KisToolEditBrushStrokes::applySize()
{
    KisPaintOpPresetSP preset = currentPaintOpPreset();
    if (!preset || !preset->settings()) {
        return;
    }
    // the brush size is in image pixels; a line on a scaled layer was
    // recorded at another size
    const qreal size = preset->settings()->paintOpSize();
    changeSelectedStrokes(
        [&](const KisBrushStrokeShape *shape) {
            const qreal scale = lengthScale(shape->expectedTransform());
            return KisBrushStrokePathEdit::withBrushSize(shape->brushStroke(), scale > 0.0 ? size / scale : size);
        },
        kundo2_i18n("Change Brush Stroke Size"));
}

void KisToolEditBrushStrokes::applyBrush()
{
    if (!image() || !currentNode() || !currentPaintOpPreset()) {
        return;
    }
    // the preset as a stroke takes it: with its resources, ready to be
    // drawn in another thread and saved with the document
    KisResourcesSnapshotSP snapshot = new KisResourcesSnapshot(image(), currentNode(), canvas()->resourceManager());
    KisPaintOpPresetSP preset = snapshot->currentPaintOpPreset();
    if (!preset) {
        return;
    }
    changeSelectedStrokes(
        [&](const KisBrushStrokeShape *shape) {
            return KisBrushStrokePathEdit::withBrush(shape->brushStroke(), preset);
        },
        kundo2_i18n("Change Brush Stroke Brush"));
}

void KisToolEditBrushStrokes::simplify()
{
    // the tolerance is in image pixels; a line on a scaled layer was
    // recorded at another size
    const qreal tolerance = simplifyTolerance();
    int before = 0;
    int after = 0;
    const bool hadSelection = !selectedStrokes().isEmpty();
    changeSelectedStrokes(
        [&](const KisBrushStrokeShape *shape) {
            const qreal scale = lengthScale(shape->expectedTransform());
            const KisRecordedBrushStrokeSP stroke = shape->brushStroke();
            const KisRecordedBrushStrokeSP simple =
                KisBrushStrokePathEdit::simplified(stroke, scale > 0.0 ? tolerance / scale : tolerance);
            before += KisBrushStrokePathEdit::pointCount(stroke);
            after += KisBrushStrokePathEdit::pointCount(simple);
            return simple;
        },
        kundo2_i18n("Simplify Brush Strokes"));
    if (!hadSelection || !before) {
        return;
    }
    KisCanvas2 *kiscanvas = static_cast<KisCanvas2 *>(canvas());
    kiscanvas->viewManager()->showFloatingMessage(after < before
                                                      ? i18n("Control points: %1 of %2 left", after, before)
                                                      : i18n("No control points can be removed at this tolerance."),
                                                  QIcon());
}

void KisToolEditBrushStrokes::createAssistants()
{
    KisCanvas2 *kiscanvas = static_cast<KisCanvas2 *>(canvas());
    KisPaintingAssistantsDecorationSP decoration = kiscanvas->paintingAssistantsDecoration();
    const QList<KisBrushStrokeShape *> shapes = selectedStrokes();
    if (!vectorLayer() || !image() || !decoration) {
        return;
    }
    if (shapes.isEmpty()) {
        kiscanvas->viewManager()->showFloatingMessage(
            i18n("No lines are selected. Click lines with this tool to select them."),
            koIcon("object-locked"));
        return;
    }
    finishEdit();

    // a Spline assistant for every curved piece of the lines' paths, a
    // Ruler assistant for every straight one; assistants are in document
    // coordinates
    const qreal tolerance = simplifyTolerance();
    QList<KisPaintingAssistantSP> created;
    Q_FOREACH (const KisBrushStrokeShape *shape, shapes) {
        const QTransform toImage = shape->expectedTransform();
        const qreal scale = lengthScale(toImage);
        const QVector<KisBrushStrokePathEdit::PathPiece> pieces =
            KisBrushStrokePathEdit::pathPieces(shape->brushStroke(), scale > 0.0 ? tolerance / scale : tolerance);
        for (const KisBrushStrokePathEdit::PathPiece &piece : pieces) {
            KisPaintingAssistantFactory *factory = KisPaintingAssistantFactoryRegistry::instance()->get(
                piece.curve ? QStringLiteral("spline") : QStringLiteral("ruler"));
            if (!factory) {
                continue;
            }
            KisPaintingAssistantSP assistant = toQShared(factory->createPaintingAssistant());
            auto add = [&](const QPointF &point) {
                assistant->addHandle(new KisPaintingAssistantHandle(image()->pixelToDocument(toImage.map(point))),
                                     HandleType::NORMAL);
            };
            // a spline's handles: its two ends, then the two controls
            add(piece.start);
            add(piece.end);
            if (piece.curve) {
                add(piece.control1);
                add(piece.control2);
            }
            created << assistant;
        }
    }
    if (created.isEmpty()) {
        return;
    }
    const QList<KisPaintingAssistantSP> before = decoration->assistants();
    canvas()->addCommand(new AssistantsCommand(kiscanvas,
                                               before,
                                               before + created,
                                               kundo2_i18n("Create Assistants from Brush Strokes")));
    kiscanvas->viewManager()->showFloatingMessage(i18n("Assistants created: %1", created.size()), QIcon());
}

void KisToolEditBrushStrokes::selectInside()
{
    KisCanvas2 *kiscanvas = static_cast<KisCanvas2 *>(canvas());
    const QList<KisBrushStrokeShape *> shapes = selectedStrokes();
    if (!vectorLayer() || !image()) {
        return;
    }
    if (shapes.isEmpty()) {
        kiscanvas->viewManager()->showFloatingMessage(
            i18n("No lines are selected. Click lines with this tool to select them."),
            koIcon("object-locked"));
        return;
    }
    finishEdit();

    // what each line encloses, in image pixels; together
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    Q_FOREACH (const KisBrushStrokeShape *shape, shapes) {
        path.addPath(shape->expectedTransform().map(KisBrushStrokePathEdit::closedPath(shape->brushStroke())));
    }
    KisPixelSelectionSP pixels = new KisPixelSelection(new KisDefaultBounds(image()));
    {
        KisPainter painter(pixels);
        painter.setPaintColor(KoColor(Qt::black, pixels->colorSpace()));
        painter.setAntiAliasPolygonFill(true);
        painter.setFillStyle(KisPainter::FillStyleForegroundColor);
        painter.setStrokeStyle(KisPainter::StrokeStyleNone);
        painter.paintPainterPath(path);
    }
    if (pixels->selectedExactRect().isEmpty()) {
        kiscanvas->viewManager()->showFloatingMessage(i18n("The selected lines enclose no area."), QIcon());
        return;
    }
    pixels->invalidateOutlineCache();
    KisSelectionToolHelper helper(kiscanvas, kundo2_i18n("Select Inside Brush Strokes"));
    helper.selectPixelSelection(pixels, SELECTION_REPLACE);
}

void KisToolEditBrushStrokes::joinSelected()
{
    KisShapeLayerSP layer = vectorLayer();
    if (!layer || !nodeEditable()) {
        return;
    }
    const QList<KisBrushStrokeShape *> shapes = selectedStrokes();
    int usable = 0;
    Q_FOREACH (const KisBrushStrokeShape *shape, shapes) {
        usable += KisBrushStrokePathEdit::canJoin(shape->brushStroke());
    }
    if (usable < 2) {
        KisCanvas2 *kiscanvas = static_cast<KisCanvas2 *>(canvas());
        kiscanvas->viewManager()->showFloatingMessage(shapes.size() < 2
                                                          ? i18n("Select two or more lines to join them.")
                                                          : i18n("Lines drawn with mirrored copies cannot be joined."),
                                                      koIcon("object-locked"));
        return;
    }
    finishEdit();
    clearPath();
    // the selection's shapes are replaced: its paths are flattened again
    m_shownSelection.clear();
    m_selectionLines.clear();

    KisBrushStrokeShape *joined = nullptr;
    // already applied; the command only undoes and redoes it
    KUndo2Command *command =
        KisBrushStrokeShapeEdit::joinStrokes(layer, shapes, kundo2_i18n("Join Brush Strokes"), &joined);
    if (!command) {
        return;
    }
    canvas()->addCommand(command);
    if (KoSelection *selection = canvas()->selectedShapesProxy()->selection()) {
        selection->select(joined);
    }
    updateCanvasPixelRect(image() ? QRectF(image()->bounds()) : QRectF());
}

void KisToolEditBrushStrokes::finishEdit()
{
    if (!m_edit) {
        return;
    }
    KUndo2MagicString text = kundo2_i18n("Move Brush Stroke Points");
    if (m_pressMode == ChangeWidth) {
        text = kundo2_i18n("Change Brush Stroke Width");
    } else if (m_pressMode == Cut) {
        text = kundo2_i18n("Cut Brush Stroke");
    } else if (m_pressMode == AddPoint) {
        text = kundo2_i18n("Add Brush Stroke Point");
    } else if (m_pressMode == DeletePoint) {
        text = kundo2_i18n("Delete Brush Stroke Point");
    } else if (m_pressMode == SwitchCorner) {
        text = kundo2_i18n("Switch Brush Stroke Corner");
    } else if (m_pressMode == ChangeDensity) {
        text = kundo2_i18n("Change Brush Stroke Density");
    } else if (m_pressMode == AddWidth) {
        text = kundo2_i18n("Add Brush Stroke Width");
    } else if (m_pressMode == Redraw) {
        text = kundo2_i18n("Redraw Brush Stroke");
    } else if (m_pressMode == RedrawWidth) {
        text = kundo2_i18n("Redraw Brush Stroke Width");
    }
    // already applied; the command only undoes and redoes it
    const QList<KisBrushStrokeShape *> edited = m_edit->shapes();
    KUndo2Command *command = m_edit->finish(text);
    m_edit.reset();
    if (command) {
        canvas()->addCommand(command);
        // an edited line stays selected
        KoSelection *selection =
            canvas()->selectedShapesProxy() ? canvas()->selectedShapesProxy()->selection() : nullptr;
        if (m_editedShapeWasSelected && selection) {
            Q_FOREACH (KisBrushStrokeShape *shape, edited) {
                selection->select(shape);
            }
        }
    }
    m_editedShapeWasSelected = false;
    m_shownSelection.clear();
    m_selectionLines.clear();
}

void KisToolEditBrushStrokes::paint(QPainter &gc, const KoViewConverter &converter)
{
    Q_UNUSED(converter);

    // the selected lines' paths; flattened again only when the selection
    // changes (an edit replaces its shapes)
    const QList<KisBrushStrokeShape *> selected = selectedStrokes();
    if (selected != m_shownSelection) {
        m_shownSelection = selected;
        m_selectionLines.clear();
        Q_FOREACH (const KisBrushStrokeShape *shape, selected) {
            const QTransform toImage = shape->expectedTransform();
            for (const QPolygonF &line : KisBrushStrokeShapeEraser::strokeLines(shape->brushStroke())) {
                m_selectionLines << toImage.map(line);
            }
        }
    }
    if (m_lines.isEmpty() && m_selectionLines.isEmpty() && m_redrawPath.isEmpty()) {
        return;
    }

    gc.save();
    gc.setRenderHint(QPainter::Antialiasing);
    {
        QPen selectionHalo(QColor(255, 255, 255, 200), 4.0);
        selectionHalo.setCosmetic(true);
        QPen selectionPen(QColor(255, 140, 0), 2.0);
        selectionPen.setCosmetic(true);
        gc.setBrush(Qt::NoBrush);
        for (const QPolygonF &line : m_selectionLines) {
            QPolygonF view;
            view.reserve(line.size());
            for (const QPointF &point : line) {
                view << pixelToView(point);
            }
            gc.setPen(selectionHalo);
            gc.drawPolyline(view);
            gc.setPen(selectionPen);
            gc.drawPolyline(view);
        }
    }
    if (editMode() == FreeTransform) {
        const QPolygonF corners = m_frameHandle >= 0 ? m_dragFrame : frame();
        if (corners.size() == 4) {
            QPen framePen(QColor(40, 120, 255), 1.0);
            framePen.setCosmetic(true);
            if (m_frameHandle >= 0) {
                // where the lines go
                QTransform transform;
                if (isUsableFrame(corners) && QTransform::quadToQuad(m_frame, corners, transform)) {
                    gc.setPen(framePen);
                    gc.setBrush(Qt::NoBrush);
                    for (const QPolygonF &line : m_selectionLines) {
                        QPolygonF view;
                        view.reserve(line.size());
                        for (const QPointF &point : line) {
                            view << pixelToView(transform.map(point));
                        }
                        gc.drawPolyline(view);
                    }
                }
            }
            QPolygonF view;
            for (const QPointF &corner : corners) {
                view << pixelToView(corner);
            }
            QPen frameHalo(QColor(255, 255, 255, 200), 3.0);
            frameHalo.setCosmetic(true);
            gc.setBrush(Qt::NoBrush);
            gc.setPen(frameHalo);
            gc.drawPolygon(view);
            gc.setPen(framePen);
            gc.drawPolygon(view);
            gc.setBrush(Qt::white);
            for (int i = 0; i < 4; i++) {
                const QPointF side = (view[i] + view[(i + 1) % 4]) / 2.0;
                gc.drawRect(QRectF(view[i].x() - 4.0, view[i].y() - 4.0, 8.0, 8.0));
                gc.drawRect(QRectF(side.x() - 3.0, side.y() - 3.0, 6.0, 6.0));
            }
        }
    }
    if (m_redrawPath.size() > 1) {
        // Redraw: the path as it is drawn
        QPolygonF view;
        view.reserve(m_redrawPath.size());
        for (const QPointF &point : std::as_const(m_redrawPath)) {
            view << pixelToView(point);
        }
        const KisPaintOpPresetSP brush = currentPaintOpPreset();
        if (m_pressMode == RedrawWidth && brush && brush->settings()) {
            // Redraw Width: as wide as the current brush
            QPen wide(QColor(230, 40, 90, 70),
                      qMax(1.0, brush->settings()->paintOpSize() / imagePixelsPerViewPixel()),
                      Qt::SolidLine,
                      Qt::RoundCap,
                      Qt::RoundJoin);
            gc.setBrush(Qt::NoBrush);
            gc.setPen(wide);
            gc.drawPolyline(view);
        }
        QPen drawnHalo(QColor(255, 255, 255, 200), 4.0);
        drawnHalo.setCosmetic(true);
        QPen drawnPen(QColor(230, 40, 90), 2.0);
        drawnPen.setCosmetic(true);
        gc.setBrush(Qt::NoBrush);
        gc.setPen(drawnHalo);
        gc.drawPolyline(view);
        gc.setPen(drawnPen);
        gc.drawPolyline(view);
    }
    if (m_lines.isEmpty()) {
        gc.restore();
        return;
    }
    const QColor pathColor(40, 120, 255);
    QPen halo(QColor(255, 255, 255, 200), 3.0);
    halo.setCosmetic(true);
    QPen pen(pathColor, 1.5);
    pen.setCosmetic(true);

    for (const QPolygonF &line : m_lines) {
        QPolygonF view;
        view.reserve(line.size());
        for (const QPointF &point : line) {
            view << pixelToView(point);
        }
        gc.setBrush(Qt::NoBrush);
        gc.setPen(halo);
        gc.drawPolyline(view);
        gc.setPen(pen);
        gc.drawPolyline(view);
    }

    // the control points, thinned out where they crowd; a corner (known
    // in Switch Corner) is a square
    gc.setPen(pen);
    gc.setBrush(Qt::white);
    QPointF last;
    bool hasLast = false;
    for (int i = 0; i < m_points.size(); i++) {
        const QPointF view = pixelToView(m_points[i]);
        if (hasLast && std::hypot(view.x() - last.x(), view.y() - last.y()) < 7.0) {
            continue;
        }
        if (m_pointCorners.value(i)) {
            gc.drawRect(QRectF(view.x() - 3.0, view.y() - 3.0, 6.0, 6.0));
        } else {
            gc.drawEllipse(view, 2.5, 2.5);
        }
        last = view;
        hasLast = true;
    }

    if (m_hasMarker) {
        const QPointF view = pixelToView(m_marker);
        gc.setBrush(pathColor);
        gc.drawEllipse(view, 4.0, 4.0);
        const Mode mode = editMode();
        if ((mode == MovePoints || mode == ChangeWidth || mode == ChangeDensity || mode == AddWidth) && reach() > 0) {
            // how far along the line the edit reaches
            const qreal radius = reach() / imagePixelsPerViewPixel();
            QPen dashed(pathColor, 1.0, Qt::DashLine);
            dashed.setCosmetic(true);
            gc.setPen(dashed);
            gc.setBrush(Qt::NoBrush);
            gc.drawEllipse(view, radius, radius);
        }
    }
    gc.restore();
}

QWidget *KisToolEditBrushStrokes::createOptionWidget()
{
    QWidget *widget = new QWidget();
    widget->setObjectName("KisToolEditBrushStrokesOptions");
    QFormLayout *layout = new QFormLayout(widget);
    KConfigGroup config = KSharedConfig::openConfig()->group(ConfigGroup);

    m_cmbMode = new QComboBox(widget);
    m_cmbMode->setObjectName("cmbEditBrushStrokesMode");
    m_cmbMode->addItems(QStringList() << i18nc("@item:inlistbox Edit Brush Strokes", "Move Points")
                                      << i18nc("@item:inlistbox Edit Brush Strokes", "Change Width")
                                      << i18nc("@item:inlistbox Edit Brush Strokes", "Cut")
                                      << i18nc("@item:inlistbox Edit Brush Strokes", "Add Point")
                                      << i18nc("@item:inlistbox Edit Brush Strokes", "Delete Point")
                                      << i18nc("@item:inlistbox Edit Brush Strokes", "Switch Corner")
                                      << i18nc("@item:inlistbox Edit Brush Strokes", "Change Density")
                                      << i18nc("@item:inlistbox Edit Brush Strokes", "Add Width")
                                      << i18nc("@item:inlistbox Edit Brush Strokes", "Free Transform")
                                      << i18nc("@item:inlistbox Edit Brush Strokes", "Redraw")
                                      << i18nc("@item:inlistbox Edit Brush Strokes", "Redraw Width"));
    m_cmbMode->setToolTip(
        i18n("Move Points: drag part of a line. Change Width: drag right for a wider line, left for "
             "a thinner one. Cut: click a line to cut it in two. Add Point: click a line for a new "
             "control point and drag to move it. Delete Point: click a control point to remove it. "
             "Switch Corner: click a control point to switch it between a corner and a curve. "
             "Change Density: drag left for a lighter line, right to bring it back. Add Width: drag "
             "right to make a line wider by a fixed amount, left to make it thinner. Free Transform: "
             "click lines to select them, then drag the corners, the sides or the inside of their "
             "frame. Redraw: start on a line and draw the path that part of it should take. Redraw "
             "Width: trace part of a line to give it the width of the current brush."));
    m_cmbMode->setCurrentIndex(qBound(0, config.readEntry("mode", 0), int(RedrawWidth)));
    connect(m_cmbMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        KSharedConfig::openConfig()->group(ConfigGroup).writeEntry("mode", index);
        clearPath();
        // the frame of Free Transform comes or goes, around the lines as
        // they are now
        m_frameShapes.clear();
        updateCanvasPixelRect(image() ? QRectF(image()->bounds()) : QRectF());
    });
    layout->addRow(i18n("Mode:"), m_cmbMode);

    m_sliderReach = new KisSliderSpinBox(widget);
    m_sliderReach->setObjectName("sliderEditBrushStrokesReach");
    m_sliderReach->setRange(0, 2000);
    m_sliderReach->setSuffix(i18n(" px"));
    m_sliderReach->setToolTip(
        i18n("How far along the line an edit reaches from the dragged point, in image pixels. "
             "0 moves a single control point."));
    m_sliderReach->setValue(qBound(0, config.readEntry("reach", 60), 2000));
    connect(m_sliderReach, QOverload<int>::of(&KisSliderSpinBox::valueChanged), this, [](int value) {
        KSharedConfig::openConfig()->group(ConfigGroup).writeEntry("reach", value);
    });
    layout->addRow(i18n("Reach:"), m_sliderReach);

    // the selected lines take the current color, brush size or brush
    QPushButton *applyColor = new QPushButton(i18n("Apply Color"), widget);
    applyColor->setObjectName("btnEditBrushStrokesApplyColor");
    applyColor->setToolTip(i18n("Draws the selected lines with the current foreground color"));
    connect(applyColor, &QPushButton::clicked, this, &KisToolEditBrushStrokes::applyColor);
    QPushButton *applyBackground = new QPushButton(i18n("Apply Background Color"), widget);
    applyBackground->setObjectName("btnEditBrushStrokesApplyBackgroundColor");
    applyBackground->setToolTip(
        i18n("Draws the selected lines with the current background color, which shows "
             "with brushes that mix the two colors or paint a gradient between them"));
    connect(applyBackground, &QPushButton::clicked, this, &KisToolEditBrushStrokes::applyBackgroundColor);
    QPushButton *applySize = new QPushButton(i18n("Apply Size"), widget);
    applySize->setObjectName("btnEditBrushStrokesApplySize");
    applySize->setToolTip(i18n("Draws the selected lines with the current brush size"));
    connect(applySize, &QPushButton::clicked, this, &KisToolEditBrushStrokes::applySize);
    QPushButton *applyBrush = new QPushButton(i18n("Apply Brush"), widget);
    applyBrush->setObjectName("btnEditBrushStrokesApplyBrush");
    applyBrush->setToolTip(i18n("Draws the selected lines with the current brush preset, at the width they have"));
    connect(applyBrush, &QPushButton::clicked, this, &KisToolEditBrushStrokes::applyBrush);
    QVBoxLayout *buttons = new QVBoxLayout();
    buttons->addWidget(applyColor);
    buttons->addWidget(applyBackground);
    buttons->addWidget(applySize);
    buttons->addWidget(applyBrush);
    QPushButton *join = new QPushButton(i18n("Join Lines"), widget);
    join->setObjectName("btnEditBrushStrokesJoin");
    join->setToolTip(
        i18n("Makes one line of the selected lines by connecting their nearest ends. It takes the "
             "brush and color of the line selected first."));
    connect(join, &QPushButton::clicked, this, &KisToolEditBrushStrokes::joinSelected);
    buttons->addWidget(join);
    QPushButton *assistants = new QPushButton(i18n("Create Assistants"), widget);
    assistants->setObjectName("btnEditBrushStrokesCreateAssistants");
    assistants->setToolTip(
        i18n("Makes painting assistants along the selected lines, simplified with the tolerance below: "
             "brushes with Snap to Assistants follow them. Turn off Snap to Single Line to follow a "
             "line across its pieces."));
    connect(assistants, &QPushButton::clicked, this, &KisToolEditBrushStrokes::createAssistants);
    buttons->addWidget(assistants);
    QPushButton *inside = new QPushButton(i18n("Select Inside"), widget);
    inside->setObjectName("btnEditBrushStrokesSelectInside");
    inside->setToolTip(
        i18n("Selects the area that each of the selected lines encloses. A line that is not closed is "
             "closed by a straight piece from its end to its start."));
    connect(inside, &QPushButton::clicked, this, &KisToolEditBrushStrokes::selectInside);
    buttons->addWidget(inside);
    layout->addRow(i18n("Selected lines:"), buttons);

    // fewer control points for the selected lines
    m_sliderSimplify = new KisDoubleSliderSpinBox(widget);
    m_sliderSimplify->setObjectName("sliderEditBrushStrokesSimplify");
    m_sliderSimplify->setRange(0.1, 20.0, 1);
    m_sliderSimplify->setSingleStep(0.1);
    m_sliderSimplify->setSuffix(i18n(" px"));
    m_sliderSimplify->setToolTip(
        i18n("How far a simplified line may leave its path, in image pixels. A larger value "
             "removes more control points."));
    m_sliderSimplify->setValue(qBound(0.1, config.readEntry("simplifyTolerance", 1.0), 20.0));
    connect(m_sliderSimplify, QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged), this, [](qreal value) {
        KSharedConfig::openConfig()->group(ConfigGroup).writeEntry("simplifyTolerance", value);
    });
    QPushButton *simplify = new QPushButton(i18n("Simplify Lines"), widget);
    simplify->setObjectName("btnEditBrushStrokesSimplify");
    simplify->setToolTip(
        i18n("Removes control points from the selected lines, keeping their shape within the tolerance"));
    connect(simplify, &QPushButton::clicked, this, &KisToolEditBrushStrokes::simplify);
    QVBoxLayout *simplifyColumn = new QVBoxLayout();
    simplifyColumn->addWidget(m_sliderSimplify);
    simplifyColumn->addWidget(simplify);
    layout->addRow(i18n("Tolerance:"), simplifyColumn);

    // keeps the form at the top of the docker
    QWidget *spacer = new QWidget(widget);
    spacer->setObjectName("SpecialSpacer");
    spacer->setFixedSize(0, 0);
    layout->addRow(spacer);
    return widget;
}
