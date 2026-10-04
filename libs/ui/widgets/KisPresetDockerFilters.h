/* SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KIS_PRESET_DOCKER_FILTERS_H
#define KIS_PRESET_DOCKER_FILTERS_H

#include <QMap>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <kritaui_export.h>

class KisResourceModel;
class KisTagFilterResourceProxyModel;
class QMenu;
class QToolButton;

/** Per-docker facets; never modifies storage activation or preset resources. */
class KRITAUI_EXPORT KisPresetDockerFilters : public QWidget
{
public:
    explicit KisPresetDockerFilters(KisTagFilterResourceProxyModel *model, QWidget *parent = nullptr);

private:
    void refresh();
    void apply();
    void populate(QMenu *menu, bool engines);
    KisTagFilterResourceProxyModel *m_model;
    KisResourceModel *m_resources;
    QToolButton *m_engines;
    QToolButton *m_bundles;
    QMap<QString, QString> m_engineNames, m_bundleNames;
    QMap<int, QString> m_storageKeys;
    QSet<QString> m_excludedEngines, m_excludedBundles;
    QTimer m_refresh;
};
#endif
