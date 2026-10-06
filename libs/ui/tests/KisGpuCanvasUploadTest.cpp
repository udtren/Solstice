/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <simpletest.h>

#include <QElapsedTimer>
#include <QFloat16>
#include <QObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QScopeGuard>
#include <QSurfaceFormat>
#include <QtEndian>

#include <KoColorModelStandardIds.h>
#include <KoColorProfile.h>
#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOpRegistry.h>

#include <KisGpuContext.h>
#include <KisGpuGLSharedBuffer.h>

#include "canvas/kis_update_info.h"
#include "gpu/KisGpuMergeBatch.h"
#include "gpu/KisGpuTileBackend.h"
#include "kis_image.h"
#include "kis_paint_device.h"
#include "kis_paint_layer.h"
#include "opengl/KisGpuCanvasUploader.h"
#include "opengl/KisOpenGLUpdateInfoBuilder.h"
#include "opengl/kis_texture_tile.h"
#include "opengl/kis_texture_tile_info_pool.h"
#include "opengl/kis_texture_tile_update_info.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <vector>

/**
 * GPU engine phase 3.2: canvas textures written from the GPU-resident
 * projection must equal the textures written by the CPU path
 * (docs/agent/gpu-engine.md).
 */
class KisGpuCanvasUploadTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void testTexturesMatchCpu_data();
    void testTexturesMatchCpu();
    void testLutProfilesUseCpu();
    void testSharedUploadMatchesCpu_data();
    void testSharedUploadMatchesCpu();
    void testGLImportFailureFallsBackToCpu_data();
    void testGLImportFailureFallsBackToCpu();
    void testInteropSelfTestPreservesBinding();
    void testSilentInteropFailureFallsBackToCpu();
    void testWidgetInteropWithSecondDevice();
    void testBufferBudgetAndRetirement();
    void benchmarkCanvasUpdate();

private:
    QOffscreenSurface m_surface;
    QOpenGLContext m_glContext;
    QString m_skipReason;
};

namespace
{
constexpr GLenum GL_RGBA32F_ = 0x8814;
constexpr GLenum GL_RGBA16F_ = 0x881A;
constexpr GLenum GL_HALF_FLOAT_ = 0x140B;
using PFN_glGetTexImage = void(QOPENGLF_APIENTRY *)(GLenum, GLint, GLenum, GLenum, void *);

const KoColorSpace *rgba(const QString &depth, const QString &profile)
{
    return KoColorSpaceRegistry::instance()->colorSpace(RGBAColorModelID.id(), depth, profile);
}

void fillRandom(KisPaintDeviceSP device, const QRect &rect, quint32 seed, float scale)
{
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::vector<float> floats(size_t(rect.width()) * rect.height() * 4);
    for (size_t i = 0; i < floats.size(); i += 4) {
        floats[i] = unit(random) * scale;
        floats[i + 1] = unit(random) * scale;
        floats[i + 2] = unit(random) * scale;
        floats[i + 3] = unit(random) < 0.3f ? 1.0f : unit(random);
    }
    if (device->pixelSize() == 16) {
        device->writeBytes(reinterpret_cast<const quint8 *>(floats.data()), rect);
    } else {
        std::vector<qfloat16> half(floats.size());
        qFloatToFloat16(half.data(), floats.data(), qsizetype(floats.size()));
        device->writeBytes(reinterpret_cast<const quint8 *>(half.data()), rect);
    }
}

/// Texture tiles for every tile of an update, as KisOpenGLImageTextures keeps them.
struct TextureSet {
    KisGLTexturesInfo info;
    std::map<std::pair<int, int>, std::unique_ptr<KisTextureTile>> tiles;

