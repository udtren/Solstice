/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QApplication>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QTest>
#include <QWidget>

#include "TestControlInputInjector.h"

namespace
{
struct Recorded {
    QEvent::Type type = QEvent::None;
    QPointF position;
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons = Qt::NoButton;
    qreal pressure = -1.0;
    Qt::MouseEventSource source = Qt::MouseEventNotSynthesized;
    ulong timestamp = 0;
    bool toApplication = false;
};

class RecordingWidget : public QWidget
{
public:
    QVector<Recorded> events;

protected:
    bool event(QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            events.append({event->type(),
                           mouse->position(),
                           mouse->button(),
                           mouse->buttons(),
                           -1.0,
                           mouse->source(),
                           mouse->timestamp()});
            event->accept();
            return true;
        }
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease: {
            auto *tablet = static_cast<QTabletEvent *>(event);
            events.append({event->type(),
                           tablet->position(),
                           tablet->button(),
                           tablet->buttons(),
                           tablet->pressure(),
                           Qt::MouseEventNotSynthesized,
                           tablet->timestamp()});
            event->accept();
            return true;
        }
        default:
            return QWidget::event(event);
        }
    }
};

/// Records proximity events, which go to the application object.
class ProximityRecorder : public QObject
{
public:
    explicit ProximityRecorder(QVector<Recorded> *events)
        : m_events(events)
    {
        qApp->installEventFilter(this);
    }
    ~ProximityRecorder() override
    {
        qApp->removeEventFilter(this);
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == qApp
            && (event->type() == QEvent::TabletEnterProximity || event->type() == QEvent::TabletLeaveProximity)) {
            Recorded recorded;
            recorded.type = event->type();
            recorded.toApplication = true;
            m_events->append(recorded);
        }
        return false;
    }

private:
    QVector<Recorded> *m_events;
};

QVector<TestControlInputInjector::Point> threePoints()
{
    TestControlInputInjector::Point a;
    a.position = QPointF(10, 10);
    a.pressure = 0.2;
    a.timeMs = 0;
    TestControlInputInjector::Point b = a;
    b.position = QPointF(20, 15);
    b.pressure = 0.6;
    b.timeMs = 30;
    TestControlInputInjector::Point c = a;
    c.position = QPointF(30, 20);
    c.pressure = 1.0;
    c.timeMs = 60;
    return {a, b, c};
}
} // namespace

class TestControlInputInjectorTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testMouseStroke();
    void testTabletStroke();
    void testRealtimeTiming();
    void testRejectsInvalidStrokes();
    void testTargetDestroyed();
};

void TestControlInputInjectorTest::testMouseStroke()
{
    RecordingWidget widget;
    widget.resize(100, 100);
    TestControlInputInjector injector;

    bool finished = false;
    TestControlInputInjector::Report report;
    injector.playStroke(&widget,
                        TestControlInputInjector::Device::Mouse,
                        Qt::LeftButton,
                        threePoints(),
                        false,
                        [&](const TestControlInputInjector::Report &r) {
                            report = r;
                            finished = true;
                        });
    QVERIFY(injector.isBusy());
    QTRY_VERIFY(finished);

    QVERIFY(report.error.isEmpty());
    QCOMPARE(report.eventsSent, 4);
    QVERIFY(report.pressAccepted);
    QVERIFY(report.releaseAccepted);

    const QVector<Recorded> &e = widget.events;
    QCOMPARE(e.size(), 4);
    QCOMPARE(e[0].type, QEvent::MouseButtonPress);
    QCOMPARE(e[0].button, Qt::LeftButton);
    QCOMPARE(e[0].buttons, Qt::MouseButtons(Qt::LeftButton));
    QCOMPARE(e[0].position, QPointF(10, 10));
    QCOMPARE(e[1].type, QEvent::MouseMove);
    QCOMPARE(e[1].button, Qt::NoButton);
    QCOMPARE(e[1].buttons, Qt::MouseButtons(Qt::LeftButton));
    QCOMPARE(e[2].position, QPointF(30, 20));
    QCOMPARE(e[3].type, QEvent::MouseButtonRelease);
    QCOMPARE(e[3].button, Qt::LeftButton);
    QCOMPARE(e[3].buttons, Qt::MouseButtons(Qt::NoButton));
    QCOMPARE(e[3].position, QPointF(30, 20));
    for (const Recorded &recorded : e) {
        QCOMPARE(recorded.source, Qt::MouseEventNotSynthesized);
    }
    QCOMPARE(e[1].timestamp - e[0].timestamp, ulong(30));
    QCOMPARE(e[2].timestamp - e[0].timestamp, ulong(60));
}

