/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <simpletest.h>

#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QSemaphore>
#include <QSet>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "canvas/KisCanvasUpdateBatcher.h"

/**
 * GPU engine (Solstice): concurrent canvas projection updates are built in
 * batches (docs/agent/gpu-engine.md). Every request must be built exactly
 * once, before its caller returns, in arrival order, by builds that never
 * overlap.
 */
class KisCanvasUpdateBatcherTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testSingleThreadBuildsOwnRequest();
    void testWaitingRequestsShareOneBuild();
    void testBatchLimits_data();
    void testBatchLimits();
    void testConcurrentRequests();
    void testReentrantRequest();
};

namespace
{
using Request = KisCanvasUpdateBatcher::Request;

/// Records builds; the first one blocks until released.
struct Recorder {
    QMutex mutex;
    QVector<QVector<int>> batches;
    QSet<int> built;
    QSemaphore firstStarted;
    QSemaphore releaseFirst;
    bool blockFirst = false;

    KisCanvasUpdateBatcher::BuildFunction function()
    {
        return [this](const QVector<Request> &requests) {
            bool block = false;
            {
                QMutexLocker locker(&mutex);
                QVector<int> ids;
                for (const Request &request : requests) {
                    ids << request.rect.x();
                    built.insert(request.rect.x());
                }
                block = blockFirst && batches.isEmpty();
                batches << ids;
            }
            if (block) {
                firstStarted.release();
                releaseFirst.acquire();
            }
        };
    }

    bool isBuilt(int id)
    {
        QMutexLocker locker(&mutex);
        return built.contains(id);
    }
};

Request request(int id, int area = 1)
{
    Request result;
    result.rect = QRect(id, 0, area, 1);
    result.flow = quint64(id) + 1;
    return result;
}

bool waitForPending(KisCanvasUpdateBatcher &batcher, int count)
{
    for (int i = 0; i < 5000; i++) {
        if (batcher.pendingCountForTesting() == count) {
            return true;
        }
        QThread::msleep(1);
    }
    return false;
}
} // namespace

void KisCanvasUpdateBatcherTest::testSingleThreadBuildsOwnRequest()
{
    KisCanvasUpdateBatcher batcher;
    Recorder recorder;
    QVector<quint64> flows;
    for (int id = 0; id < 5; id++) {
        batcher.process(request(id), [&](const QVector<Request> &requests) {
            for (const Request &r : requests) {
                flows << r.flow;
            }
            recorder.function()(requests);
        });
        QVERIFY(recorder.isBuilt(id));
    }
    QCOMPARE(recorder.batches, (QVector<QVector<int>>{{0}, {1}, {2}, {3}, {4}}));
    QCOMPARE(flows, (QVector<quint64>{1, 2, 3, 4, 5}));
    QCOMPARE(batcher.pendingCountForTesting(), 0);
}

void KisCanvasUpdateBatcherTest::testWaitingRequestsShareOneBuild()
{
    KisCanvasUpdateBatcher batcher;
    Recorder recorder;
    recorder.blockFirst = true;
    std::atomic<int> returnedEarly{0};

    std::vector<std::thread> threads;
    auto start = [&](int id) {
        threads.emplace_back([&, id]() {
            batcher.process(request(id), recorder.function());
            if (!recorder.isBuilt(id)) {
                returnedEarly++;
            }
        });
    };
    start(0);
    recorder.firstStarted.acquire();
    for (int id = 1; id <= 5; id++) {
        start(id);
        QVERIFY(waitForPending(batcher, id));
    }
    // Nobody returns while their requests wait for the running build.
    QCOMPARE(recorder.batches.size(), 1);
    recorder.releaseFirst.release();
    for (std::thread &thread : threads) {
        thread.join();
    }
    QCOMPARE(returnedEarly.load(), 0);
    QCOMPARE(recorder.batches, (QVector<QVector<int>>{{0}, {1, 2, 3, 4, 5}}));
}