    KisTextureTile *tile(int col, int row, const QRect &imageRect, QOpenGLFunctions *f)
    {
        auto &slot = tiles[{col, row}];
        if (!slot) {
            const int pixelSize = info.type == GL_FLOAT ? 16 : 8;
            const QByteArray fill(info.width * info.height * pixelSize, 0);
            slot.reset(new KisTextureTile(imageRect, &info, fill, KisOpenGL::BilinearFilterMode, nullptr, 4, f));
        }
        return slot.get();
    }
};

/// Applies @p updates in order, each in its own acquire/release scope like
/// KisOpenGLImageTextures::recalculateCache(), and reads every texture back (level 0).
std::map<std::pair<int, int>, std::vector<float>> applyAllAndRead(QOpenGLContext *context,
                                                                  TextureSet &textures,
                                                                  KisOpenGLUpdateInfoBuilder &builder,
                                                                  const QVector<KisOpenGLUpdateInfoSP> &updates,
                                                                  const QRect &bounds)
{
    QOpenGLFunctions *f = context->functions();
    for (const KisOpenGLUpdateInfoSP &update : updates) {
        KisGpuCanvasUploader::acquire(update->tileList);
        for (const KisTextureTileUpdateInfoSP &tileInfo : update->tileList) {
            const QRect imageRect =
                builder.calculateEffectiveTileRect(tileInfo->tileCol(), tileInfo->tileRow(), bounds);
            textures.tile(tileInfo->tileCol(), tileInfo->tileRow(), imageRect, f)->update(*tileInfo, true);
        }
        KisGpuCanvasUploader::release(update->tileList);
    }

    auto getTexImage = reinterpret_cast<PFN_glGetTexImage>(context->getProcAddress("glGetTexImage"));
    std::map<std::pair<int, int>, std::vector<float>> result;
    for (auto &entry : textures.tiles) {
        std::vector<float> pixels(size_t(textures.info.width) * textures.info.height * 4);
        entry.second->bindToActiveTexture(true);
        getTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
        result[entry.first] = std::move(pixels);
    }
    return result;
}

/// Applies @p update to @p textures and reads every texture back (level 0).
std::map<std::pair<int, int>, std::vector<float>> applyAndRead(QOpenGLContext *context,
                                                               TextureSet &textures,
                                                               KisOpenGLUpdateInfoBuilder &builder,
                                                               KisOpenGLUpdateInfoSP update,
                                                               const QRect &bounds)
{
    return applyAllAndRead(context, textures, builder, {update}, bounds);
}
/// Two layers over the whole image, so the projection is a real composite.
KisImageSP createCanvasImage(const KoColorSpace *space, const QRect &bounds, float valueScale)
{
    KisImageSP image = new KisImage(nullptr, bounds.width(), bounds.height(), space, "canvas");
    KisPaintLayerSP base = new KisPaintLayer(image, "base", 255);
    fillRandom(base->paintDevice(), bounds, 1, valueScale);
    image->addNode(base, image->root());
    KisPaintLayerSP top = new KisPaintLayer(image, "top", 200);
    fillRandom(top->paintDevice(), QRect(37, 50, 300, 200), 2, valueScale);
    image->addNode(top, image->root());
    image->refreshGraphAsync();
    image->waitForDone();
    return image;
}

/// The canvas configuration of KisOpenGLImageTextures (256 px textures, 16 px border).
void setUpCanvas(KisOpenGLUpdateInfoBuilder &builder,
                 const KoColorSpace *displaySpace,
                 std::initializer_list<TextureSet *> textureSets)
{
    builder.setTextureBorder(16);
    builder.setEffectiveTextureSize(QSize(224, 224));
    builder.setTextureInfoPool(toQShared(new KisTextureTileInfoPool(256, 256)));
    builder.setConversionOptions(ConversionOptions(displaySpace,
                                                   KoColorConversionTransformation::internalRenderingIntent(),
                                                   KoColorConversionTransformation::HighQuality
                                                       | KoColorConversionTransformation::BlackpointCompensation));
    const bool halfTexture = displaySpace->pixelSize() == 8;
    for (TextureSet *set : textureSets) {
        set->info.width = 256;
        set->info.height = 256;
        set->info.effectiveWidth = 224;
        set->info.effectiveHeight = 224;
        set->info.border = 16;
        set->info.internalFormat = halfTexture ? GL_RGBA16F_ : GL_RGBA32F_;
        set->info.format = GL_RGBA;
        set->info.type = halfTexture ? GL_HALF_FLOAT_ : GL_FLOAT;
    }
}

using TextureContents = std::map<std::pair<int, int>, std::vector<float>>;

/// Largest channel difference between two sets of read-back textures.
float maxTextureDifference(const TextureContents &cpu, const TextureContents &gpu, QString *where)
{
    float worst = 0.0f;
    for (const auto &entry : cpu) {
        const std::vector<float> &a = entry.second;
        const std::vector<float> &b = gpu.at(entry.first);
        for (size_t i = 0; i < a.size(); i++) {
            const float difference = std::abs(a[i] - b[i]);
            if (difference > worst || std::isnan(difference)) {
                worst = std::isnan(difference) ? INFINITY : difference;
                *where = QStringLiteral("tile (%1, %2) pixel (%3, %4) channel %5: CPU %6, GPU %7")
                             .arg(entry.first.first)
                             .arg(entry.first.second)
                             .arg((i / 4) % 256)
                             .arg((i / 4) / 256)
                             .arg(i % 4)
                             .arg(a[i])
                             .arg(b[i]);
            }
        }
    }
    return worst;
}

/// Data rows shared by the comparisons of the GPU and the CPU canvas paths.
void addConversionRows()
{
    QTest::addColumn<QString>("imageDepth");
    QTest::addColumn<QString>("imageProfile");
    QTest::addColumn<QString>("displayDepth");
    QTest::addColumn<QString>("displayProfile");
    QTest::addColumn<float>("valueScale");
    QTest::addColumn<float>("tolerance");

    const QString f32 = Float32BitsColorDepthID.id();
    const QString f16 = Float16BitsColorDepthID.id();
    const QString linear = QStringLiteral("sRGB-elle-V2-g10.icc");
    const QString srgb = QStringLiteral("sRGB-elle-V2-srgbtrc.icc");
    const QString rec2020 = QStringLiteral("Rec2020-elle-V4-g10.icc");
    const QString largeRgb = QStringLiteral("LargeRGB-elle-V4-g10.icc"); // D50 white
    const QString cieRgb = QStringLiteral("CIERGB-elle-V4-g10.icc"); // E white

    // Same profile: only the storage format may change.
    QTest::newRow("identity F32") << f32 << linear << f32 << linear << 2.0f << 0.0f;
    QTest::newRow("identity F16 image") << f16 << linear << f16 << linear << 2.0f << 0.0f;
    QTest::newRow("F32 image, F16 texture") << f32 << linear << f16 << linear << 2.0f << 1e-3f;
    // Matrix-shaper conversions (display values within [0, 1]).
    QTest::newRow("linear sRGB to sRGB TRC") << f32 << linear << f32 << srgb << 1.0f << 5e-4f;
    QTest::newRow("Rec2020 linear to sRGB TRC") << f32 << rec2020 << f32 << srgb << 0.8f << 5e-4f;
    QTest::newRow("sRGB TRC to linear sRGB") << f32 << srgb << f32 << linear << 1.0f << 5e-4f;
    // Different white points (D50 / E vs D65): the colorants must share the PCS.
    QTest::newRow("LargeRGB linear to linear sRGB") << f32 << largeRgb << f32 << linear << 1.0f << 5e-4f;
    QTest::newRow("CIERGB linear to linear sRGB") << f32 << cieRgb << f32 << linear << 1.0f << 5e-4f;
    QTest::newRow("sRGB TRC to LargeRGB linear") << f32 << srgb << f32 << largeRgb << 1.0f << 5e-4f;
    QTest::newRow("F16 image, sRGB TRC F16 texture") << f16 << linear << f16 << srgb << 1.0f << 2e-3f;
}
} // namespace

