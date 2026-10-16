/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KisAssetLibrarySettings.h"

#include <KoResourcePaths.h>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <kconfiggroup.h>
#include <ksharedconfig.h>

KisAssetLibrarySettings::KisAssetLibrarySettings(Usage usage)
    : m_usage(usage)
{
}

QString KisAssetLibrarySettings::filePath()
{
    return QDir(KoResourcePaths::saveLocation("data", "krita_asset_library/", true))
        .filePath(QStringLiteral("config.json"));
}

void KisAssetLibrarySettings::load()
{
    const QByteArray legacy = KSharedConfig::openConfig()
                                  ->group(QStringLiteral("asset_library"))
                                  .readEntry(QStringLiteral("settings_json"), QString())
                                  .toUtf8();
    load(filePath(), legacy);
}

void KisAssetLibrarySettings::load(const QString &path, const QByteArray &legacySettings)
{
    QFile file(path);
    const QByteArray bytes = file.open(QIODevice::ReadOnly) ? file.readAll() : legacySettings;
    fromJson(QJsonDocument::fromJson(bytes).object());
}

void KisAssetLibrarySettings::fromJson(const QJsonObject &object)
{
    m_raw = object;
    const bool welcome = m_usage == Usage::Welcome;
    const auto boundedInt = [&](const char *key, int fallback, int minimum, int maximum) {
        const int value = object.value(QLatin1String(key)).toInt(fallback);
        return welcome ? std::clamp(value, minimum, maximum)
                       : (value >= minimum && value <= maximum ? value : fallback);
    };
    paths.clear();
    for (const QJsonValue &value : object.value(QStringLiteral("paths")).toArray()) {
        const QJsonObject item = value.toObject();
        KisAssetLibraryPathEntry entry;
        entry.alias = item.value(QStringLiteral("alias")).toString();
        entry.path = item.value(QStringLiteral("path")).toString();
        entry.includeSubfolders =
            item.value(QStringLiteral("include_subfolders")).toBool(item.value(QStringLiteral("nested")).toBool(false));
        entry.extensions = item.value(QStringLiteral("extensions")).toString(entry.extensions);
        if (!entry.path.isEmpty())
            paths.append(entry);
    }
    if (!welcome) {
        windowWidth = std::max(80, object.value(QStringLiteral("window_width")).toInt(windowWidth));
        windowHeight = std::max(80, object.value(QStringLiteral("window_height")).toInt(windowHeight));
        const QJsonArray sizes = object.value(QStringLiteral("splitter_sizes")).toArray();
        if (sizes.size() == 2)
            splitterSizes = {std::max(80, sizes.at(0).toInt(150)), std::max(0, sizes.at(1).toInt(570))};
        rightPanelHidden = object.value(QStringLiteral("right_panel_hidden")).toBool(rightPanelHidden);
        expandedWindowWidth = std::max(80, object.value(QStringLiteral("expanded_window_width")).toInt(windowWidth));
        collapsedWindowWidth =
            std::max(80, object.value(QStringLiteral("collapsed_window_width")).toInt(collapsedWindowWidth));
    }
    autoColumns = object.value(QStringLiteral("auto_columns")).toBool(welcome ? true : autoColumns);
    columns = boundedInt("columns", welcome ? 3 : columns, 1, 12);
    thumbnailSize =
        std::clamp(object.value(QStringLiteral("thumbnail_size")).toInt(welcome ? 140 : thumbnailSize), 48, 512);
    const int legacyFont = object.value(QStringLiteral("font_size")).toInt(10);
    uiFontSize = boundedInt("ui_font_size", legacyFont, 7, 32);
    headerFontSize = boundedInt("header_font_size", uiFontSize, 7, 32);
    assetNameFontSize = boundedInt("asset_name_font_size", legacyFont, 7, 32);
}

QJsonObject KisAssetLibrarySettings::toJson() const
{
    QJsonObject object = m_usage == Usage::Welcome ? m_raw : QJsonObject();
    QJsonArray pathArray;
    for (const KisAssetLibraryPathEntry &entry : paths) {
        pathArray.append(QJsonObject{{QStringLiteral("alias"), entry.alias},
                                     {QStringLiteral("path"), entry.path},
                                     {QStringLiteral("include_subfolders"), entry.includeSubfolders},
                                     {QStringLiteral("extensions"), entry.extensions}});
    }
    object.insert(QStringLiteral("paths"), pathArray);
    object.insert(QStringLiteral("auto_columns"), autoColumns);
    object.insert(QStringLiteral("columns"), columns);
    object.insert(QStringLiteral("thumbnail_size"), thumbnailSize);
    object.insert(QStringLiteral("ui_font_size"), uiFontSize);
    object.insert(QStringLiteral("header_font_size"), headerFontSize);
    object.insert(QStringLiteral("asset_name_font_size"), assetNameFontSize);
    object.remove(QStringLiteral("font_size"));
    if (m_usage == Usage::Docker) {
        QJsonArray sizes;
        for (int value : splitterSizes)
            sizes.append(value);
        object.insert(QStringLiteral("window_width"), windowWidth);
        object.insert(QStringLiteral("window_height"), windowHeight);
        object.insert(QStringLiteral("splitter_sizes"), sizes);
        object.insert(QStringLiteral("right_panel_hidden"), rightPanelHidden);
        object.insert(QStringLiteral("expanded_window_width"), expandedWindowWidth);
        object.insert(QStringLiteral("collapsed_window_width"), collapsedWindowWidth);
    }
    return object;
}

bool KisAssetLibrarySettings::save() const
{
    return save(filePath());
}

bool KisAssetLibrarySettings::save(const QString &path) const
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(QJsonDocument(toJson()).toJson()) >= 0;
}
