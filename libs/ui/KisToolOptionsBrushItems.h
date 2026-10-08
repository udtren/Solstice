/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISTOOLOPTIONSBRUSHITEMS_H
#define KISTOOLOPTIONSBRUSHITEMS_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include "kritaui_export.h"

/**
 * Which Brush Editor items each brush engine shows in the Tool Options
 * docker (the eyes in the editor). Stored per engine in kritarc, as
 * Solstice/ToolOptionsBrushItems/<paintop id>, a comma separated list of
 * KisPaintOpOption::toolOptionsId(). See docs/agent/tool-options-brush.md.
 */
class KRITAUI_EXPORT KisToolOptionsBrushItems : public QObject
{
    Q_OBJECT
public:
    /// Use instance(); public for Q_GLOBAL_STATIC.
    KisToolOptionsBrushItems();

    static KisToolOptionsBrushItems *instance();

    QSet<QString> shownItems(const QString &paintOpId) const;
    bool isShown(const QString &paintOpId, const QString &itemId) const;
    void setShown(const QString &paintOpId, const QString &itemId, bool shown);

    /// Whether the Brush section in Tool Options is collapsed
    bool isSectionCollapsed() const;
    void setSectionCollapsed(bool collapsed);

Q_SIGNALS:
    void sigShownItemsChanged(const QString &paintOpId);
    void sigSectionCollapsedChanged(bool collapsed);

private:
    mutable QHash<QString, QSet<QString>> m_shownItems;
};

#endif // KISTOOLOPTIONSBRUSHITEMS_H
