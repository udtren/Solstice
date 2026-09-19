/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKACCESSGRIDEDITDIALOG_H
#define QUICKACCESSGRIDEDITDIALOG_H

#include "QuickAccessModel.h"

#include <QDialog>
#include <QList>
#include <QSet>
#include <QVector>

class QPoint;
class QRect;
class QTabWidget;
class QToolButton;
class QuickAccessGridCanvas;
class QuickAccessGridItemButton;

class QuickAccessGridEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit QuickAccessGridEditDialog(const QuickAccess::Document &document, QWidget *parent = nullptr);

    QuickAccess::Document document() const;

private Q_SLOTS:
    void currentTabChanged(int index);
    void undoCurrentTab();

private:
    friend class QuickAccessGridCanvas;
    friend class QuickAccessGridItemButton;

    QuickAccess::Grid *gridForTab(int tabIndex);
    const QuickAccess::Item *itemForId(int tabIndex, const QString &itemId) const;
    void selectItem(int tabIndex, const QString &itemId, bool toggle, bool preserveGroup);
    void selectItemsInRect(int tabIndex, const QRect &rect, bool additive);
    bool isSelected(int tabIndex, const QString &itemId) const;
    void showMovePreview(int tabIndex, const QString &itemId, int rowDelta, int columnDelta);
    void showResizePreview(int tabIndex, const QString &itemId, int rowDelta, int columnDelta);
    void clearPreview(int tabIndex);
    void moveSelected(int tabIndex, int rowDelta, int columnDelta);
    void resizeItem(int tabIndex, const QString &itemId, int rowDelta, int columnDelta);
    void showItemContextMenu(int tabIndex, const QString &itemId, const QPoint &globalPosition);
    void removeSelected(int tabIndex);
    void copyOrMoveSelected(int sourceTab, int targetTab, bool move);
    void pushUndo(int tabIndex);
    void refreshCanvas(int tabIndex);
    void updateUndoButton();

    QuickAccess::Document m_document;
    QTabWidget *m_tabs{nullptr};
    QToolButton *m_undoButton{nullptr};
    QList<QuickAccessGridCanvas *> m_canvases;
    QVector<QSet<QString>> m_selection;
    QVector<QList<QList<QuickAccess::Item>>> m_history;
};

#endif
