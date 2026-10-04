/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <simpletest.h>

#include <QBuffer>
#include <QElapsedTimer>
#include <QObject>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>

#include <KoColorModelStandardIds.h>
#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOpRegistry.h>

#include <KisGpuCommandList.h>
#include <KisGpuContext.h>
#include <KisGpuLayerCompositor.h>
#include <KisGpuLayerStackCompositor.h>
#include <KisGpuTileFill.h>

#include "gpu/KisGpuTileAccess.h"
#include "gpu/KisGpuTileBackend.h"
#include "kis_datamanager.h"
#include "kis_paint_device.h"
#include "kis_paint_device_writer.h"
#include "kis_painter.h"
#include "kis_transaction.h"
#include "kis_types.h"
#include "tiles3/kis_tile.h"
#include "tiles3/kis_tile_data_pooler.h"
#include "tiles3/kis_tile_data_store_iterators.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#include <vector>

/**
 * GPU engine phase 1: GPU-resident tiles of KisPaintDevice
 * (docs/agent/gpu-engine.md).
 */
class KisGpuPaintDeviceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void testSupport();
    void testSharedUploadArena_data();
    void testSharedUploadArena();
    void testRetiredResourcesReleasedOutsideLock();
    void testTileAllocationBoundsResourceReclamation();
    void testGpuFillVisibleOnCpu();
    void testBulkReadback_data();
    void testBulkReadback();
    void testCompositeMatchesCpu();
    void testUndoRedo();
    void testCopyOnWriteIsolation();
    void testPoolerKeepsGpuTilesResident();
    void testCpuWriteAfterGpuWrite();
    void testSaveLoadRoundTrip();
    void testF16Device();
    void testSlotsReleased();
    void testSaveWithoutSync();
    void testLockedTileRejectsGpuWrite();
    void testDownloadFailureIsRetried();
    void testPersistentDownloadFailureIsReported();
    void testRetryAllocatesOnlyWhatItNeeds();
    void testNoSubmissionAfterFailure();
    void testFailedSubmissionKeepsContent();
    void testGpuValidCannotBePublishedAfterCpuWrite();
    void testOlderUploadDoesNotOverwriteNewer();
    void testSnapshotDuringCpuWriteIsNotPublished();
    void testDeferredSlotRelease();
    void testMemoryBudgetEviction();
    void testBatchedEviction_data();
    void testBatchedEviction();
    void testMixedEvictionBatch();
    void testPreparedAccessPreventsEviction();
    void testEvictionFailureKeepsContent();
    void testGpuTileDiskSwapRoundTrip();
    void testEvictionPreservesUndoRedo();
    void testCopySourcePreventsEviction();
    void testConcurrentEviction();
    void benchmarkTransfers();

private:
    bool gpuFill(KisPaintDeviceSP device, const QRect &rect, const QVector<float> &color);
    bool gpuCompositeOver(KisPaintDeviceSP dst, KisPaintDeviceSP src, const QRect &rect, float opacity);

    KisGpuTileBackend *m_backend = nullptr;
    std::unique_ptr<KisGpuCommandList> m_commands;
    std::unique_ptr<KisGpuTileFill> m_fill32;
    std::unique_ptr<KisGpuTileFill> m_fill16;
    std::unique_ptr<KisGpuLayerStackCompositor> m_compositor32;
};

namespace
{
const KoColorSpace *rgbaFloat(bool f16 = false)
{
    return KoColorSpaceRegistry::instance()->colorSpace(RGBAColorModelID.id(),
                                                        f16 ? Float16BitsColorDepthID.id()
                                                            : Float32BitsColorDepthID.id(),
                                                        QString());
}

void fillRandom(KisPaintDeviceSP device, const QRect &rect, quint32 seed)
{
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::vector<float> pixels(size_t(rect.width()) * rect.height() * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = unit(random);
        pixels[i + 1] = unit(random);
        pixels[i + 2] = unit(random);
        const float a = unit(random);
        pixels[i + 3] = a < 0.2f ? 0.0f : (a < 0.4f ? 1.0f : unit(random));
    }
    device->writeBytes(reinterpret_cast<const quint8 *>(pixels.data()), rect);
}

std::vector<float> readPixels(KisPaintDeviceSP device, const QRect &rect)
{
    std::vector<float> pixels(size_t(rect.width()) * rect.height() * 4);
    device->readBytes(reinterpret_cast<quint8 *>(pixels.data()), rect);
    return pixels;
}

/// Largest channel difference; the color of fully transparent pixels is ignored.
float maxDifference(const std::vector<float> &a, const std::vector<float> &b)
{
    float worst = 0.0f;
    for (size_t pixel = 0; pixel < a.size() / 4; pixel++) {
        const bool transparent = a[pixel * 4 + 3] == 0.0f && b[pixel * 4 + 3] == 0.0f;
        for (size_t c = transparent ? 3 : 0; c < 4; c++) {
            const float difference = std::abs(a[pixel * 4 + c] - b[pixel * 4 + c]);
            worst = std::isnan(difference) ? INFINITY : qMax(worst, difference);
        }
    }
    return worst;
}

class BufferWriter : public KisPaintDeviceWriter
{
public:
    explicit BufferWriter(QBuffer *buffer)
        : m_buffer(buffer)
    {
    }
    bool write(const QByteArray &data) override
    {
        return m_buffer->write(data) == data.size();
    }
    bool write(const char *data, qint64 length) override
    {
        return m_buffer->write(data, length) == length;
    }

private:
    QBuffer *m_buffer;
};
} // namespace

#define REQUIRE_GPU()                                                                                                  \
    if (!m_backend) {                                                                                                  \
        QSKIP(qPrintable(QStringLiteral("GPU engine unavailable: %1").arg(KisGpuTileBackend::unavailableReason())));   \
    }

void KisGpuPaintDeviceTest::initTestCase()
{
    m_backend = KisGpuTileBackend::instance();
    if (!m_backend) {
        return;
    }
    QString error;
    m_commands.reset(new KisGpuCommandList(m_backend->context()));
    m_fill32 = KisGpuTileFill::create(m_backend->context(), KisGpuTileFormat::RGBA32F, &error);
    m_fill16 = KisGpuTileFill::create(m_backend->context(), KisGpuTileFormat::RGBA16F, &error);
    m_compositor32 = KisGpuLayerStackCompositor::create(m_backend->context(), KisGpuTileFormat::RGBA32F, &error);
    QVERIFY2(m_fill32 && m_fill16 && m_compositor32, qPrintable(error));
}

void KisGpuPaintDeviceTest::cleanupTestCase()
{
    if (m_backend) {
        m_backend->flush();
        QCOMPARE(m_backend->context().validationErrorCount(), 0);
    }
    m_compositor32.reset();
    m_fill16.reset();
    m_fill32.reset();
    m_commands.reset();
}

void KisGpuPaintDeviceTest::init()
{
    // Failure injection must not leak from one test into the next.
    if (m_backend) {
        m_backend->injectDownloadFailuresForTesting(0);
        m_backend->injectReadbackLimitForTesting(~quint64(0));
        m_backend->resetFailureForTesting();
        m_backend->context().injectSubmitFailuresForTesting(0);
    }
}

bool KisGpuPaintDeviceTest::gpuFill(KisPaintDeviceSP device, const QRect &rect, const QVector<float> &color)
{
    KisGpuTileFill *fill = device->pixelSize() == 8 ? m_fill16.get() : m_fill32.get();
    KisGpuTileAccess access(device, rect, KisGpuTileAccess::WriteOnly);
    m_commands->begin();
    QString error;
    if (!access.prepare(*m_commands, &error)
        || !fill->record(*m_commands, access.addresses(), color.constData(), &error)) {
        qWarning() << error;
        return false;
    }
    const quint64 value = KisGpuTileAccess::submitAndFinish(*m_commands, {&access});
    device->setDirty(rect);
    return value && m_commands->wait();
}

