/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KIS_DOCKER_BORROW_REGISTRY_H
#define KIS_DOCKER_BORROW_REGISTRY_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include "kritaui_export.h"

class QDockWidget;
class QLabel;
class QMainWindow;
class QWidget;

/**
 * A widget that shows the content of a main-window docker outside the
 * docker (the toolbar docker box, Quick Adjust's Tool Options pad, the
 * Docker Tray). See docs/agent/docker-tray.md.
 *
 * Inherit QObject or QWidget first, then this class: the base destructor
 * returns anything still borrowed while the derived widget's children are
 * alive. It does not call the virtual methods of the borrower being
 * destroyed.
 */
class KRITAUI_EXPORT KisDockerBorrower
{
public:
    KisDockerBorrower();
    virtual ~KisDockerBorrower();

    /// Names this borrower in the labels of other borrowers.
    virtual QString dockerBorrowerName() const = 0;

    /// The text the docker shows while this borrower holds its content.
    virtual QString dockerPlaceholderText() const;

    /**
     * This borrower now holds the content of \p dockerId. Put \p content
     * (and \p titleArea, the title-bar controls of the animation dockers,
     * or nullptr) into a layout.
     */
    virtual void dockerContentGranted(const QString &dockerId, QWidget *content, QWidget *titleArea) = 0;

    /**
     * The content of \p dockerId is held by \p holderName. Called on the
     * previous holder just before its widgets move to the new holder (stop
     * using them), and on every waiting borrower when the holder changes.
     */
    virtual void dockerContentHeldElsewhere(const QString &dockerId, const QString &holderName) = 0;

private:
    Q_DISABLE_COPY(KisDockerBorrower)
};

/**
 * Lends the content widget of a main window's dockers to borrowers.
 *
 * The docker itself (QDockWidget) never leaves the main window; only its
 * widget() moves, and the docker shows a placeholder label meanwhile. The
 * docker's visibility is not changed. The last borrower that requested a
 * docker holds it; earlier ones wait and get it back, in order, when the
 * holder releases it.
 *
 * One registry exists per main window, as its child.
 */
class KRITAUI_EXPORT KisDockerBorrowRegistry : public QObject
{
    Q_OBJECT
public:
    /// The registry of \p window, created on first use; nullptr without a window.
    static KisDockerBorrowRegistry *forWindow(QMainWindow *window);

    ~KisDockerBorrowRegistry() override;

    /**
     * Gives the content of the docker named \p dockerId to \p borrower,
     * taking it from the current holder if there is one. Returns false if
     * the window has no such docker or the docker has no content.
     */
    bool request(const QString &dockerId, KisDockerBorrower *borrower);

    /// Gives the content back (to the next waiting borrower, or the docker).
    void release(const QString &dockerId, KisDockerBorrower *borrower);

    void releaseAll(KisDockerBorrower *borrower);

    KisDockerBorrower *holder(const QString &dockerId) const;

    /// Borrowers of \p dockerId, oldest first; the last one is the holder.
    QList<KisDockerBorrower *> borrowers(const QString &dockerId) const;

    QDockWidget *docker(const QString &dockerId) const;

private:
    explicit KisDockerBorrowRegistry(QMainWindow *window);

    struct Entry {
        QPointer<QDockWidget> docker;
        QPointer<QWidget> content;
        QPointer<QWidget> titleArea;
        QPointer<QLabel> placeholder;
        QList<KisDockerBorrower *> borrowers;
    };

    void hand(const QString &dockerId, Entry &entry);
    void returnToDocker(Entry &entry);
    void notifyWaiting(const QString &dockerId, const Entry &entry);
    void forget(KisDockerBorrower *borrower);

    friend class KisDockerBorrower;

    QPointer<QMainWindow> m_window;
    QHash<QString, Entry> m_entries;
};

#endif // KIS_DOCKER_BORROW_REGISTRY_H
