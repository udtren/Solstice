/*
 *  SPDX-FileCopyrightText: 2010 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_simple_update_queue_test.h"
#include "KisPaintTrace.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <simpletest.h>

#include "kistest.h"

#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>

#include "kis_group_layer.h"
#include "kis_paint_layer.h"
#include "kis_adjustment_layer.h"
#include "filter/kis_filter.h"
#include "filter/kis_filter_configuration.h"
#include "filter/kis_filter_registry.h"
#include "kis_selection.h"

#include "kis_update_job_item.h"
#include "kis_simple_update_queue.h"
#include "scheduler_utils.h"
#include <KisGlobalResourcesInterface.h>

#include "lod_override.h"

void KisSimpleUpdateQueueTest::testTraceSplitAndMerge()
{
    if (!KisPaintTrace::enabled())
        QSKIP("Enable KRITA_PAINT_TRACE to verify provenance");
    const QRect bounds(0, 0, 1024, 1024);
    KisImageSP image = new KisImage(0, 1024, 1024, KoColorSpaceRegistry::instance()->rgb8(), "trace");
    KisPaintLayerSP layer = new KisPaintLayer(image, "trace", OPACITY_OPAQUE_U8);
    image->barrierLock();
    image->addNode(layer);
    image->unlock();
    image->waitForDone();

    const quint64 first = KisPaintTrace::nextId(), second = KisPaintTrace::nextId();
    KisTestableSimpleUpdateQueue merged;
    {
        KisPaintTrace::FlowScope source(first);
        merged.addUpdateJob(layer, QRect(0, 0, 30, 30), bounds, 0);
    }
    const quint64 survivor = merged.getWalkersList().first()->paintTraceId();
    {
        KisPaintTrace::FlowScope source(second);
        merged.addUpdateJob(layer, QRect(0, 0, 40, 40), bounds, 0);
    }
    QCOMPARE(merged.getWalkersList().size(), 1);
    QCOMPARE(merged.getWalkersList().first()->paintTraceId(), survivor);
    // Recalculation must not replace the identity used by existing edges.
    merged.getWalkersList().first()->recalculate(QRect(0, 0, 40, 40));
    QCOMPARE(merged.getWalkersList().first()->paintTraceId(), survivor);

    const quint64 splitSource = KisPaintTrace::nextId();
    KisTestableSimpleUpdateQueue split;
    {
        KisPaintTrace::FlowScope source(splitSource);
        split.addUpdateJob(layer, QRect(0, 0, 1000, 1000), bounds, 0);
    }
    QVERIFY(split.getWalkersList().size() > 1);

    class OptimizeQueue : public KisTestableSimpleUpdateQueue
    {
    public:
        OptimizeQueue()
        {
            m_maxMergeAlpha = 0;
            m_maxCollectAlpha = 2;
        }
    } optimized;
    optimized.addUpdateJob(layer, QRect(0, 0, 30, 30), bounds, 0);
    optimized.addUpdateJob(layer, QRect(0, 0, 40, 40), bounds, 0);
    QCOMPARE(optimized.getWalkersList().size(), 2);
    const quint64 kept = optimized.getWalkersList().first()->paintTraceId();
    const quint64 removed = optimized.getWalkersList().last()->paintTraceId();
    optimized.optimize();
    QCOMPARE(optimized.getWalkersList().size(), 1);

    QVERIFY(KisPaintTrace::flush());
    QFile file(QString::fromLocal8Bit(qgetenv("KRITA_PAINT_TRACE"))
               + QStringLiteral(".%1.json").arg(QCoreApplication::applicationPid()));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QHash<quint64, QSet<quint64>> edges;
    for (const auto &entry : QJsonDocument::fromJson(file.readAll()).object()["traceEvents"].toArray()) {
        const auto event = entry.toObject();
        const auto args = event["args"].toObject();
        const quint64 id = args["id"].toString().toULongLong();
        const quint64 parent = args["parent"].toString().toULongLong();
        if (event["name"] == "projection.request" && parent)
            edges[parent].insert(id);
        if (event["name"] == "projection.walker_request" || event["name"] == "projection.walker_merged")
            edges[id].insert(parent);
    }
    auto reachable = [&edges](quint64 source, quint64 destination) {
        QSet<quint64> visited;
        QList<quint64> pending{source};
        while (!pending.isEmpty()) {
            const auto next = pending.takeLast();
            if (next == destination)
                return true;
            if (visited.contains(next))
                continue;
            visited.insert(next);
            for (quint64 child : edges.value(next))
                pending.append(child);
        }
        return false;
    };
    QVERIFY(reachable(first, survivor));
    QVERIFY(reachable(second, survivor));
    for (const auto &walker : split.getWalkersList())
        QVERIFY(reachable(splitSource, walker->paintTraceId()));
    QVERIFY(reachable(removed, kept));
    QVERIFY(!reachable(splitSource, survivor));
}

void KisSimpleUpdateQueueTest::testJobProcessing()
{
    KisTestableUpdaterContext context(2);

    QRect imageRect(0,0,200,200);

    const KoColorSpace * cs = KoColorSpaceRegistry::instance()->rgb8();
    KisImageSP image = new KisImage(0, imageRect.width(), imageRect.height(), cs, "merge test");

    KisPaintLayerSP paintLayer = new KisPaintLayer(image, "test", OPACITY_OPAQUE_U8);

    image->barrierLock();
    image->addNode(paintLayer);
    image->unlock();

    QRect dirtyRect1(0,0,50,100);
    QRect dirtyRect2(0,0,100,100);
    QRect dirtyRect3(50,0,50,100);
    QRect dirtyRect4(150,150,50,50);
    QRect dirtyRect5(dirtyRect4); // theoretically, should be merged with 4

    QVector<KisUpdateJobItem*> jobs;
    KisWalkersList walkersList;

    /**
     * Process the queue and look what has been added into
     * the updater context
     */

    KisTestableSimpleUpdateQueue queue;

    queue.addUpdateJob(paintLayer, dirtyRect1, imageRect, 0);
    queue.addUpdateJob(paintLayer, dirtyRect2, imageRect, 0);
    queue.addUpdateJob(paintLayer, dirtyRect3, imageRect, 0);
    queue.addUpdateJob(paintLayer, dirtyRect4, imageRect, 0);

    {
        TestUtil::LodOverride l(1, image);
        queue.addUpdateJob(paintLayer, dirtyRect5, imageRect, 1);
    }

    queue.processQueue(context);

    jobs = context.getJobs();

    QVERIFY(checkWalker(jobs[0]->walker(), dirtyRect2));
    QVERIFY(checkWalker(jobs[1]->walker(), dirtyRect4));
    QCOMPARE(jobs.size(), 2);

    QCOMPARE(context.currentLevelOfDetail(), 0);


    walkersList = queue.getWalkersList();

    QCOMPARE(walkersList.size(), 1);
    QVERIFY(checkWalker(walkersList[0], dirtyRect5, 1));
}

