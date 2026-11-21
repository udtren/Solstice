/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisDockerTrayLayout.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtMath>

#include <kconfiggroup.h>

const QSize KisDockerTrayLayout::DefaultCellSize(300, 400);

namespace
{
const char ConfigGroup[] = "DockerTray";
const char ConfigKey[] = "Layout";

int clampExtent(const QJsonValue &value, int fallback)
{
    if (!value.isDouble()) {
        return fallback;
    }
    return qBound(KisDockerTrayLayout::MinimumExtent, value.toInt(), KisDockerTrayLayout::MaximumExtent);
}

/// Shrinks \p values to fit \p budget in proportion, never below \p minimums.
QVector<int> shrink(const QVector<int> &values, const QVector<int> &minimums, int budget)
{
    QVector<int> result = values;
    QVector<bool> pinned(values.size(), false);
    for (int pass = 0; pass <= values.size(); ++pass) {
        qint64 pinnedSum = 0;
        qint64 freeSum = 0;
        for (int i = 0; i < values.size(); ++i) {
            if (pinned[i]) {
                pinnedSum += result[i];
            } else {
                freeSum += values[i];
            }
        }
        if (freeSum <= 0 || pinnedSum + freeSum <= budget) {
            break;
        }
        const qreal factor = qMax<qreal>(0.0, qreal(budget - pinnedSum) / qreal(freeSum));
        bool pinnedMore = false;
        for (int i = 0; i < values.size(); ++i) {
            if (pinned[i]) {
                continue;
            }
            const int scaled = qFloor(values[i] * factor);
            if (scaled < minimums[i]) {
                result[i] = minimums[i];
                pinned[i] = true;
                pinnedMore = true;
            } else {
                result[i] = scaled;
            }
        }
        if (!pinnedMore) {
            break;
        }
    }
    return result;
}
} // namespace

bool KisDockerTrayLayout::isEmpty() const
{
    for (const QVector<KisDockerTrayCell> &column : columns) {
        if (!column.isEmpty()) {
            return false;
        }
    }
    return true;
}

QPoint KisDockerTrayLayout::find(const QString &dockerId) const
{
    for (int c = 0; c < columns.size(); ++c) {
        for (int r = 0; r < columns[c].size(); ++r) {
            if (columns[c][r].dockerId == dockerId) {
                return QPoint(c, r);
            }
        }
    }
    return QPoint(-1, -1);
}

bool KisDockerTrayLayout::addDocker(const QString &dockerId, int column, const QSize &size)
{
    if (dockerId.isEmpty() || find(dockerId).x() >= 0 || column < 0 || column > columns.size()) {
        return false;
    }
    if (column == columns.size()) {
        columns.append(QVector<KisDockerTrayCell>());
    }
    columns[column].append(KisDockerTrayCell{dockerId, QSize()});
    setSize(dockerId, size);
    return true;
}

bool KisDockerTrayLayout::removeDocker(const QString &dockerId)
{
    const QPoint position = find(dockerId);
    if (position.x() < 0) {
        return false;
    }
    columns[position.x()].removeAt(position.y());
    return true;
}

bool KisDockerTrayLayout::moveDocker(const QString &dockerId, int delta)
{
    const QPoint position = find(dockerId);
    if (position.x() < 0 || (delta != -1 && delta != 1)) {
        return false;
    }
    const int c = position.x();
    const int r = position.y();
    const int target = r + delta;
    if (target >= 0 && target < columns[c].size()) {
        columns[c].move(r, target);
        return true;
    }
    const int targetColumn = c + delta;
    if (targetColumn < 0 || targetColumn >= columns.size()) {
        return false;
    }
    const KisDockerTrayCell cell = columns[c].takeAt(r);
    if (delta < 0) {
        columns[targetColumn].append(cell);
    } else {
        columns[targetColumn].prepend(cell);
    }
    return true;
}

bool KisDockerTrayLayout::moveColumn(int column, int delta)
{
    const int target = column + delta;
    if (column < 0 || column >= columns.size() || target < 0 || target >= columns.size()) {
        return false;
    }
    columns.move(column, target);
    return true;
}

void KisDockerTrayLayout::addColumn()
{
    columns.append(QVector<KisDockerTrayCell>());
}

bool KisDockerTrayLayout::removeColumn(int column)
{
    if (column < 0 || column >= columns.size()) {
        return false;
    }
    columns.removeAt(column);
    return true;
}

bool KisDockerTrayLayout::setSize(const QString &dockerId, const QSize &size)
{
    const QPoint position = find(dockerId);
    if (position.x() < 0) {
        return false;
    }
    columns[position.x()][position.y()].size =
        QSize(qBound(MinimumExtent, size.width(), MaximumExtent), qBound(MinimumExtent, size.height(), MaximumExtent));
    return true;
}

QStringList KisDockerTrayLayout::dockerIds() const
{
    QStringList ids;
    for (const QVector<KisDockerTrayCell> &column : columns) {
        for (const KisDockerTrayCell &cell : column) {
            ids << cell.dockerId;
        }
    }
    return ids;
}