void KisGpuCanvasUploadTest::initTestCase()
{
    KisGpuMergeBatch::setEnabled(true);
    if (!KisGpuTileBackend::instance()) {
        m_skipReason = KisGpuTileBackend::unavailableReason();
        return;
    }

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    m_surface.setFormat(format);
    m_surface.create();
    m_glContext.setFormat(format);
    m_glContext.setShareContext(QOpenGLContext::globalShareContext());
    if (!m_glContext.create() || !m_glContext.makeCurrent(&m_surface)) {
        m_skipReason = QStringLiteral("no desktop OpenGL context");
        return;
    }
    if (!KisGpuGLSharedBuffer::glInteropSupported(KisGpuTileBackend::instance()->context(),
                                                  &m_glContext,
                                                  &m_skipReason)) {
        return;
    }
    // Advertised support followed by incorrect bytes is a failure, not a skip.
    KisGpuCanvasUploader::checkGLInterop();
    QVERIFY2(KisGpuCanvasUploader::isEnabled(), "advertised interop failed the real-data self-test");
}

void KisGpuCanvasUploadTest::cleanupTestCase()
{
    if (KisGpuTileBackend *backend = KisGpuTileBackend::existingInstance()) {
        backend->flush();
        QCOMPARE(backend->context().validationErrorCount(), 0);
    }
    KisGpuMergeBatch::setEnabled(false);
}

void KisGpuCanvasUploadTest::testTexturesMatchCpu_data()
{
    addConversionRows();
}

void KisGpuCanvasUploadTest::testTexturesMatchCpu()
{
    if (!m_skipReason.isEmpty()) {
        QSKIP(qPrintable(m_skipReason));
    }
    QFETCH(QString, imageDepth);
    QFETCH(QString, imageProfile);
    QFETCH(QString, displayDepth);
    QFETCH(QString, displayProfile);
    QFETCH(float, valueScale);
    QFETCH(float, tolerance);

    const KoColorSpace *imageSpace = rgba(imageDepth, imageProfile);
    const KoColorSpace *displaySpace = rgba(displayDepth, displayProfile);
    QVERIFY(imageSpace && displaySpace);

    // Not a multiple of the 224 px texture tiles: exercises the edge borders.
    const QRect bounds(0, 0, 500, 300);
    KisImageSP image = createCanvasImage(imageSpace, bounds, valueScale);
    KisOpenGLUpdateInfoBuilder builder;
    TextureSet cpuTextures;
    TextureSet gpuTextures;
    setUpCanvas(builder, displaySpace, {&cpuTextures, &gpuTextures});

    // A full update and a partial one at the image edge, like the canvas does.
    const QVector<QRect> updates = {bounds, QRect(420, 230, 80, 70), QRect(100, 100, 50, 50)};
    for (const QRect &rect : updates) {
        KisGpuCanvasUploader::setGLInteropAvailable(false);
        KisOpenGLUpdateInfoSP cpuUpdate = builder.buildUpdateInfo(rect, image, true);
        KisGpuCanvasUploader::setGLInteropAvailable(true);
        const quint64 before = KisGpuCanvasUploader::uploadCount();
        KisOpenGLUpdateInfoSP gpuUpdate = builder.buildUpdateInfo(rect, image, true);
        QVERIFY2(KisGpuCanvasUploader::uploadCount() > before, "the GPU path was not used");
        QCOMPARE(gpuUpdate->tileList.size(), cpuUpdate->tileList.size());

        const auto cpu = applyAndRead(&m_glContext, cpuTextures, builder, cpuUpdate, bounds);
        const auto gpu = applyAndRead(&m_glContext, gpuTextures, builder, gpuUpdate, bounds);
        QCOMPARE(gpu.size(), cpu.size());

        QString where;
        const float worst = maxTextureDifference(cpu, gpu, &where);
        qInfo().noquote() << QStringLiteral("update %1,%2 %3x%4: max difference %5")
                                 .arg(rect.x())
                                 .arg(rect.y())
                                 .arg(rect.width())
                                 .arg(rect.height())
                                 .arg(worst);
        QVERIFY2(worst <= tolerance, qPrintable(QStringLiteral("max difference %1 at %2").arg(worst).arg(where)));
    }
}

