/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTest>

// The brush tips of the presets come from the resource database
#define TESTBRUSH
#include <kistest.h>
#include <testutil.h>

#include <KisGlobalResourcesInterface.h>
#include <KisPaintOpPresetUpdateProxy.h>
#include <KisResourceLoaderRegistry.h>
#include <KisResourceLocator.h>
#include <KisResourceModel.h>
#include <KisResourceModelProvider.h>
#include <KisResourceStorage.h>
#include <KisResourceTypes.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_settings.h>
#include <brushengine/kis_uniform_paintop_property.h>

#include <KisMaskingBrushOption.h>
#include <kis_brush_option_widget.h>
#include <lager/state.hpp>

#include "../kis_brushop_settings_widget.h"

/**
 * Phase 2a of the brush option shared model
 * (docs/agent/brush-option-shared-model-plan.md): the brush tip and masking
 * brush options moved their state into one value each. The Pixel Brush editor
 * must still write the same keys and values for every preset, and the options
 * must behave the same on a shared model's cursor as on their own state.
 *
 * The references in the .properties files in data/brushtip were written by
 * the code before phase 2a (SOLSTICE_WRITE_REFERENCES=1 writes them again).
 */
class KisBrushTipOptionParityTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void testLegacyRewrite_data();
    void testLegacyRewrite();
    void testBrushTipExternalCursor_data();
    void testBrushTipExternalCursor();
    void testBrushTipCursorChangeReachesWidget();
    void testMaskingExternalCursor_data();
    void testMaskingExternalCursor();
    void testMaskingPreserveMode();
    void testLightnessMode();
    void testResizeWhileEditorOpen();
};

namespace
{
QString dataDir()
{
    return QDir(QString(FILES_DATA_DIR)).filePath(QStringLiteral("brushtip"));
}

/// What KisPaintopBox::slotGuiChangedCurrentPreset() does after the Brush
/// Editor has read a preset: clear it and let every option write again.
QMap<QString, QVariant> fullRewrite(KisPaintOpPresetSP preset)
{
    QScopedPointer<KisBrushOpSettingsWidget> widget(
        new KisBrushOpSettingsWidget(nullptr, KisGlobalResourcesInterface::instance(), KoCanvasResourcesInterfaceSP()));
    widget->setResourcesInterface(KisGlobalResourcesInterface::instance());
    widget->setConfigurationSafe(preset->settings());

    KisPaintOpSettingsSP settings = preset->settings();
    settings->resetSettings();
    widget->writeConfigurationSafe(settings);
    return settings->getProperties();
}

/// One line per property; backslashes and line breaks in values are escaped.
QStringList serialize(const QMap<QString, QVariant> &properties)
{
    QStringList lines;
    for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
        QString value = it.value().toString();
        value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        value.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
        value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        lines << it.key() + QLatin1Char('=') + value;
    }
    return lines;
}

/// The flags of the Pixel Brush.
const KisBrushOptionWidgetFlags pixelBrushFlags =
    KisBrushOptionWidgetFlag::SupportsHSLBrushMode | KisBrushOptionWidgetFlag::SupportsPrecision;

KisPaintOpPresetSP loadPreset(const QString &fileName)
{
    KisPaintOpPresetSP preset(new KisPaintOpPreset(QDir(dataDir()).filePath(fileName)));
    return preset->load(KisGlobalResourcesInterface::instance()) && preset->settings() ? preset : KisPaintOpPresetSP();
}

void addPresetRows(bool maskedOnly)
{
    QTest::addColumn<QString>("fileName");
    const QStringList files = QDir(dataDir()).entryList({QStringLiteral("*.kpp")}, QDir::Files, QDir::Name);
    QVERIFY(files.size() >= 16);
    for (const QString &file : files) {
        if (maskedOnly) {
            KisPaintOpPresetSP preset = loadPreset(file);
            QVERIFY(preset);
            if (!preset->settings()->getBool(QStringLiteral("MaskingBrush/Enabled"))) {
                continue;
            }
        }
        QTest::newRow(qPrintable(file)) << file;
    }
}

template<class Option>
QStringList writtenProperties(const Option &option)
{
    KisPropertiesConfigurationSP config(new KisPropertiesConfiguration());
    option.writeOptionSetting(config);
    return serialize(config->getProperties());
}

// A brush tip option does not own its configuration page (the editor's
// page stack does). Tests that create the option alone delete the page, or
// its preview timers fire after the option is gone.

/// The brush tip size of a preset, which the masking brush uses as its master size.
qreal brushTipSize(KisPaintOpPresetSP preset)
{
    KisBrushTipOptionData tip;
    tip.read(preset->settings().data(), KisGlobalResourcesInterface::instance(), pixelBrushFlags);
    return tip.commonBrushSize;
}
} // namespace

