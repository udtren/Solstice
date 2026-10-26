/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TESTCONTROLINPUTINJECTOR_H
#define TESTCONTROLINPUTINJECTOR_H

#include <QElapsedTimer>
#include <QEvent>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <functional>

class QPointingDevice;

/**
 * Plays one synthetic pointer stroke into a widget, so the widget's event
 * filters (Krita's KisInputManager on the canvas) see it like device input.
 * The events are marked spontaneous and delivered through
 * QCoreApplication::notify(), as QTest does: tools cancel a shape on a
 * non-spontaneous release, and sendEvent() clears the flag in Qt 6.
 *
 * Mouse: press, moves, release.
 * Tablet: TabletEnterProximity (to the application), one hover TabletMove at
 * the first point, TabletPress after HoverSettleMs, a system-synthesized
 * mouse press, TabletMoves, TabletRelease and TabletLeaveProximity. The hover
 * move lets KoToolProxy switch the canvas to the stylus before the press, as
 * a real pen's approach does; the first stylus event on a canvas creates new
 * tool instances, which ends a stroke that has already started. The
 * synthesized press stands for the one Windows generates after a pen press:
 * KisInputManager eats exactly one mouse press after each TabletPress
 * (EventEater::peckish), and without it the next real mouse stroke would lose
 * its press. Leaving proximity restores mouse input.
 *
 * In real-time mode each point is sent at its time offset; otherwise the
 * events are sent from consecutive event-loop iterations. Positions are in
 * the widget's coordinates. Only one stroke plays at a time.
 */
class TestControlInputInjector : public QObject
{
    Q_OBJECT
public:
    enum class Device {
        Mouse,
        Tablet
    };

    struct Point {
        QPointF position;
        qreal pressure = 1.0;
        qreal xTilt = 0.0;
        qreal yTilt = 0.0;
        /// Offset from the first point, in milliseconds.
        qint64 timeMs = 0;
    };

    struct Report {
        int eventsSent = 0;
        bool pressAccepted = false;
        bool releaseAccepted = false;
        qint64 durationMs = 0;
        /// Empty on success.
        QString error;
    };

    using Done = std::function<void(const Report &report)>;

    /// Time between the hover move and the press of a tablet stroke. Longer
    /// than KisInputManager's move compression (tabletEventsDelay, 10 ms by
    /// default), which a press would otherwise discard.
    static constexpr qint64 HoverSettleMs = 50;

    explicit TestControlInputInjector(QObject *parent = nullptr);
    ~TestControlInputInjector() override;

    bool isBusy() const;

    /**
     * Starts the stroke and calls @p done once it ends or fails (target
     * destroyed, invalid points). Point times must not decrease.
     */
    void playStroke(QWidget *target,
                    Device device,
                    Qt::MouseButton button,
                    const QVector<Point> &points,
                    bool realtime,
                    Done done);

private:
    struct Step {
        QEvent::Type type;
        int pointIndex;
        /// Mouse event sent with Qt::MouseEventSynthesizedBySystem.
        bool synthesizedMouse = false;
        /// Tablet move without buttons or pressure, before the press.
        bool hover = false;
    };

    /// Time offset of @p step: tablet steps after the hover move are
    /// shifted by HoverSettleMs.
    qint64 stepOffsetMs(const Step &step) const;
    void scheduleNext();
    void sendStep(const Step &step);
    void finish(const QString &error);

    QPointingDevice *m_stylus = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;

    QPointer<QWidget> m_target;
    Device m_device = Device::Mouse;
    Qt::MouseButton m_button = Qt::LeftButton;
    QVector<Point> m_points;
    QVector<Step> m_steps;
    int m_nextStep = 0;
    bool m_realtime = true;
    bool m_busy = false;
    qint64 m_startMs = 0;
    ulong m_timestampBase = 0;
    Report m_report;
    Done m_done;
};

#endif // TESTCONTROLINPUTINJECTOR_H
