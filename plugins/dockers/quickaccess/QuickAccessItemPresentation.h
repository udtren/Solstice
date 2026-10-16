/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKACCESSITEMPRESENTATION_H
#define QUICKACCESSITEMPRESENTATION_H
#include "QuickAccessModel.h"
namespace QuickAccessItemPresentation
{
const QString BrushDisplayKey = QStringLiteral("display");
const QString BrushDisplayStroke = QStringLiteral("stroke");
const QString BrushDisplayIcon = QStringLiteral("icon");

bool showsStrokePreview(const QuickAccess::Item &item);
QJsonObject aliasFor(const QuickAccess::Document &document, const QString &category, const QString &id);
QString compatibleString(const QJsonObject &object,
                         const QString &nativeName,
                         const QString &legacyName,
                         const QString &fallback = QString());
int compatibleInt(const QJsonObject &object, const QString &nativeName, const QString &legacyName, int fallback);
QString defaultIconsDirectory();
QString bundledIconPath(const QString &category, const QString &iconName);
QString resolveDefaultIcon(const QString &iconName);
} // namespace QuickAccessItemPresentation
#endif