void KisGpuCanvasUploadTest::testLutProfilesUseCpu()
{
    // A matrix-shaper profile that also has an AToB0 tag: LCMS would use the
    // LUT, so the GPU must not replace it with the matrix and the curves.
    // (The tag entry points at existing tag data; only the tag table matters.)
    const KoColorSpace *linearSpace = rgba(Float32BitsColorDepthID.id(), QStringLiteral("sRGB-elle-V2-g10.icc"));
    const KoColorSpace *srgbSpace = rgba(Float32BitsColorDepthID.id(), QStringLiteral("sRGB-elle-V2-srgbtrc.icc"));
    QVERIFY(linearSpace && srgbSpace);

    KisGpuCanvasPatchWriter::Conversion conversion;
    QVERIFY(KisGpuCanvasUploader::conversionFor(linearSpace,
                                                srgbSpace,
                                                KoColorConversionTransformation::IntentPerceptual,
                                                &conversion));

    QByteArray data = srgbSpace->profile()->rawData();
    QVERIFY(data.size() > 132);
    const quint32 count = qFromBigEndian<quint32>(data.constData() + 128);
    // Rename the copyright tag entry to AToB0.
    bool renamed = false;
    for (quint32 i = 0; i < count; i++) {
        char *signature = data.data() + 132 + i * 12;
        if (QByteArray(signature, 4) == "cprt") {
            memcpy(signature, "A2B0", 4);
            renamed = true;
        }
    }
    QVERIFY(renamed);
    // A distinct description (ASCII and UTF-16 copies), or the registry
    // returns the color space of the original profile.
    data.replace(QByteArray("elle-V2"), QByteArray("lut0-V2"));
    data.replace(QByteArray("e\0l\0l\0e\0", 8),
                 QByteArray("l\0u\0t\0"
                            "0\0",
                            8));
    const KoColorProfile *lutProfile =
        KoColorSpaceRegistry::instance()->createColorProfile(RGBAColorModelID.id(), Float32BitsColorDepthID.id(), data);
    QVERIFY(lutProfile);
    QVERIFY(lutProfile->hasColorants() && lutProfile->hasTRC());
    const KoColorSpace *lutSpace =
        KoColorSpaceRegistry::instance()->colorSpace(RGBAColorModelID.id(), Float32BitsColorDepthID.id(), lutProfile);
    QVERIFY(lutSpace && lutSpace->profile()->rawData() == data);
    QVERIFY(!KisGpuCanvasUploader::conversionFor(linearSpace,
                                                 lutSpace,
                                                 KoColorConversionTransformation::IntentPerceptual,
                                                 &conversion));
    QVERIFY(!KisGpuCanvasUploader::conversionFor(lutSpace,
                                                 linearSpace,
                                                 KoColorConversionTransformation::IntentPerceptual,
                                                 &conversion));
}

void KisGpuCanvasUploadTest::testSharedUploadMatchesCpu_data()
{
    addConversionRows();
}

void KisGpuCanvasUploadTest::testSharedUploadMatchesCpu()
{
    if (!m_skipReason.isEmpty()) {
        QSKIP(qPrintable(m_skipReason));
    }
    QFETCH(QString, imageDepth);
    QFETCH(QString, imageProfile);
    QFETCH(QString, displayDepth);
    QFETCH(QString, displayProfile);
    QFETCH(float, valueScale);
    QFETCH(float, tolerance);

    const KoColorSpace *imageSpace = rgba(imageDepth, imageProfile);
    const KoColorSpace *displaySpace = rgba(displayDepth, displayProfile);
    QVERIFY(imageSpace && displaySpace);
    const QRect bounds(0, 0, 500, 300);
    KisImageSP image = createCanvasImage(imageSpace, bounds, valueScale);
    KisOpenGLUpdateInfoBuilder builder;
    TextureSet cpuTextures;
    TextureSet gpuTextures;
    setUpCanvas(builder, displaySpace, {&cpuTextures, &gpuTextures});

    // Concurrent canvas updates, built together as KisCanvas2 batches them:
    // overlapping ones, a distant one (a separate source access in the same
    // submission), a single pixel and one outside the image.
    const QVector<QRect> rects = {QRect(0, 0, 40, 40),
                                  QRect(430, 240, 70, 60),
                                  QRect(20, 20, 60, 60),
                                  QRect(250, 10, 1, 1),
                                  QRect(600, 0, 10, 10)};
    KisGpuCanvasUploader::setGLInteropAvailable(false);
    QVERIFY(!builder.usesGpuUpload(image->projection()));
    const QVector<KisOpenGLUpdateInfoSP> cpuUpdates = builder.buildUpdateInfos(rects, image);
    KisGpuCanvasUploader::setGLInteropAvailable(true);
    QVERIFY(builder.usesGpuUpload(image->projection()));
    const quint64 before = KisGpuCanvasUploader::uploadCount();
    const QVector<KisOpenGLUpdateInfoSP> gpuUpdates = builder.buildUpdateInfos(rects, image);
    QCOMPARE(KisGpuCanvasUploader::uploadCount(), before + 1);
    QCOMPARE(gpuUpdates.size(), rects.size());
    QCOMPARE(cpuUpdates.size(), rects.size());

    KisGpuCanvasUpload *shared = nullptr;
    KisTextureTileUpdateInfoSPList gpuTiles;
    for (int i = 0; i < rects.size(); i++) {
        QCOMPARE(gpuUpdates[i]->dirtyImageRect(), cpuUpdates[i]->dirtyImageRect());
        QCOMPARE(gpuUpdates[i]->tileList.size(), cpuUpdates[i]->tileList.size());
        for (const KisTextureTileUpdateInfoSP &tile : gpuUpdates[i]->tileList) {
            QVERIFY(tile->gpuUpload());
            shared = shared ? shared : tile->gpuUpload();
            QCOMPARE(tile->gpuUpload(), shared);
        }
        gpuTiles.append(gpuUpdates[i]->tileList);
    }
    QVERIFY(shared);
    QVERIFY(gpuUpdates.last()->tileList.isEmpty() && gpuUpdates.last()->dirtyImageRect().isEmpty());

    const TextureContents cpu = applyAllAndRead(&m_glContext, cpuTextures, builder, cpuUpdates, bounds);
    // KisOpenGLCanvas2::updateCanvasProjection(): one hold over the whole
    // batch; the per-update holds nest inside it.
    QVERIFY(KisGpuCanvasUploader::acquire(gpuTiles));
    const TextureContents gpu = applyAllAndRead(&m_glContext, gpuTextures, builder, gpuUpdates, bounds);
    QVERIFY(shared->isAcquired());
    KisGpuCanvasUploader::release(gpuTiles);
    QVERIFY(!shared->isAcquired());

    QCOMPARE(gpu.size(), cpu.size());
    QString where;
    const float worst = maxTextureDifference(cpu, gpu, &where);
    QVERIFY2(worst <= tolerance, qPrintable(QStringLiteral("max difference %1 at %2").arg(worst).arg(where)));
}

