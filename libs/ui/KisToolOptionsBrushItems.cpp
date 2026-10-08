/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisToolOptionsBrushItems.h"

#include <QGlobalStatic>
#include <QStringList>

#include <kis_config.h>

namespace
{
QString itemsKey(const QString &paintOpId)
{
    return QStringLiteral("Solstice/ToolOptionsBrushItems/") + paintOpId;
}

const QString collapsedKey = QStringLiteral("Solstice/ToolOptionsBrushCollapsed");
} // namespace

Q_GLOBAL_STATIC(KisToolOptionsBrushItems, s_instance)

KisToolOptionsBrushItems *KisToolOptionsBrushItems::instance()
{
    return s_instance;
}

KisToolOptionsBrushItems::KisToolOptionsBrushItems()
{
}

QSet<QString> KisToolOptionsBrushItems::shownItems(const QString &paintOpId) const
{
    auto it = m_shownItems.find(paintOpId);
    if (it == m_shownItems.end()) {
        const QStringList items = KisConfig(true)
                                      .readEntry<QString>(itemsKey(paintOpId), QString())
                                      .split(QLatin1Char(','), Qt::SkipEmptyParts);
        it = m_shownItems.insert(paintOpId, QSet<QString>(items.begin(), items.end()));
    }
    return it.value();
}

bool KisToolOptionsBrushItems::isShown(const QString &paintOpId, const QString &itemId) const
{
    return shownItems(paintOpId).contains(itemId);
}

void KisToolOptionsBrushItems::setShown(const QString &paintOpId, const QString &itemId, bool shown)
{
    QSet<QString> items = shownItems(paintOpId);
    if (items.contains(itemId) == shown) {
        return;
    }

    if (shown) {
        items.insert(itemId);
    } else {
        items.remove(itemId);
    }
    m_shownItems.insert(paintOpId, items);

    QStringList sorted(items.begin(), items.end());
    sorted.sort();
    KisConfig(false).writeEntry<QString>(itemsKey(paintOpId), sorted.join(QLatin1Char(',')));

    Q_EMIT sigShownItemsChanged(paintOpId);
}

bool KisToolOptionsBrushItems::isSectionCollapsed() const
{
    return KisConfig(true).readEntry<bool>(collapsedKey, false);
}

void KisToolOptionsBrushItems::setSectionCollapsed(bool collapsed)
{
    if (isSectionCollapsed() == collapsed) {
        return;
    }
    KisConfig(false).writeEntry<bool>(collapsedKey, collapsed);
    Q_EMIT sigSectionCollapsedChanged(collapsed);
}
