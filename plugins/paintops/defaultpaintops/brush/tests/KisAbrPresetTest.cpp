/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QTest>
#include <QtMath>

#include "KisBrushTestMain.h"

#include <KisAbrStorage.h>
#include <KisAsynchronousStrokeUpdateHelper.h>
#include <KisLocalStrokeResources.h>
#include <KisSizeOptionData.h>
#include <KisStandardOptionData.h>
#include <KoCanvasResourceProvider.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOpRegistry.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_settings.h>
#include <kis_abr_brush_collection.h>
#include <kis_image.h>
#include <kis_paint_device.h>
#include <kis_paint_layer.h>
#include <kis_resources_snapshot.h>
#include <kis_undo_stores.h>
#include <strokes/KisFreehandStrokeInfo.h>
#include <strokes/freehand_stroke.h>

/**
 * Phase 4 of docs/agent/abr-import-plan.md: an ABR file's brush presets
 * become Pixel Brush presets that paint with the file's tips.
 */
class KisAbrPresetTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testPresetsFromBundledFile();
    void testPresetsPaint();
    void testReload();
};

namespace
{
QString bundledFile()
{
    return QString(FILES_DATA_DIR) + "/../../../../../../libs/brush/tests/data/brushes_by_mar_ka_d338ela.abr";
}

/// the preset with the file's tips and patterns as its resources, as they
/// would be in the resource database
KisPaintOpPresetSP withFileResources(KisPaintOpPresetSP preset, const KisAbrBrushCollection &collection)
{
    QList<KoResourceSP> resources;
    for (KisAbrBrushSP tip : collection.brushes()) {
        resources << tip;
    }
    for (KoPatternSP pattern : collection.patternsMap()->values()) {
        resources << pattern;
    }
    KisPaintOpPresetSP copy = preset->clone().dynamicCast<KisPaintOpPreset>();
    copy->setResourcesInterface(QSharedPointer<KisLocalStrokeResources>::create(resources));
    return copy;
}

QImage paintedImage(KisPaintOpPresetSP preset);

int paintedPixels(KisPaintOpPresetSP preset)
{
    const QImage result = paintedImage(preset);
    int painted = 0;
    for (int y = 0; y < result.height(); y++) {
        for (int x = 0; x < result.width(); x++) {
            painted += qAlpha(result.pixel(x, y)) > 20;
        }
    }
    return painted;
}

QImage paintedImage(KisPaintOpPresetSP preset)
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisImageSP image = new KisImage(new KisSurrogateUndoStore(), 300, 200, cs, "abr");
    KisPaintLayerSP layer = new KisPaintLayer(image, "layer", OPACITY_OPAQUE_U8, cs);
    image->addNode(layer, image->root());

    const KoColor color(Qt::black, cs);
    KoCanvasResourceProvider provider;
    provider.setResource(KoCanvasResource::ForegroundColor, QVariant::fromValue(color));
    provider.setResource(KoCanvasResource::BackgroundColor, QVariant::fromValue(KoColor(Qt::white, cs)));
    provider.setResource(KoCanvasResource::CurrentPaintOpPreset, QVariant::fromValue(preset));
    provider.setResource(KoCanvasResource::Opacity, 1.0);
    provider.setResource(KoCanvasResource::CurrentCompositeOp, COMPOSITE_OVER);
    provider.setResource(KoCanvasResource::CurrentEffectiveCompositeOp, COMPOSITE_OVER);
    provider.setResource(KoCanvasResource::EffectiveZoom, 1.0);
    provider.setResource(KoCanvasResource::HdrExposure, 0.0);
    provider.setResource(KoCanvasResource::EraserMode, false);
    provider.setResource(KoCanvasResource::GlobalAlphaLock, false);
    provider.setResource(KoCanvasResource::MirrorHorizontal, false);
    provider.setResource(KoCanvasResource::MirrorVertical, false);
    provider.setResource(KoCanvasResource::EffectiveLodAvailability, false);
    provider.setResource(KoCanvasResource::Size, preset->settings()->paintOpSize());

