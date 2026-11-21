/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KIS_DLG_DOCKER_TRAY_H
#define KIS_DLG_DOCKER_TRAY_H

#include <QDialog>
#include <QScopedPointer>
#include <QString>
#include <QVector>

#include "KisDockerTrayLayout.h"
#include "kritaui_export.h"

/**
 * Configures the Docker Tray: which dockers it shows, in which column and
 * order, and the size of each (docs/agent/docker-tray-plan.md). Works on a
 * copy of the layout; read the result with trayLayout() after accept().
 */
class KRITAUI_EXPORT KisDlgDockerTray : public QDialog
{
    Q_OBJECT
public:
    struct DockerInfo {
        QString id;
        QString title;
    };

    KisDlgDockerTray(const KisDockerTrayLayout &layout, const QVector<DockerInfo> &dockers, QWidget *parent = nullptr);
    ~KisDlgDockerTray() override;

    KisDockerTrayLayout trayLayout() const;

private Q_SLOTS:
    void slotAdd();
    void slotRemove();
    void slotMoveUp();
    void slotMoveDown();
    void slotAddColumn();
    void slotRemoveColumn();
    void slotRestoreDefaults();
    void slotSizeChanged();
    void slotSelectionChanged();

private:
    void move(int delta);
    void refresh(const QString &selectDocker = QString(), int selectColumn = -1);
    QString title(const QString &dockerId) const;
    QString selectedDocker() const;
    /// Column of the selected column or docker, or -1.
    int selectedColumn() const;

    struct Private;
    const QScopedPointer<Private> m_d;
};

#endif // KIS_DLG_DOCKER_TRAY_H
