/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKADJUSTTOOLOPTIONSPAD_H
#define QUICKADJUSTTOOLOPTIONSPAD_H

#include <QObject>
#include <QPointer>
#include <ksharedconfig.h>
#include <functional>

#include "KisDockerBorrowRegistry.h"

class QDockWidget;
class QLabel;
class QMainWindow;
class QToolButton;
class QWidget;

/// Owns the borrowed Tool Options widget's temporary host. The dock supplies
/// its current canvas context; no view manager is retained across callbacks.
/// The content is borrowed through KisDockerBorrowRegistry, so another
/// borrower (toolbar docker box, Docker Tray) can take it and give it back.
class QuickAdjustToolOptionsPad : public QObject, public KisDockerBorrower
{
public:
    struct Context {
        bool canvasAvailable = false;
        QMainWindow *mainWindow = nullptr;
        bool toolOptionsInDocker = true;
    };
    using ContextProvider = std::function<Context()>;
    QuickAdjustToolOptionsPad(QDockWidget *owner,
                              QToolButton *toggle,
                              bool compactPopup,
                              ContextProvider context,
                              KSharedConfigPtr config = KSharedConfig::openConfig());
    ~QuickAdjustToolOptionsPad() override;
    void ensure();
    void setVisible(bool visible);
    void resizeToContent();
    void position();
    void hide();

    QString dockerBorrowerName() const override;
    QString dockerPlaceholderText() const override;
    void dockerContentGranted(const QString &dockerId, QWidget *content, QWidget *titleArea) override;
    void dockerContentHeldElsewhere(const QString &dockerId, const QString &holderName) override;

private:
    void restore();
    QDockWidget *m_owner;
    QToolButton *m_toolOptionsToggle;
    bool m_compactPopup;
    ContextProvider m_context;
    KSharedConfigPtr m_config;
    QPointer<QDockWidget> m_toolOptionsDocker;
    QPointer<QWidget> m_toolOptionsPad;
    QPointer<QWidget> m_borrowedToolOptions;
    QPointer<QLabel> m_waitingLabel;
    QPointer<KisDockerBorrowRegistry> m_registry;
    bool m_toolOptionsDockerWasVisible{false};
};

#endif
