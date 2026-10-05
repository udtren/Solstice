/*
 *  SPDX-FileCopyrightText: 2026 Solstice contributors
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QSignalSpy>
#include <QTest>

#include <kistest.h>
#include <testutil.h>

#include <KisGlobalResourcesInterface.h>
#include <KisPaintOpPresetUpdateProxy.h>
#include <KoCompositeOpRegistry.h>
#include <brushengine/kis_locked_properties_proxy.h>
#include <brushengine/kis_locked_properties_server.h>
#include <brushengine/kis_paintop_config_widget.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_registry.h>
#include <brushengine/kis_paintop_settings.h>
#include <brushengine/kis_uniform_paintop_property.h>
#include <kis_simple_paintop_factory.h>

#include <KisAirbrushOptionWidget.h>
#include <KisCompositeOpOptionWidget.h>
#include <KisPaintOpOptionWidgetUtils.h>
#include <KisPaintOpOptionsModel.h>
#include <KisSizeOptionWidget.h>
#include <KisStandardOptionData.h>
#include <boost/operators.hpp>
#include <kis_paintop_settings_widget.h>

#include "../KisBrushSizeOptionData.h"
#include "../KisBrushSizeOptionWidget.h"
#include "../KisDeformOptionData.h"
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
    KisPaintOpPresetSP preset =
        KisPaintOpRegistry::instance()->defaultPreset(KoID("deformbrush"), KisGlobalResourcesInterface::instance());
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

namespace
{
const QStringList EditorOwnedKeys = {QStringLiteral("lodUserAllowed"), QStringLiteral("lodSizeThreshold")};

PropertiesMap withoutKeys(PropertiesMap properties, const QStringList &keys)
{
    Q_FOREACH (const QString &key, keys) {
        properties.remove(key);
    }
    return properties;
}

QSet<QString> changedKeys(const PropertiesMap &before, const PropertiesMap &after)
{
    QSet<QString> result;
    Q_FOREACH (const QString &key, before.keys() + after.keys()) {
        if (before.value(key) != after.value(key) || before.contains(key) != after.contains(key)) {
            result.insert(key);
        }
    }
    return result;
}

template<typename Data>
KisPaintOpOptionState<Data> *optionState(KisPaintOpOptionsModel *model, const QString &id)
{
    return dynamic_cast<KisPaintOpOptionState<Data> *>(model->option(id));
}

struct ModelFixture {
    QScopedPointer<KisPaintOpConfigWidget> widget;
    KisPaintOpOptionsModel *model = nullptr;

    ModelFixture()
        : widget(createConfigWidget())
    {
        KisPaintOpSettingsWidget *settingsWidget = dynamic_cast<KisPaintOpSettingsWidget *>(widget.data());
        model = settingsWidget ? settingsWidget->optionsModel() : nullptr;
        if (model) {
            model->setPreservedKeys(EditorOwnedKeys);
        }
    }

    void setDeformAmount(qreal value)
    {
        auto *state = optionState<KisDeformOptionData>(model, QStringLiteral("Deform"));
        KisDeformOptionData data = state->data();
        data.deformAmount = value;
        state->cursor().set(data);
    }

    void setBrushDiameter(qreal value)
    {
        auto *state = optionState<KisBrushSizeOptionData>(model, QStringLiteral("BrushSize"));
        KisBrushSizeOptionData data = state->data();
        data.brushDiameter = value;
        state->cursor().set(data);
    }
};

/**
 * An option that writes nothing while disabled, like KisTextureOptionData.
 */
struct ConditionalTestData : public boost::equality_comparable<ConditionalTestData> {
    bool enabled = false;
    int value = 1;

    bool read(const KisPropertiesConfiguration *setting)
    {
        enabled = setting->getBool(QStringLiteral("Test/enabled"), false);
        value = setting->getInt(QStringLiteral("Test/value"), 1);
        return true;
    }

    void write(KisPropertiesConfiguration *setting) const
    {
        if (!enabled) {
            return;
        }
        setting->setProperty(QStringLiteral("Test/enabled"), true);
        setting->setProperty(QStringLiteral("Test/value"), value);
    }

    friend bool operator==(const ConditionalTestData &lhs, const ConditionalTestData &rhs)
    {
        return lhs.enabled == rhs.enabled && lhs.value == rhs.value;
    }
};
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

    // Changed-key delivery
    void testChangedKeys();
    void testChangedKeysInsidePostponer();
    void testChangedKeysOnReset();
    void testChangedKeysOnSetSettings();

    // The shared options model (Deform pilot)
    void testDeformUsesOptionsModel();
    void testModelFirstWriteMatchesLegacy_data();
    void testModelFirstWriteMatchesLegacy();
    void testModelWritesOnlyChangedOption();
    void testModelIngestsExternalChanges();
    void testEditorWriteKeepsPresetClean();
    void testModelFullRewriteAfterReplace();
    void testModelRemovesKeysNoLongerWritten();
    void testModelKeepsPreviousOfLockedKeys();
    void testConfigurationWritesAllOptions();
};