void KisGpuCanvasUploadTest::testGLImportFailureFallsBackToCpu_data()
{
    addConversionRows();
}

void KisGpuCanvasUploadTest::testGLImportFailureFallsBackToCpu()
{
    if (!m_skipReason.isEmpty()) {
        QSKIP(qPrintable(m_skipReason));
    }
    QFETCH(QString, imageDepth);
    QFETCH(QString, imageProfile);
    QFETCH(QString, displayDepth);
    QFETCH(QString, displayProfile);
    QFETCH(float, valueScale);
    QFETCH(float, tolerance);

    const KoColorSpace *imageSpace = rgba(imageDepth, imageProfile);
    const KoColorSpace *displaySpace = rgba(displayDepth, displayProfile);
    QVERIFY(imageSpace && displaySpace);
    const QRect bounds(0, 0, 500, 300);
    KisImageSP image = createCanvasImage(imageSpace, bounds, valueScale);
    KisOpenGLUpdateInfoBuilder builder;
    TextureSet cpuTextures;
    TextureSet gpuTextures;
    setUpCanvas(builder, displaySpace, {&cpuTextures, &gpuTextures});

    auto buildCpu = [&](const QRect &rect) {
        KisGpuCanvasUploader::setGLInteropAvailable(false);
        return builder.buildUpdateInfo(rect, image, true);
    };
    auto buildGpu = [&](const QRect &rect) {
        KisGpuCanvasUploader::setGLInteropAvailable(true);
        const quint64 before = KisGpuCanvasUploader::uploadCount();
        KisOpenGLUpdateInfoSP update = builder.buildUpdateInfo(rect, image, true);
        return KisGpuCanvasUploader::uploadCount() > before ? update : KisOpenGLUpdateInfoSP();
    };

    // 1. One upload that GL cannot import (a full update and a partial one
    //    with extended edge patches). 2. Two updates merged like the canvas
    //    update compressor does (KisOpenGLUpdateInfo::tryMergeWith()), with
    //    disjoint texture tiles: GL imports the second upload but not the
    //    first, so one update mixes read-back and GL-uploaded tiles.
    struct Step {
        QVector<QRect> rects;
    };
    const QVector<Step> steps = {{{bounds}},
                                 {{QRect(420, 230, 80, 70)}},
                                 {{QRect(0, 0, 200, 300), QRect(300, 0, 200, 300)}}};
    for (const Step &step : steps) {
        KisOpenGLUpdateInfoSP cpuUpdate;
        KisOpenGLUpdateInfoSP gpuUpdate;
        QVector<KisGpuCanvasUpload *> uploads;
        for (const QRect &rect : step.rects) {
            KisOpenGLUpdateInfoSP cpu = buildCpu(rect);
            KisOpenGLUpdateInfoSP gpu = buildGpu(rect);
            QVERIFY2(gpu, "the GPU path was not used");
            uploads << gpu->tileList.first()->gpuUpload();
            if (!cpuUpdate) {
                cpuUpdate = cpu;
                gpuUpdate = gpu;
            } else {
                QVERIFY(cpuUpdate->tryMergeWith(*cpu));
                QVERIFY(gpuUpdate->tryMergeWith(*gpu));
            }
        }

        // What KisOpenGLImageTextures::recalculateCache() does: acquire()
        // fails for the first upload; its patches are read back through
        // Vulkan and uploaded from the CPU, the others are uploaded by GL.
        KisGpuGLSharedBuffer::injectGLImportFailuresForTesting(1);
        QVERIFY(!KisGpuCanvasUploader::acquire(gpuUpdate->tileList));
        KisGpuGLSharedBuffer::injectGLImportFailuresForTesting(0);
        QVERIFY(!KisGpuCanvasUploader::isEnabled());
        QVERIFY(KisGpuCanvasUploader::readBackFailedUploads(gpuUpdate->tileList));
        int readBackTiles = 0;
        int glTiles = 0;
        for (const KisTextureTileUpdateInfoSP &tile : gpuUpdate->tileList) {
            if (KisGpuCanvasUpload *upload = tile->gpuUpload()) {
                QVERIFY(upload != uploads.first() && upload->isAcquired());
                glTiles++;
            } else {
                readBackTiles++;
            }
        }
        QVERIFY(readBackTiles > 0);
        QCOMPARE(glTiles > 0, step.rects.size() > 1);

        // applyAndRead() nests another hold inside ours; release ours after
        // it, like the scope guard of recalculateCache().
        const TextureContents cpu = applyAndRead(&m_glContext, cpuTextures, builder, cpuUpdate, bounds);
        const TextureContents gpu = applyAndRead(&m_glContext, gpuTextures, builder, gpuUpdate, bounds);
        KisGpuCanvasUploader::release(gpuUpdate->tileList);
        QCOMPARE(gpu.size(), cpu.size());
        QString where;
        const float worst = maxTextureDifference(cpu, gpu, &where);
        QVERIFY2(worst <= tolerance, qPrintable(QStringLiteral("max difference %1 at %2").arg(worst).arg(where)));
    }
    KisGpuCanvasUploader::setGLInteropAvailable(true);
}

