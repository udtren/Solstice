/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KIS_MENU_MNEMONIC_FILTER_H
#define KIS_MENU_MNEMONIC_FILTER_H

#include <QObject>

class QAction;
class QMenuBar;

// Owned by one menu bar. Observes menu construction without consuming input.
class KisMenuMnemonicFilter : public QObject
{
public:
    explicit KisMenuMnemonicFilter(QMenuBar *menuBar);
    void setSuppressed(bool suppressed);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateAction(QAction *action);
    QMenuBar *m_menuBar;
    bool m_suppressed = false;
    bool m_updating = false;
};

#endif
