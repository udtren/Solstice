/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef RESTNOTEDOCK_H
#define RESTNOTEDOCK_H

#include <KoDockFactoryBase.h>

#include <QDateTime>
#include <QDockWidget>
#include <QJsonObject>
#include <QPointer>

class QLabel;
class QPushButton;
class QTimer;
class QWidget;

struct RestNoteConfig {
    int workMinutes{50};
    int breakMinutes{10};
    bool microEnabled{true};
    int microIntervalMinutes{20};
    int microDurationSeconds{20};
    int microSkipThresholdSeconds{180};
    bool idleEnabled{true};
    int idleThresholdSeconds{45};
    int toastMargin{128};
    int toastWidth{480};
    int toastHeight{220};
    int toastTitleFontSize{28};
    int toastMessageFontSize{24};
    int overlayTitleFontSize{50};
    int overlayMessageFontSize{32};
    int overlaySkipFontSize{18};

    int workSeconds() const;
    int breakSeconds() const;
    int microIntervalSeconds() const;
    static QString filePath();
    void load();
    bool save() const;
};

class RestNoteDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit RestNoteDock(QWidget *parent = nullptr);
    ~RestNoteDock() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class State {
        Running,
        Paused,
        Break,
        MicroBreak,
        Idle
    };

    void buildUi();
    void updateSizing();
    void tick();
    void refreshDisplay();
    void startBigBreak();
    void endBigBreak();
    void maybeStartMicroBreak();
    void endMicroBreak();
    void cancelTransientWindows();
    void showSettings();
    QWidget *mainWindowHost() const;
    QScreen *currentScreen() const;

    RestNoteConfig m_config;
    State m_state{State::Running};
    int m_remaining{0};
    int m_microRemaining{0};
    QDateTime m_lastActivity;
    QTimer *m_tickTimer{nullptr};
    QWidget *m_root{nullptr};
    QLabel *m_statusLabel{nullptr};
    QLabel *m_timeLabel{nullptr};
    QLabel *m_subLabel{nullptr};
    QPushButton *m_pauseButton{nullptr};
    QPushButton *m_resetButton{nullptr};
    QPushButton *m_restButton{nullptr};
    QPushButton *m_settingsButton{nullptr};
    QPointer<QWidget> m_overlay;
    QPointer<QWidget> m_microToast;
};

class RestNoteDockFactory : public KoDockFactoryBase
{
public:
    QString id() const override;
    QDockWidget *createDockWidget() override;
    DockPosition defaultDockPosition() const override;
};

#endif
