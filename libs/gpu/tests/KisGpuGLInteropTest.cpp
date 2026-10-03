/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <simpletest.h>

#include <QFloat16>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

#include <KisGpuBuffer.h>
#include <KisGpuCommandList.h>
#include <KisGpuContext.h>
#include <KisGpuGLSharedImage.h>
#include <KisGpuTilePool.h>

#include <memory>

/**
 * Phase 0 spike: Vulkan writes tiles into an image that an OpenGL context
 * reads through GL_EXT_memory_object, synchronized with exported semaphores.
 */
class KisGpuGLInteropTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void testVulkanToGLRoundTrip();

private:
    QVector<float> readTexturePixel(quint32 texture, int x, int y);

    std::unique_ptr<KisGpuContext> m_context;
    QString m_unavailableReason;
    QOffscreenSurface m_surface;
    QOpenGLContext m_glContext;
};

void KisGpuGLInteropTest::initTestCase()
{
    m_context = KisGpuContext::create(&m_unavailableReason);

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    m_surface.setFormat(format);
    m_surface.create();
    m_glContext.setFormat(format);
    if (!m_glContext.create() || !m_glContext.makeCurrent(&m_surface)) {
        m_unavailableReason = QStringLiteral("cannot create a desktop OpenGL context");
        m_context.reset();
        return;
    }
    if (m_context) {
        qInfo().noquote() << "GL renderer:"
                          << reinterpret_cast<const char *>(m_glContext.functions()->glGetString(GL_RENDERER));
    }
}

void KisGpuGLInteropTest::cleanupTestCase()
{
    if (m_context) {
        QCOMPARE(m_context->validationErrorCount(), 0);
    }
    if (m_glContext.isValid()) {
        m_glContext.doneCurrent();
    }
    m_context.reset();
}

QVector<float> KisGpuGLInteropTest::readTexturePixel(quint32 texture, int x, int y)
{
    QOpenGLFunctions *f = m_glContext.functions();
    GLuint framebuffer = 0;
    f->glGenFramebuffers(1, &framebuffer);
    f->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    QVector<float> pixel(4, -1.0f);
    if (f->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        f->glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, pixel.data());
    }
    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    f->glDeleteFramebuffers(1, &framebuffer);
    return pixel;
}

void KisGpuGLInteropTest::testVulkanToGLRoundTrip()
{
    if (!m_context) {
        QSKIP(qPrintable(QStringLiteral("Interop unavailable: %1").arg(m_unavailableReason)));
    }

    QString error;
    std::unique_ptr<KisGpuGLSharedImage> shared =
        KisGpuGLSharedImage::create(*m_context, &m_glContext, QSize(128, 128), KisGpuTileFormat::RGBA16F, &error);
    QVERIFY2(shared, qPrintable(error));

    KisGpuTilePool pool(*m_context, KisGpuTileFormat::RGBA16F, 16);
    QVector<quint32> slots;
    for (int i = 0; i < 4; i++) {
        slots << pool.allocate();
    }

    std::unique_ptr<KisGpuBuffer> staging =
        KisGpuBuffer::create(*m_context, pool.tileBytes() * 4, KisGpuBuffer::Location::Upload);
    QVERIFY(staging);

    auto fillTiles = [&](float base) {
        qfloat16 *out = static_cast<qfloat16 *>(staging->mapped());
        for (int tile = 0; tile < 4; tile++) {
            for (int pixel = 0; pixel < KisGpuTilePool::TileSize * KisGpuTilePool::TileSize; pixel++) {
                out[0] = qfloat16(base + 0.125f * tile);
                out[1] = qfloat16(0.25f);
                out[2] = qfloat16(0.5f);
                out[3] = qfloat16(1.0f);
                out += 4;
            }
        }
    };

    KisGpuCommandList commands(*m_context);

    for (int round = 0; round < 3; round++) {
        const float base = 0.125f * round;
        fillTiles(base);

        commands.begin();
        pool.recordUpload(commands, *staging, 0, slots);
        commands.computeBarrier();
        shared->recordCopyFromTiles(commands, pool, slots, 2);
        commands.barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         VK_ACCESS_2_MEMORY_READ_BIT);
        // From the second round on, Vulkan must wait until GL released the image.
        const QVector<VkSemaphoreSubmitInfo> waits = shared->takeGLDoneWait();
        QCOMPARE(waits.size(), round == 0 ? 0 : 1);
        QVERIFY(commands.submit(waits, {shared->vulkanDoneSignal()}));

        // GL waits on the GPU for the Vulkan semaphore; no CPU wait is needed.
        shared->glAcquire();
        for (int tile = 0; tile < 4; tile++) {
            const int x = (tile % 2) * 64 + 10;
            const int y = (tile / 2) * 64 + 20;
            const QVector<float> pixel = readTexturePixel(shared->glTexture(), x, y);
            QCOMPARE(pixel[0], base + 0.125f * tile);
            QCOMPARE(pixel[1], 0.25f);
            QCOMPARE(pixel[2], 0.5f);
            QCOMPARE(pixel[3], 1.0f);
        }
        shared->glRelease();
        QVERIFY(commands.wait());
    }

    // Make the pending GL release consumed before destruction.
    commands.begin();
    QVERIFY(commands.submit(shared->takeGLDoneWait()));
    QVERIFY(commands.wait());

    for (quint32 slot : slots) {
        pool.release(slot);
    }
}

SIMPLE_TEST_MAIN(KisGpuGLInteropTest)

#include "KisGpuGLInteropTest.moc"