void KisGpuCanvasUploadTest::testInteropSelfTestPreservesBinding()
{
    if (!m_skipReason.isEmpty())
        QSKIP(qPrintable(m_skipReason));
    auto *f = m_glContext.functions();
    GLuint previousBuffers[3] = {};
    const GLenum targets[] = {GL_PIXEL_UNPACK_BUFFER, GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER};
    const GLenum bindings[] = {GL_PIXEL_UNPACK_BUFFER_BINDING,
                               GL_COPY_READ_BUFFER_BINDING,
                               GL_COPY_WRITE_BUFFER_BINDING};
    f->glGenBuffers(3, previousBuffers);
    for (int i = 0; i < 3; ++i)
        f->glBindBuffer(targets[i], previousBuffers[i]);
    auto cleanup = qScopeGuard([&]() {
        for (GLenum target : targets)
            f->glBindBuffer(target, 0);
        f->glDeleteBuffers(3, previousBuffers);
    });
    QString reason;
    QVERIFY2(KisGpuGLSharedBuffer::testGLInterop(KisGpuTileBackend::instance()->context(), &reason),
             qPrintable(reason));
    for (int i = 0; i < 3; ++i) {
        GLint bound = 0;
        f->glGetIntegerv(bindings[i], &bound);
        QCOMPARE(GLuint(bound), previousBuffers[i]);
    }
    QCOMPARE(f->glGetError(), GLenum(GL_NO_ERROR));
}

void KisGpuCanvasUploadTest::testSilentInteropFailureFallsBackToCpu()
{
    if (!m_skipReason.isEmpty())
        QSKIP(qPrintable(m_skipReason));
    auto cleanup = qScopeGuard([]() {
        KisGpuGLSharedBuffer::injectUnsharedGLBuffersForTesting(0);
        KisGpuCanvasUploader::setGLInteropAvailable(true);
    });
    const QRect bounds(0, 0, 500, 300);
    const KoColorSpace *space = rgba(Float32BitsColorDepthID.id(), QStringLiteral("sRGB-elle-V2-g10.icc"));
    KisImageSP image = createCanvasImage(space, bounds, 1.0f);
    KisOpenGLUpdateInfoBuilder builder;
    TextureSet referenceTextures;
    TextureSet fallbackTextures;
    setUpCanvas(builder, space, {&referenceTextures, &fallbackTextures});
    KisGpuCanvasUploader::setGLInteropAvailable(false);
    const auto reference = builder.buildUpdateInfo(bounds, image, true);

    // Real GL storage containing zeros, not an injected false return or GL error.
    QString reason;
    KisGpuGLSharedBuffer::injectUnsharedGLBuffersForTesting(1);
    QVERIFY(!KisGpuGLSharedBuffer::testGLInterop(KisGpuTileBackend::instance()->context(), &reason));
    QVERIFY2(reason.contains(QStringLiteral("GL read 0x0 (GL error 0)")), qPrintable(reason));
    KisGpuCanvasUploader::resetGLInteropForTesting();
    KisGpuGLSharedBuffer::injectUnsharedGLBuffersForTesting(1);
    KisGpuCanvasUploader::checkGLInterop();
    QVERIFY(!KisGpuCanvasUploader::isEnabled());
    // A later canvas must not turn interop back on during this session.
    KisGpuCanvasUploader::checkGLInterop();
    QVERIFY(!KisGpuCanvasUploader::isEnabled());
    const quint64 before = KisGpuCanvasUploader::uploadCount();
    const auto fallback = builder.buildUpdateInfo(bounds, image, true);
    QCOMPARE(KisGpuCanvasUploader::uploadCount(), before);
    for (const auto &tile : fallback->tileList)
        QVERIFY(!tile->gpuUpload());
    const auto expected = applyAndRead(&m_glContext, referenceTextures, builder, reference, bounds);
    const auto actual = applyAndRead(&m_glContext, fallbackTextures, builder, fallback, bounds);
    QString where;
    QCOMPARE(maxTextureDifference(expected, actual, &where), 0.0f);
    QVERIFY(!KisGpuTileBackend::instance()->hasFailed());
}

