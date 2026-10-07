/* SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KIS_PRESET_DOCKER_FILTERS_H
#define KIS_PRESET_DOCKER_FILTERS_H

#include <QHash>
#include <QMap>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <kritaui_export.h>

class KisResourceItemListView;
class KisResourceModel;
class KisTagFilterResourceProxyModel;
class QComboBox;
class QMenu;
class QModelIndex;
class QToolButton;

/**
 * Per-docker facets and grouping; never modifies storage activation or
 * preset resources. Grouping (docs/agent/brush-preset-grouping.md) shows
 * @p view in groups by brush engine or bundle.
 */
class KRITAUI_EXPORT KisPresetDockerFilters : public QWidget
{
public:
    enum Grouping {
        NoGrouping = 0,
        GroupByEngine = 1,
        GroupByBundle = 2
    };

    explicit KisPresetDockerFilters(KisTagFilterResourceProxyModel *model,
                                    KisResourceItemListView *view = nullptr,
                                    QWidget *parent = nullptr);
    ~KisPresetDockerFilters() override;

    /// (sort key, header label) of the group of a preset index.
    QPair<QString, QString> groupOf(const QModelIndex &index) const;

private:
    void refresh();
    void apply();
    void applyGrouping();
    void populate(QMenu *menu, bool engines);
    KisTagFilterResourceProxyModel *m_model;
    KisResourceModel *m_resources;
    QToolButton *m_engines;
    QToolButton *m_bundles;
    QComboBox *m_grouping;
    QPointer<KisResourceItemListView> m_view;
    /// Groups by resource id; bundle membership is a database query.
    mutable QHash<int, QPair<QString, QString>> m_groupCache;
    QMap<QString, QString> m_engineNames, m_bundleNames;
    QMap<int, QString> m_storageKeys;
    QSet<QString> m_excludedEngines, m_excludedBundles;
    QTimer m_refresh;
};
#endif
