/*
 *  SPDX-FileCopyrightText: 2026 Solstice contributors
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QSignalSpy>
#include <QTest>

#include <simpletest.h>
#include <testutil.h>

#include <KisGlobalResourcesInterface.h>
#include <KisPaintOpPresetUpdateProxy.h>
#include <KoCompositeOpRegistry.h>
#include <brushengine/kis_locked_properties_server.h>
#include <brushengine/kis_locked_properties_proxy.h>
#include <brushengine/kis_paintop_config_widget.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_registry.h>
#include <brushengine/kis_paintop_settings.h>
#include <brushengine/kis_uniform_paintop_property.h>
#include <kis_simple_paintop_factory.h>

#include <KisAirbrushOptionWidget.h>
#include <KisCompositeOpOptionWidget.h>
#include <KisPaintOpOptionWidgetUtils.h>
#include <KisSizeOptionWidget.h>
#include <KisStandardOptionData.h>
#include <kis_paintop_settings_widget.h>

#include "../KisBrushSizeOptionWidget.h"
#include "../KisDeformOptionWidget.h"
#include "../kis_deform_paintop.h"
#include "../kis_deform_paintop_settings.h"
#include "../kis_deform_paintop_settings_widget.h"

using PropertiesMap = QMap<QString, QVariant>;

namespace
{
const QString DeformAmountKey = QStringLiteral("Deform/deformAmount");
const QString BrushDiameterKey = QStringLiteral("Brush/diameter");

KisPaintOpPresetSP loadPreset(const QString &fileName)
{
    KisPaintOpPresetSP preset(new KisPaintOpPreset(TestUtil::fetchDataFileLazy(fileName)));
    const bool loaded = preset->load(KisGlobalResourcesInterface::instance());
    if (!loaded) {
        return KisPaintOpPresetSP();
    }
    // create the proxy so that settings changes are delivered
    preset->updateProxy();
    return preset;
}

KisPaintOpPresetSP defaultPreset()
{
    KisPaintOpPresetSP preset = KisPaintOpRegistry::instance()->defaultPreset(KoID("deformbrush"),
                                                                              KisGlobalResourcesInterface::instance());
    preset->updateProxy();
    return preset;
}

/**
 * The Deform settings widget as it was before the shared options model:
 * every option widget owns its state.
 */
class LegacyDeformSettingsWidget : public KisPaintOpSettingsWidget
{
public:
    LegacyDeformSettingsWidget()
        : KisPaintOpSettingsWidget(nullptr)
    {
        namespace kpowu = KisPaintOpOptionWidgetUtils;

        addPaintOpOption(kpowu::createOptionWidget<KisBrushSizeOptionWidget>());
        addPaintOpOption(kpowu::createOptionWidgetWithLodLimitations<KisDeformOptionWidget>());
        addPaintOpOption(kpowu::createOptionWidget<KisCompositeOpOptionWidget>());
        addPaintOpOption(kpowu::createOpacityOptionWidget());
        addPaintOpOption(kpowu::createOptionWidget<KisSizeOptionWidget>());
        addPaintOpOption(kpowu::createRotationOptionWidget());
        addPaintOpOption(kpowu::createOptionWidget<KisAirbrushOptionWidget>());
        addPaintOpOption(kpowu::createRateOptionWidget());
    }

    KisPropertiesConfigurationSP configuration() const override
    {
        KisPropertiesConfigurationSP config = new KisDeformPaintOpSettings(resourcesInterface());
        config->setProperty("paintop", "deformbrush");
        writeConfiguration(config);
        return config;
    }
};

KisPaintOpConfigWidget *createConfigWidget()
{
    return KisPaintOpRegistry::instance()
        ->get("deformbrush")
        ->createConfigWidget(nullptr, KisGlobalResourcesInterface::instance(), KoCanvasResourcesInterfaceSP());
}

/**
 * What KisPaintopBox::slotGuiChangedCurrentPreset() does to the preset
 * after the legacy Brush Editor has read it: clear everything and let the
 * widget write all options again.
 */
PropertiesMap legacyFullRewrite(KisPaintOpPresetSP preset)
{
    QScopedPointer<LegacyDeformSettingsWidget> widget(new LegacyDeformSettingsWidget());
    widget->setResourcesInterface(KisGlobalResourcesInterface::instance());
    widget->setConfigurationSafe(preset->settings());

    KisPaintOpSettingsSP settings = preset->settings();
    {
        KisPaintOpPreset::UpdatedPostponer postponer(preset);
        settings->resetSettings();
        widget->writeConfigurationSafe(settings);
    }
    return settings->getProperties();
}