void KisSimpleUpdateQueueTest::testSplitUpdate()
{
    testSplit(false);
}

void KisSimpleUpdateQueueTest::testSplitFullRefresh()
{
    testSplit(true);
}

void KisSimpleUpdateQueueTest::testSplit(bool useFullRefresh)
{
    QRect imageRect(0,0,1024,1024);

    const KoColorSpace * cs = KoColorSpaceRegistry::instance()->rgb8();
    KisImageSP image = new KisImage(0, imageRect.width(), imageRect.height(), cs, "merge test");

    KisPaintLayerSP paintLayer = new KisPaintLayer(image, "test", OPACITY_OPAQUE_U8);

    image->barrierLock();
    image->addNode(paintLayer);
    image->unlock();

    QRect dirtyRect1(0,0,1000,1000);

    KisTestableSimpleUpdateQueue queue;
    KisWalkersList& walkersList = queue.getWalkersList();

    if(!useFullRefresh) {
        queue.addUpdateJob(paintLayer, dirtyRect1, imageRect, 0);
    }
    else {
        queue.addFullRefreshJob(paintLayer, dirtyRect1, imageRect, 0);
    }

    QCOMPARE(walkersList.size(), 4);

    QVERIFY(checkWalker(walkersList[0], QRect(0,0,512,512)));
    QVERIFY(checkWalker(walkersList[1], QRect(512,0,488,512)));
    QVERIFY(checkWalker(walkersList[2], QRect(0,512,512,488)));
    QVERIFY(checkWalker(walkersList[3], QRect(512,512,488,488)));

    queue.optimize();

    //must change nothing

    QCOMPARE(walkersList.size(), 4);
    QVERIFY(checkWalker(walkersList[0], QRect(0,0,512,512)));
    QVERIFY(checkWalker(walkersList[1], QRect(512,0,488,512)));
    QVERIFY(checkWalker(walkersList[2], QRect(0,512,512,488)));
    QVERIFY(checkWalker(walkersList[3], QRect(512,512,488,488)));
}

