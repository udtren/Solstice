/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QuickColorPopup.h"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QTabletEvent>

QuickColorPopup::QuickColorPopup(QWidget *parent, Qt::WindowFlags flags)
    : QFrame(parent, flags)
    , m_cursorPosition([] {
        return QCursor::pos();
    })
{
    m_leavePoll.setInterval(leavePollInterval);
    connect(&m_leavePoll, &QTimer::timeout, this, &QuickColorPopup::pollPointer);
}

QuickColorPopup::~QuickColorPopup()
{
    stopTracking();
}

QPoint QuickColorPopup::pointerPosition() const
{
    // Some tablet drivers do not move the cursor with the pen, so a recent
    // tablet position is more reliable than QCursor::pos().
    if (m_tabletPositionAge.isValid() && !m_tabletPositionAge.hasExpired(tabletPositionLifetime))
        return m_tabletPosition;
    return m_cursorPosition();
}

bool QuickColorPopup::pointerInside() const
{
    return frameGeometry().contains(pointerPosition());
}

bool QuickColorPopup::isLeavePollActive() const
{
    return m_leavePoll.isActive();
}

void QuickColorPopup::setCursorPositionSource(std::function<QPoint()> source)
{
    m_cursorPosition = std::move(source);
}

void QuickColorPopup::enterEvent(QEnterEvent *event)
{
    QFrame::enterEvent(event);
    m_leavePoll.stop();
}

void QuickColorPopup::leaveEvent(QEvent *event)
{
    QFrame::leaveEvent(event);

    // A combo box opens its list as a separate Qt::Popup window. Moving
    // the pointer into that window sends Leave to this popup even though
    // the user is still interacting with one of its child controls.
    if (QApplication::activePopupWidget())
        return;

    // A hovering pen on Windows can produce a Leave while the pointer is
    // still over the popup. Trust the position, and poll until it is outside.
    if (pointerInside()) {
        m_leavePoll.start();
        return;
    }
    close();
}

void QuickColorPopup::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    if (!m_filterInstalled && qApp) {
        qApp->installEventFilter(this);
        m_filterInstalled = true;
    }
}

void QuickColorPopup::closeEvent(QCloseEvent *event)
{
    stopTracking();
    QFrame::closeEvent(event);
}

bool QuickColorPopup::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::TabletMove:
    case QEvent::TabletPress:
    case QEvent::TabletRelease:
        m_tabletPosition = static_cast<QTabletEvent *>(event)->globalPosition().toPoint();
        m_tabletPositionAge.start();
        break;
    case QEvent::TabletLeaveProximity:
        m_tabletPositionAge.invalidate();
        break;
    default:
        break;
    }
    return QFrame::eventFilter(watched, event);
}

void QuickColorPopup::pollPointer()
{
    // Keep the popup while a combo-box list is open, as leaveEvent() does.
    if (!isVisible() || QApplication::activePopupWidget() || pointerInside())
        return;
    m_leavePoll.stop();
    close();
}

void QuickColorPopup::stopTracking()
{
    m_leavePoll.stop();
    if (m_filterInstalled && qApp) {
        qApp->removeEventFilter(this);
        m_filterInstalled = false;
    }
}