void KisGpuCanvasUploadTest::testWidgetInteropWithSecondDevice()
{
    if (!m_skipReason.isEmpty())
        QSKIP(qPrintable(m_skipReason));
    auto restoreContext = qScopeGuard([&]() {
        m_glContext.makeCurrent(&m_surface);
    });
    QOpenGLWidget widget;
    widget.setFormat(m_glContext.format());
    widget.resize(128, 128);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    widget.makeCurrent();
    QVERIFY(widget.isValid());
    QVERIFY(QOpenGLContext::areSharing(widget.context(), &m_glContext));
    QString reason;
    auto &context = KisGpuTileBackend::instance()->context();
    QVERIFY2(KisGpuGLSharedBuffer::testGLInterop(context, &reason), qPrintable(reason));
    // Model another plugin's independent VkInstance/VkDevice. Keep it alive
    // through process exit, like the backend (validation-layer teardown rule).
    static KisGpuContext *otherContext = KisGpuContext::create(&reason).release();
    QVERIFY2(otherContext, qPrintable(reason));
    QVERIFY(context.instance() != otherContext->instance());
    QVERIFY(context.device() != otherContext->device());
    QVERIFY2(KisGpuGLSharedBuffer::testGLInterop(context, &reason), qPrintable(reason));
    QCOMPARE(otherContext->validationErrorCount(), 0);
}

void KisGpuCanvasUploadTest::testBufferBudgetAndRetirement()
{
    if (!m_skipReason.isEmpty())
        QSKIP(qPrintable(m_skipReason));
    QVERIFY(m_glContext.makeCurrent(&m_surface));
    KisGpuCanvasUploader::setGLInteropAvailable(true);
    KisGpuCanvasUploader::trimBuffersForTesting();
    QCOMPARE(KisGpuCanvasUploader::reservedBufferBytes(), quint64(0));
    const quint64 oldBudget = KisGpuCanvasUploader::setBufferMemoryBudgetForTesting(1 << 20);
    auto restore = qScopeGuard([&]() {
        m_glContext.makeCurrent(&m_surface);
        KisGpuCanvasUploader::trimBuffersForTesting();
        KisGpuCanvasUploader::setBufferMemoryBudgetForTesting(oldBudget);
        KisGpuCanvasUploader::setGLInteropAvailable(true);
    });
    const QRect bounds(0, 0, 200, 200);
    const auto *space = rgba(Float32BitsColorDepthID.id(), QStringLiteral("sRGB-elle-V2-g10.icc"));
    auto image = createCanvasImage(space, bounds, 1.0f);
    KisOpenGLUpdateInfoBuilder builder;
    TextureSet cpuTextures, gpuTextures;
    setUpCanvas(builder, space, {&cpuTextures, &gpuTextures});
    KisGpuCanvasUploader::setGLInteropAvailable(false);
    auto cpu = builder.buildUpdateInfo(bounds, image, true);
    const auto expected = applyAndRead(&m_glContext, cpuTextures, builder, cpu, bounds);
    KisGpuCanvasUploader::setGLInteropAvailable(true);
    auto live = builder.buildUpdateInfo(bounds, image, true);
    QVERIFY(live->tileList.first()->gpuUpload());
    const quint64 liveBytes = KisGpuCanvasUploader::reservedBufferBytes();
    QVERIFY(liveBytes > 0 && liveBytes <= (1 << 20));
    // Keep the first update pending: the second must use CPU pixels and must
    // not overwrite the first update's shared storage or disable interop.
    auto fallback = builder.buildUpdateInfo(bounds, image, true);
    QVERIFY(!fallback->tileList.first()->gpuUpload());
    QCOMPARE(KisGpuCanvasUploader::reservedBufferBytes(), liveBytes);
    QVERIFY(KisGpuCanvasUploader::isEnabled());
    QCOMPARE(applyAndRead(&m_glContext, gpuTextures, builder, fallback, bounds), expected);
    KisGpuCanvasUploader::trimBuffersForTesting();
    QCOMPARE(KisGpuCanvasUploader::reservedBufferBytes(), liveBytes);
    QCOMPARE(applyAndRead(&m_glContext, gpuTextures, builder, live, bounds), expected);
    const quint32 name = live->tileList.first()->gpuUpload()->glBuffer();
    live.clear();
    // A different share group must not delete numerically identical GL names.
    QOpenGLContext unrelated;
    unrelated.setFormat(m_glContext.format());
    QVERIFY(unrelated.create());
    QVERIFY(unrelated.makeCurrent(&m_surface));
    QVERIFY(!QOpenGLContext::areSharing(&unrelated, &m_glContext));
    KisGpuCanvasUploader::trimBuffersForTesting();
    QCOMPARE(KisGpuCanvasUploader::reservedBufferBytes(), liveBytes);
    QVERIFY(m_glContext.makeCurrent(&m_surface));
    KisGpuTileBackend::instance()->context().injectSubmitFailuresForTesting(1);
    KisGpuCanvasUploader::trimBuffersForTesting();
    QCOMPARE(KisGpuCanvasUploader::reservedBufferBytes(), liveBytes);
    QVERIFY(m_glContext.functions()->glIsBuffer(name));
    KisGpuCanvasUploader::trimBuffersForTesting();
    QCOMPARE(KisGpuCanvasUploader::reservedBufferBytes(), quint64(0));
    QVERIFY(!m_glContext.functions()->glIsBuffer(name));
    // Drop an update before GL ever imports it; drain its Vulkan signal too.
    auto dropped = builder.buildUpdateInfo(bounds, image, true);
    QVERIFY(dropped->tileList.first()->gpuUpload());
    dropped.clear();
    KisGpuCanvasUploader::trimBuffersForTesting();
    QCOMPARE(KisGpuCanvasUploader::reservedBufferBytes(), quint64(0));
    // A failed import is retired only after its CPU recovery no longer needs it.
    auto failed = builder.buildUpdateInfo(bounds, image, true);
    QVERIFY(failed->tileList.first()->gpuUpload());
    KisGpuGLSharedBuffer::injectGLImportFailuresForTesting(1);
    QVERIFY(!KisGpuCanvasUploader::acquire(failed->tileList));
    QVERIFY(KisGpuCanvasUploader::readBackFailedUploads(failed->tileList));
    QCOMPARE(applyAndRead(&m_glContext, gpuTextures, builder, failed, bounds), expected);
    failed.clear();
    KisGpuCanvasUploader::trimBuffersForTesting();
    QCOMPARE(KisGpuCanvasUploader::reservedBufferBytes(), quint64(0));
}