void KisSimpleUpdateQueueTest::testChecksum()
{
    QRect imageRect(0,0,512,512);
    QRect dirtyRect(100,100,100,100);

    const KoColorSpace * colorSpace = KoColorSpaceRegistry::instance()->rgb8();
    KisImageSP image = new KisImage(0, imageRect.width(), imageRect.height(), colorSpace, "test");

    KisLayerSP paintLayer1 = new KisPaintLayer(image, "paint1", OPACITY_OPAQUE_U8);
    KisAdjustmentLayerSP adjustmentLayer = new KisAdjustmentLayer(image, "adj", 0, 0);

    image->barrierLock();
    image->addNode(paintLayer1, image->rootLayer());
    image->addNode(adjustmentLayer, image->rootLayer());
    image->unlock();

    KisFilterSP filter = KisFilterRegistry::instance()->value("blur");
    Q_ASSERT(filter);
    KisFilterConfigurationSP configuration = filter->defaultConfiguration(KisGlobalResourcesInterface::instance());


    KisTestableSimpleUpdateQueue queue;
    KisWalkersList& walkersList = queue.getWalkersList();

    {
        TestUtil::LodOverride l(1, image);
        queue.addUpdateJob(adjustmentLayer, dirtyRect, imageRect, 1);
        QCOMPARE(walkersList[0]->checksumValid(), true);
        QCOMPARE(walkersList[0]->levelOfDetail(), 1);
    }

    adjustmentLayer->setFilter(configuration->cloneWithResourcesSnapshot());

    {
        TestUtil::LodOverride l(1, image);
        QCOMPARE(walkersList[0]->checksumValid(), false);
    }


    QVector<KisUpdateJobItem*> jobs;
    KisTestableUpdaterContext context(2);

    {
        TestUtil::LodOverride l(1, image);
        queue.processQueue(context);
    }

    jobs = context.getJobs();

    {
        TestUtil::LodOverride l(1, image);
        QCOMPARE(jobs[0]->walker()->checksumValid(), true);
    }
}

