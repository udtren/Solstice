/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KIS_ASSET_LIBRARY_SETTINGS_H
#define KIS_ASSET_LIBRARY_SETTINGS_H

#include <QJsonObject>
#include <QVector>
#include <kritaui_export.h>

struct KisAssetLibraryPathEntry {
    QString alias;
    QString path;
    bool includeSubfolders{false};
    QString extensions{QStringLiteral("kra,jpg,jpeg,png,svg")};
};

class KRITAUI_EXPORT KisAssetLibrarySettings
{
public:
    // Preserve the two existing consumers' validation and save contracts.
    enum class Usage {
        Docker,
        Welcome
    };
    explicit KisAssetLibrarySettings(Usage usage = Usage::Docker);

    QVector<KisAssetLibraryPathEntry> paths;
    int windowWidth{720};
    int windowHeight{520};
    QList<int> splitterSizes{150, 570};
    bool rightPanelHidden{false};
    int expandedWindowWidth{720};
    int collapsedWindowWidth{170};
    bool autoColumns{true};
    int columns{3};
    int thumbnailSize{140};
    int uiFontSize{10};
    int headerFontSize{10};
    int assetNameFontSize{10};

    static QString filePath();
    void load();
    void load(const QString &path, const QByteArray &legacySettings);
    bool save() const;
    bool save(const QString &path) const;
    void fromJson(const QJsonObject &object);
    QJsonObject toJson() const;

private:
    Usage m_usage;
    QJsonObject m_raw;
};
#endif