bool KisGpuPaintDeviceTest::gpuCompositeOver(KisPaintDeviceSP dst,
                                             KisPaintDeviceSP src,
                                             const QRect &rect,
                                             float opacity)
{
    KisGpuTileAccess srcAccess(src, rect, KisGpuTileAccess::ReadOnly);
    KisGpuTileAccess dstAccess(dst, rect, KisGpuTileAccess::ReadWrite);
    if (srcAccess.tileGrid() != dstAccess.tileGrid()) {
        qWarning() << "devices are not tile-aligned with each other";
        return false;
    }

    QString error;
    m_commands->begin();
    if (!srcAccess.prepare(*m_commands, &error) || !dstAccess.prepare(*m_commands, &error)) {
        qWarning() << error;
        return false;
    }

    // Stack: the destination as the bottom layer, the source over it.
    const QVector<VkDeviceAddress> dstTiles = dstAccess.addresses();
    const QVector<VkDeviceAddress> layerTiles = dstTiles + srcAccess.addresses();
    if (!m_compositor32->record(*m_commands, layerTiles, dstTiles, {1.0f, opacity}, &error)) {
        qWarning() << error;
        return false;
    }

    const quint64 value = KisGpuTileAccess::submitAndFinish(*m_commands, {&srcAccess, &dstAccess});
    dst->setDirty(rect);
    return value && m_commands->wait();
}

void KisGpuPaintDeviceTest::testRetiredResourcesReleasedOutsideLock()
{
    REQUIRE_GPU();
    m_backend->flush();
    QSemaphore entered, release, published;
    m_backend->retireAfter(0, std::shared_ptr<int>(new int, [&](int *p) {
                               entered.release();
                               release.acquire();
                               delete p;
                           }));
    std::thread collector([&] {
        m_backend->collectGarbage();
    });
    const bool destroying = entered.tryAcquire(1, 5000);
    std::thread publisher([&] {
        m_backend->retireAfter(0, std::make_shared<int>(42));
        published.release();
    });
    const bool unblocked = published.tryAcquire(1, 5000);
    // Always release/join before assertions so an old implementation fails
    // cleanly instead of leaving a blocked worker behind.
    release.release();
    publisher.join();
    collector.join();
    m_backend->flush();
    QVERIFY(destroying);
    QVERIFY2(unblocked, "retiring another upload must not wait for a slow resource destructor");
}