void KisGpuCanvasUploadTest::benchmarkCanvasUpdate()
{
    if (!m_skipReason.isEmpty()) {
        QSKIP(qPrintable(m_skipReason));
    }
    const int size = 4096;
    const QRect bounds(0, 0, size, size);
    const KoColorSpace *imageSpace = rgba(Float32BitsColorDepthID.id(), QStringLiteral("sRGB-elle-V2-g10.icc"));
    const KoColorSpace *displaySpace = rgba(Float32BitsColorDepthID.id(), QStringLiteral("sRGB-elle-V2-srgbtrc.icc"));
    QVERIFY(imageSpace && displaySpace);
    KisImageSP image = new KisImage(nullptr, size, size, imageSpace, "canvas benchmark");
    for (int i = 0; i < 8; i++) {
        KisPaintLayerSP layer = new KisPaintLayer(image, QStringLiteral("layer %1").arg(i), 220);
        fillRandom(layer->paintDevice(), bounds, 10 + i, 1.0f);
        image->addNode(layer, image->root());
    }
    image->refreshGraphAsync();
    image->waitForDone();

    KisOpenGLUpdateInfoBuilder builder;
    setUpCanvas(builder, displaySpace, {});
    const bool oldInterop = KisGpuCanvasUploader::isEnabled();
    const auto restoreInterop = qScopeGuard([&]() {
        KisGpuCanvasUploader::setGLInteropAvailable(oldInterop);
    });
    const int repeats = qBound(1,
                               qEnvironmentVariableIsSet("KRITA_GPU_BENCH_REPEATS")
                                   ? qEnvironmentVariableIntValue("KRITA_GPU_BENCH_REPEATS")
                                   : 3,
                               20);
    auto &context = KisGpuTileBackend::existingInstance()->context();
    for (const QRect &rect : {bounds, QRect(2000, 2000, 256, 256)}) {
        std::vector<double> samples[2];
        TextureContents finalTextures[2];
        for (int iteration = 0; iteration <= repeats; ++iteration) {
            for (int order = 0; order < 2; ++order) {
                const bool gpu = (iteration % 2) ? !order : order;
                // Both paths start with fresh GPU-authoritative projection pixels.
                // Projection work is outside the canvas preparation interval.
                const quint64 beforeProjection = KisGpuMergeBatch::gpuCompositeCount();
                image->refreshGraphAsync();
                image->waitForDone();
                context.waitIdle();
                QVERIFY(KisGpuMergeBatch::gpuCompositeCount() > beforeProjection);
                KisGpuCanvasUploader::setGLInteropAvailable(gpu);
                const quint64 beforeUpload = KisGpuCanvasUploader::uploadCount();
                QElapsedTimer timer;
                timer.start();
                KisOpenGLUpdateInfoSP update = builder.buildUpdateInfo(rect, image, true);
                if (gpu) {
                    context.waitIdle();
                }
                const double elapsed = timer.nsecsElapsed() / 1.0e6;
                QVERIFY(update && !update->tileList.isEmpty());
                QCOMPARE(KisGpuCanvasUploader::uploadCount() - beforeUpload, gpu ? quint64(1) : quint64(0));
                for (const auto &tile : update->tileList) {
                    QCOMPARE(bool(tile->gpuUpload()), gpu);
                }
                if (iteration) {
                    samples[gpu].push_back(elapsed);
                }
                if (iteration == repeats) {
                    // Verify this exact workload, including margins and GL import,
                    // without charging texture copies/readback to preparation.
                    TextureSet textures;
                    setUpCanvas(builder, displaySpace, {&textures});
                    finalTextures[gpu] = applyAndRead(&m_glContext, textures, builder, update, bounds);
                }
            }
        }
        QCOMPARE(finalTextures[0].size(), finalTextures[1].size());
        QString where;
        const float worst = maxTextureDifference(finalTextures[0], finalTextures[1], &where);
        QVERIFY2(worst <= 5e-4f, qPrintable(QStringLiteral("max difference %1 at %2").arg(worst).arg(where)));
        for (int gpu = 0; gpu < 2; ++gpu) {
            auto &values = samples[gpu];
            std::sort(values.begin(), values.end());
            const double median = (values[(values.size() - 1) / 2] + values[values.size() / 2]) / 2;
            qInfo() << "Canvas preparation" << rect.size() << (gpu ? "GPU" : "CPU") << "samples" << repeats
                    << "median ms" << median << "min" << values.front() << "max" << values.back()
                    << "GPU completion included; projection, GL copies and verification excluded; max error" << worst;
        }
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    QSurfaceFormat::setDefaultFormat(format);
    SIMPLE_MAIN_IMPL(KisGpuCanvasUploadTest)
}

#include "KisGpuCanvasUploadTest.moc"
