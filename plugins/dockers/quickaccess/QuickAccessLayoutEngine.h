/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKACCESSLAYOUTENGINE_H
#define QUICKACCESSLAYOUTENGINE_H

#include "QuickAccessModel.h"

namespace QuickAccess
{

struct PlacementIssue {
    QString itemId;
    QString code;
    QString message;
};

struct LayoutResult {
    QList<Item> items;
    QList<PlacementIssue> issues;
    bool isValid() const
    {
        return issues.isEmpty();
    }
};

class LayoutEngine
{
public:
    explicit LayoutEngine(int columns);

    LayoutResult addItem(const QList<Item> &items, const Item &newItem) const;
    LayoutResult moveItem(const QList<Item> &items, const QString &itemId, int row, int column) const;
    LayoutResult resizeItem(const QList<Item> &items, const QString &itemId, int rowSpan, int columnSpan) const;
    LayoutResult validate(const QList<Item> &items) const;
    LayoutResult compact(const QList<Item> &items) const;

private:
    LayoutResult placeWithPush(const QList<Item> &existingItems, Item activeItem) const;
    Item firstFreePosition(Item item, const QList<Item> &placed) const;
    bool needsReposition(const Item &item, const QList<Item> &placed) const;
    QList<PlacementIssue> boundsIssues(const Item &item) const;
    static bool overlaps(const Item &first, const Item &second);
    QList<Item> stableOrder(const QList<Item> &items) const;
    int linearIndex(int row, int column) const;

    int m_columns;
};

} // namespace QuickAccess

#endif