void KisGpuPaintDeviceTest::testTileAllocationBoundsResourceReclamation()
{
    REQUIRE_GPU();
    m_backend->flush();
    std::atomic<int> destroyed{0};
    for (int i = 0; i < 40; ++i) {
        m_backend->retireAfter(0, std::shared_ptr<int>(new int, [&](int *p) {
                                   ++destroyed;
                                   delete p;
                               }));
    }
    const QRect rect(0, 0, 64, 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    fillRandom(device, rect, 937);
    KisGpuTileAccess access(device, rect, KisGpuTileAccess::ReadOnly);
    m_commands->begin();
    const bool prepared = access.prepare(*m_commands);
    const int reclaimed = destroyed.load();
    KisGpuTileAccess::finishUnsubmitted(*m_commands, {&access});
    m_backend->flush();
    QVERIFY(prepared);
    QCOMPARE(reclaimed, 16);
    QCOMPARE(destroyed.load(), 40);
}

void KisGpuPaintDeviceTest::testSharedUploadArena_data()
{
    QTest::addColumn<bool>("f16");
    QTest::addColumn<bool>("failSubmit");
    QTest::addColumn<bool>("oversized");
    for (bool f16 : {false, true})
        for (bool fail : {false, true})
            for (bool oversized : {false, true})
                QTest::newRow(qPrintable(QString("f16%1-fail%2-oversized%3").arg(f16).arg(fail).arg(oversized)))
                    << f16 << fail << oversized;
}

void KisGpuPaintDeviceTest::testSharedUploadArena()
{
    REQUIRE_GPU();
    QFETCH(bool, f16);
    QFETCH(bool, failSubmit);
    QFETCH(bool, oversized);
    const auto *cs = rgbaFloat(f16);
    QVERIFY(cs);
    const QRect sourceRect(0, 0, 256, 128);
    const QRect bounds(0, 0, 256, 5 * 128);
    const VkDeviceSize bytes = VkDeviceSize(sourceRect.width()) * sourceRect.height() * cs->pixelSize();
    KisPaintDeviceSP output = new KisPaintDevice(cs), expected = new KisPaintDevice(cs);
    KoColor original(cs);
    cs->fromNormalisedChannelsValue(original.data(), {0.25f, 0.5f, 0.75f, 1.0f});
    output->fill(bounds, original);
    expected->fill(bounds, original);
    KisGpuTileAccess target(output, bounds, KisGpuTileAccess::WriteOnly);
    std::vector<std::unique_ptr<KisGpuTileAccess>> sources;
    QVector<KisGpuTileAccess *> accesses{&target};
    QVector<VkDeviceAddress> addresses;
    QString error;
    m_commands->begin();
    QVERIFY(target.prepare(*m_commands, &error));
    {
        KisGpuTileAccess::UploadArena arena(oversized ? bytes / 2 : 2 * bytes);
        for (int i = 0; i < 5; ++i) {
            KisPaintDeviceSP source = new KisPaintDevice(cs);
            KoColor color(cs);
            cs->fromNormalisedChannelsValue(color.data(), {i / 8.0f, (5 - i) / 8.0f, 0.125f, 1.0f});
            // writeBytes ensures independent tile data (fill may share tiles),
            // making the upload size and chunk-boundary assertions deterministic.
            QByteArray pixels(int(bytes), Qt::Uninitialized);
            for (int p = 0; p < sourceRect.width() * sourceRect.height(); ++p)
                std::memcpy(pixels.data() + p * cs->pixelSize(), color.data(), cs->pixelSize());
            source->writeBytes(reinterpret_cast<const quint8 *>(pixels.constData()), sourceRect);
            auto access = std::make_unique<KisGpuTileAccess>(source, sourceRect, KisGpuTileAccess::ReadOnly);
            QVERIFY2(access->prepare(*m_commands, arena, &error), qPrintable(error));
            addresses += access->addresses();
            accesses << access.get();
            sources.push_back(std::move(access));
            if (!failSubmit)
                expected->fill(sourceRect.translated(0, i * 128), color);
        }
        QCOMPARE(arena.allocationCount(), oversized ? 5 : 3);
        QCOMPARE(arena.reservedBytes(), bytes * 5);
    } // The arena dies before submission; each access must retain its storage.
    auto compositor = KisGpuLayerCompositor::create(m_backend->context(),
                                                    f16 ? KisGpuTileFormat::RGBA16F : KisGpuTileFormat::RGBA32F,
                                                    &error);
    QVERIFY2(compositor, qPrintable(error));
    QVERIFY2(compositor->record(*m_commands,
                                addresses,
                                target.addresses(),
                                {{KisGpuBlendOp::Over, 1.0f, false, 0xf}},
                                4,
                                bounds,
                                &error),
             qPrintable(error));
    if (failSubmit)
        m_backend->context().injectSubmitFailuresForTesting(1);
    const auto value = KisGpuTileAccess::submitAndFinish(*m_commands, accesses);
    QCOMPARE(value == 0, failSubmit);
    // Accesses also die before completion; deferred retirement owns all chunks.
    sources.clear();
    if (value)
        QVERIFY(m_commands->wait());
    QByteArray actual(bounds.width() * bounds.height() * cs->pixelSize(), Qt::Uninitialized);
    QByteArray reference(actual.size(), Qt::Uninitialized);
    output->readBytes(reinterpret_cast<quint8 *>(actual.data()), bounds);
    expected->readBytes(reinterpret_cast<quint8 *>(reference.data()), bounds);
    QCOMPARE(actual, reference);
}

void KisGpuPaintDeviceTest::testBulkReadback_data()
{
    QTest::addColumn<bool>("f16");
    QTest::addColumn<bool>("planar");
    QTest::addColumn<bool>("retry");
    for (bool f16 : {false, true}) {
        for (bool planar : {false, true}) {
            for (bool retry : {false, true}) {
                const QByteArray name =
                    QByteArray(f16 ? "F16" : "F32") + (planar ? "-planar" : "-strided") + (retry ? "-retry" : "");
                QTest::newRow(name.constData()) << f16 << planar << retry;
            }
        }
    }
}

void KisGpuPaintDeviceTest::testBulkReadback()
{
    REQUIRE_GPU();
    QFETCH(bool, f16);
    QFETCH(bool, planar);
    QFETCH(bool, retry);
    const KoColorSpace *cs = rgbaFloat(f16);
    KisPaintDeviceSP device = new KisPaintDevice(cs);
    KisPaintDeviceSP expected = new KisPaintDevice(cs);
    const QPoint offset(7, -11);
    device->moveTo(offset);
    expected->moveTo(offset);
    // 289 + 1 stale tiles cross the 256-tile batch boundary, with sparse holes,
    // negative coordinates, a device offset and an unaligned read rectangle.
    const QRect red = QRect(-128, -128, 17 * 64, 17 * 64).translated(offset);
    const QRect blue = QRect(18 * 64, 18 * 64, 64, 64).translated(offset);
    QVERIFY(gpuFill(device, red, {1, 0, 0, 1}));
    QVERIFY(gpuFill(device, blue, {0, 0, 1, 1}));
    KoColor redPixel(cs);
    KoColor bluePixel(cs);
    cs->fromNormalisedChannelsValue(redPixel.data(), {1, 0, 0, 1});
    cs->fromNormalisedChannelsValue(bluePixel.data(), {0, 0, 1, 1});
    expected->fill(red, redPixel);
    expected->fill(blue, bluePixel);
    const QRect rect = red.united(blue).adjusted(-3, -5, 9, 7);
    const QRect localRect = rect.translated(-offset);
    const QRect extent = device->extent();
    const int pixelSize = device->pixelSize();
    QByteArray reference(rect.width() * rect.height() * pixelSize, Qt::Uninitialized);
    expected->readBytes(reinterpret_cast<quint8 *>(reference.data()), rect);

    const quint64 before = m_backend->context().completedValue();
    device->dataManager()->readBytes(nullptr, -128, -128, 512, 512);
    quint8 unused = 0;
    device->dataManager()->readBytes(&unused, 0, 0, 0, 0);
    QCOMPARE(m_backend->context().completedValue(), before);
    if (retry) {
        m_backend->injectDownloadFailuresForTesting(1);
    }
    if (planar) {
        // The planar API takes data-manager coordinates (unlike readBytes).
        const QVector<quint8 *> planes =
            device->readPlanarBytes(localRect.x(), localRect.y(), rect.width(), rect.height());
        const auto cleanup = qScopeGuard([&]() {
            for (quint8 *plane : planes)
                delete[] plane;
        });
        QCOMPARE(planes.size(), 4);
        const int channelSize = pixelSize / 4;
        for (int i = 0; i < rect.width() * rect.height(); ++i) {
            for (int c = 0; c < 4; ++c) {
                QVERIFY(std::memcmp(planes[c] + i * channelSize,
                                    reference.constData() + i * pixelSize + c * channelSize,
                                    channelSize)
                        == 0);
            }
        }
    } else {
        const int rowBytes = rect.width() * pixelSize;
        const int stride = rowBytes + 37;
        QByteArray actual(stride * rect.height(), char(0x5a));
        device->dataManager()->readBytes(reinterpret_cast<quint8 *>(actual.data()),
                                         localRect.x(),
                                         localRect.y(),
                                         rect.width(),
                                         rect.height(),
                                         stride);
        for (int y = 0; y < rect.height(); ++y) {
            QCOMPARE(actual.mid(y * stride, rowBytes), reference.mid(y * rowBytes, rowBytes));
            QCOMPARE(actual.mid(y * stride + rowBytes, 37), QByteArray(37, char(0x5a)));
        }
    }
    // A failed batch retries its 256 tiles individually; otherwise two submissions.
    QCOMPARE(m_backend->context().completedValue() - before, quint64(retry ? 257 : 2));
    QCOMPARE(device->extent(), extent);
    QByteArray again(reference.size(), Qt::Uninitialized);
    const quint64 after = m_backend->context().completedValue();
    device->readBytes(reinterpret_cast<quint8 *>(again.data()), rect);
    QCOMPARE(again, reference);
    QCOMPARE(m_backend->context().completedValue(), after);
}

void KisGpuPaintDeviceTest::testSupport()
{
    KisPaintDeviceSP rgba8 = new KisPaintDevice(KoColorSpaceRegistry::instance()->rgb8());
    QString reason;
    QVERIFY(!KisGpuTileAccess::isSupported(rgba8, &reason));
    QVERIFY(reason.contains(QStringLiteral("not RGBA F32/F16")));

    REQUIRE_GPU();
    QVERIFY(KisGpuTileAccess::isSupported(new KisPaintDevice(rgbaFloat()), &reason));
    QVERIFY(KisGpuTileAccess::isSupported(new KisPaintDevice(rgbaFloat(true)), &reason));
}

void KisGpuPaintDeviceTest::testGpuFillVisibleOnCpu()
{
    REQUIRE_GPU();
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    const QRect content(0, 0, 256, 256);
    fillRandom(device, content, 1);
    const std::vector<float> before = readPixels(device, content);

    // Tiles (1,1)..(2,2), plus a tile outside the old extent.
    const QRect filled(64, 64, 128, 128);
    const QRect outside(320, 0, 64, 64);
    QVERIFY(gpuFill(device, filled, {0.25f, 0.5f, 0.75f, 1.0f}));
    QVERIFY(gpuFill(device, outside, {1.0f, 0.0f, 0.0f, 0.5f}));

    const std::vector<float> after = readPixels(device, content);
    for (int y = 0; y < content.height(); y++) {
        for (int x = 0; x < content.width(); x++) {
            const size_t i = (size_t(y) * content.width() + x) * 4;
            if (filled.contains(x, y)) {
                QCOMPARE(after[i + 0], 0.25f);
                QCOMPARE(after[i + 1], 0.5f);
                QCOMPARE(after[i + 2], 0.75f);
                QCOMPARE(after[i + 3], 1.0f);
            } else {
                for (int c = 0; c < 4; c++) {
                    QCOMPARE(after[i + c], before[i + c]);
                }
            }
        }
    }

    const std::vector<float> outsidePixels = readPixels(device, outside);
    QCOMPARE(outsidePixels[0], 1.0f);
    QCOMPARE(outsidePixels[3], 0.5f);
    QCOMPARE(device->extent(), QRect(0, 0, 384, 256));
}

void KisGpuPaintDeviceTest::testCompositeMatchesCpu()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 320, 192);
    KisPaintDeviceSP src = new KisPaintDevice(rgbaFloat());
    KisPaintDeviceSP dst = new KisPaintDevice(rgbaFloat());
    fillRandom(src, rect, 2);
    fillRandom(dst, rect, 3);

    KisPaintDeviceSP expected = new KisPaintDevice(*dst);
    {
        KisPainter painter(expected);
        painter.setCompositeOpId(COMPOSITE_OVER);
        painter.setOpacityF(0.7);
        painter.bitBlt(rect.topLeft(), src, rect);
    }

    QVERIFY(gpuCompositeOver(dst, src, rect, 0.7f));
    const float difference = maxDifference(readPixels(expected, rect), readPixels(dst, rect));
    QVERIFY2(difference <= 1e-5f, qPrintable(QString::number(difference)));
}