void KisPaintOpOptionsModelTest::initTestCase()
{
    qRegisterMetaType<QSet<QString>>("QSet<QString>");

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
    // drop the locks the tests added
    KisPropertiesConfigurationSP locked = new KisPropertiesConfiguration();
    locked->setProperty(DeformAmountKey, 0.0);
    KisLockedPropertiesServer::instance()->removeFromLockedProperties(locked);
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

void KisPaintOpOptionsModelTest::testChangedKeys()
{
    KisPaintOpPresetSP preset = defaultPreset();
    preset->settings()->setProperty(DeformAmountKey, 0.5);

    QSignalSpy keysSpy(preset->updateProxy(), SIGNAL(sigSettingsKeysChanged(QSet<QString>, bool)));

    // an unchanged value records nothing
    preset->settings()->setProperty(DeformAmountKey, 0.5);
    QCOMPARE(keysSpy.count(), 0);

    preset->settings()->setProperty(DeformAmountKey, 0.6);
    QCOMPARE(keysSpy.count(), 1);
    QCOMPARE(keysSpy.at(0).at(0).value<QSet<QString>>(), QSet<QString>({DeformAmountKey}));
    QCOMPARE(keysSpy.at(0).at(1).toBool(), false);

    // a removal is recorded silently and delivered with the next change
    preset->settings()->removeProperty(DeformAmountKey);
    QCOMPARE(keysSpy.count(), 1);
    preset->settings()->setProperty(BrushDiameterKey, 21.0);
    QCOMPARE(keysSpy.count(), 2);
    QCOMPARE(keysSpy.at(1).at(0).value<QSet<QString>>(), QSet<QString>({DeformAmountKey, BrushDiameterKey}));
}

void KisPaintOpOptionsModelTest::testChangedKeysInsidePostponer()
{
    KisPaintOpPresetSP preset = defaultPreset();
    QSignalSpy keysSpy(preset->updateProxy(), SIGNAL(sigSettingsKeysChanged(QSet<QString>, bool)));
    QSignalSpy uncompressed(preset->updateProxy(), SIGNAL(sigSettingsChangedUncompressed()));

    {
        KisPaintOpPreset::UpdatedPostponer postponer(preset);
        preset->settings()->setProperty(DeformAmountKey, 0.71);
        preset->settings()->setProperty(BrushDiameterKey, 31.0);
        QCOMPARE(keysSpy.count(), 0);
    }

    QCOMPARE(keysSpy.count(), 1);
    QCOMPARE(uncompressed.count(), 1);
    QCOMPARE(keysSpy.at(0).at(0).value<QSet<QString>>(), QSet<QString>({DeformAmountKey, BrushDiameterKey}));
}

void KisPaintOpOptionsModelTest::testChangedKeysOnReset()
{
    KisPaintOpPresetSP preset = defaultPreset();
    QSignalSpy keysSpy(preset->updateProxy(), SIGNAL(sigSettingsKeysChanged(QSet<QString>, bool)));

    {
        KisPaintOpPreset::UpdatedPostponer postponer(preset);
        preset->settings()->resetSettings();
    }

    QCOMPARE(keysSpy.count(), 1);
    QCOMPARE(keysSpy.at(0).at(1).toBool(), true);
}

void KisPaintOpOptionsModelTest::testChangedKeysOnSetSettings()
{
    KisPaintOpPresetSP preset = defaultPreset();
    QSignalSpy keysSpy(preset->updateProxy(), SIGNAL(sigSettingsKeysChanged(QSet<QString>, bool)));

    preset->setSettings(preset->settings()->clone());

    QCOMPARE(keysSpy.count(), 1);
    QCOMPARE(keysSpy.at(0).at(1).toBool(), true);
}

void KisPaintOpOptionsModelTest::testDeformUsesOptionsModel()
{
    ModelFixture fixture;
    QVERIFY(fixture.model);
    QCOMPARE(fixture.model->options().size(), 8);
    QVERIFY(optionState<KisDeformOptionData>(fixture.model, QStringLiteral("Deform")));
    QVERIFY(optionState<KisBrushSizeOptionData>(fixture.model, QStringLiteral("BrushSize")));
}

void KisPaintOpOptionsModelTest::testModelFirstWriteMatchesLegacy_data()
{
    testLegacyFullRewrite_data();
}

void KisPaintOpOptionsModelTest::testModelFirstWriteMatchesLegacy()
{
    QFETCH(QString, fileName);

    // legacy: the user edits the amount, then the editor rewrites everything
    KisPaintOpPresetSP legacyPreset = loadPreset(fileName);
    QVERIFY(legacyPreset);
    legacyPreset->settings()->setProperty(DeformAmountKey, 0.123);
    const PropertiesMap expected = withoutKeys(legacyFullRewrite(legacyPreset), EditorOwnedKeys);

    // model: the same edit, made through the option state
    KisPaintOpPresetSP preset = loadPreset(fileName);
    QVERIFY(preset);
    preset->setDirty(false);

    ModelFixture fixture;
    fixture.model->attachPreset(preset);
    QVERIFY(fixture.model->needsFullRewrite());

    fixture.setDeformAmount(0.123);

    QVERIFY(!fixture.model->needsFullRewrite());
    const PropertiesMap actual = withoutKeys(preset->settings()->getProperties(), EditorOwnedKeys);

    QCOMPARE(QStringList(actual.keys()), QStringList(expected.keys()));
    Q_FOREACH (const QString &key, expected.keys()) {
        QVERIFY2(actual.value(key) == expected.value(key), qPrintable(key));
    }
    QVERIFY(!hasLegacyCurveKeys(actual));
    QVERIFY(preset->isDirty());
}

void KisPaintOpOptionsModelTest::testModelWritesOnlyChangedOption()
{
    KisPaintOpPresetSP preset = loadPreset(QStringLiteral("v_Distort_Move.kpp"));
    QVERIFY(preset);

    ModelFixture fixture;
    fixture.model->attachPreset(preset);
    fixture.setDeformAmount(0.2); // the first write normalizes everything

    // a key no option writes survives later writes
    preset->settings()->setProperty(QStringLiteral("Test/foreign"), 1);
    const PropertiesMap before = preset->settings()->getProperties();

    QSignalSpy uncompressed(preset->updateProxy(), SIGNAL(sigSettingsChangedUncompressed()));
    QSignalSpy written(fixture.model, SIGNAL(sigPresetSettingsWritten()));

    fixture.setBrushDiameter(57.0);

    const PropertiesMap after = preset->settings()->getProperties();
    const QSet<QString> changed = changedKeys(before, after);

    QCOMPARE(changed, QSet<QString>({BrushDiameterKey}));
    QVERIFY(after.contains(QStringLiteral("Test/foreign")));
    QCOMPARE(written.count(), 1);
    // the option is written inside one postponed update
    QCOMPARE(uncompressed.count(), 1);
}

void KisPaintOpOptionsModelTest::testModelIngestsExternalChanges()
{
    KisPaintOpPresetSP preset = defaultPreset();

    ModelFixture fixture;
    fixture.model->attachPreset(preset);

    QSignalSpy written(fixture.model, SIGNAL(sigPresetSettingsWritten()));

    // the On-Canvas Brush Editor writes through a uniform property
    KisUniformPaintOpPropertySP amount;
    Q_FOREACH (KisUniformPaintOpPropertySP property, preset->uniformProperties()) {
        if (property->id() == QLatin1String("deform_amount")) {
            amount = property;
        }
    }
    QVERIFY(amount);
    amount->setValue(0.66);
    QCOMPARE(optionState<KisDeformOptionData>(fixture.model, QStringLiteral("Deform"))->data().deformAmount, 0.66);

    // the toolbar writes a key directly
    preset->settings()->setProperty(BrushDiameterKey, 55.0);
    QCOMPARE(optionState<KisBrushSizeOptionData>(fixture.model, QStringLiteral("BrushSize"))->data().brushDiameter,
             55.0);

    // nothing is written back
    QCOMPARE(written.count(), 0);
    QVERIFY(fixture.model->needsFullRewrite());
}

void KisPaintOpOptionsModelTest::testEditorWriteKeepsPresetClean()
{
    KisPaintOpPresetSP preset = defaultPreset();

    ModelFixture fixture;
    fixture.model->attachPreset(preset);
    fixture.setDeformAmount(0.4);
    preset->setDirty(false);

    // KisPaintopBox::slotGuiChangedCurrentPreset() for an attached model:
    // no reset, and the widget does not rewrite the options
    {
        KisPaintOpPreset::UpdatedPostponer postponer(preset);
        fixture.widget->writeConfigurationSafe(preset->settings());
    }

    QVERIFY(!preset->isDirty());
    QCOMPARE(preset->settings()->getProperty(DeformAmountKey).toReal(), 0.4);
}

void KisPaintOpOptionsModelTest::testModelFullRewriteAfterReplace()
{
    KisPaintOpPresetSP preset = defaultPreset();

    ModelFixture fixture;
    fixture.model->attachPreset(preset);
    fixture.setDeformAmount(0.4);
    QVERIFY(!fixture.model->needsFullRewrite());

    // a reload replaces the settings object, possibly with legacy keys
    KisPaintOpSettingsSP replacement = preset->settings()->clone();
    replacement->setProperty(QStringLiteral("CustomSize"), true);
    replacement->setProperty(DeformAmountKey, 0.8);
    preset->setSettings(replacement);

    QVERIFY(fixture.model->needsFullRewrite());
    QCOMPARE(optionState<KisDeformOptionData>(fixture.model, QStringLiteral("Deform"))->data().deformAmount, 0.8);
    QVERIFY(preset->settings()->hasProperty(QStringLiteral("CustomSize")));

    fixture.setBrushDiameter(44.0);

    QVERIFY(!preset->settings()->hasProperty(QStringLiteral("CustomSize")));
    QCOMPARE(preset->settings()->getProperty(DeformAmountKey).toReal(), 0.8);
    QCOMPARE(preset->settings()->getProperty(BrushDiameterKey).toReal(), 44.0);
}

void KisPaintOpOptionsModelTest::testModelRemovesKeysNoLongerWritten()
{
    KisPaintOpPresetSP preset = defaultPreset();

    KisPaintOpOptionsModel model;
    auto *state = model.addOption(QStringLiteral("Conditional"), ConditionalTestData());
    model.attachPreset(preset);

    ConditionalTestData data;
    data.enabled = true;
    data.value = 5;
    state->cursor().set(data);
    QCOMPARE(preset->settings()->getProperty(QStringLiteral("Test/value")).toInt(), 5);
    QCOMPARE(model.optionKeys(QStringLiteral("Conditional")),
             QSet<QString>({QStringLiteral("Test/enabled"), QStringLiteral("Test/value")}));

    data.enabled = false;
    state->cursor().set(data);
    QVERIFY(!preset->settings()->hasProperty(QStringLiteral("Test/enabled")));
    QVERIFY(!preset->settings()->hasProperty(QStringLiteral("Test/value")));
    QVERIFY(model.optionKeys(QStringLiteral("Conditional")).isEmpty());
}

void KisPaintOpOptionsModelTest::testModelKeepsPreviousOfLockedKeys()
{
    KisPaintOpPresetSP preset = defaultPreset();
    preset->settings()->setProperty(DeformAmountKey, 0.2);

    ModelFixture fixture;
    fixture.model->attachPreset(preset);
    fixture.setBrushDiameter(30.0); // first write

    KisPropertiesConfigurationSP locked = new KisPropertiesConfiguration();
    locked->setProperty(DeformAmountKey, 0.9);
    KisLockedPropertiesServer::instance()->addToLockedProperties(locked);

    // the next read applies the lock and remembers the original value
    KisLockedPropertiesProxySP proxy =
        KisLockedPropertiesServer::instance()->createLockedPropertiesProxy(preset->settings());
    QCOMPARE(proxy->getProperty(DeformAmountKey).toReal(), 0.9);
    QCOMPARE(preset->settings()->getProperty(DeformAmountKey + "_previous").toReal(), 0.2);

    // a write of another option keeps the original value; the legacy full
    // rewrite replaced it with the locked value
    fixture.setBrushDiameter(31.0);
    QCOMPARE(preset->settings()->getProperty(DeformAmountKey + "_previous").toReal(), 0.2);
}

void KisPaintOpOptionsModelTest::testConfigurationWritesAllOptions()
{
    KisPaintOpPresetSP preset = loadPreset(QStringLiteral("v_Distort_Grow.kpp"));
    QVERIFY(preset);

    QScopedPointer<LegacyDeformSettingsWidget> legacy(new LegacyDeformSettingsWidget());
    legacy->setResourcesInterface(KisGlobalResourcesInterface::instance());
    legacy->setConfigurationSafe(preset->settings());

    ModelFixture fixture;
    fixture.widget->setConfigurationSafe(preset->settings());

    // writing to a configuration other than the attached preset's
    KisPaintOpPresetSP expectedTarget = defaultPreset();
    KisPaintOpPresetSP actualTarget = defaultPreset();
    {
        KisPaintOpPreset::UpdatedPostponer p1(expectedTarget);
        KisPaintOpPreset::UpdatedPostponer p2(actualTarget);
        legacy->writeConfigurationSafe(expectedTarget->settings());
        fixture.widget->writeConfigurationSafe(actualTarget->settings());
    }

    QCOMPARE(actualTarget->settings()->getProperties(), expectedTarget->settings()->getProperties());
}

KISTEST_MAIN(KisPaintOpOptionsModelTest)
#include "KisPaintOpOptionsModelTest.moc"