    KisResourcesSnapshotSP resources = new KisResourcesSnapshot(image, layer, &provider, nullptr, {}, preset);
    resources->setOpacity(1.0);
    resources->setFGColorOverride(color);
    FreehandStrokeStrategy *stroke =
        new FreehandStrokeStrategy(resources, new KisFreehandStrokeInfo(), kundo2_noi18n("abr stroke"));
    KisStrokeId id = image->startStroke(stroke);
    KisPaintInformation previous(QPointF(40, 100), 0.5);
    for (int i = 1; i <= 40; i++) {
        KisPaintInformation next(QPointF(40 + i * 5, 100 + 30 * qSin(i / 6.0)), 0.3 + 0.6 * qSin(i / 40.0 * M_PI));
        next.setCurrentTime(i * 10);
        image->addJob(id, new FreehandStrokeStrategy::Data(0, previous, next));
        previous = next;
    }
    image->addJob(id, new KisAsynchronousStrokeUpdateHelper::UpdateData(true));
    image->endStroke(id);
    image->waitForDone();

    return layer->paintDevice()->convertToQImage(nullptr, QRect(0, 0, 300, 200));
}
} // namespace

void KisAbrPresetTest::testPresetsFromBundledFile()
{
    KisAbrBrushCollection collection(bundledFile());
    QVERIFY(collection.load());
    const auto presets = collection.presetsMap();
    qInfo() << "presets:" << presets->size();
    QVERIFY(presets->size() >= 30);

    int pressureSize = 0;
    int abrTips = 0;
    int autoTips = 0;
    int masked = 0;
    for (auto it = presets->constBegin(); it != presets->constEnd(); ++it) {
        KisPaintOpPresetSP preset = it.value();
        QVERIFY(it.key().startsWith("brushes_by_mar_ka_d338ela_preset_"));
        QVERIFY(it.key().endsWith(".kpp"));
        QVERIFY(!preset->name().isEmpty());
        QVERIFY(preset->valid());
        QVERIFY(!preset->image().isNull());
        KisPaintOpSettingsSP settings = preset->settings();
        QCOMPARE(settings->getString("paintop"), QString("paintbrush"));
        const QString tip = settings->getString("brush_definition");
        abrTips += tip.contains("abr_brush");
        autoTips += tip.contains("auto_brush");
        masked += settings->getBool("MaskingBrush/Enabled");

        // read back with the Pixel Brush's own option data
        KisSizeOptionData size;
        QVERIFY(size.read(settings.data()));
        if (size.isChecked && size.sensorStruct().sensorPressure.isActive) {
            pressureSize++;
        }
        KisOpacityOptionData opacity;
        QVERIFY(opacity.read(settings.data()));
        QVERIFY(opacity.strengthValue > 0.0 && opacity.strengthValue <= 1.0);
    }
    qInfo() << "ABR tips" << abrTips << "auto tips" << autoTips << "pressure size" << pressureSize << "masked"
            << masked;
    QVERIFY(abrTips > 0);
    QVERIFY(pressureSize > 0);
}

void KisAbrPresetTest::testPresetsPaint()
{
    KisAbrBrushCollection collection(bundledFile());
    QVERIFY(collection.load());
    int checked = 0;
    for (KisPaintOpPresetSP preset : collection.presetsMap()->values()) {
        const int painted = paintedPixels(withFileResources(preset, collection));
        if (painted <= 0) {
            qWarning() << "painted nothing:" << preset->name() << preset->settings()->getString("brush_definition");
        }
        QVERIFY2(painted > 0, qPrintable(preset->name()));
        if (++checked == 12) {
            break;
        }
    }
}

/// The storage hands out copies, so that editing a preset leaves the file's
/// version, and reloading the preset returns to that version
void KisAbrPresetTest::testReload()
{
    KisAbrStorage storage(bundledFile());
    auto presets = storage.resources(ResourceType::PaintOpPresets);
    QVERIFY(presets->hasNext());
    presets->next();
    const QString url = presets->url();

    KisPaintOpPresetSP edited = storage.resource(url).dynamicCast<KisPaintOpPreset>();
    QVERIFY(edited);
    KisPaintOpPresetSP again = storage.resource(url).dynamicCast<KisPaintOpPreset>();
    QVERIFY(edited.data() != again.data());
    const qreal opacity = edited->settings()->paintOpOpacity();
    const QString name = edited->name();

    edited->settings()->setPaintOpOpacity(opacity * 0.5);
    edited->setName(QStringLiteral("edited"));
    QCOMPARE(storage.resource(url)->name(), name);

    QVERIFY(storage.loadVersionedResource(edited));
    QCOMPARE(edited->name(), name);
    QCOMPARE(edited->settings()->paintOpOpacity(), opacity);
}

SOLSTICE_BRUSH_TEST_MAIN_WITH_BUNDLES(KisAbrPresetTest, QStringLiteral("Krita_4_Default_Resources.bundle"))

#include "KisAbrPresetTest.moc"
