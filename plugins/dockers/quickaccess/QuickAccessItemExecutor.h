/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKACCESSITEMEXECUTOR_H
#define QUICKACCESSITEMEXECUTOR_H

#include "QuickAccessModel.h"

#include <QObject>
#include <QPointer>

class KisCanvas2;

namespace QuickAccess
{

class ItemExecutor : public QObject
{
    Q_OBJECT
public:
    explicit ItemExecutor(QObject *parent = nullptr);

    void setCanvas(KisCanvas2 *canvas);
    bool execute(const Item &item, QString *error = nullptr);

private:
    bool executeBrush(const Item &item, QString *error);
    bool executeAction(const Item &item, QString *error);
    bool executeDockerToggle(const Item &item, QString *error);
    bool executeColor(const Item &item, QString *error);
    bool executeBrushSize(const Item &item, QString *error);
    bool executeBrushBlendMode(const Item &item, QString *error);
    bool executeScript(const Item &item, QString *error);

    QPointer<KisCanvas2> m_canvas;
};

} // namespace QuickAccess

#endif