void TestControlInputInjectorTest::testTabletStroke()
{
    RecordingWidget widget;
    widget.resize(100, 100);
    QVector<Recorded> proximity;
    ProximityRecorder recorder(&proximity);
    TestControlInputInjector injector;

    bool finished = false;
    TestControlInputInjector::Report report;
    injector.playStroke(&widget,
                        TestControlInputInjector::Device::Tablet,
                        Qt::LeftButton,
                        threePoints(),
                        false,
                        [&](const TestControlInputInjector::Report &r) {
                            report = r;
                            finished = true;
                        });
    QTRY_VERIFY(finished);
    QVERIFY(report.error.isEmpty());
    QCOMPARE(report.eventsSent, 8);

    QCOMPARE(proximity.size(), 2);
    QCOMPARE(proximity[0].type, QEvent::TabletEnterProximity);
    QCOMPARE(proximity[1].type, QEvent::TabletLeaveProximity);

    const QVector<Recorded> &e = widget.events;
    QCOMPARE(e.size(), 6);
    QCOMPARE(e[0].type, QEvent::TabletPress);
    QCOMPARE(e[0].pressure, 0.2);
    QCOMPARE(e[1].type, QEvent::MouseButtonPress);
    QCOMPARE(e[1].source, Qt::MouseEventSynthesizedBySystem);
    QCOMPARE(e[2].type, QEvent::TabletMove);
    QCOMPARE(e[2].pressure, 0.6);
    QCOMPARE(e[2].buttons, Qt::MouseButtons(Qt::LeftButton));
    QCOMPARE(e[3].type, QEvent::TabletMove);
    QCOMPARE(e[3].pressure, 1.0);
    QCOMPARE(e[4].type, QEvent::TabletRelease);
    QCOMPARE(e[4].pressure, 0.0);
    QCOMPARE(e[4].buttons, Qt::MouseButtons(Qt::NoButton));
    QCOMPARE(e[5].type, QEvent::MouseButtonRelease);
    QCOMPARE(e[5].source, Qt::MouseEventSynthesizedBySystem);
}

void TestControlInputInjectorTest::testRealtimeTiming()
{
    RecordingWidget widget;
    TestControlInputInjector injector;

    bool finished = false;
    TestControlInputInjector::Report report;
    injector.playStroke(&widget,
                        TestControlInputInjector::Device::Mouse,
                        Qt::LeftButton,
                        threePoints(),
                        true,
                        [&](const TestControlInputInjector::Report &r) {
                            report = r;
                            finished = true;
                        });
    QTRY_VERIFY(finished);
    QVERIFY(report.error.isEmpty());
    QVERIFY2(report.durationMs >= 60, qPrintable(QString::number(report.durationMs)));
}

void TestControlInputInjectorTest::testRejectsInvalidStrokes()
{
    RecordingWidget widget;
    TestControlInputInjector injector;
    QString error;
    auto record = [&error](const TestControlInputInjector::Report &r) {
        error = r.error;
    };

    injector.playStroke(&widget, TestControlInputInjector::Device::Mouse, Qt::LeftButton, {}, false, record);
    QVERIFY(!error.isEmpty());

    QVector<TestControlInputInjector::Point> points = threePoints();
    std::swap(points[1].timeMs, points[2].timeMs);
    error.clear();
    injector.playStroke(&widget, TestControlInputInjector::Device::Mouse, Qt::LeftButton, points, false, record);
    QVERIFY(!error.isEmpty());

    error.clear();
    injector.playStroke(nullptr, TestControlInputInjector::Device::Mouse, Qt::LeftButton, threePoints(), false, record);
    QVERIFY(!error.isEmpty());

    bool finished = false;
    injector.playStroke(&widget,
                        TestControlInputInjector::Device::Mouse,
                        Qt::LeftButton,
                        threePoints(),
                        true,
                        [&finished](const TestControlInputInjector::Report &) {
                            finished = true;
                        });
    error.clear();
    injector.playStroke(&widget, TestControlInputInjector::Device::Mouse, Qt::LeftButton, threePoints(), false, record);
    QVERIFY(error.contains("still playing"));
    QTRY_VERIFY(finished);
}

void TestControlInputInjectorTest::testTargetDestroyed()
{
    auto *widget = new RecordingWidget;
    TestControlInputInjector injector;

    QString error;
    bool finished = false;
    injector.playStroke(widget,
                        TestControlInputInjector::Device::Mouse,
                        Qt::LeftButton,
                        threePoints(),
                        true,
                        [&](const TestControlInputInjector::Report &r) {
                            error = r.error;
                            finished = true;
                        });
    QTest::qWait(10);
    delete widget;
    QTRY_VERIFY(finished);
    QVERIFY(error.contains("destroyed"));
    QVERIFY(!injector.isBusy());
}

QTEST_MAIN(TestControlInputInjectorTest)

#include "TestControlInputInjectorTest.moc"
