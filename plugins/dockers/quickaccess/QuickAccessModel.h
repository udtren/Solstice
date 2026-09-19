/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKACCESSMODEL_H
#define QUICKACCESSMODEL_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace QuickAccess
{

constexpr int CurrentFormatVersion = 1;

enum class ItemType {
    Brush,
    Action,
    Label,
    Separator,
    DockerToggle,
    Color,
    Script,
    BrushSize,
    BrushBlendMode,
};

QString itemTypeToString(ItemType type);
bool itemTypeFromString(const QString &value, ItemType *type);

struct Item {
    QString id;
    ItemType type{ItemType::Action};
    int row{0};
    int column{0};
    int rowSpan{1};
    int columnSpan{1};
    QJsonObject payload;

    void normalize();
    int right() const;
    int bottom() const;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Item *item, QString *error = nullptr);

    bool operator==(const Item &other) const;
};

struct Grid {
    QString id;
    QString name;
    int columns{8};
    QList<Item> items;

    void normalize();
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Grid *grid, QString *error = nullptr);
    bool operator==(const Grid &other) const;
};

struct Tab {
    QString id;
    QString name;
    QList<Grid> grids;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Tab *tab, QString *error = nullptr);
    bool operator==(const Tab &other) const;
};

struct Document {
    int formatVersion{CurrentFormatVersion};
    QString activeTabId;
    QList<Tab> tabs;
    QJsonObject aliases;
    QJsonArray gesturePages;

    static Document createDefault();
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Document *document, QString *error = nullptr);
    bool operator==(const Document &other) const;
};

} // namespace QuickAccess

#endif