void KisCanvasUpdateBatcherTest::testBatchLimits_data()
{
    QTest::addColumn<int>("maxRequests");
    QTest::addColumn<qint64>("maxPixels");
    QTest::addColumn<QVector<int>>("areas");
    QTest::addColumn<QVector<QVector<int>>>("expected");

    QTest::newRow("request count") << 2 << qint64(1000) << QVector<int>{1, 1, 1, 1, 1}
                                   << QVector<QVector<int>>{{0}, {1, 2}, {3, 4}, {5}};
    // The first request of a batch is taken even if it is too large alone.
    QTest::newRow("pixel count") << 32 << qint64(100) << QVector<int>{40, 40, 200, 10}
                                 << QVector<QVector<int>>{{0}, {1, 2}, {3}, {4}};
}

void KisCanvasUpdateBatcherTest::testBatchLimits()
{
    QFETCH(int, maxRequests);
    QFETCH(qint64, maxPixels);
    QFETCH(QVector<int>, areas);
    QFETCH(QVector<QVector<int>>, expected);

    KisCanvasUpdateBatcher batcher(nullptr, maxRequests, maxPixels);
    Recorder recorder;
    recorder.blockFirst = true;
    std::vector<std::thread> threads;
    threads.emplace_back([&]() {
        batcher.process(request(0), recorder.function());
    });
    recorder.firstStarted.acquire();
    // Started one at a time: the arrival order is the id order.
    for (int i = 0; i < areas.size(); i++) {
        const int id = i + 1;
        const int area = areas[i];
        threads.emplace_back([&, id, area]() {
            batcher.process(request(id, area), recorder.function());
        });
        QVERIFY(waitForPending(batcher, id));
    }
    recorder.releaseFirst.release();
    for (std::thread &thread : threads) {
        thread.join();
    }
    QCOMPARE(recorder.batches, expected);
}

void KisCanvasUpdateBatcherTest::testConcurrentRequests()
{
    constexpr int ThreadCount = 8;
    constexpr int RequestsPerThread = 300;
    KisCanvasUpdateBatcher batcher(nullptr, 8);
    Recorder recorder;
    std::atomic<int> building{0};
    std::atomic<int> overlaps{0};
    std::atomic<int> returnedEarly{0};
    auto build = [&](const QVector<Request> &requests) {
        if (building.fetch_add(1) != 0) {
            overlaps++;
        }
        recorder.function()(requests);
        QThread::usleep(50);
        building.fetch_sub(1);
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < ThreadCount; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < RequestsPerThread; i++) {
                const int id = t * RequestsPerThread + i;
                batcher.process(request(id), build);
                if (!recorder.isBuilt(id)) {
                    returnedEarly++;
                }
            }
        });
    }
    for (std::thread &thread : threads) {
        thread.join();
    }

    QCOMPARE(overlaps.load(), 0);
    QCOMPARE(returnedEarly.load(), 0);
    int total = 0;
    int largest = 0;
    QSet<int> seen;
    for (const QVector<int> &batch : recorder.batches) {
        QVERIFY(batch.size() <= 8);
        largest = qMax(largest, int(batch.size()));
        for (int id : batch) {
            QVERIFY2(!seen.contains(id), "a request was built twice");
            seen.insert(id);
            total++;
        }
    }
    QCOMPARE(total, ThreadCount * RequestsPerThread);
    QCOMPARE(batcher.pendingCountForTesting(), 0);
    qInfo() << "builds" << recorder.batches.size() << "for" << total << "requests, largest batch" << largest;
}

void KisCanvasUpdateBatcherTest::testReentrantRequest()
{
    // A build that triggers another update of the same canvas must not wait
    // for itself.
    KisCanvasUpdateBatcher batcher;
    Recorder recorder;
    std::function<void(const QVector<Request> &)> build = [&](const QVector<Request> &requests) {
        recorder.function()(requests);
        if (requests.first().rect.x() == 0) {
            batcher.process(request(1), build);
        }
    };
    batcher.process(request(0), build);
    QCOMPARE(recorder.batches, (QVector<QVector<int>>{{0}, {1}}));
    // The batcher still works normally afterwards.
    batcher.process(request(2), build);
    QCOMPARE(recorder.batches.size(), 3);
}

SIMPLE_TEST_MAIN(KisCanvasUpdateBatcherTest)

#include "KisCanvasUpdateBatcherTest.moc"
