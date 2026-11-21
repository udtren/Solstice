/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKCOLORPOPUP_H
#define QUICKCOLORPOPUP_H

#include <QElapsedTimer>
#include <QFrame>
#include <QPoint>
#include <QTimer>
#include <functional>

/**
 * The compact HueSVC popup: a frameless, always-on-top Qt::Tool window that
 * closes when the pointer leaves it.
 *
 * A Leave event alone is not trusted. Under a hovering pen on Windows, Qt
 * receives the pen through WM_POINTER messages, Windows' own mouse tracking
 * never sees the pointer over the new window, and a Leave arrives while the
 * pointer is still inside. On Leave the popup therefore checks the pointer
 * position; while it is inside, a short poll closes the popup once the
 * pointer really is outside.
 */
class QuickColorPopup : public QFrame
{
public:
    explicit QuickColorPopup(QWidget *parent = nullptr, Qt::WindowFlags flags = {});
    ~QuickColorPopup() override;

    /// Global pointer position used for the inside check: the last tablet
    /// position while tablet events are recent, the cursor position otherwise.
    QPoint pointerPosition() const;
    bool pointerInside() const;

    bool isLeavePollActive() const;
    static constexpr int leavePollInterval = 100;
    /// A tablet position older than this falls back to QCursor::pos().
    static constexpr int tabletPositionLifetime = 250;

    /// Tests replace QCursor::pos() so they never move the real cursor.
    void setCursorPositionSource(std::function<QPoint()> source);

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void pollPointer();
    void stopTracking();

    QTimer m_leavePoll;
    std::function<QPoint()> m_cursorPosition;
    QPoint m_tabletPosition;
    QElapsedTimer m_tabletPositionAge;
    bool m_filterInstalled{false};
};

#endif
