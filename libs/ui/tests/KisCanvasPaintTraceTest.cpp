/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../canvas/KisCanvasPaintTrace.h"
#include <QTest>

class KisCanvasPaintTraceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testMappingAndViewInvalidation()
    {
        const QRect viewport(0, 0, 100, 100);
        QTransform transform;
        transform.translate(10, 20);
        transform.scale(0.5, 0.5);
        QVERIFY(KisCanvasPaintTrace::supportsMapping(transform, false));
        QCOMPARE(KisCanvasPaintTrace::visibleImageRect(QRect(-40, -60, 100, 100), transform, viewport),
                 QRect(0, 0, 40, 40));
        QCOMPARE(KisCanvasPaintTrace::visibleImageRect(QRect(400, 0, 10, 10), transform, viewport), QRect());
        QTransform mirror(-1, 0, 0, 1, 100, 0);
        QVERIFY(KisCanvasPaintTrace::supportsMapping(mirror, false));
        QCOMPARE(KisCanvasPaintTrace::visibleImageRect(QRect(10, 10, 20, 20), mirror, viewport), QRect(70, 10, 20, 20));
        QVERIFY(!KisCanvasPaintTrace::supportsMapping(transform, true));
        QVERIFY(!KisCanvasPaintTrace::supportsMapping(QTransform().rotate(15), false));
        QVERIFY(!KisCanvasPaintTrace::supportsMapping(QTransform().scale(0, 1), false));
        KisCanvasPaintTrace trace;
        QVERIFY(!trace.setView(transform, viewport, 1, false));
        QVERIFY(trace.addUpdate(1, viewport));
        QVERIFY(!trace.setView(transform, viewport, 1, false));
        QVERIFY(trace.setView(mirror, viewport, 1, false));
        QVERIFY(trace.paint(viewport, viewport, 2).uploads.isEmpty());
        QVERIFY(trace.setView(mirror, viewport, 2, false));
        QCOMPARE(trace.swapped(), quint64(0));
        QVERIFY(trace.setView(mirror, QRect(0, 0, 50, 50), 2, false));
        QVERIFY(trace.setView(mirror, QRect(0, 0, 50, 50), 2, true));
    }
    void testPartialRenderAndDelayedBlit()
    {
        KisCanvasPaintTrace trace;
        const QRect all(0, 0, 100, 100), left(0, 0, 50, 100), right(50, 0, 50, 100);
        QVERIFY(trace.addUpdate(1, all));
        // A decoration-only paint cannot consume unrendered image content.
        QVERIFY(trace.paint({}, all, 2).uploads.isEmpty());
        QCOMPARE(trace.swapped(), quint64(2));
        QVERIFY(trace.paint(left, all, 3).uploads.isEmpty());
        // Image content rendered into a cache but not yet blitted.
        QVERIFY(trace.paint(right, left, 4).uploads.isEmpty());
        const auto covered = trace.paint({}, right, 5);
        QCOMPARE(covered.previous, quint64(4));
        QCOMPARE(covered.uploads, QVector<quint64>{1});
        QCOMPARE(trace.swapped(), quint64(5));
        QCOMPARE(trace.swapped(), quint64(0));
    }
    void testLateUploadAndSeparateWidgets()
    {
        KisCanvasPaintTrace first, second;
        const QRect rect(0, 0, 10, 10);
        QVERIFY(first.addUpdate(1, rect));
        QCOMPARE(first.paint(rect, rect, 2).uploads, QVector<quint64>{1});
        QVERIFY(first.addUpdate(3, rect));
        // Swap after upload 3 belongs to the already painted frame 2.
        QCOMPARE(first.swapped(), quint64(2));
        QCOMPARE(second.swapped(), quint64(0));
        QCOMPARE(first.paint(rect, rect, 4).uploads, QVector<quint64>{3});
        first.reset();
        QCOMPARE(first.swapped(), quint64(0));
        QVERIFY(first.paint(rect, rect, 5).uploads.isEmpty());
    }
    void testBoundedAndReset()
    {
        KisCanvasPaintTrace trace;
        const QRect rect(0, 0, 10, 10);
        QVERIFY(trace.addUpdate(1, {}));
        for (int i = 0; i < KisCanvasPaintTrace::capacity; ++i)
            QVERIFY(trace.addUpdate(i + 1, rect));
        QVERIFY(!trace.addUpdate(5000, rect));
        trace.reset();
        QVERIFY(trace.paint(rect, rect, 5001).uploads.isEmpty());
        QVERIFY(trace.addUpdate(5002, rect));
    }
};
QTEST_GUILESS_MAIN(KisCanvasPaintTraceTest)
#include "KisCanvasPaintTraceTest.moc"
