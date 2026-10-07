/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisSolsticeStyle.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>

#include <kis_painting_tweaks.h>

namespace
{
constexpr qreal Radius = 3.0;

/// @p amount of @p a mixed into @p b.
QColor mix(const QColor &a, const QColor &b, qreal amount)
{
    return KisPaintingTweaks::blendColors(a, b, amount);
}

/// A rect for a 1px antialiased outline inside @p rect.
QRectF outlineRect(const QRect &rect)
{
    return QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
}

void drawRoundedPanel(QPainter *painter, const QRectF &rect, const QColor &fill, const QColor &outline)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(outline.isValid() ? QPen(outline, 1.0) : QPen(Qt::NoPen));
    painter->setBrush(fill);
    painter->drawRoundedRect(rect, Radius, Radius);
    painter->restore();
}

/// Square indicator rect centered in @p rect.
QRectF indicatorRect(const QRect &rect)
{
    const qreal side = qMin(rect.width(), rect.height()) - 1.0;
    QRectF square(0, 0, side, side);
    square.moveCenter(QRectF(rect).center());
    return square;
}
} // namespace

KisSolsticeStyle::KisSolsticeStyle()
    : QProxyStyle(QStyleFactory::create(QStringLiteral("fusion")))
{
    setObjectName(QStringLiteral("fusion"));
}

QString KisSolsticeStyle::styleKey()
{
    return QStringLiteral("Solstice");
}

bool KisSolsticeStyle::isStyleKey(const QString &key)
{
    return key.compare(styleKey(), Qt::CaseInsensitive) == 0;
}

QStyle *KisSolsticeStyle::createStyle(const QString &name)
{
    if (qobject_cast<KisSolsticeStyle *>(QApplication::style())
        && name.compare(QStringLiteral("fusion"), Qt::CaseInsensitive) == 0) {
        return new KisSolsticeStyle();
    }
    return QStyleFactory::create(name);
}

