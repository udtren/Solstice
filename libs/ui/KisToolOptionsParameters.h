/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISTOOLOPTIONSPARAMETERS_H
#define KISTOOLOPTIONSPARAMETERS_H

#include <functional>

#include <QObject>
#include <QPointer>

#include "kritaui_export.h"

class QToolButton;
class QWidget;
class KisPaintOpOption;

/**
 * Brush Editor page parameters shown in Tool Options (phase 3b of
 * docs/agent/tool-options-brush.md): the eye button next to a page control,
 * and the Tool Options copy of the control.
 */
namespace KisToolOptionsParameters
{
/// A checkable eye button, hidden until the engine supports Tool Options
KRITAUI_EXPORT QToolButton *createEyeButton(QWidget *parent);

/**
 * Puts @p eye in front of @p label (or of @p control without a label) in
 * its layout: the label or control is wrapped with the eye in a row. When
 * @p label is null, the label is searched in the control's grid or form
 * layout row. Returns false when no layout holds the target.
 */
KRITAUI_EXPORT bool placeEyeButton(QToolButton *eye, QWidget *control, QWidget *label = nullptr);

/// Whether KisToolOptionsParameterMirror supports @p control
KRITAUI_EXPORT bool canMirror(QWidget *control);
} // namespace KisToolOptionsParameters

/**
 * A copy of a Brush Editor control for Tool Options, kept in sync with it.
 *
 * Supported controls: KisDoubleSliderSpinBox, KisSliderSpinBox,
 * KisMultipliersDoubleSliderSpinBox, QCheckBox, KisAngleSelector,
 * KisSpacingSelectionWidget, KisCompositeOpListWidget, and a widget holding
 * radio buttons (e.g. a group box).
 *
 * The copy writes through the editor control in the way the control's own
 * connection to its option listens to (setValue(), click(), the spacing
 * widget's and the composite op list's signals). It reads the control again
 * after every change of the option, since the option model updates some
 * controls with blocked signals. The copy follows the control's enabled
 * state, and isShownInEditor() tells whether the mode widget is shown in its
 * page (e.g. the Auto or Predefined brush tip).
 */
class KRITAUI_EXPORT KisToolOptionsParameterMirror : public QObject
{
    Q_OBJECT
public:
    /// @p modeWidget: see KisPaintOpOption::addToolOptionsParameter()
    KisToolOptionsParameterMirror(QWidget *control, KisPaintOpOption *option, QWidget *modeWidget, QWidget *parent);
    ~KisToolOptionsParameterMirror() override;

    /// The copy, owned by the parent given to the constructor
    QWidget *widget() const;
    bool isShownInEditor() const;

Q_SIGNALS:
    void sigShownInEditorChanged(bool shown);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void schedulePull();
    void pull();

    QPointer<QWidget> m_control;
    QPointer<QWidget> m_modeWidget;
    QPointer<KisPaintOpOption> m_option;
    QPointer<QWidget> m_widget;
    std::function<void()> m_pullValue;
    bool m_pullScheduled{false};
    bool m_shownInEditor{true};
    // radio buttons follow the enabled state one by one
    bool m_perButtonEnabled{false};
};

#endif // KISTOOLOPTIONSPARAMETERS_H
