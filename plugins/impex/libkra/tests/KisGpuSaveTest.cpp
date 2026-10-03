/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <simpletest.h>

#include <QFile>
#include <QFloat16>
#include <QObject>

#include <KoColorModelStandardIds.h>
#include <KoColorSpaceRegistry.h>

#include <KisDocument.h>
#include <KisGpuEngineUi.h>
#include <KisPart.h>
#include <testui.h>

#include "gpu/KisGpuMergeBatch.h"
#include "kis_image.h"
#include "kis_paint_device.h"
#include "kis_paint_layer.h"

#ifdef HAVE_KRITA_GPU_ENGINE
#include "gpu/KisGpuTileBackend.h"
#endif

#include <random>
#include <vector>

/**
 * GPU engine phase 3.3 (docs/agent/gpu-engine.md): image data lost on the
 * GPU while a document is being saved must not replace the target file.
 */
class KisGpuSaveTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testLossDuringSaveKeepsTarget();
};

void KisGpuSaveTest::testLossDuringSaveKeepsTarget()
{
#ifdef HAVE_KRITA_GPU_ENGINE
    KisGpuMergeBatch::setEnabled(true);
    KisGpuTileBackend *backend = KisGpuTileBackend::instance();
    if (!backend) {
        QSKIP(qPrintable(KisGpuTileBackend::unavailableReason()));
    }

    // A document whose projection was composited on the GPU and exists
    // only there (phase 3.1); writing the .kra reads it (merged image).
    const KoColorSpace *space =
        KoColorSpaceRegistry::instance()->colorSpace(RGBAColorModelID.id(), Float32BitsColorDepthID.id(), QString());
    QScopedPointer<KisDocument> document(KisPart::instance()->createDocument());
    // No dialogs: a failed save would otherwise block on a modal message box.
    document->setFileBatchMode(true);
    KisImageSP image = new KisImage(document->createUndoStore(), 300, 200, space, "gpu save");
    std::mt19937 random(7);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int i = 0; i < 2; i++) {
        KisPaintLayerSP layer = new KisPaintLayer(image, QStringLiteral("layer %1").arg(i), 200);
        std::vector<float> pixels(size_t(300) * 200 * 4);
        for (float &value : pixels) {
            value = unit(random);
        }
        layer->paintDevice()->writeBytes(reinterpret_cast<const quint8 *>(pixels.data()), image->bounds());
        image->addNode(layer, image->root());
    }
    document->setCurrentImage(image);
    image->refreshGraphAsync();
    image->waitForDone();
    // Saving works normally (and downloads the projection)...
    QVERIFY(document->exportDocumentSync(QStringLiteral("gpu_save_baseline.kra"), KisDocument::nativeFormatMimeType()));
    // ...then the projection is composited on the GPU again and exists only there.
    const quint64 compositesBefore = KisGpuMergeBatch::gpuCompositeCount();
    image->refreshGraphAsync();
    image->waitForDone();
    QVERIFY(KisGpuMergeBatch::gpuCompositeCount() > compositesBefore);

    const QString target = QStringLiteral("gpu_save_target.kra");
    {
        QFile file(target);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("original content");
    }

    // Every GPU read-back fails from now on (device loss).
    KisGpuEngineUi::setAcknowledgedLossCountForTesting(backend->contentLossCount());
    const quint64 lostBefore = backend->contentLossCount();
    backend->injectDownloadFailuresForTesting(1000000);

    QVERIFY(!document->exportDocumentSync(target, KisDocument::nativeFormatMimeType()));
    QVERIFY2(backend->contentLossCount() > lostBefore, "the save did not lose GPU data");
    {
        QFile file(target);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("original content"));
    }

    // After the user agrees to save what is displayed (confirmSave()), the
    // same save writes the file.
    KisGpuEngineUi::setAcknowledgedLossCountForTesting(backend->contentLossCount());
    QVERIFY(document->exportDocumentSync(target, KisDocument::nativeFormatMimeType()));
    {
        QFile file(target);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll() != QByteArray("original content"));
    }

    backend->injectDownloadFailuresForTesting(0);
    backend->resetFailureForTesting();
    KisGpuEngineUi::setAcknowledgedLossCountForTesting(backend->contentLossCount());
    KisGpuMergeBatch::setEnabled(false);
#else
    QSKIP("built without the GPU engine");
#endif
}

KISTEST_MAIN(KisGpuSaveTest)

#include "KisGpuSaveTest.moc"
