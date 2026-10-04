/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KisMenuMnemonicFilter.h"

#include <QAction>
#include <QActionEvent>
#include <QMenuBar>
#include <QScopedValueRollback>
#include <QVariant>

namespace
{
constexpr auto OriginalText = "solsticeOriginalMenuText";
constexpr auto SuppressedText = "solsticeSuppressedMenuText";

QString withoutMnemonics(const QString &text)
{
    QString result;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] != QLatin1Char('&')) {
            result += text[i];
        } else if (i + 1 < text.size() && text[i + 1] == QLatin1Char('&')) {
            result += QStringLiteral("&&"); // Keep a literal ampersand in Qt labels.
            ++i;
        }
    }
    return result;
}
} // namespace

KisMenuMnemonicFilter::KisMenuMnemonicFilter(QMenuBar *menuBar)
    : QObject(menuBar)
    , m_menuBar(menuBar)
{
    menuBar->installEventFilter(this);
}

void KisMenuMnemonicFilter::setSuppressed(bool suppressed)
{
    m_suppressed = suppressed;
    for (QAction *action : m_menuBar->actions())
        updateAction(action);
}

bool KisMenuMnemonicFilter::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_menuBar && !m_updating
        && (event->type() == QEvent::ActionAdded || event->type() == QEvent::ActionChanged)) {
        // Apply before QMenuBar processes the event and registers shortcuts.
        updateAction(static_cast<QActionEvent *>(event)->action());
    }
    return QObject::eventFilter(watched, event);
}

void KisMenuMnemonicFilter::updateAction(QAction *action)
{
    QScopedValueRollback<bool> updating(m_updating, true);
    const QVariant previous = action->property(SuppressedText);
    const QString text = action->text();
    // A plugin/XMLGUI update replaces the cached original, but our own label
    // changes (or unrelated enabled/icon changes) must not overwrite it.
    if (!previous.isValid() || text != previous.toString())
        action->setProperty(OriginalText, text);

    const QString original = action->property(OriginalText).toString();
    if (m_suppressed) {
        const QString replacement = withoutMnemonics(original);
        action->setProperty(SuppressedText, replacement);
        if (text != replacement)
            action->setText(replacement);
    } else {
        action->setProperty(SuppressedText, QVariant());
        if (text != original)
            action->setText(original);
    }
}
