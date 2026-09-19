/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKACCESSGESTURE_H
#define QUICKACCESSGESTURE_H

#include "QuickAccessModel.h"

#include <QDialog>
#include <QObject>

#include <functional>

class QCheckBox;
class QFrame;
class QKeyEvent;
class QPushButton;
class QSpinBox;
class QTabWidget;

class QuickAccessGestureController : public QObject
{
public:
    QuickAccessGestureController(QuickAccess::Document *document,
                                 std::function<void(const QuickAccess::Item &)> execute,
                                 QObject *parent = nullptr);
    ~QuickAccessGestureController() override;

    void reloadSettings();
    void cancelGesture();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString eventKey(QKeyEvent *event) const;
    QJsonObject pageForKey(const QString &key) const;
    QString directionForDelta(const QPoint &delta) const;
    void showPreview(const QJsonObject &page, const QPoint &position);
    void executeConfig(const QJsonObject &config);

    QuickAccess::Document *m_document;
    std::function<void(const QuickAccess::Item &)> m_execute;
    bool m_enabled{true};
    bool m_showPreview{true};
    int m_threshold{20};
    QString m_activeKey;
    QJsonObject m_activePage;
    QPoint m_startPosition;
    QPoint m_lastPosition;
    QFrame *m_preview{nullptr};
};

class QuickAccessGestureDialog : public QDialog
{
public:
    explicit QuickAccessGestureDialog(const QuickAccess::Document &document, QWidget *parent = nullptr);

    QJsonArray pages() const;
    bool gesturesEnabled() const;
    bool showPreview() const;
    int movementThreshold() const;

private:
    void addPage(const QJsonObject &page = QJsonObject());
    QWidget *createPage(int pageIndex);
    void editDirection(int pageIndex, const QString &direction, QPushButton *button);
    void updateResourceButton(QPushButton *button, const QJsonObject &config) const;
    QString directionLabel(const QJsonObject &config) const;

    const QuickAccess::Document *m_document{nullptr};
    QList<QJsonObject> m_pages;
    QTabWidget *m_tabs{nullptr};
    QCheckBox *m_enabled{nullptr};
    QCheckBox *m_preview{nullptr};
    QSpinBox *m_threshold{nullptr};
};

#endif