void KisGpuPaintDeviceTest::testUndoRedo()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 192, 128);
    KisPaintDeviceSP src = new KisPaintDevice(rgbaFloat());
    KisPaintDeviceSP dst = new KisPaintDevice(rgbaFloat());
    fillRandom(src, rect, 4);
    fillRandom(dst, rect, 5);
    const std::vector<float> before = readPixels(dst, rect);

    KisTransaction transaction(dst);
    QVERIFY(gpuCompositeOver(dst, src, rect, 1.0f));
    QScopedPointer<KUndo2Command> command(transaction.endAndTake());
    command->redo(); // the undo stack's initial redo() is a no-op
    const std::vector<float> after = readPixels(dst, rect);
    QVERIFY(maxDifference(before, after) > 0.0f);

    command->undo();
    QCOMPARE(maxDifference(before, readPixels(dst, rect)), 0.0f);

    command->redo();
    QCOMPARE(maxDifference(after, readPixels(dst, rect)), 0.0f);

    // A second GPU write on top of the redone state, then undo it again.
    KisTransaction second(dst);
    QVERIFY(gpuFill(dst, rect, {0.0f, 1.0f, 0.0f, 1.0f}));
    QScopedPointer<KUndo2Command> secondCommand(second.endAndTake());
    secondCommand->redo();
    secondCommand->undo();
    QCOMPARE(maxDifference(after, readPixels(dst, rect)), 0.0f);
}

void KisGpuPaintDeviceTest::testPoolerKeepsGpuTilesResident()
{
    REQUIRE_GPU();
    auto *store = KisTileDataStore::instance();
    store->testingSuspendPooler();
    const auto resume = qScopeGuard([&]() {
        store->testingResumePooler();
    });
    struct TestPooler : KisTileDataPooler {
        using KisTileDataPooler::KisTileDataPooler;
        using KisTileDataPooler::processLists;
    } pooler(store, 65536);
    const QRect rect(0, 0, 512, 512);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuFill(device, rect, {0.25f, 0.5f, 0.75f, 1.0f}));
    KisPaintDeviceSP snapshot = new KisPaintDevice(*device);
    QList<KisTileData *> candidates, donors;
    QVector<KisTileSP> tiles;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            bool existing = false;
            auto tile = device->dataManager()->getReadOnlyTileLazy(x, y, existing);
            QVERIFY(existing);
            tiles << tile;
            candidates << tile->tileData();
        }
    auto &context = m_backend->context();
    const auto before = context.completedValue();
    qint32 occupied = 0;
    QElapsedTimer timer;
    timer.start();
    pooler.processLists(candidates, donors, occupied);
    qInfo() << "Speculative pooler, 64 shared GPU tiles: ms" << timer.nsecsElapsed() / 1e6 << "submissions"
            << context.completedValue() - before << "clone memory metric" << occupied;
    QCOMPARE(context.completedValue(), before);
    QCOMPARE(occupied, 0);
    for (const auto *td : candidates)
        QVERIFY(!td->gpuState()->cpuValid());
    // A real CPU consumer still obtains current pixels and correct COW.
    const auto oldPixels = readPixels(snapshot, rect);
    for (size_t i = 0; i < oldPixels.size(); i += 4) {
        QCOMPARE(oldPixels[i], 0.25f);
        QCOMPARE(oldPixels[i + 3], 1.0f);
    }
    // Once CPU pixels are current, the ordinary speculative clone path works.
    pooler.processLists(candidates, donors, occupied);
    QVERIFY(occupied > 0);
    const float red[] = {1, 0, 0, 1};
    device->fill(0, 0, 64, 64, reinterpret_cast<const quint8 *>(red));
    QCOMPARE(readPixels(snapshot, rect), oldPixels);
    QCOMPARE(readPixels(device, QRect(0, 0, 1, 1))[0], 1.0f);
}

void KisGpuPaintDeviceTest::testCopyOnWriteIsolation()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 128, 128);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    fillRandom(device, rect, 6);
    const std::vector<float> original = readPixels(device, rect);

    // CPU-valid shared tiles: copy-on-write clones on the CPU, then uploads.
    KisPaintDeviceSP cpuCopy = new KisPaintDevice(*device);
    QVERIFY(gpuFill(device, rect, {1.0f, 1.0f, 1.0f, 1.0f}));
    QCOMPARE(maxDifference(original, readPixels(cpuCopy, rect)), 0.0f);

    // GPU-only shared tiles: copy-on-write clones with a GPU slot copy.
    QVERIFY(gpuFill(device, rect, {0.5f, 0.5f, 0.5f, 1.0f})); // tiles are now GPU-valid, CPU-stale
    KisPaintDeviceSP shared = new KisPaintDevice(*device);
    KisPaintDeviceSP overlay = new KisPaintDevice(rgbaFloat());
    fillRandom(overlay, rect, 7);
    QVERIFY(gpuCompositeOver(device, overlay, rect, 1.0f));

    const std::vector<float> gray = [&]() {
        std::vector<float> pixels(size_t(rect.width()) * rect.height() * 4, 0.5f);
        for (size_t i = 3; i < pixels.size(); i += 4) {
            pixels[i] = 1.0f;
        }
        return pixels;
    }();
    QCOMPARE(maxDifference(gray, readPixels(shared, rect)), 0.0f);
    QVERIFY(maxDifference(gray, readPixels(device, rect)) > 0.0f);
}

void KisGpuPaintDeviceTest::testCpuWriteAfterGpuWrite()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 128, 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuFill(device, rect, {0.0f, 0.0f, 1.0f, 1.0f}));

    // CPU write: downloads the tile first, then makes the GPU copy stale.
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    device->setPixel(10, 10, KoColor(reinterpret_cast<const quint8 *>(red), device->colorSpace()));

    // A GPU read must see the CPU edit (re-upload), and the rest of the GPU fill.
    KisPaintDeviceSP copy = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuCompositeOver(copy, device, rect, 1.0f));
    const std::vector<float> pixels = readPixels(copy, rect);
    const size_t edited = (size_t(10) * rect.width() + 10) * 4;
    QCOMPARE(pixels[edited + 0], 1.0f);
    QCOMPARE(pixels[edited + 2], 0.0f);
    QCOMPARE(pixels[0 + 2], 1.0f);
    QCOMPARE(pixels[(size_t(5) * rect.width() + 100) * 4 + 2], 1.0f);
}

void KisGpuPaintDeviceTest::testSaveLoadRoundTrip()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 256, 192);
    KisPaintDeviceSP src = new KisPaintDevice(rgbaFloat());
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    fillRandom(src, rect, 8);
    fillRandom(device, rect, 9);
    QVERIFY(gpuCompositeOver(device, src, rect, 0.5f));

    KisGpuTileAccess::syncToCpu(device, rect);

    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    BufferWriter writer(&buffer);
    QVERIFY(device->write(writer));

    buffer.seek(0);
    KisPaintDeviceSP loaded = new KisPaintDevice(rgbaFloat());
    QVERIFY(loaded->read(&buffer));
    QCOMPARE(maxDifference(readPixels(device, rect), readPixels(loaded, rect)), 0.0f);
}

void KisGpuPaintDeviceTest::testF16Device()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 128, 128);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat(true));
    QVERIFY(gpuFill(device, rect, {0.25f, 0.5f, 0.75f, 1.0f}));

    KoColor color;
    device->pixel(70, 70, &color);
    QVector<float> channels(4);
    color.colorSpace()->normalisedChannelsValue(color.data(), channels);
    QCOMPARE(channels[0], 0.25f);
    QCOMPARE(channels[1], 0.5f);
    QCOMPARE(channels[2], 0.75f);
    QCOMPARE(channels[3], 1.0f);
}

void KisGpuPaintDeviceTest::testSlotsReleased()
{
    REQUIRE_GPU();
    m_backend->flush();
    const quint32 baseline = m_backend->allocatedSlots(16);

    {
        const QRect rect(0, 0, 512, 512);
        KisPaintDeviceSP src = new KisPaintDevice(rgbaFloat());
        KisPaintDeviceSP dst = new KisPaintDevice(rgbaFloat());
        fillRandom(src, rect, 10);
        fillRandom(dst, rect, 11);

        KisTransaction transaction(dst);
        QVERIFY(gpuCompositeOver(dst, src, rect, 1.0f));
        QScopedPointer<KUndo2Command> command(transaction.endAndTake());
        command->redo();
        QVERIFY(m_backend->allocatedSlots(16) > baseline);
        command->undo();
        command->redo();
    }

    m_backend->flush();
    QCOMPARE(m_backend->allocatedSlots(16), baseline);
}

