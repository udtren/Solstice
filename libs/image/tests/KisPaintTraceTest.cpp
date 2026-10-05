/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KisPaintTrace.h"
#include "kis_painter.h"
#include "kis_stroke_job.h"
#include <KoTestConfig.h>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTest>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

class TraceJobStrategy : public KisStrokeJobStrategy
{
public:
    std::function<void()> callback;
    void run(KisStrokeJobData *) override
    {
        callback();
    }
    QString debugId() const override
    {
        return QStringLiteral("trace test");
    }
};

class KisPaintTraceTest : public QObject
{
    Q_OBJECT
private:
    QJsonObject snapshot()
    {
        if (!KisPaintTrace::flush())
            return {};
        QFile file(QString::fromLocal8Bit(qgetenv("KRITA_PAINT_TRACE"))
                   + QStringLiteral(".%1.json").arg(QCoreApplication::applicationPid()));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
private Q_SLOTS:
    void testDisabledProcess()
    {
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(), {"--disabled"});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
    }
    void testConcurrentScopes()
    {
        QVERIFY(KisPaintTrace::enabled());
        std::atomic<int> ready{0};
        std::atomic<bool> start{false};
        std::vector<std::thread> workers;
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([this, &ready, &start]() {
                ++ready;
                while (!start.load())
                    std::this_thread::yield();
                for (int j = 0; j < 200; ++j) {
                    KisPaintTrace::Scope scope("test.scope", this);
                    KisPaintTrace::instant("test.instant", this);
                }
            });
        }
        while (ready.load() != 4)
            std::this_thread::yield();
        start = true;
        for (auto &worker : workers)
            worker.join();
        const QJsonObject root = snapshot();
        const auto events = root["traceEvents"].toArray();
        QCOMPARE(events.size(), 1600);
        QSet<double> threads;
        for (const auto &value : events) {
            const auto row = value.toObject();
            threads.insert(row["tid"].toDouble());
            QCOMPARE(row["args"].toObject()["owner"].toString(), QString::number(quintptr(this), 16));
            QVERIFY(row["ts"].toDouble() >= 0);
            if (row["ph"] == "X")
                QVERIFY(row["dur"].toDouble() >= 0);
        }
        QCOMPARE(threads.size(), 4);
        QCOMPARE(root["metadata"].toObject()["dropped_events"].toInt(), 0);
    }
    void testInputDispatchIdentity()
    {
        QCOMPARE(KisPaintTrace::currentInput(), quint64(0));
        quint64 outer = 0;
        {
            KisPaintTrace::InputScope input("test.input", this);
            outer = KisPaintTrace::currentInput();
            QVERIFY(outer != 0);
            {
                KisPaintTrace::InputScope nonInput(nullptr, this);
                QCOMPARE(KisPaintTrace::currentInput(), quint64(0));
            }
            QCOMPARE(KisPaintTrace::currentInput(), outer);
            {
                KisPaintTrace::InputScope nested("test.nested", this);
                QVERIFY(KisPaintTrace::currentInput() > outer);
            }
            QCOMPARE(KisPaintTrace::currentInput(), outer);
            quint64 workerInput = outer;
            std::thread worker([&workerInput]() {
                workerInput = KisPaintTrace::currentInput();
            });
            worker.join();
            QCOMPARE(workerInput, quint64(0));
            KisPaintTrace::link("test.accepted", this, outer);
        }
        QCOMPARE(KisPaintTrace::currentInput(), quint64(0));
        const auto root = snapshot();
        QCOMPARE(root["metadata"].toObject()["schema"].toInt(), 2);
        const auto last = root["traceEvents"].toArray().last().toObject();
        QCOMPARE(last["args"].toObject()["id"].toString(), QString::number(outer));
    }
    void testJobLineageAcrossThreads()
    {
        const int before = snapshot()["traceEvents"].toArray().size();
        TraceJobStrategy parentStrategy, childStrategy;
        std::unique_ptr<KisStrokeJob> parent, child;
        quint64 input = 0, parentId = 0, childId = 0;
        childStrategy.callback = [&]() {
            childId = KisPaintTrace::currentJob();
            KisPaintTrace::Scope span("test.child_span");
        };
        parentStrategy.callback = [&]() {
            parentId = KisPaintTrace::currentJob();
            child.reset(new KisStrokeJob(&childStrategy, nullptr, 0, true));
        };
        {
            KisPaintTrace::InputScope scope("test.job_input", this);
            input = KisPaintTrace::currentInput();
            parent.reset(new KisStrokeJob(&parentStrategy, nullptr, 0, true));
            // Simulate a queued job discarded before execution.
            KisStrokeJob discarded(&childStrategy, nullptr, 0, true);
        }
        bool restored = false;
        std::thread first([&]() {
            parent->run();
            restored = KisPaintTrace::currentJob() == 0 && KisPaintTrace::currentInput() == 0;
        });
        first.join();
        QVERIFY(restored);
        std::thread second([&]() {
            child->run();
        });
        second.join();
        QVERIFY(parentId && childId && parentId != childId);
        parent.reset();
        child.reset();
        const auto events = snapshot()["traceEvents"].toArray();
        QHash<quint64, quint64> parents;
        QSet<quint64> started, destroyed;
        quint64 spanJob = 0;
        for (int i = before; i < events.size(); ++i) {
            const auto event = events[i].toObject();
            const auto args = event["args"].toObject();
            const quint64 id = args["id"].toString().toULongLong();
            if (event["name"] == "job.created")
                parents[id] = args["parent"].toString().toULongLong();
            if (event["name"] == "job.started")
                started.insert(id);
            if (event["name"] == "job.destroyed")
                destroyed.insert(id);
            if (event["name"] == "test.child_span")
                spanJob = args["job"].toString().toULongLong();
        }
        QCOMPARE(parents.size(), 3);
        QCOMPARE(started.size(), 2);
        QCOMPARE(destroyed.size(), 3);
        QCOMPARE(parents[parentId], input);
        QCOMPARE(parents[childId], parentId);
        QCOMPARE(spanJob, childId);
        QCOMPARE(KisPaintTrace::currentJob(), quint64(0));
    }
    void testDirtyFlowAndPainterDrain()
    {
        const int before = snapshot()["traceEvents"].toArray().size();
        KisPainter painter;
        const quint64 first = KisPaintTrace::nextId(), second = KisPaintTrace::nextId();
        const quint64 dirty = KisPaintTrace::nextId();
        painter.addDirtyRect(QRect(1, 2, 3, 4));
        painter.recordPaintTraceBatch(first);
        painter.recordPaintTraceBatch(second);
        {
            KisPaintTrace::FlowScope scope(dirty);
            QCOMPARE(KisPaintTrace::currentFlow(), dirty);
            {
                KisPaintTrace::FlowScope masked(0);
                QCOMPARE(KisPaintTrace::currentFlow(), quint64(0));
            }
            QCOMPARE(KisPaintTrace::currentFlow(), dirty);
            quint64 otherThread = dirty;
            std::thread worker([&]() {
                otherThread = KisPaintTrace::currentFlow();
            });
            worker.join();
            QCOMPARE(otherThread, quint64(0));
            QCOMPARE(painter.takeDirtyRegion(), QVector<QRect>{QRect(1, 2, 3, 4)});
            QVERIFY(painter.takeDirtyRegion().isEmpty());
        }
        QCOMPARE(KisPaintTrace::currentFlow(), quint64(0));
        const auto events = snapshot()["traceEvents"].toArray();
        QSet<quint64> linked;
        int count = 0;
        for (int i = before; i < events.size(); ++i) {
            const auto event = events[i].toObject();
            if (event["name"] != "batch.to_dirty")
                continue;
            ++count;
            const auto args = event["args"].toObject();
            QCOMPARE(args["parent"].toString().toULongLong(), dirty);
            linked.insert(args["id"].toString().toULongLong());
        }
        QCOMPARE(count, 2);
        QVERIFY(linked.contains(first) && linked.contains(second));
    }
    void testStrokeConditionsSnapshot()
    {
        QJsonObject conditions{{"preset_name", QString::fromUtf8("Brush \u7b46")},
                               {"nominal_size_px", 256.0},
                               {"incremental", false}};
        const auto id = KisPaintTrace::nextId();
        KisPaintTrace::strokeConditions(this, 0, conditions);
        QVERIFY(snapshot()["metadata"].toObject()["stroke_conditions"].toArray().isEmpty());
        KisPaintTrace::strokeConditions(this, id, conditions);
        conditions.insert("nominal_size_px", 64.0);
        const auto strokes = snapshot()["metadata"].toObject()["stroke_conditions"].toArray();
        QCOMPARE(strokes.size(), 1);
        const auto row = strokes.first().toObject();
        QCOMPARE(row["input"].toString(), QString::number(id));
        QCOMPARE(row["canvas"].toString(), QString::number(quintptr(this), 16));
        QCOMPARE(row["conditions"].toObject()["nominal_size_px"].toDouble(), 256.0);
        QCOMPARE(row["conditions"].toObject()["preset_name"].toString(), conditions["preset_name"].toString());
    }
    void testRectangleCoordinates()
    {
        const auto id = KisPaintTrace::nextId();
        KisPaintTrace::rectangle("test.rect", this, id, QRect(-17, 23, 41, 5), 0);
        auto row = snapshot()["traceEvents"].toArray().last().toObject();
        QCOMPARE(row["args"].toObject()["rect"].toArray(), (QJsonArray{-17, 23, 41, 5}));
        QCOMPARE(row["args"].toObject()["lod"].toInt(), 0);
        QCOMPARE(row["args"].toObject()["id"].toString(), QString::number(id));
        KisPaintTrace::rectangle("test.empty_rect", this, id, QRect(), 2);
        row = snapshot()["traceEvents"].toArray().last().toObject();
        QCOMPARE(row["args"].toObject()["rect"].toArray(), (QJsonArray{0, 0, 0, 0}));
        QCOMPARE(row["args"].toObject()["lod"].toInt(), 2);
    }
    void testBoundedRecording()
    {
        for (int i = 0; i < 1025; ++i)
            KisPaintTrace::strokeConditions(this, KisPaintTrace::nextId(), {});
        QCOMPARE(snapshot()["metadata"].toObject()["stroke_conditions"].toArray().size(), 1024);
        QVERIFY(snapshot()["metadata"].toObject()["dropped_events"].toInt() > 0);
        for (int i = 0; i < 262145; ++i)
            KisPaintTrace::instant("test.overflow");
        const auto root = snapshot();
        QCOMPARE(root["traceEvents"].toArray().size(), 262144);
        QVERIFY(root["metadata"].toObject()["dropped_events"].toInt() > 0);
    }
};

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    qputenv("EXTRA_RESOURCE_DIRS", QByteArray(KRITA_RESOURCE_DIRS_FOR_TESTS));
    qputenv("KRITA_PLUGIN_PATH", QByteArray(KRITA_PLUGINS_DIR_FOR_TESTS));
    QApplication app(argc, argv);
    const QString prefix = QDir::tempPath() + QStringLiteral("/solstice-paint-trace-test-%1").arg(app.applicationPid());
    if (app.arguments().contains("--disabled")) {
        qunsetenv("KRITA_PAINT_TRACE");
        {
            KisPaintTrace::Scope scope("disabled");
        }
        KisPaintTrace::instant("disabled");
        KisPaintTrace::strokeConditions(nullptr, 123, {});
        KisPaintTrace::InputScope input("disabled", nullptr);
        KisPaintTrace::JobScope job(123);
        return !KisPaintTrace::enabled() && !KisPaintTrace::nextId() && !KisPaintTrace::currentInput()
                && KisPaintTrace::flush()
            ? 0
            : 1;
    }
    qputenv("KRITA_PAINT_TRACE", prefix.toLocal8Bit());
    KisPaintTraceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "KisPaintTraceTest.moc"
