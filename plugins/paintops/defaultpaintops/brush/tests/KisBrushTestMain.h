/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISBRUSHTESTMAIN_H
#define KISBRUSHTESTMAIN_H

#include <QStandardPaths>
#include <QTest>

// The brush tips of the presets come from the resource database
#define TESTBRUSH
#include <kistest.h>

#include <KisResourceLoaderRegistry.h>
#include <KisResourceLocator.h>
#include <KisResourceModelProvider.h>
#include <KisResourceStorage.h>

/**
 * KISTEST_MAIN, plus the bundles the test presets come from (file names in
 * krita/data/bundles, as string literals). The test resource database has no
 * brush tips or patterns; the bundles provide those of the presets and the
 * fallback brush and pattern.
 */
#define SOLSTICE_BRUSH_TEST_MAIN_WITH_BUNDLES(TestObject, ...)                                                         \
    int main(int argc, char *argv[])                                                                                   \
    {                                                                                                                  \
        qputenv("LANGUAGE", "en");                                                                                     \
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));                                         \
        qputenv("QT_LOGGING_RULES", "");                                                                               \
        QStandardPaths::setTestModeEnabled(true);                                                                      \
        qputenv("EXTRA_RESOURCE_DIRS", QByteArray(KRITA_RESOURCE_DIRS_FOR_TESTS));                                     \
        qputenv("KRITA_PLUGIN_PATH", QByteArray(KRITA_PLUGINS_DIR_FOR_TESTS));                                         \
        kisTestSetupFontconfig();                                                                                      \
        QApplication app(argc, argv);                                                                                  \
        app.setAttribute(Qt::AA_Use96Dpi, true);                                                                       \
                                                                                                                       \
        registerResources();                                                                                           \
                                                                                                                       \
        /* Loading the presets of a bundle creates resource models. A model                                            \
           created between beginExternalResourceImport and                                                             \
           endExternalResourceImport crashes in endInsertRows, so create them                                          \
           all first. */                                                                                               \
        Q_FOREACH (const QString &type, KisResourceLoaderRegistry::instance()->resourceTypes()) {                      \
            KisResourceModelProvider::resourceModel(type);                                                             \
        }                                                                                                              \
                                                                                                                       \
        Q_FOREACH (const QString &name, QStringList({__VA_ARGS__})) {                                                  \
            const QString bundle = QStringLiteral(SYSTEM_RESOURCES_DATA_DIR "bundles/") + name;                        \
            if (!KisResourceLocator::instance()->addStorage(bundle,                                                    \
                                                            KisResourceStorageSP(new KisResourceStorage(bundle)))) {   \
                qFatal("Could not add %s", qPrintable(bundle));                                                        \
            }                                                                                                          \
        }                                                                                                              \
                                                                                                                       \
        TestObject tc;                                                                                                 \
        QTEST_SET_MAIN_SOURCE_PATH                                                                                     \
        return QTest::qExec(&tc, argc, argv);                                                                          \
    }

/// The Krita 4 default bundle, which the Pixel Brush test presets come from
#define SOLSTICE_BRUSH_TEST_MAIN(TestObject)                                                                           \
    SOLSTICE_BRUSH_TEST_MAIN_WITH_BUNDLES(TestObject, QStringLiteral("Krita_4_Default_Resources.bundle"))

#endif // KISBRUSHTESTMAIN_H