void KisGpuPaintDeviceTest::testSaveWithoutSync()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 192, 128);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    fillRandom(device, rect, 20);
    QVERIFY(gpuFill(device, QRect(64, 0, 64, 64), {0.9f, 0.1f, 0.2f, 0.7f}));

    // The tiles written by the GPU are stale on the CPU: writing the device
    // (the .kra tile compressor) must download them through the tile locks.
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    BufferWriter writer(&buffer);
    QVERIFY(device->write(writer));

    buffer.seek(0);
    KisPaintDeviceSP loaded = new KisPaintDevice(rgbaFloat());
    QVERIFY(loaded->read(&buffer));
    const std::vector<float> pixels = readPixels(loaded, QRect(64, 0, 64, 64));
    QCOMPARE(pixels[0], 0.9f);
    QCOMPARE(pixels[3], 0.7f);
    QCOMPARE(maxDifference(readPixels(device, rect), readPixels(loaded, rect)), 0.0f);
}

void KisGpuPaintDeviceTest::testLockedTileRejectsGpuWrite()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 64, 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    fillRandom(device, rect, 21);
    const std::vector<float> before = readPixels(device, rect);

    // Exclusive (not shared) tile data, held by a CPU reader.
    KisTileSP tile = device->dataManager()->getTile(0, 0, true);
    QVERIFY(tile->tileData()->numUsers() == 1);
    tile->lockForRead();

    KisGpuTileAccess access(device, rect, KisGpuTileAccess::WriteOnly);
    m_commands->begin();
    QString error;
    QVERIFY(!access.prepare(*m_commands, &error));
    QVERIFY(error.contains(QStringLiteral("locked")));
    KisGpuTileAccess::submitAndFinish(*m_commands, {&access});
    m_commands->wait();
    tile->unlockForRead();

    QCOMPARE(maxDifference(before, readPixels(device, rect)), 0.0f);
}

void KisGpuPaintDeviceTest::testDownloadFailureIsRetried()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 128, 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuFill(device, rect, {0.25f, 0.5f, 0.75f, 1.0f}));

    const quint64 lostBefore = m_backend->contentLossCount();
    m_backend->injectDownloadFailuresForTesting(1);
    const std::vector<float> pixels = readPixels(device, rect);
    m_backend->injectDownloadFailuresForTesting(0);

    QCOMPARE(pixels[0], 0.25f);
    QCOMPARE(pixels[pixels.size() - 2], 0.75f);
    QCOMPARE(m_backend->contentLossCount(), lostBefore);
    QVERIFY(!m_backend->hasFailed());
}

void KisGpuPaintDeviceTest::testPersistentDownloadFailureIsReported()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 128, 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    fillRandom(device, rect, 22);
    const std::vector<float> beforeGpuWork = readPixels(device, rect);

    // Tile (0,0): written in place. Tile (1,0): written through a GPU
    // copy-on-write clone (its source is GPU-only and shared with a copy).
    QVERIFY(gpuFill(device, QRect(64, 0, 64, 64), {0.0f, 0.0f, 1.0f, 1.0f}));
    KisPaintDeviceSP sharedCopy = new KisPaintDevice(*device);
    QVERIFY(gpuFill(device, rect, {1.0f, 0.0f, 0.0f, 1.0f}));
    // The CPU buffers hold what the CPU last had: for tile (1,0) that is
    // older than the first (blue) GPU write too, because that content only
    // ever existed on the GPU.
    const std::vector<float> &lastCpuContent = beforeGpuWork;
    Q_UNUSED(sharedCopy);
    // Another device with GPU-only content, read after the engine failed.
    KisPaintDeviceSP second = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuFill(second, QRect(0, 0, 64, 64), {0.0f, 1.0f, 0.0f, 1.0f}));

    // Every read-back attempt fails: the latest content is lost.
    const quint64 lostBefore = m_backend->contentLossCount();
    std::atomic<int> listenerCalls{0};
    KisGpuTileBackend::setFailureListener([&listenerCalls]() {
        listenerCalls++;
    });
    m_backend->injectDownloadFailuresForTesting(1000);
    const std::vector<float> pixels = readPixels(device, rect);
    readPixels(second, QRect(0, 0, 64, 64)); // a second loss, already failed
    m_backend->injectDownloadFailuresForTesting(0);
    KisGpuTileBackend::setFailureListener({});

    // The loss is reported (to the UI once, KisGpuEngineUi) and the engine
    // stops taking GPU work...
    QCOMPARE(listenerCalls.load(), 1);
    QCOMPARE(m_backend->contentLossCount(), lostBefore + 3);
    QVERIFY(m_backend->hasFailed());
    QVERIFY(!KisGpuTileAccess::isSupported(device));

    // ...and the tiles show the content the CPU last had: never zeros or
    // other meaningless data (also for the GPU copy-on-write clone).
    QCOMPARE(maxDifference(lastCpuContent, pixels), 0.0f);
    for (int col = 0; col < 2; col++) {
        KisTileGpuState *state = device->dataManager()->getTile(col, 0, false)->tileData()->gpuState();
        QVERIFY(state && state->cpuValid() && !state->gpuValid() && state->contentLost());
    }
}

void KisGpuPaintDeviceTest::testNoSubmissionAfterFailure()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 128, 64);

    // A GPU write prepared while the engine still works...
    KisPaintDeviceSP target = new KisPaintDevice(rgbaFloat());
    fillRandom(target, rect, 25);
    const std::vector<float> targetBefore = readPixels(target, rect);
    KisPaintDeviceSP shared = new KisPaintDevice(*target); // forces copy-on-write
    KisTransaction transaction(target);
    KisGpuTileAccess writer(target, rect, KisGpuTileAccess::WriteOnly);
    m_commands->begin();
    QVERIFY(writer.prepare(*m_commands));
    const float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    QVERIFY(m_fill32->record(*m_commands, writer.addresses(), blue));

    // ...then another thread loses GPU content and the engine stops.
    {
        KisGpuCommandList commands(m_backend->context());
        KisPaintDeviceSP other = new KisPaintDevice(rgbaFloat());
        KisGpuTileAccess access(other, QRect(0, 0, 64, 64), KisGpuTileAccess::WriteOnly);
        commands.begin();
        QVERIFY(access.prepare(commands));
        const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        QVERIFY(m_fill32->record(commands, access.addresses(), red));
        QVERIFY(KisGpuTileAccess::submitAndFinish(commands, {&access}));
        commands.wait();
        m_backend->injectDownloadFailuresForTesting(1000);
        readPixels(other, QRect(0, 0, 64, 64));
        m_backend->injectDownloadFailuresForTesting(0);
    }
    QVERIFY(m_backend->hasFailed());

    // The prepared write is not submitted and leaves the device unchanged.
    QCOMPARE(KisGpuTileAccess::submitAndFinish(*m_commands, {&writer}), quint64(0));
    QScopedPointer<KUndo2Command> command(transaction.endAndTake());
    QCOMPARE(maxDifference(targetBefore, readPixels(target, rect)), 0.0f);
    QCOMPARE(maxDifference(targetBefore, readPixels(shared, rect)), 0.0f);

    // New GPU work is refused at prepare() already.
    KisGpuTileAccess late(target, rect, KisGpuTileAccess::WriteOnly);
    m_commands->begin();
    QString error;
    QVERIFY(!late.prepare(*m_commands, &error));
    QVERIFY(error.contains(QStringLiteral("stopped")));
    QCOMPARE(KisGpuTileAccess::submitAndFinish(*m_commands, {&late}), quint64(0));
    QCOMPARE(maxDifference(targetBefore, readPixels(target, rect)), 0.0f);
}

