/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KIS_DOCKER_TRAY_H
#define KIS_DOCKER_TRAY_H

#include <QScopedPointer>
#include <QWidget>

#include <ksharedconfig.h>

#include "KisDockerBorrowRegistry.h"
#include "KisDockerTrayLayout.h"
#include "KisPopupWidgetInterface.h"
#include "kritaui_export.h"

class QMainWindow;

/**
 * The Docker Tray: a frameless popup that shows several dockers of its
 * main window at once, in columns, with the sizes stored in
 * KisDockerTrayLayout. Hidden dockers work too. The content is borrowed
 * through KisDockerBorrowRegistry when the tray opens and returned when it
 * closes. See docs/agent/docker-tray-plan.md.
 *
 * Two ways to close it:
 * - dismiss() (KisPopupWidgetInterface) only hides it; the input manager
 *   calls it while deregistering, so it must not emit finished();
 * - closeTray() hides it and emits finished(), for closings the tray
 *   starts itself (Esc, the shortcut, application deactivation).
 */
class KRITAUI_EXPORT KisDockerTray : public QWidget, public KisDockerBorrower, public KisPopupWidgetInterface
{
    Q_OBJECT
    Q_INTERFACES(KisPopupWidgetInterface)

public:
    /// The tray of \p window, created on first use; nullptr without a window.
    static KisDockerTray *forWindow(QMainWindow *window, KSharedConfigPtr config = KSharedConfig::openConfig());

    ~KisDockerTray() override;

    /// Opens the tray centered at the cursor; \p position is ignored.
    void popup(const QPoint &position) override;

    /**
     * Opens the tray centered at \p globalPosition, kept inside the
     * available geometry of that screen. Re-reads the layout. If no docker
     * of the layout exists, nothing is shown and finished() is emitted.
     */
    void popupAt(const QPoint &globalPosition);

    bool onScreen() override;
    void dismiss() override;
    void closeTray();

    QString dockerBorrowerName() const override;
    void dockerContentGranted(const QString &dockerId, QWidget *content, QWidget *titleArea) override;
    void dockerContentHeldElsewhere(const QString &dockerId, const QString &holderName) override;

    KisDockerTrayLayout trayLayout() const;

Q_SIGNALS:
    void finished() override; // KisPopupWidgetInterface

protected:
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    KisDockerTray(QMainWindow *window, KSharedConfigPtr config);

    void rebuild(const KisDockerTrayLayout &layout);
    void applySizes(const QRect &availableGeometry);
    void moveCenteredAt(const QPoint &globalPosition, const QRect &availableGeometry);
    void releaseAll();

    struct Private;
    const QScopedPointer<Private> m_d;
};

#endif // KIS_DOCKER_TRAY_H
