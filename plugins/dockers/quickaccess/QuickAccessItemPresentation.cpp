/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QuickAccessItemPresentation.h"
#include <QDir>
#include <QFileInfo>
namespace QuickAccessItemPresentation
{
bool showsStrokePreview(const QuickAccess::Item &item)
{
    return item.type == QuickAccess::ItemType::Brush
        && item.payload.value(BrushDisplayKey).toString() == BrushDisplayStroke;
}

QJsonObject aliasFor(const QuickAccess::Document &document, const QString &category, const QString &id)
{
    return document.aliases.value(category).toObject().value(id).toObject();
}

QString compatibleString(const QJsonObject &object,
                         const QString &nativeName,
                         const QString &legacyName,
                         const QString &fallback)
{
    QString value = object.value(nativeName).toString();
    if (value.isEmpty())
        value = object.value(legacyName).toString();
    return value.isEmpty() ? fallback : value;
}

int compatibleInt(const QJsonObject &object, const QString &nativeName, const QString &legacyName, int fallback)
{
    bool ok = false;
    int value = object.value(nativeName).toVariant().toInt(&ok);
    if (!ok)
        value = object.value(legacyName).toVariant().toInt(&ok);
    return ok ? value : fallback;
}

QString defaultIconsDirectory()
{
    return QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath())
        .filePath(QStringLiteral("resources/default_icons"));
}

QString bundledIconPath(const QString &category, const QString &iconName)
{
    const QString fileName = QFileInfo(iconName).fileName();
    if (fileName.isEmpty())
        return QString();
    const QString path = QStringLiteral(":/quickaccess/%1/%2").arg(category, fileName);
    return QFileInfo::exists(path) ? path : QString();
}

QString resolveDefaultIcon(const QString &iconName)
{
    if (QFileInfo(iconName).isAbsolute() && QFileInfo::exists(iconName))
        return iconName;
    const QString bundled = bundledIconPath(QStringLiteral("default_icons"), iconName);
    if (!bundled.isEmpty())
        return bundled;
    return QString();
}
} // namespace QuickAccessItemPresentation