QStringList deformPresetFiles()
{
    return {QStringLiteral("deform-default.kpp"),
            QStringLiteral("deformbrush.kpp"),
            QStringLiteral("v_Distort_Grow.kpp"),
            QStringLiteral("v_Distort_Move.kpp"),
            QStringLiteral("v_Distort_Shrink.kpp"),
            QStringLiteral("Move_tool.kpp")};
}

bool hasLegacyCurveKeys(const PropertiesMap &properties)
{
    Q_FOREACH (const QString &key, properties.keys()) {
        if (key.startsWith(QLatin1String("Custom")) || key.startsWith(QLatin1String("CurveOpacity"))
            || key.startsWith(QLatin1String("CurveSize")) || key.startsWith(QLatin1String("CurveRotation"))) {
            return true;
        }
    }
    return false;
}
} // namespace

class KisPaintOpOptionsModelTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanup();

    // Current behavior that the shared model must keep
    void testNotificationsOutsidePostponer();
    void testNotificationsInsidePostponer();
    void testDirtyOnlyOnValueChange();
    void testRemovePropertyIsSilent();
    void testLockedPropertyRead();
    void testUniformPropertyWrite();
    void testLegacyFullRewrite_data();
    void testLegacyFullRewrite();
};

void KisPaintOpOptionsModelTest::initTestCase()
{
    if (!KisPaintOpRegistry::instance()->get("deformbrush")) {
        KisPaintOpRegistry::instance()->add(
            new KisSimplePaintOpFactory<KisDeformPaintOp, KisDeformPaintOpSettings, KisDeformPaintOpSettingsWidget>(
                "deformbrush",
                "Deform",
                KisPaintOpFactory::categoryStable(),
                "krita-deform.png",
                QString(),
                QStringList(COMPOSITE_COPY),
                16));
    }
}

void KisPaintOpOptionsModelTest::cleanup()
{
    KisLockedPropertiesServer::instance()->lockedProperties()->lockedProperties()->clearProperties();
}

void KisPaintOpOptionsModelTest::testNotificationsOutsidePostponer()
{
    KisPaintOpPresetSP preset = defaultPreset();
    KisPaintOpPresetUpdateProxy *proxy = preset->updateProxy();

    QSignalSpy early(proxy, SIGNAL(sigSettingsChangedUncompressedEarlyWarning()));
    QSignalSpy uncompressed(proxy, SIGNAL(sigSettingsChangedUncompressed()));
    QSignalSpy compressed(proxy, SIGNAL(sigSettingsChanged()));

    preset->settings()->setProperty(DeformAmountKey, 0.31);
    QCOMPARE(early.count(), 1);
    QCOMPARE(uncompressed.count(), 1);
    // the compressor delivers the first notification synchronously
    QCOMPARE(compressed.count(), 1);

    preset->settings()->setProperty(DeformAmountKey, 0.32);
    QCOMPARE(early.count(), 2);
    QCOMPARE(uncompressed.count(), 2);
    QCOMPARE(compressed.count(), 1);

    QTRY_COMPARE_WITH_TIMEOUT(compressed.count(), 2, 1000);
}

void KisPaintOpOptionsModelTest::testNotificationsInsidePostponer()
{
    KisPaintOpPresetSP preset = defaultPreset();
    KisPaintOpPresetUpdateProxy *proxy = preset->updateProxy();

    QSignalSpy uncompressed(proxy, SIGNAL(sigSettingsChangedUncompressed()));
    QSignalSpy compressed(proxy, SIGNAL(sigSettingsChanged()));

    {
        KisPaintOpPreset::UpdatedPostponer postponer(preset);
        preset->settings()->setProperty(DeformAmountKey, 0.41);
        preset->settings()->setProperty(BrushDiameterKey, 33.0);
        QCOMPARE(uncompressed.count(), 0);
        QCOMPARE(compressed.count(), 0);
    }

    QCOMPARE(uncompressed.count(), 1);
    QCOMPARE(compressed.count(), 1);
}

