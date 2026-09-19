/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QuickAccessLayoutEngine.h"

#include <algorithm>
#include <stdexcept>

namespace QuickAccess
{

LayoutEngine::LayoutEngine(int columns)
    : m_columns(qMax(1, columns))
{
}

LayoutResult LayoutEngine::addItem(const QList<Item> &items, const Item &newItem) const
{
    for (const Item &item : items) {
        if (item.id == newItem.id)
            throw std::invalid_argument("Duplicate quick-access item id");
    }
    return placeWithPush(items, newItem);
}

LayoutResult LayoutEngine::moveItem(const QList<Item> &items, const QString &itemId, int row, int column) const
{
    QList<Item> rest;
    Item moving;
    bool found = false;
    for (const Item &item : items) {
        if (item.id == itemId) {
            moving = item;
            found = true;
        } else {
            rest.append(item);
        }
    }
    if (!found)
        throw std::invalid_argument("Quick-access item not found");
    moving.row = row;
    moving.column = column;
    return placeWithPush(rest, moving);
}

LayoutResult
LayoutEngine::resizeItem(const QList<Item> &items, const QString &itemId, int rowSpan, int columnSpan) const
{
    QList<Item> rest;
    Item resized;
    bool found = false;
    for (const Item &item : items) {
        if (item.id == itemId) {
            resized = item;
            found = true;
        } else {
            rest.append(item);
        }
    }
    if (!found)
        throw std::invalid_argument("Quick-access item not found");
    resized.rowSpan = qMax(1, rowSpan);
    resized.columnSpan = qMax(1, columnSpan);
    resized.normalize();
    return placeWithPush(rest, resized);
}

LayoutResult LayoutEngine::validate(const QList<Item> &items) const
{
    LayoutResult result;
    result.items = items;
    for (const Item &item : items)
        result.issues.append(boundsIssues(item));
    for (int i = 0; i < items.size(); ++i) {
        for (int j = i + 1; j < items.size(); ++j) {
            if (overlaps(items.at(i), items.at(j))) {
                result.issues.append({items.at(i).id,
                                      QStringLiteral("overlap"),
                                      QStringLiteral("Item overlaps with %1.").arg(items.at(j).id)});
                result.issues.append({items.at(j).id,
                                      QStringLiteral("overlap"),
                                      QStringLiteral("Item overlaps with %1.").arg(items.at(i).id)});
            }
        }
    }
    return result;
}

LayoutResult LayoutEngine::compact(const QList<Item> &items) const
{
    QList<Item> placed;
    for (Item item : stableOrder(items)) {
        item.row = 0;
        item.column = 0;
        if (item.columnSpan <= m_columns)
            item = firstFreePosition(item, placed);
        placed.append(item);
    }
    return validate(placed);
}

LayoutResult LayoutEngine::placeWithPush(const QList<Item> &existingItems, Item activeItem) const
{
    activeItem.row = qMax(0, activeItem.row);
    activeItem.column = qMax(0, activeItem.column);
    activeItem.normalize();
    QList<Item> placed{activeItem};
    if (activeItem.columnSpan <= m_columns) {
        for (Item item : stableOrder(existingItems)) {
            item.row = qMax(0, item.row);
            item.column = qMax(0, item.column);
            item.normalize();
            if (item.columnSpan <= m_columns && needsReposition(item, placed)) {
                item = firstFreePosition(item, placed);
            }
            placed.append(item);
        }
    } else {
        placed.append(existingItems);
    }
    return validate(placed);
}

Item LayoutEngine::firstFreePosition(Item item, const QList<Item> &placed) const
{
    int cursor = linearIndex(item.row, item.column);
    for (;;) {
        item.row = cursor / m_columns;
        item.column = cursor % m_columns;
        if (item.column + item.columnSpan <= m_columns && !needsReposition(item, placed))
            return item;
        ++cursor;
    }
}

bool LayoutEngine::needsReposition(const Item &item, const QList<Item> &placed) const
{
    if (!boundsIssues(item).isEmpty())
        return true;
    for (const Item &other : placed) {
        if (overlaps(item, other))
            return true;
    }
    return false;
}

QList<PlacementIssue> LayoutEngine::boundsIssues(const Item &item) const
{
    QList<PlacementIssue> issues;
    if (item.row < 0 || item.column < 0) {
        issues.append({item.id, QStringLiteral("negative_position"), QStringLiteral("Item has a negative position.")});
    }
    if (item.columnSpan > m_columns) {
        issues.append(
            {item.id, QStringLiteral("too_wide"), QStringLiteral("Item width exceeds the configured column count.")});
    } else if (item.column + item.columnSpan > m_columns) {
        issues.append(
            {item.id, QStringLiteral("overflow"), QStringLiteral("Item extends beyond the configured column count.")});
    }
    return issues;
}

bool LayoutEngine::overlaps(const Item &first, const Item &second)
{
    return !(first.right() <= second.column || second.right() <= first.column || first.bottom() <= second.row
             || second.bottom() <= first.row);
}

QList<Item> LayoutEngine::stableOrder(const QList<Item> &items) const
{
    QList<Item> ordered = items;
    std::sort(ordered.begin(), ordered.end(), [this](const Item &first, const Item &second) {
        const int firstIndex = linearIndex(qMax(0, first.row), qMax(0, first.column));
        const int secondIndex = linearIndex(qMax(0, second.row), qMax(0, second.column));
        return firstIndex == secondIndex ? first.id < second.id : firstIndex < secondIndex;
    });
    return ordered;
}

int LayoutEngine::linearIndex(int row, int column) const
{
    return qMax(0, row) * m_columns + qMax(0, column);
}

} // namespace QuickAccess
