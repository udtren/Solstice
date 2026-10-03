/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKACCESSRESOURCESDIALOG_H
#define QUICKACCESSRESOURCESDIALOG_H

#include "QuickAccessModel.h"

#include <QDialog>

class KisCanvas2;
class QIcon;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTabWidget;

class QuickAccessResourcesDialog : public QDialog
{
    Q_OBJECT
public:
    QuickAccessResourcesDialog(KisCanvas2 *canvas, const QuickAccess::Document &document, QWidget *parent = nullptr);

Q_SIGNALS:
    void brushRequested(const QString &name);
    void actionRequested(const QString &id);
    void dockerRequested(const QString &id);
    void aliasChanged(const QString &category, const QString &id, const QJsonObject &alias);

private:
    enum Page {
        ActionsPage,
        DockersPage,
        BrushesPage,
    };
    enum ItemRole {
        IdentifierRole = Qt::UserRole + 1,
    };

    void populateBrushes();
    void populateActions();
    void populateDockers();
    void populateTableRow(QTableWidget *table,
                          const QString &category,
                          const QString &id,
                          const QString &displayName,
                          const QString &shortcut,
                          const QIcon &nativeIcon);
    void filterItems(const QString &text);
    void addSelectedBrushes();
    void saveAllAliases();
    void saveTableRow(QTableWidget *table, const QString &category, int row);
    void resetTableRow(QTableWidget *table, int row);
    void chooseRowIcon(QTableWidget *table, int row);
    QJsonObject aliasFor(const QString &category, const QString &id) const;
    QString resolveIcon(const QString &iconName) const;
    static void setColorButton(QPushButton *button, const QString &color);

    KisCanvas2 *m_canvas{nullptr};
    QuickAccess::Document m_document;
    QLineEdit *m_search{nullptr};
    QTabWidget *m_tabs{nullptr};
    QListWidget *m_brushes{nullptr};
    QTableWidget *m_actions{nullptr};
    QTableWidget *m_dockers{nullptr};
};

#endif