void KisPaintOpOptionsModelTest::testDirtyOnlyOnValueChange()
{
    KisPaintOpPresetSP preset = defaultPreset();
    preset->settings()->setProperty(DeformAmountKey, 0.5);
    preset->setDirty(false);

    QSignalSpy uncompressed(preset->updateProxy(), SIGNAL(sigSettingsChangedUncompressed()));

    preset->settings()->setProperty(DeformAmountKey, 0.5);
    QVERIFY(!preset->isDirty());
    // a notification is sent even when nothing changed
    QCOMPARE(uncompressed.count(), 1);

    preset->settings()->setProperty(DeformAmountKey, 0.6);
    QVERIFY(preset->isDirty());
}

void KisPaintOpOptionsModelTest::testRemovePropertyIsSilent()
{
    KisPaintOpPresetSP preset = defaultPreset();
    preset->settings()->setProperty(QStringLiteral("SomeKey"), 1);
    preset->setDirty(false);

    QSignalSpy uncompressed(preset->updateProxy(), SIGNAL(sigSettingsChangedUncompressed()));

    preset->settings()->removeProperty(QStringLiteral("SomeKey"));
    QVERIFY(!preset->settings()->hasProperty(QStringLiteral("SomeKey")));
    QCOMPARE(uncompressed.count(), 0);
    QVERIFY(!preset->isDirty());
}

void KisPaintOpOptionsModelTest::testLockedPropertyRead()
{
    KisPaintOpPresetSP preset = defaultPreset();
    KisPaintOpSettingsSP settings = preset->settings();
    settings->setProperty(DeformAmountKey, 0.2);
    preset->setDirty(false);

    KisPropertiesConfigurationSP locked = new KisPropertiesConfiguration();
    locked->setProperty(DeformAmountKey, 0.9);
    KisLockedPropertiesServer::instance()->addToLockedProperties(locked);

    KisLockedPropertiesProxySP proxy = KisLockedPropertiesServer::instance()->createLockedPropertiesProxy(settings);

    // reading a locked key applies the locked value and keeps the original
    QCOMPARE(proxy->getProperty(DeformAmountKey).toReal(), 0.9);
    QCOMPARE(settings->getProperty(DeformAmountKey).toReal(), 0.9);
    QCOMPARE(settings->getProperty(DeformAmountKey + "_previous").toReal(), 0.2);
    QVERIFY(!preset->isDirty());

    // after unlocking, the next read restores the original value
    KisLockedPropertiesServer::instance()->removeFromLockedProperties(locked);
    QCOMPARE(proxy->getProperty(DeformAmountKey).toReal(), 0.2);
    QVERIFY(!settings->hasProperty(DeformAmountKey + "_previous"));
    QVERIFY(!preset->isDirty());
}

void KisPaintOpOptionsModelTest::testUniformPropertyWrite()
{
    KisPaintOpPresetSP preset = defaultPreset();
    KisUniformPaintOpPropertySP amount;
    Q_FOREACH (KisUniformPaintOpPropertySP property, preset->uniformProperties()) {
        if (property->id() == QLatin1String("deform_amount")) {
            amount = property;
        }
    }
    QVERIFY(amount);

    QSignalSpy uncompressed(preset->updateProxy(), SIGNAL(sigSettingsChangedUncompressed()));

    amount->setValue(0.77);
    QCOMPARE(preset->settings()->getProperty(DeformAmountKey).toReal(), 0.77);
    // the property rewrites the whole option without a postponer
    QVERIFY(uncompressed.count() >= 1);
}

void KisPaintOpOptionsModelTest::testLegacyFullRewrite_data()
{
    QTest::addColumn<QString>("fileName");
    Q_FOREACH (const QString &fileName, deformPresetFiles()) {
        QTest::newRow(fileName.toLatin1()) << fileName;
    }
}

void KisPaintOpOptionsModelTest::testLegacyFullRewrite()
{
    QFETCH(QString, fileName);

    KisPaintOpPresetSP preset = loadPreset(fileName);
    QVERIFY(preset);
    preset->setDirty(false);

    const PropertiesMap rewritten = legacyFullRewrite(preset);

    // legacy curve keys are dropped, option keys are written
    QVERIFY(!hasLegacyCurveKeys(rewritten));
    QVERIFY(rewritten.contains(DeformAmountKey));
    QVERIFY(rewritten.contains(BrushDiameterKey));
    QCOMPARE(rewritten.value(QStringLiteral("paintop")).toString(), QStringLiteral("deformbrush"));
    // the reset re-adds "paintop", so the preset always becomes dirty
    QVERIFY(preset->isDirty());
}

KISTEST_MAIN(KisPaintOpOptionsModelTest)
#include "KisPaintOpOptionsModelTest.moc"
