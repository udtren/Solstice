/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KISWELCOMEASSETLIBRARYWIDGET_H
#define KISWELCOMEASSETLIBRARYWIDGET_H

#include <QWidget>

#include <memory>

class KisMainWindow;

class KisWelcomeAssetLibraryWidget : public QWidget
{
public:
    explicit KisWelcomeAssetLibraryWidget(QWidget *parent = nullptr);
    ~KisWelcomeAssetLibraryWidget() override;

    void setMainWindow(KisMainWindow *mainWindow);
    void reloadSettings();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Private;
    const std::unique_ptr<Private> d;
};

#endif