void KisGpuPaintDeviceTest::testFailedSubmissionKeepsContent()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 128, 64);
    KisPaintDeviceSP target = new KisPaintDevice(rgbaFloat());
    fillRandom(target, rect, 26);
    const std::vector<float> targetBefore = readPixels(target, rect);
    KisPaintDeviceSP shared = new KisPaintDevice(*target); // forces copy-on-write

    KisTransaction transaction(target);
    KisGpuTileAccess writer(target, rect, KisGpuTileAccess::WriteOnly);
    m_commands->begin();
    QVERIFY(writer.prepare(*m_commands));
    const float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    QVERIFY(m_fill32->record(*m_commands, writer.addresses(), blue));

    // submit() is attempted and fails: the recording was already ended by
    // submit(), and must not be ended a second time (validation would
    // report it in cleanupTestCase()).
    m_backend->context().injectSubmitFailuresForTesting(1);
    QCOMPARE(KisGpuTileAccess::submitAndFinish(*m_commands, {&writer}), quint64(0));
    QVERIFY(!m_commands->isRecording());
    QScopedPointer<KUndo2Command> command(transaction.endAndTake());

    QCOMPARE(maxDifference(targetBefore, readPixels(target, rect)), 0.0f);
    QCOMPARE(maxDifference(targetBefore, readPixels(shared, rect)), 0.0f);
    QVERIFY(!m_backend->hasFailed());

    // The command list is usable again.
    QVERIFY(gpuFill(target, rect, {0.5f, 0.5f, 0.5f, 1.0f}));
    QCOMPARE(readPixels(target, rect)[0], 0.5f);
}

void KisGpuPaintDeviceTest::testRetryAllocatesOnlyWhatItNeeds()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 256, 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuFill(device, rect, {0.1f, 0.2f, 0.3f, 1.0f}));

    // A batch buffer for the four tiles cannot be allocated, a one-tile
    // buffer can: the tile-by-tile retry must succeed.
    const quint64 lostBefore = m_backend->contentLossCount();
    m_backend->injectReadbackLimitForTesting(64 * 64 * 16);
    KisGpuTileAccess::syncToCpu(device, rect);
    m_backend->injectReadbackLimitForTesting(~quint64(0));

    QCOMPARE(m_backend->contentLossCount(), lostBefore);
    QVERIFY(!m_backend->hasFailed());
    const std::vector<float> pixels = readPixels(device, rect);
    QCOMPARE(pixels[0], 0.1f);
    QCOMPARE(pixels[pixels.size() - 2], 0.3f);
}

void KisGpuPaintDeviceTest::testGpuValidCannotBePublishedAfterCpuWrite()
{
    // Sequential: the interleaving that was possible before (check the
    // generation, CPU write, then publish) now fails the publication.
    {
        KisTileGpuState state;
        state.setValid(KisTileGpuState::CpuValid);
        const int generation = state.generation();
        state.notifyCpuWrite();
        QVERIFY(!state.markGpuValidIfGeneration(generation));
        QVERIFY(!state.gpuValid());
        QVERIFY(state.cpuValid());
        QVERIFY(state.markGpuValidIfGeneration(state.generation()));
        QVERIFY(state.gpuValid());
        state.notifyCpuWrite();
        QVERIFY(!state.gpuValid());
    }

    // Flags and generation share one atomic word and are updated by
    // compare-and-swap, so the check and the publication cannot be split by
    // a concurrent CPU write (KisTileGpuState).
}

namespace
{
std::vector<float> solidPixels(const QRect &rect, const float color[4])
{
    std::vector<float> pixels(size_t(rect.width()) * rect.height() * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        std::copy(color, color + 4, pixels.begin() + i);
    }
    return pixels;
}
} // namespace

void KisGpuPaintDeviceTest::testOlderUploadDoesNotOverwriteNewer()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 64, 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    fillRandom(device, rect, 23);
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};

    // Access A snapshots the current CPU content for an upload...
    KisGpuCommandList commandsA(m_backend->context());
    KisGpuTileAccess accessA(device, rect, KisGpuTileAccess::ReadOnly);
    commandsA.begin();
    QVERIFY(accessA.prepare(commandsA));

    // ...the CPU changes the tile in place, and access B uploads and
    // publishes the newer content.
    const std::vector<float> redPixels = solidPixels(rect, red);
    device->writeBytes(reinterpret_cast<const quint8 *>(redPixels.data()), rect);
    KisPaintDeviceSP copy = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuCompositeOver(copy, device, rect, 1.0f));

    // A still runs, but must not upload its older snapshot over B's content.
    QVERIFY(KisGpuTileAccess::submitAndFinish(commandsA, {&accessA}));
    commandsA.wait();

    KisTileGpuState *state = device->dataManager()->getTile(0, 0, false)->tileData()->gpuState();
    QVERIFY(state->gpuValid());
    KisPaintDeviceSP check = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuCompositeOver(check, device, rect, 1.0f));
    QCOMPARE(maxDifference(redPixels, readPixels(check, rect)), 0.0f);
}

void KisGpuPaintDeviceTest::testSnapshotDuringCpuWriteIsNotPublished()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 64, 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    fillRandom(device, rect, 27);
    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    const std::vector<float> greenPixels = solidPixels(rect, green);

    // A CPU writer holds the tile; half of its write is done when a GPU
    // reader takes its snapshot, the other half afterwards.
    KisTileSP tile = device->dataManager()->getTile(0, 0, true);
    tile->lockForWrite();
    const size_t half = size_t(64 * 32 * 16);
    std::memcpy(tile->data(), greenPixels.data(), half);

    KisGpuCommandList commands(m_backend->context());
    KisGpuTileAccess reader(device, rect, KisGpuTileAccess::ReadOnly);
    commands.begin();
    QVERIFY(reader.prepare(commands));

    std::memcpy(tile->data() + half, reinterpret_cast<const quint8 *>(greenPixels.data()) + half, half);
    tile->unlockForWrite();

    // The torn snapshot is used for this submission but never published as
    // the tile's GPU copy...
    QVERIFY(KisGpuTileAccess::submitAndFinish(commands, {&reader}));
    commands.wait();
    QVERIFY(!tile->tileData()->gpuState()->gpuValid());

    // ...so the next GPU reader uploads the complete content.
    KisPaintDeviceSP check = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuCompositeOver(check, device, rect, 1.0f));
    QCOMPARE(maxDifference(greenPixels, readPixels(check, rect)), 0.0f);
}

void KisGpuPaintDeviceTest::testDeferredSlotRelease()
{
    REQUIRE_GPU();
    m_backend->flush();
    const quint32 baseline = m_backend->allocatedSlots(16);
    quint64 submitted = 0;

    {
        const QRect rect(0, 0, 512, 512);
        KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
        KisGpuTileAccess access(device, rect, KisGpuTileAccess::WriteOnly);
        m_commands->begin();
        QVERIFY(access.prepare(*m_commands));
        const float color[4] = {0.5f, 0.5f, 0.5f, 1.0f};
        // Several fills keep the GPU busy after the device is gone.
        for (int i = 0; i < 50; i++) {
            m_fill32->record(*m_commands, access.addresses(), color);
            m_commands->computeBarrier();
        }
        submitted = KisGpuTileAccess::submitAndFinish(*m_commands, {&access});
        QVERIFY(submitted);
        // The device and its tiles die here, possibly before the GPU is done.
    }

    // Slots of in-flight work are not reused until the work has completed.
    m_backend->collectGarbage();
    const bool stillRunning = m_backend->context().completedValue() < submitted;
    const quint32 whileRunning = m_backend->allocatedSlots(16);
    if (stillRunning) {
        QVERIFY(whileRunning > baseline);
    } else {
        qInfo() << "GPU finished before the check; deferred release not observed this run";
    }
    m_commands->wait();
    m_backend->collectGarbage();
    QCOMPARE(m_backend->allocatedSlots(16), baseline);
}