void KisSolsticeStyle::drawPrimitive(PrimitiveElement element,
                                     const QStyleOption *option,
                                     QPainter *painter,
                                     const QWidget *widget) const
{
    const QPalette &palette = option->palette;
    const bool enabled = option->state & State_Enabled;
    const bool hover = enabled && (option->state & State_MouseOver);
    const QColor highlight = palette.color(QPalette::Highlight);

    switch (element) {
    case PE_PanelButtonCommand: {
        const QColor button = palette.color(QPalette::Button);
        const QColor text = palette.color(QPalette::ButtonText);
        QColor fill = button;
        if (option->state & State_Sunken) {
            fill = mix(Qt::black, button, 0.18);
        } else if (option->state & State_On) {
            fill = mix(highlight, button, 0.35);
        } else if (hover) {
            fill = mix(text, button, 0.08);
        }
        const QColor outline = enabled && (option->state & State_HasFocus) ? highlight : mix(text, button, 0.20);
        drawRoundedPanel(painter, outlineRect(option->rect), fill, outline);
        return;
    }
    case PE_PanelButtonTool: {
        const bool autoRaise = option->state & State_AutoRaise;
        const bool down = option->state & State_Sunken;
        const bool on = option->state & State_On;
        const bool raised = option->state & State_Raised;
        if (autoRaise && !hover && !down && !on && !raised) {
            return;
        }
        const QColor window = palette.color(QPalette::Window);
        const QColor text = palette.color(QPalette::WindowText);
        QColor fill = autoRaise ? mix(text, window, 0.10) : palette.color(QPalette::Button);
        if (down) {
            fill = mix(Qt::black, window, 0.20);
        } else if (on) {
            fill = mix(highlight, window, hover ? 0.50 : 0.40);
        } else if (hover && !autoRaise) {
            fill = mix(text, palette.color(QPalette::Button), 0.08);
        }
        const QColor outline = autoRaise && !on ? QColor() : mix(text, window, 0.20);
        drawRoundedPanel(painter, outlineRect(option->rect), fill, outline);
        return;
    }
    case PE_PanelLineEdit: {
        const QStyleOptionFrame *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
        if (!frame || frame->lineWidth <= 0) {
            break; // inside a spin box or combo box: Fusion's own drawing
        }
        const QColor base = palette.color(QPalette::Base);
        const QColor text = palette.color(QPalette::Text);
        const QColor outline = enabled && (option->state & State_HasFocus) ? highlight
            : hover                                                        ? mix(text, base, 0.32)
                                                                           : mix(text, base, 0.20);
        drawRoundedPanel(painter, outlineRect(option->rect), base, outline);
        return;
    }
    case PE_IndicatorCheckBox: {
        const QRectF box = indicatorRect(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const QColor base = palette.color(QPalette::Base);
        const QColor text = palette.color(QPalette::Text);
        const bool checked = option->state & (State_On | State_NoChange);
        const QColor accent = enabled ? highlight : mix(text, base, 0.30);
        const QColor outline = checked ? accent : hover ? highlight : mix(text, base, 0.35);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(outline, 1.0));
        painter->setBrush(checked ? accent : base);
        painter->drawRoundedRect(box, 2.5, 2.5);
        if (checked) {
            const QColor mark = palette.color(QPalette::HighlightedText);
            QPen pen(mark, qMax<qreal>(1.5, box.width() / 8.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            if (option->state & State_NoChange) {
                painter->drawLine(QPointF(box.left() + box.width() * 0.28, box.center().y()),
                                  QPointF(box.right() - box.width() * 0.28, box.center().y()));
            } else {
                QPainterPath path;
                path.moveTo(box.left() + box.width() * 0.24, box.top() + box.height() * 0.52);
                path.lineTo(box.left() + box.width() * 0.42, box.top() + box.height() * 0.70);
                path.lineTo(box.left() + box.width() * 0.76, box.top() + box.height() * 0.32);
                painter->drawPath(path);
            }
        }
        painter->restore();
        return;
    }
    case PE_IndicatorRadioButton: {
        const QRectF circle = indicatorRect(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const QColor base = palette.color(QPalette::Base);
        const QColor text = palette.color(QPalette::Text);
        const bool checked = option->state & State_On;
        const QColor accent = enabled ? highlight : mix(text, base, 0.30);
        const QColor outline = checked ? accent : hover ? highlight : mix(text, base, 0.35);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(outline, 1.0));
        painter->setBrush(base);
        painter->drawEllipse(circle);
        if (checked) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(accent);
            const qreal dot = circle.width() * 0.5;
            painter->drawEllipse(QRectF(circle.center() - QPointF(dot, dot) / 2.0, QSizeF(dot, dot)));
        }
        painter->restore();
        return;
    }
    default:
        break;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void KisSolsticeStyle::drawControl(ControlElement element,
                                   const QStyleOption *option,
                                   QPainter *painter,
                                   const QWidget *widget) const
{
    switch (element) {
    case CE_MenuItem: {
        const QStyleOptionMenuItem *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (item && (item->state & State_Selected) && (item->state & State_Enabled)
            && item->menuItemType != QStyleOptionMenuItem::Separator) {
            // A rounded highlight inset from the menu edges; Fusion draws the
            // item's text, icon and check on top as if not selected, in the
            // highlighted text color.
            const QRectF highlight = QRectF(item->rect).adjusted(2, 0.5, -2, -0.5);
            drawRoundedPanel(painter, highlight, item->palette.color(QPalette::Highlight), QColor());
            QStyleOptionMenuItem copy = *item;
            copy.state &= ~State_Selected;
            const QColor text = item->palette.color(QPalette::HighlightedText);
            copy.palette.setColor(QPalette::Text, text);
            copy.palette.setColor(QPalette::WindowText, text);
            copy.palette.setColor(QPalette::ButtonText, text);
            QProxyStyle::drawControl(element, &copy, painter, widget);
            return;
        }
        break;
    }
    case CE_TabBarTabShape: {
        const QStyleOptionTab *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
        if (!tab) {
            break;
        }
        const QColor window = tab->palette.color(QPalette::Window);
        const QColor text = tab->palette.color(QPalette::WindowText);
        const bool selected = tab->state & State_Selected;
        const bool hover = (tab->state & State_Enabled) && (tab->state & State_MouseOver);
        const QRect rect = tab->rect;
        painter->fillRect(rect, mix(text, window, selected ? 0.12 : hover ? 0.09 : 0.03));
        const QColor line = mix(text, window, 0.16);
        const QColor accent = tab->palette.color(QPalette::Highlight);
        switch (tab->shape) {
        case QTabBar::RoundedSouth:
        case QTabBar::TriangularSouth:
            painter->fillRect(QRect(rect.right(), rect.top(), 1, rect.height()), line);
            if (selected)
                painter->fillRect(QRect(rect.left(), rect.top(), rect.width(), 2), accent);
            break;
        case QTabBar::RoundedWest:
        case QTabBar::TriangularWest:
            painter->fillRect(QRect(rect.left(), rect.bottom(), rect.width(), 1), line);
            if (selected)
                painter->fillRect(QRect(rect.right() - 1, rect.top(), 2, rect.height()), accent);
            break;
        case QTabBar::RoundedEast:
        case QTabBar::TriangularEast:
            painter->fillRect(QRect(rect.left(), rect.bottom(), rect.width(), 1), line);
            if (selected)
                painter->fillRect(QRect(rect.left(), rect.top(), 2, rect.height()), accent);
            break;
        default:
            painter->fillRect(QRect(rect.right(), rect.top(), 1, rect.height()), line);
            if (selected)
                painter->fillRect(QRect(rect.left(), rect.bottom() - 1, rect.width(), 2), accent);
            break;
        }
        return;
    }
    default:
        break;
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

void KisSolsticeStyle::drawComplexControl(ComplexControl control,
                                          const QStyleOptionComplex *option,
                                          QPainter *painter,
                                          const QWidget *widget) const
{
    if (control == CC_SpinBox) {
        if (const QStyleOptionSpinBox *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
            // A rounded field like a line edit; flat step buttons inside it,
            // separated by a line, with Fusion's arrows (or plus and minus).
            const QPalette &palette = spin->palette;
            const bool enabled = spin->state & State_Enabled;
            const QColor base = palette.color(QPalette::Base);
            const QColor text = palette.color(QPalette::Text);
            const QColor highlight = palette.color(QPalette::Highlight);
            const QRectF field = outlineRect(spin->rect);
            if (spin->frame) {
                const QColor outline = enabled && (spin->state & State_HasFocus) ? highlight
                    : enabled && (spin->state & State_MouseOver)                 ? mix(text, base, 0.32)
                                                                                 : mix(text, base, 0.20);
                drawRoundedPanel(painter, field, base, outline);
            } else {
                painter->fillRect(spin->rect, base);
            }
            if (spin->buttonSymbols == QAbstractSpinBox::NoButtons) {
                return;
            }
            const QRect up = subControlRect(control, spin, SC_SpinBoxUp, widget);
            const QRect down = subControlRect(control, spin, SC_SpinBoxDown, widget);
            const QRect buttons = up.united(down);
            if (!buttons.isValid()) {
                return;
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            QPainterPath clip;
            clip.addRoundedRect(field.adjusted(1, 1, -1, -1), Radius - 1.0, Radius - 1.0);
            painter->setClipPath(clip);
            painter->fillRect(buttons, mix(text, base, 0.05));
            const struct {
                QRect rect;
                SubControl part;
                QAbstractSpinBox::StepEnabledFlag step;
                PrimitiveElement arrow;
                PrimitiveElement sign;
            } steps[] = {{up, SC_SpinBoxUp, QAbstractSpinBox::StepUpEnabled, PE_IndicatorArrowUp, PE_IndicatorSpinPlus},
                         {down,
                          SC_SpinBoxDown,
                          QAbstractSpinBox::StepDownEnabled,
                          PE_IndicatorArrowDown,
                          PE_IndicatorSpinMinus}};
            for (const auto &step : steps) {
                const bool stepEnabled = enabled && (spin->stepEnabled & step.step);
                const bool active = stepEnabled && (spin->activeSubControls & step.part);
                if (active) {
                    const qreal amount = (spin->state & State_Sunken) ? 0.24 : 0.12;
                    painter->fillRect(step.rect, mix(text, base, amount));
                }
            }
            painter->setClipping(false);
            const QColor line = mix(text, base, 0.16);
            const bool rightToLeft = spin->direction == Qt::RightToLeft;
            painter->fillRect(
                QRect(rightToLeft ? buttons.right() : buttons.left() - 1, buttons.top() + 1, 1, buttons.height() - 2),
                line);
            painter->restore();
            for (const auto &step : steps) {
                QStyleOption arrow(*spin);
                arrow.rect = step.rect;
                if (!(enabled && (spin->stepEnabled & step.step))) {
                    arrow.state &= ~State_Enabled;
                    arrow.palette.setCurrentColorGroup(QPalette::Disabled);
                }
                const bool plusMinus = spin->buttonSymbols == QAbstractSpinBox::PlusMinus;
                proxy()->drawPrimitive(plusMinus ? step.sign : step.arrow, &arrow, painter, widget);
            }
            return;
        }
    }
    if (control == CC_ScrollBar) {
        if (const QStyleOptionSlider *bar = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            // A flat track with a rounded handle; Fusion's arrows on top.
            const QColor window = bar->palette.color(QPalette::Window);
            const QColor text = bar->palette.color(QPalette::WindowText);
            painter->fillRect(bar->rect, mix(text, window, 0.03));

            const QRect slider = subControlRect(control, bar, SC_ScrollBarSlider, widget);
            if (slider.isValid() && (bar->state & State_Enabled)) {
                const bool active = bar->activeSubControls & SC_ScrollBarSlider;
                const bool pressed = active && (bar->state & State_Sunken);
                const bool hover = active && (bar->state & State_MouseOver);
                const qreal amount = pressed ? 0.55 : hover ? 0.42 : 0.30;
                const qreal inset = bar->orientation == Qt::Horizontal ? slider.height() * 0.2 : slider.width() * 0.2;
                const QRectF handle = bar->orientation == Qt::Horizontal
                    ? QRectF(slider).adjusted(1, inset, -1, -inset)
                    : QRectF(slider).adjusted(inset, 1, -inset, -1);
                drawRoundedPanel(painter, handle, mix(text, window, amount), QColor());
            }

            for (SubControl part : {SC_ScrollBarSubLine, SC_ScrollBarAddLine}) {
                const QRect rect = subControlRect(control, bar, part, widget);
                if (!rect.isValid()) {
                    continue;
                }
                QStyleOption arrow(*bar);
                arrow.rect = rect;
                const bool forward = part == SC_ScrollBarAddLine;
                const PrimitiveElement element = bar->orientation == Qt::Horizontal
                    ? (forward ? PE_IndicatorArrowRight : PE_IndicatorArrowLeft)
                    : (forward ? PE_IndicatorArrowDown : PE_IndicatorArrowUp);
                proxy()->drawPrimitive(element, &arrow, painter, widget);
            }
            return;
        }
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}
