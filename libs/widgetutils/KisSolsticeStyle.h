/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISSOLSTICESTYLE_H
#define KISSOLSTICESTYLE_H

#include <QProxyStyle>

#include "kritawidgetutils_export.h"

/**
 * The "Solstice" widget style (docs/agent/ui-modernization-plan.md, phase 3):
 * Fusion with flat, rounded common controls in the palette's colors.
 *
 * It changes only drawing, never sizes, so layouts stay as with Fusion:
 * push and tool buttons, line edit frames, check boxes and radio buttons,
 * menu item highlights, scroll bars and tab shapes. Everything else is
 * Fusion's.
 *
 * The style reports the object name "fusion": Krita's widget-local proxy
 * styles clone the application style by name, and spin boxes adjust their
 * geometry for Fusion. Those clones therefore draw plain Fusion.
 */
class KRITAWIDGETUTILS_EXPORT KisSolsticeStyle : public QProxyStyle
{
    Q_OBJECT
public:
    KisSolsticeStyle();

    /// The name used in the Styles menu and the widgetStyle setting.
    static QString styleKey();
    /// Whether @p key (case-insensitive) selects this style.
    static bool isStyleKey(const QString &key);

    void drawPrimitive(PrimitiveElement element,
                       const QStyleOption *option,
                       QPainter *painter,
                       const QWidget *widget = nullptr) const override;
    void drawControl(ControlElement element,
                     const QStyleOption *option,
                     QPainter *painter,
                     const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control,
                            const QStyleOptionComplex *option,
                            QPainter *painter,
                            const QWidget *widget = nullptr) const override;
};

#endif // KISSOLSTICESTYLE_H