void KisGpuPaintDeviceTest::testMemoryBudgetEviction()
{
    REQUIRE_GPU();
    m_backend->flush();
    m_backend->evictTiles(~quint64(0));
    const quint64 originalBudget = m_backend->memoryBudget();
    auto restore = qScopeGuard([&]() {
        m_backend->setMemoryBudgetForTesting(originalBudget);
    });
    const quint64 budget = 2 * m_backend->pool(16)->tileBytes();
    m_backend->setMemoryBudgetForTesting(budget);
    const QRect rect(0, 0, 64, 64);
    QVector<KisPaintDeviceSP> devices;
    for (int i = 0; i < 8; ++i) {
        KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
        devices << device;
        QVERIFY(gpuFill(device, rect, {float(i) / 8, 0.5f, 0.25f, 1}));
        QVERIFY(m_backend->reservedTileBytes() <= budget);
    }
    // Earlier GPU-only contents survived eviction; re-upload is also possible.
    for (int i = 0; i < devices.size(); ++i) {
        const auto pixels = readPixels(devices[i], rect);
        for (size_t p = 0; p < pixels.size(); p += 4) {
            QCOMPARE(pixels[p], float(i) / 8);
            QCOMPARE(pixels[p + 1], 0.5f);
            QCOMPARE(pixels[p + 3], 1.0f);
        }
    }
    QVERIFY(!m_backend->hasFailed());
}

void KisGpuPaintDeviceTest::testBatchedEviction_data()
{
    QTest::addColumn<bool>("f16");
    QTest::addColumn<int>("scenario");
    for (bool f16 : {false, true})
        for (int scenario = 0; scenario < 4; ++scenario)
            QTest::newRow(qPrintable(QString("f16%1-case%2").arg(f16).arg(scenario))) << f16 << scenario;
}

void KisGpuPaintDeviceTest::testBatchedEviction()
{
    REQUIRE_GPU();
    QFETCH(bool, f16);
    QFETCH(int, scenario);
    m_backend->flush();
    m_backend->evictTiles(~quint64(0));
    const QRect bounds(0, 0, 17 * 64, 17 * 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat(f16));
    QVERIFY(gpuFill(device, bounds, {0.25f, 0.5f, 0.75f, 1}));
    QVector<KisTileSP> tiles;
    for (int row = 0; row < 17; ++row)
        for (int col = 0; col < 17; ++col)
            tiles << device->dataManager()->getTile(col, row, false);
    bool locked = scenario == 2;
    if (locked)
        tiles[0]->tileData()->blockSwappingForReadback();
    auto unlock = qScopeGuard([&] {
        if (locked)
            tiles[0]->tileData()->unblockSwapping();
    });
    if (scenario == 1)
        m_backend->injectDownloadFailuresForTesting(1);
    if (scenario == 3)
        m_backend->injectReadbackLimitForTesting(0);
    const quint64 before = m_backend->context().completedValue();
    const quint64 evicted = m_backend->evictTiles(~quint64(0));
    const quint64 submissions = m_backend->context().completedValue() - before;
    int retained = 0;
    for (const auto &tile : tiles) {
        const auto *state = tile->tileData()->gpuState();
        if (state->slot != KisTileGpuState::InvalidSlot) {
            ++retained;
            QVERIFY(state->gpuValid());
        } else {
            QVERIFY(state->cpuValid());
        }
    }
    if (scenario == 0 || scenario == 2) {
        QCOMPARE(submissions, quint64(2)); // 289 (or 288 unlocked) tiles, not one submission per tile
        QCOMPARE(retained, scenario == 2 ? 1 : 0);
        QVERIFY(evicted >= quint64(289 - retained) * device->pixelSize() * 64 * 64);
    } else if (scenario == 1) {
        QCOMPARE(submissions, quint64(1));
        QVERIFY(retained > 0 && retained <= 256);
    } else {
        QCOMPARE(submissions, quint64(0));
        QCOMPARE(retained, 289);
    }
    QVERIFY(!m_backend->hasFailed());
    if (locked) {
        tiles[0]->tileData()->unblockSwapping();
        locked = false;
    }
    m_backend->injectDownloadFailuresForTesting(0);
    m_backend->injectReadbackLimitForTesting(~quint64(0));
    m_backend->evictTiles(~quint64(0));
    for (const auto &tile : tiles)
        QCOMPARE(tile->tileData()->gpuState()->slot.load(), KisTileGpuState::InvalidSlot);
    KoColor color(device->colorSpace());
    device->colorSpace()->fromNormalisedChannelsValue(color.data(), {0.25f, 0.5f, 0.75f, 1});
    QByteArray pixels(bounds.width() * bounds.height() * device->pixelSize(), Qt::Uninitialized);
    device->readBytes(reinterpret_cast<quint8 *>(pixels.data()), bounds);
    for (int offset = 0; offset < pixels.size(); offset += device->pixelSize())
        QCOMPARE(std::memcmp(pixels.constData() + offset, color.data(), device->pixelSize()), 0);
}

void KisGpuPaintDeviceTest::testMixedEvictionBatch()
{
    REQUIRE_GPU();
    KisPaintDeviceSP f32 = new KisPaintDevice(rgbaFloat()), f16 = new KisPaintDevice(rgbaFloat(true));
    QVERIFY(gpuFill(f32, QRect(0, 0, 128, 64), {0.25f, 0.5f, 0.75f, 1}));
    QVERIFY(gpuFill(f16, QRect(0, 0, 64, 64), {0.75f, 0.5f, 0.25f, 1}));
    const auto a = f32->dataManager()->getTile(0, 0, false);
    const auto b = f32->dataManager()->getTile(1, 0, false);
    const auto c = f16->dataManager()->getTile(0, 0, false);
    a->tileData()->blockSwapping(); // A CPU-current neighbor must not need a download.
    a->tileData()->unblockSwapping();
    auto *store = KisTileDataStore::instance();
    auto *iteration = store->beginIteration();
    // Pixel sizes are downloaded separately. Fail F16, but reclaim F32; the
    // duplicate must not double-count or recursively lock the same tile.
    m_backend->injectDownloadFailuresForTesting(1);
    const quint64 before = m_backend->context().completedValue();
    const quint64 released =
        store->tryEvictGpuTileDataBatch({a->tileData(), b->tileData(), c->tileData(), a->tileData()});
    store->endIteration(iteration);
    QCOMPARE(released, quint64(2 * 64 * 64 * 16));
    QCOMPARE(m_backend->context().completedValue() - before, quint64(1));
    QCOMPARE(a->tileData()->gpuState()->slot.load(), KisTileGpuState::InvalidSlot);
    QCOMPARE(b->tileData()->gpuState()->slot.load(), KisTileGpuState::InvalidSlot);
    QVERIFY(c->tileData()->gpuState()->slot != KisTileGpuState::InvalidSlot);
    QVERIFY(c->tileData()->gpuState()->gpuValid());
    QVERIFY(!m_backend->hasFailed());
    iteration = store->beginIteration();
    const quint64 remaining = store->tryEvictGpuTileDataBatch({a->tileData(), b->tileData(), c->tileData()});
    const quint64 alreadyEvicted = store->tryEvictGpuTileDataBatch({a->tileData(), b->tileData(), c->tileData()});
    store->endIteration(iteration);
    QCOMPARE(remaining, quint64(64 * 64 * 8));
    QCOMPARE(alreadyEvicted, quint64(0));
    QCOMPARE(readPixels(f32, QRect(0, 0, 128, 64))[0], 0.25f);
    KoColor color;
    f16->pixel(0, 0, &color);
    QVector<float> channels(4);
    color.colorSpace()->normalisedChannelsValue(color.data(), channels);
    QCOMPARE(channels[0], 0.75f);
}

void KisGpuPaintDeviceTest::testPreparedAccessPreventsEviction()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 64, 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuFill(device, rect, {0.5f, 0.25f, 0.75f, 1}));
    KisGpuTileAccess access(device, rect, KisGpuTileAccess::ReadOnly);
    m_commands->begin();
    QVERIFY(access.prepare(*m_commands));
    auto tile = device->dataManager()->getTile(0, 0, false);
    KisTileGpuState *state = tile->tileData()->gpuState();
    const quint32 slot = state->slot;
    m_backend->evictTiles(~quint64(0));
    QCOMPARE(state->slot.load(), slot);
    KisGpuTileAccess::finishUnsubmitted(*m_commands, {&access});
    m_backend->evictTiles(~quint64(0));
    QCOMPARE(state->slot.load(), KisTileGpuState::InvalidSlot);
    QCOMPARE(readPixels(device, rect)[0], 0.5f);
}