void KisBrushTipOptionParityTest::initTestCase()
{
    for (const QString &type : {ResourceType::Brushes, ResourceType::Patterns}) {
        QVERIFY2(KisResourceModelProvider::resourceModel(type)->rowCount() > 0, qPrintable(type));
    }
}

void KisBrushTipOptionParityTest::testLegacyRewrite_data()
{
    addPresetRows(false);
}

void KisBrushTipOptionParityTest::testLegacyRewrite()
{
    QFETCH(QString, fileName);
    KisPaintOpPresetSP preset = loadPreset(fileName);
    QVERIFY(preset);

    const QStringList actual = serialize(fullRewrite(preset));
    const QString referencePath = QDir(dataDir()).filePath(QFileInfo(fileName).completeBaseName() + ".properties");

    if (qEnvironmentVariableIsSet("SOLSTICE_WRITE_REFERENCES")) {
        QFile file(referencePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write((actual.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
        return;
    }

    QFile file(referencePath);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(referencePath));
    const QStringList expected = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    int differences = 0;
    for (int i = 0; i < qMax(expected.size(), actual.size()); ++i) {
        const QString e = i < expected.size() ? expected[i] : QString();
        const QString a = i < actual.size() ? actual[i] : QString();
        if (e != a && differences++ < 5) {
            qInfo().noquote() << "expected:" << e.left(300);
            qInfo().noquote() << "actual:  " << a.left(300);
        }
    }
    QCOMPARE(differences, 0);
}

void KisBrushTipOptionParityTest::testBrushTipExternalCursor_data()
{
    addPresetRows(false);
}

/// A brush tip option on a shared model's cursor writes what one with its own
/// state writes.
void KisBrushTipOptionParityTest::testBrushTipExternalCursor()
{
    QFETCH(QString, fileName);
    KisPaintOpPresetSP preset = loadPreset(fileName);
    QVERIFY(preset);

    KisBrushOptionWidget legacy(pixelBrushFlags);
    const QScopedPointer<QWidget> legacyPage(legacy.configurationPage());
    legacy.setResourcesInterface(KisGlobalResourcesInterface::instance());
    legacy.readOptionSetting(preset->settings());

    lager::state<KisBrushTipOptionData, lager::automatic_tag> state;
    KisBrushOptionWidget shared(pixelBrushFlags, state);
    const QScopedPointer<QWidget> sharedPage(shared.configurationPage());
    shared.setResourcesInterface(KisGlobalResourcesInterface::instance());
    shared.readOptionSetting(preset->settings());

    QCOMPARE(writtenProperties(shared), writtenProperties(legacy));
    QCOMPARE(state->commonBrushSize, legacy.effectiveBrushSize().get());
    QCOMPARE(shared.lightnessModeEnabled().get(), legacy.lightnessModeEnabled().get());
}

/// A change made through the cursor reaches the widget and what it writes.
void KisBrushTipOptionParityTest::testBrushTipCursorChangeReachesWidget()
{
    KisPaintOpPresetSP preset = loadPreset(QStringLiteral("b_Basic-5_Size_Opacity.kpp"));
    QVERIFY(preset);

    lager::state<KisBrushTipOptionData, lager::automatic_tag> state;
    KisBrushOptionWidget widget(pixelBrushFlags, state);
    const QScopedPointer<QWidget> widgetPage(widget.configurationPage());
    widget.setResourcesInterface(KisGlobalResourcesInterface::instance());
    widget.readOptionSetting(preset->settings());
    QCOMPARE(state->brush.type, KisBrushModel::Auto);

    int changes = 0;
    connect(&widget, &KisPaintOpOption::sigSettingChanged, this, [&changes]() {
        changes++;
    });

    KisBrushTipOptionData data = state.get();
    data.commonBrushSize = 42.0;
    state.set(data);

    QVERIFY(changes > 0);
    QCOMPARE(widget.effectiveBrushSize().get(), 42.0);

    KisPropertiesConfigurationSP config(new KisPropertiesConfiguration());
    widget.writeOptionSetting(config);
    KisBrushTipOptionData written;
    QVERIFY(written.read(config.data(), KisGlobalResourcesInterface::instance(), pixelBrushFlags));
    QCOMPARE(written.commonBrushSize, 42.0);
}

void KisBrushTipOptionParityTest::testMaskingExternalCursor_data()
{
    addPresetRows(true);
}

/// A masking brush option on a shared model's cursor writes what one with its
/// own state writes, also after the master size has changed.
void KisBrushTipOptionParityTest::testMaskingExternalCursor()
{
    QFETCH(QString, fileName);
    KisPaintOpPresetSP preset = loadPreset(fileName);
    QVERIFY(preset);

    lager::state<qreal, lager::automatic_tag> masterSize(brushTipSize(preset));

    KisMaskingBrushOption legacy(masterSize);
    legacy.setResourcesInterface(KisGlobalResourcesInterface::instance());
    legacy.readOptionSetting(preset->settings());

    lager::state<KisMaskingBrushOptionData, lager::automatic_tag> state;
    KisMaskingBrushOption shared(state, masterSize);
    shared.setResourcesInterface(KisGlobalResourcesInterface::instance());
    shared.readOptionSetting(preset->settings());

    QVERIFY(state->preserveMode);
    QCOMPARE(writtenProperties(shared), writtenProperties(legacy));

    // The master size belongs to the brush tip option, which notifies the
    // change itself. Ending the preserve mode must not notify: the editor may
    // be reading the brush tip, and a notification starts a full rewrite.
    int changes = 0;
    connect(&legacy, &KisPaintOpOption::sigSettingChanged, this, [&changes]() {
        changes++;
    });
    connect(&shared, &KisPaintOpOption::sigSettingChanged, this, [&changes]() {
        changes++;
    });
    const QStringList preserved = writtenProperties(shared);

    masterSize.set(masterSize.get() * 2.0);

    QVERIFY(!state->preserveMode);
    QCOMPARE(changes, 0);
    QCOMPARE(writtenProperties(shared), writtenProperties(legacy));
    QVERIFY(writtenProperties(shared) != preserved);
}

/// The stored size coefficient is written back unchanged until the master
/// size or the masking brush size changes.
void KisBrushTipOptionParityTest::testMaskingPreserveMode()
{
    KisPaintOpPresetSP preset = loadPreset(QStringLiteral("h_Charcoal_Pencil_Medium.kpp"));
    QVERIFY(preset);
    const qreal master = brushTipSize(preset);
    const qreal stored = preset->settings()->getDouble(QStringLiteral("MaskingBrush/MasterSizeCoeff"));

    KisMaskingBrushOptionData data;
    data.read(preset->settings().data(), master, KisGlobalResourcesInterface::instance());
    QVERIFY(data.masking.useMasterSize);
    QVERIFY(data.preserveModeHolds(master));
    QCOMPARE(data.bakedMaskingData(master).masterSizeCoeff, stored);

    const qreal otherMaster = master * 2.0;
    QVERIFY(!data.preserveModeHolds(otherMaster));
    QCOMPARE(data.bakedMaskingData(otherMaster).masterSizeCoeff, data.commonBrushSize / otherMaster);

    KisMaskingBrushOptionData resized = data;
    resized.commonBrushSize *= 0.5;
    QVERIFY(!resized.preserveModeHolds(master));
    QCOMPARE(resized.bakedMaskingData(master).masterSizeCoeff, resized.commonBrushSize / master);

    resized.startPreserveMode(master);
    QVERIFY(resized.preserveModeHolds(master));
    QCOMPARE(resized.bakedMaskingData(master).masterSizeCoeff, stored);
}

/// The lightness map needs an image tip and an engine with HSL brush modes;
/// otherwise the effective application falls back as the editor shows it.
void KisBrushTipOptionParityTest::testLightnessMode()
{
    KisBrushTipOptionData data;
    data.brush.type = KisBrushModel::Predefined;
    data.brush.predefinedBrush.resourceSignature = KoResourceSignature(ResourceType::Brushes,
                                                                       QStringLiteral("md5"),
                                                                       QStringLiteral("tip.png"),
                                                                       QStringLiteral("tip"));
    data.brush.predefinedBrush.brushType = IMAGE;
    data.brush.predefinedBrush.baseSize = QSize(100, 80);
    data.brush.predefinedBrush.application = LIGHTNESSMAP;
    data.commonBrushSize = 50.0;

    QVERIFY(data.lightnessModeEnabled(pixelBrushFlags));
    QCOMPARE(data.bakedBrushData(pixelBrushFlags).predefinedBrush.application, LIGHTNESSMAP);
    QCOMPARE(data.bakedBrushData(pixelBrushFlags).predefinedBrush.scale, 0.5);

    const KisBrushOptionWidgetFlags noHsl = KisBrushOptionWidgetFlag::SupportsPrecision;
    QVERIFY(!data.lightnessModeEnabled(noHsl));
    QCOMPARE(data.bakedBrushData(noHsl).predefinedBrush.application, IMAGESTAMP);

    data.brush.predefinedBrush.brushType = MASK;
    QVERIFY(!data.lightnessModeEnabled(pixelBrushFlags));
    QCOMPARE(data.bakedBrushData(pixelBrushFlags).predefinedBrush.application, ALPHAMASK);

    data.brush.type = KisBrushModel::Auto;
    data.brush.predefinedBrush.brushType = IMAGE;
    QVERIFY(!data.lightnessModeEnabled(pixelBrushFlags));
}

namespace
{
int s_safeAsserts = 0;
QtMessageHandler s_previousHandler = nullptr;

void countSafeAsserts(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message.contains(QStringLiteral("SAFE ASSERT"))) {
        s_safeAsserts++;
    }
    s_previousHandler(type, context, message);
}
} // namespace

/// Resizing the brush on the canvas (Shift + drag) while the Brush Editor is
/// open: the tool writes the size, the editor reads the preset again and
/// writes everything back, as KisPaintopBox does.
void KisBrushTipOptionParityTest::testResizeWhileEditorOpen()
{
    KisPaintOpPresetSP preset = loadPreset(QStringLiteral("b_Basic-5_Size_Opacity.kpp"));
    QVERIFY(preset);
    KisPaintOpSettingsSP settings = preset->settings();

    KisBrushOpSettingsWidget widget(nullptr, KisGlobalResourcesInterface::instance(), KoCanvasResourcesInterfaceSP());
    widget.setResourcesInterface(KisGlobalResourcesInterface::instance());
    widget.setConfigurationSafe(settings);

    bool insideRead = false;
    bool insideWrite = false;
    connect(&widget, &KisPaintOpConfigWidget::sigConfigurationUpdated, this, [&]() {
        if (insideRead)
            return;
        insideWrite = true;
        {
            KisPaintOpPreset::UpdatedPostponer postponer(preset);
            settings->resetSettings();
            widget.writeConfigurationSafe(settings);
        }
        insideWrite = false;
    });
    connect(preset->updateProxy(), &KisPaintOpPresetUpdateProxy::sigSettingsChangedUncompressed, this, [&]() {
        if (insideWrite)
            return;
        insideRead = true;
        widget.setConfigurationSafe(settings);
        insideRead = false;
    });

    // Reading the preset into the editor must not report a change of the
    // editor: in the application that starts a full rewrite of the preset
    // while it is being read (the masking brush did so when its preserve mode
    // ended, which caused safe asserts on Shift + drag).
    int changesWhileReading = 0;
    connect(&widget, &KisConfigWidget::sigConfigurationItemChanged, this, [&]() {
        if (insideRead)
            changesWhileReading++;
    });

    QList<KisUniformPaintOpPropertySP> properties = settings->uniformProperties(settings, preset->updateProxy());
    QVERIFY(!properties.isEmpty());

    s_safeAsserts = 0;
    s_previousHandler = qInstallMessageHandler(countSafeAsserts);
    qreal size = settings->paintOpSize();
    for (int i = 0; i < 20; i++) {
        size += 3.0;
        settings->setPaintOpSize(size);
        QTest::qWait(30);
    }
    QTest::qWait(500);
    qInstallMessageHandler(s_previousHandler);

    QCOMPARE(changesWhileReading, 0);
    QCOMPARE(s_safeAsserts, 0);
    QVERIFY(settings->hasProperty(QStringLiteral("brush_definition")));
    QCOMPARE(settings->paintOpSize(), size);
}

/**
 * KISTEST_MAIN, plus the bundle the presets come from. The test resource
 * database has no brush tips or patterns; the bundle provides those of the
 * presets and the fallback brush and pattern.
 */
int main(int argc, char *argv[])
{
    qputenv("LANGUAGE", "en");
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    qputenv("QT_LOGGING_RULES", "");
    QStandardPaths::setTestModeEnabled(true);
    qputenv("EXTRA_RESOURCE_DIRS", QByteArray(KRITA_RESOURCE_DIRS_FOR_TESTS));
    qputenv("KRITA_PLUGIN_PATH", QByteArray(KRITA_PLUGINS_DIR_FOR_TESTS));
    kisTestSetupFontconfig();
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);

    registerResources();

    // Loading the presets of the bundle creates resource models. A model
    // created between beginExternalResourceImport and endExternalResourceImport
    // crashes in endInsertRows, so create them all first.
    Q_FOREACH (const QString &type, KisResourceLoaderRegistry::instance()->resourceTypes()) {
        KisResourceModelProvider::resourceModel(type);
    }

    const QString bundle = QStringLiteral(SYSTEM_RESOURCES_DATA_DIR "bundles/Krita_4_Default_Resources.bundle");
    if (!KisResourceLocator::instance()->addStorage(bundle, KisResourceStorageSP(new KisResourceStorage(bundle)))) {
        qFatal("Could not add %s", qPrintable(bundle));
    }

    KisBrushTipOptionParityTest tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}

#include "KisBrushTipOptionParityTest.moc"