KisDockerTrayLayout KisDockerTrayLayout::defaultLayout()
{
    KisDockerTrayLayout layout;
    layout.columns = {{KisDockerTrayCell{QStringLiteral("KisLayerBox"), DefaultCellSize}}};
    return layout;
}

KisDockerTrayLayout KisDockerTrayLayout::fromJson(const QByteArray &json, bool *ok)
{
    if (ok) {
        *ok = false;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return defaultLayout();
    }
    const QJsonArray trays = document.object().value(QStringLiteral("trays")).toArray();
    if (trays.isEmpty() || !trays.first().isObject()) {
        return defaultLayout();
    }

    KisDockerTrayLayout layout;
    QSet<QString> seen;
    const QJsonArray columns = trays.first().toObject().value(QStringLiteral("columns")).toArray();
    for (const QJsonValue &columnValue : columns) {
        QVector<KisDockerTrayCell> column;
        const QJsonArray items = columnValue.toObject().value(QStringLiteral("items")).toArray();
        for (const QJsonValue &itemValue : items) {
            const QJsonObject item = itemValue.toObject();
            const QString id = item.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || seen.contains(id)) {
                continue;
            }
            seen.insert(id);
            column.append(
                KisDockerTrayCell{id,
                                  QSize(clampExtent(item.value(QStringLiteral("w")), DefaultCellSize.width()),
                                        clampExtent(item.value(QStringLiteral("h")), DefaultCellSize.height()))});
        }
        if (!column.isEmpty()) {
            layout.columns.append(column);
        }
    }

    if (ok) {
        *ok = true;
    }
    return layout;
}

QByteArray KisDockerTrayLayout::toJson() const
{
    QJsonArray columnArray;
    for (const QVector<KisDockerTrayCell> &column : columns) {
        QJsonArray items;
        for (const KisDockerTrayCell &cell : column) {
            items.append(QJsonObject{{QStringLiteral("id"), cell.dockerId},
                                     {QStringLiteral("w"), cell.size.width()},
                                     {QStringLiteral("h"), cell.size.height()}});
        }
        columnArray.append(QJsonObject{{QStringLiteral("items"), items}});
    }
    const QJsonObject tray{{QStringLiteral("name"), QStringLiteral("Default")},
                           {QStringLiteral("columns"), columnArray}};
    const QJsonObject root{{QStringLiteral("version"), 1}, {QStringLiteral("trays"), QJsonArray{tray}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

KisDockerTrayLayout KisDockerTrayLayout::load(KSharedConfigPtr config)
{
    const QString json = config->group(ConfigGroup).readEntry(ConfigKey, QString());
    return json.isEmpty() ? defaultLayout() : fromJson(json.toUtf8());
}

void KisDockerTrayLayout::save(KSharedConfigPtr config) const
{
    KConfigGroup group = config->group(ConfigGroup);
    group.writeEntry(ConfigKey, QString::fromUtf8(toJson()));
    group.sync();
}

QVector<QVector<QSize>> KisDockerTrayGeometry::fit(const QVector<QVector<QSize>> &wanted,
                                                   const QVector<QVector<QSize>> &minimum,
                                                   const QSize &available,
                                                   int spacing)
{
    QVector<int> widths;
    QVector<int> minimumWidths;
    QVector<QVector<int>> heights;
    QVector<QVector<int>> minimumHeights;

    for (int c = 0; c < wanted.size(); ++c) {
        int width = 0;
        int minimumWidth = 0;
        QVector<int> columnHeights;
        QVector<int> columnMinimumHeights;
        for (int r = 0; r < wanted[c].size(); ++r) {
            const QSize minimumSize = c < minimum.size() && r < minimum[c].size() ? minimum[c][r] : QSize(0, 0);
            const QSize size = wanted[c][r].expandedTo(minimumSize);
            width = qMax(width, size.width());
            minimumWidth = qMax(minimumWidth, minimumSize.width());
            columnHeights << size.height();
            columnMinimumHeights << minimumSize.height();
        }
        widths << width;
        minimumWidths << minimumWidth;
        heights << columnHeights;
        minimumHeights << columnMinimumHeights;
    }

    if (available.isValid()) {
        const int gaps = spacing * qMax(0, int(widths.size()) - 1);
        widths = shrink(widths, minimumWidths, available.width() - gaps);
        for (int c = 0; c < heights.size(); ++c) {
            const int rowGaps = spacing * qMax(0, int(heights[c].size()) - 1);
            heights[c] = shrink(heights[c], minimumHeights[c], available.height() - rowGaps);
        }
    }

    QVector<QVector<QSize>> result;
    for (int c = 0; c < widths.size(); ++c) {
        QVector<QSize> column;
        for (int r = 0; r < heights[c].size(); ++r) {
            column << QSize(widths[c], heights[c][r]);
        }
        result << column;
    }
    return result;
}