void KisSimpleUpdateQueueTest::testMixingTypes()
{
    QRect imageRect(0,0,1024,1024);

    const KoColorSpace * cs = KoColorSpaceRegistry::instance()->rgb8();
    KisImageSP image = new KisImage(0, imageRect.width(), imageRect.height(), cs, "merge test");

    KisPaintLayerSP paintLayer = new KisPaintLayer(image, "test", OPACITY_OPAQUE_U8);

    image->barrierLock();
    image->addNode(paintLayer);
    image->unlock();

    QRect dirtyRect1(0,0,200,200);
    QRect dirtyRect2(0,0,200,200);
    QRect dirtyRect3(20,20,200,200);

    KisTestableSimpleUpdateQueue queue;
    KisWalkersList& walkersList = queue.getWalkersList();

    queue.addUpdateJob(paintLayer, {dirtyRect1}, imageRect, 0, KisProjectionUpdateFlag::None);
    queue.addFullRefreshJob(paintLayer, {dirtyRect2}, imageRect, 0, KisProjectionUpdateFlag::None);
    queue.addFullRefreshJob(paintLayer, {dirtyRect3}, imageRect, 0, KisProjectionUpdateFlag::None);
    queue.addUpdateJob(paintLayer, {dirtyRect1}, imageRect, 0, KisProjectionUpdateFlag::NoFilthy);
    queue.addFullRefreshJob(paintLayer, {dirtyRect1}, imageRect, 0, KisProjectionUpdateFlag::NoFilthy);
    queue.addUpdateJob(paintLayer, {dirtyRect1}, imageRect, 0, KisProjectionUpdateFlag::DontInvalidateFrames);
    queue.addFullRefreshJob(paintLayer, {dirtyRect1}, imageRect, 0, KisProjectionUpdateFlag::DontInvalidateFrames);

    QCOMPARE(walkersList.size(), 6);

    QVERIFY(checkWalker(walkersList[0], QRect(0,0,200,200)));
    QVERIFY(checkWalker(walkersList[1], QRect(0,0,220,220)));
    QVERIFY(checkWalker(walkersList[2], QRect(0,0,200,200)));
    QVERIFY(checkWalker(walkersList[3], QRect(0,0,200,200)));
    QVERIFY(checkWalker(walkersList[4], QRect(0,0,200,200)));
    QVERIFY(checkWalker(walkersList[5], QRect(0,0,200,200)));

    QCOMPARE(walkersList[0]->type(), KisBaseRectsWalker::UPDATE);
    QCOMPARE(walkersList[1]->type(), KisBaseRectsWalker::FULL_REFRESH);
    QCOMPARE(walkersList[2]->type(), KisBaseRectsWalker::UPDATE_NO_FILTHY);
    QCOMPARE(walkersList[3]->type(), KisBaseRectsWalker::FULL_REFRESH_NO_FILTHY);
    QCOMPARE(walkersList[4]->type(), KisBaseRectsWalker::UPDATE);
    QCOMPARE(walkersList[4]->clonesDontInvalidateFrames(), true);
    QCOMPARE(walkersList[5]->type(), KisBaseRectsWalker::FULL_REFRESH);
    QCOMPARE(walkersList[5]->clonesDontInvalidateFrames(), true);
}

void KisSimpleUpdateQueueTest::testSpontaneousJobsCompression()
{
    KisTestableSimpleUpdateQueue queue;
    KisSpontaneousJobsList &jobsList = queue.getSpontaneousJobsList();

    QVERIFY(queue.isEmpty());
    QCOMPARE(queue.sizeMetric(), 0);
    QVERIFY(jobsList.isEmpty());

    // we cannot use scoped pointers for the jobs, since they become owned by the queue
    KisSpontaneousJob *job1 = new KisNoopSpontaneousJob(false);
    queue.addSpontaneousJob(job1);

    QVERIFY(!queue.isEmpty());
    QCOMPARE(queue.sizeMetric(), 1);
    QCOMPARE(jobsList.size(), 1);
    QCOMPARE(jobsList[0], job1);

    KisSpontaneousJob *job2 = new KisNoopSpontaneousJob(false);
    queue.addSpontaneousJob(job2);

    QVERIFY(!queue.isEmpty());
    QCOMPARE(queue.sizeMetric(), 2);
    QCOMPARE(jobsList.size(), 2);
    QCOMPARE(jobsList[0], job1);
    QCOMPARE(jobsList[1], job2);

    KisSpontaneousJob *job3 = new KisNoopSpontaneousJob(true);
    queue.addSpontaneousJob(job3);

    QVERIFY(!queue.isEmpty());
    QCOMPARE(queue.sizeMetric(), 1);
    QCOMPARE(jobsList.size(), 1);
    QCOMPARE(jobsList[0], job3);
}

KISTEST_MAIN(KisSimpleUpdateQueueTest)