void KisGpuPaintDeviceTest::testEvictionFailureKeepsContent()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 64, 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuFill(device, rect, {0.75f, 0.5f, 0.25f, 1}));
    auto tile = device->dataManager()->getTile(0, 0, false);
    auto *state = tile->tileData()->gpuState();
    const quint32 slot = state->slot;
    m_backend->injectDownloadFailuresForTesting(1);
    auto *store = KisTileDataStore::instance();
    auto *iteration = store->beginIteration();
    const bool evicted = store->tryEvictGpuTileData(tile->tileData());
    store->endIteration(iteration);
    QVERIFY(!evicted);
    QCOMPARE(state->slot.load(), slot);
    QVERIFY(state->gpuValid());
    QVERIFY(!m_backend->hasFailed());
    QCOMPARE(readPixels(device, rect)[0], 0.75f);
}

void KisGpuPaintDeviceTest::testGpuTileDiskSwapRoundTrip()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 64, 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuFill(device, rect, {0.25f, 0.5f, 0.75f, 1}));
    auto tile = device->dataManager()->getTile(0, 0, false);
    auto *td = tile->tileData();
    QVERIFY(!td->gpuState()->cpuValid());
    auto *store = KisTileDataStore::instance();
    auto *iteration = store->beginIteration();
    const bool swapped = iteration->trySwapOut(td);
    store->endIteration(iteration);
    QVERIFY(swapped);
    QVERIFY(!td->data());
    QCOMPARE(td->gpuState()->slot.load(), KisTileGpuState::InvalidSlot);
    QCOMPARE(readPixels(device, rect)[0], 0.25f);
    // Reload the same state into a fresh slot, then overwrite on the GPU.
    QVERIFY(gpuFill(device, rect, {0.75f, 0.5f, 0.25f, 1}));
    QCOMPARE(readPixels(device, rect)[0], 0.75f);
}

void KisGpuPaintDeviceTest::testEvictionPreservesUndoRedo()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 128, 128);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuFill(device, rect, {0.25f, 0.5f, 0.75f, 1}));
    KisTransaction transaction(device);
    QVERIFY(gpuFill(device, rect, {0.75f, 0.5f, 0.25f, 1}));
    QScopedPointer<KUndo2Command> command(transaction.endAndTake());
    command->redo();
    m_backend->evictTiles(~quint64(0));
    QCOMPARE(readPixels(device, rect)[0], 0.75f);
    command->undo();
    QCOMPARE(readPixels(device, rect)[0], 0.25f);
    command->redo();
    QCOMPARE(readPixels(device, rect)[0], 0.75f);
}

void KisGpuPaintDeviceTest::testCopySourcePreventsEviction()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 64, 64);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    QVERIFY(gpuFill(device, rect, {0.25f, 0.5f, 0.75f, 1}));
    KisPaintDeviceSP copy = new KisPaintDevice(*device);
    auto sourceTile = device->dataManager()->getTile(0, 0, false);
    auto *sourceState = sourceTile->tileData()->gpuState();
    const quint32 sourceSlot = sourceState->slot;
    KisGpuTileAccess access(copy, rect, KisGpuTileAccess::ReadWrite);
    m_commands->begin();
    QVERIFY(access.prepare(*m_commands));
    m_backend->evictTiles(~quint64(0));
    QCOMPARE(sourceState->slot.load(), sourceSlot);
    const quint64 value = KisGpuTileAccess::submitAndFinish(*m_commands, {&access});
    QVERIFY(value);
    QCOMPARE(sourceState->lastUse, value);
    QVERIFY(m_commands->wait());
    m_backend->evictTiles(~quint64(0));
    QCOMPARE(readPixels(copy, rect), readPixels(device, rect));
    QCOMPARE(readPixels(copy, rect)[0], 0.25f);
}

void KisGpuPaintDeviceTest::testConcurrentEviction()
{
    REQUIRE_GPU();
    const QRect rect(0, 0, 128, 128);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    std::atomic<bool> stop{false};
    std::thread evictor([&]() {
        while (!stop.load()) {
            m_backend->evictTiles(~quint64(0));
            std::this_thread::yield();
        }
    });
    auto join = qScopeGuard([&]() {
        stop.store(true);
        evictor.join();
    });
    for (int i = 0; i < 32; ++i) {
        const float value = float(i) / 32;
        QVERIFY(gpuFill(device, rect, {value, 0.5f, 0.25f, 1}));
        const auto pixels = readPixels(device, rect);
        for (size_t p = 0; p < pixels.size(); p += 4) {
            QCOMPARE(pixels[p], value);
            QCOMPARE(pixels[p + 3], 1.0f);
        }
    }
}

void KisGpuPaintDeviceTest::benchmarkTransfers()
{
    REQUIRE_GPU();
    const int size =
        qEnvironmentVariableIsSet("KRITA_GPU_BENCH_SIZE") ? qEnvironmentVariableIntValue("KRITA_GPU_BENCH_SIZE") : 4096;
    const QRect rect(0, 0, size, size);
    KisPaintDeviceSP device = new KisPaintDevice(rgbaFloat());
    fillRandom(device, rect, 12);

    auto timeAccess = [&](KisGpuTileAccess::Mode mode, bool fill) {
        QElapsedTimer timer;
        timer.start();
        KisGpuTileAccess access(device, rect, mode);
        m_commands->begin();
        if (!access.prepare(*m_commands)) {
            return -1.0;
        }
        if (fill) {
            const float color[4] = {0.1f, 0.2f, 0.3f, 1.0f};
            m_fill32->record(*m_commands, access.addresses(), color);
        }
        KisGpuTileAccess::submitAndFinish(*m_commands, {&access});
        m_commands->wait();
        return timer.nsecsElapsed() / 1.0e6;
    };

    const double firstUpload = timeAccess(KisGpuTileAccess::ReadOnly, false);
    const double resident = timeAccess(KisGpuTileAccess::ReadOnly, false);
    const double gpuWrite = timeAccess(KisGpuTileAccess::WriteOnly, true);

    std::vector<float> pixels(size_t(size) * size * 4);
    QElapsedTimer timer;
    timer.start();
    device->readBytes(reinterpret_cast<quint8 *>(pixels.data()), rect);
    const double lazyDownload = timer.nsecsElapsed() / 1.0e6;

    timeAccess(KisGpuTileAccess::WriteOnly, true);
    timer.restart();
    KisGpuTileAccess::syncToCpu(device, rect);
    const double batchedDownload = timer.nsecsElapsed() / 1.0e6;
    timer.restart();
    device->readBytes(reinterpret_cast<quint8 *>(pixels.data()), rect);
    const double readAfterSync = timer.nsecsElapsed() / 1.0e6;

    const int tiles = (size / 64) * (size / 64);
    qInfo().noquote() << QStringLiteral("Device %1x%1 RGBA F32 (%2 tiles, %3 MiB):")
                             .arg(size)
                             .arg(tiles)
                             .arg(double(size) * size * 16 / (1 << 20), 0, 'f', 0);
    qInfo().noquote() << QStringLiteral("  first GPU read (upload all tiles):  %1 ms").arg(firstUpload, 0, 'f', 1);
    qInfo().noquote() << QStringLiteral("  GPU read when resident:             %1 ms").arg(resident, 0, 'f', 1);
    qInfo().noquote() << QStringLiteral("  GPU fill (write-only, in place):    %1 ms").arg(gpuWrite, 0, 'f', 1);
    qInfo().noquote() << QStringLiteral("  readBytes with automatic batching: %1 ms").arg(lazyDownload, 0, 'f', 1);
    qInfo().noquote() << QStringLiteral("  syncToCpu (batched downloads):      %1 ms").arg(batchedDownload, 0, 'f', 1);
    qInfo().noquote() << QStringLiteral("  readBytes after syncToCpu:          %1 ms").arg(readAfterSync, 0, 'f', 1);
    QVERIFY(firstUpload >= 0 && resident >= 0 && gpuWrite >= 0);
}

SIMPLE_TEST_MAIN(KisGpuPaintDeviceTest)

#include "KisGpuPaintDeviceTest.moc"
