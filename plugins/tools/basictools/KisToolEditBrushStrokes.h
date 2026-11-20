/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISTOOLEDITBRUSHSTROKES_H
#define KISTOOLEDITBRUSHSTROKES_H

#include <functional>

#include <QList>
#include <QPolygonF>
#include <QScopedPointer>
#include <QTransform>
#include <QVector>

#include <KisBrushStrokePathEdit.h>
#include <KoIcon.h>
#include <KoToolFactoryBase.h>
#include <kis_tool.h>
#include <kis_types.h>
#include <klocalizedstring.h>
#include <kundo2magicstring.h>

class QComboBox;
class KisDoubleSliderSpinBox;
class KisSliderSpinBox;
class KisBrushStrokeShape;

/**
 * Edits the paths of brush strokes on a vector layer (Solstice): drags part
 * of a line, changes its width, or cuts it at a point. See
 * docs/agent/vector-brush-strokes.md ("Editing the path").
 */
class KisToolEditBrushStrokes : public KisTool
{
    Q_OBJECT
public:
    enum Mode {
        MovePoints,
        ChangeWidth,
        Cut,
        AddPoint,
        DeletePoint,
        SwitchCorner,
        ChangeDensity,
        AddWidth,
        FreeTransform,
        Redraw,
        RedrawWidth,
    };

    KisToolEditBrushStrokes(KoCanvasBase *canvas);
    ~KisToolEditBrushStrokes() override;

    void beginPrimaryAction(KoPointerEvent *event) override;
    void continuePrimaryAction(KoPointerEvent *event) override;
    void endPrimaryAction(KoPointerEvent *event) override;
    void mouseMoveEvent(KoPointerEvent *event) override;

    void paint(QPainter &gc, const KoViewConverter &converter) override;
    QWidget *createOptionWidget() override;

public Q_SLOTS:
    void activate(const QSet<KoShape *> &shapes) override;
    void deactivate() override;

private:
    /// The stroke shape of the active vector layer nearest to a point
    struct Target {
        KisBrushStrokeShape *shape = nullptr;
        KisBrushStrokePathEdit::Hit hit;
    };
    Target findTarget(const QPointF &imagePoint) const;
    KisShapeLayerSP vectorLayer() const;
    /// image pixels shown by one pixel of the view
    qreal imagePixelsPerViewPixel() const;

    /// Shows the path and control points of @p shape, with @p place marked
    void showPath(const KisBrushStrokeShape *shape, const KisBrushStrokePathEdit::Hit &place);
    void clearPath();
    void finishEdit();

    /// The stroke shapes of the active vector layer that are selected (the
    /// layer's shape selection, shared with the Select Shapes Tool)
    QList<KisBrushStrokeShape *> selectedStrokes() const;
    /// Adds @p shape to the selection, or takes it out
    void toggleSelected(KisBrushStrokeShape *shape);
    void clearSelection();
    /// Replaces the selected lines' strokes by what @p change returns for
    /// them, as one undo step; the new shapes stay selected
    void changeSelectedStrokes(const std::function<KisRecordedBrushStrokeSP(const KisBrushStrokeShape *)> &change,
                               const KUndo2MagicString &text);

    /// Free Transform: the frame around the selected lines (empty without
    /// a selection); made again when the selection is another one
    QPolygonF frame();
    /// The part of the frame at @p imagePoint: 0 to 3 its corners, 4 to 7
    /// its sides (top, right, bottom, left), 8 its inside, -1 none
    int frameHandleAt(const QPointF &imagePoint);
    /// Moves the selected lines' points from the frame to @p target
    void transformSelected(const QPolygonF &target);

private Q_SLOTS:
    void applyColor();
    void applyBackgroundColor();
    void applySize();
    void applyBrush();
    void simplify();
    void joinSelected();
    void createAssistants();
    void selectInside();

private:
    Mode editMode() const;
    int reach() const;
    /// how far a simplified line may leave its path, image pixels
    qreal simplifyTolerance() const;

    QComboBox *m_cmbMode{nullptr};
    KisSliderSpinBox *m_sliderReach{nullptr};
    KisDoubleSliderSpinBox *m_sliderSimplify{nullptr};

    // the path shown on the canvas, in image pixels
    QVector<QPolygonF> m_lines;
    QPolygonF m_points;
    /// Switch Corner: which of m_points are corners
    QVector<bool> m_pointCorners;
    QPointF m_marker;
    bool m_hasMarker{false};

    // the edit of the drag in progress
    QScopedPointer<KisBrushStrokeShapeEdit> m_edit;
    /// the mode at the press
    Mode m_pressMode{MovePoints};
    /// Add Point: the stroke with the new point, which a drag then moves
    KisRecordedBrushStrokeSP m_strokeWithPoint;
    KisBrushStrokePathEdit::Hit m_anchor;
    QTransform m_toRecord;
    qreal m_recordScale{1.0};
    QPointF m_startPoint;
    QPointF m_startViewPoint;
    /// the line of the drag in progress was selected when it began
    bool m_editedShapeWasSelected{false};

    // Free Transform: the frame of the selected lines in image pixels (top
    // left, top right, bottom right, bottom left) and the lines it is of
    QPolygonF m_frame;
    QList<KisBrushStrokeShape *> m_frameShapes;
    /// the part of the frame that is dragged (frameHandleAt()), and the
    /// frame as the drag has it
    int m_frameHandle{-1};
    QPolygonF m_dragFrame;

    /// Redraw and Redraw Width: the path drawn so far, in image pixels
    QPolygonF m_redrawPath;
    /// Redraw Width: the pen pressure at each point of the path
    QVector<qreal> m_redrawPressures;

    // the selected lines' paths as last drawn, in image pixels
    QList<KisBrushStrokeShape *> m_shownSelection;
    QVector<QPolygonF> m_selectionLines;
};

class KisToolEditBrushStrokesFactory : public KoToolFactoryBase
{
public:
    KisToolEditBrushStrokesFactory()
        : KoToolFactoryBase("KritaShape/KisToolEditBrushStrokes")
    {
        setToolTip(i18n("Edit Brush Strokes Tool"));
        setSection(ToolBoxSection::Shape);
        setIconName(koIconNameCStr("krita_tool_edit_brush_strokes"));
        setPriority(13);
        setActivationShapeId(KRITA_TOOL_ACTIVATION_ID);
    }

    ~KisToolEditBrushStrokesFactory() override
    {
    }

    KoToolBase *createTool(KoCanvasBase *canvas) override
    {
        return new KisToolEditBrushStrokes(canvas);
    }
};

#endif // KISTOOLEDITBRUSHSTROKES_H
