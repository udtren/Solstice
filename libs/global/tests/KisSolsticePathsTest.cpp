/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <simpletest.h>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <KConfig>
#include <KConfigGroup>

#include "KisSolsticePaths.h"

class KisSolsticePathsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testLegacyLocations();
    void testKConfigNameRelativeToConfigLocation();
    void testResourceDefaultsThroughRelativeName();
};

void KisSolsticePathsTest::testLegacyLocations()
{
    // Phase 1 (docs/agent/settings-location.md): the same locations as before.
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QCOMPARE(KisSolsticePaths::configDir(), config);
    QCOMPARE(KisSolsticePaths::configFilePath("kritadisplayrc"), QDir(config).filePath("kritadisplayrc"));
    QCOMPARE(KisSolsticePaths::kconfigName("kritashortcutsrc"), QStringLiteral("kritashortcutsrc"));
    QCOMPARE(KisSolsticePaths::mainConfigName(), QStringLiteral("kritarc"));
    QCOMPARE(KisSolsticePaths::logDir(), data);
    QCOMPARE(KisSolsticePaths::logFilePath("krita.log"), QDir(data).filePath("krita.log"));
    QCOMPARE(KisSolsticePaths::crashLogPath(), QDir(config).filePath("kritacrash.log"));
    QCOMPARE(KisSolsticePaths::defaultResourceDir(), QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QCOMPARE(KisSolsticePaths::cacheDir(), QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    QCOMPARE(KisSolsticePaths::xmlguiDataDir(), QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QVERIFY(KisSolsticePaths::profileRoot().endsWith(QStringLiteral("qttest/Solstice")));
}

void KisSolsticePathsTest::testKConfigNameRelativeToConfigLocation()
{
    // Phase 2 relies on KConfig names relative to GenericConfigLocation that
    // leave it ("../..."): KConfig appends them to that folder.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath("solsticetestrc");
    const QString name =
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).relativeFilePath(target);
    QVERIFY2(name.startsWith(QStringLiteral("..")), qPrintable(name));
    {
        KConfig config(name, KConfig::SimpleConfig);
        config.group("General").writeEntry("value", 42);
        QVERIFY(config.sync());
    }
    QVERIFY(QFile::exists(target));
    KConfig reread(name, KConfig::SimpleConfig);
    QCOMPARE(reread.group("General").readEntry("value", 0), 42);
}

void KisSolsticePathsTest::testResourceDefaultsThroughRelativeName()
{
    // KConfig reads defaults from ":/kconfig/" + name. With a "../" name the
    // Qt resource path is cleaned, so the defaults must be registered at the
    // cleaned path (here :/solstice-paths-test/defaultsrc).
    QVERIFY(QFile::exists(QStringLiteral(":/kconfig/../solstice-paths-test/defaultsrc")));
    KConfig::setMainConfigName(QStringLiteral("../solstice-paths-test/defaultsrc"));
    KConfig config(QString(), KConfig::NoGlobals);
    QCOMPARE(config.group("General").readEntry("solsticeDefault", QString()), QStringLiteral("fromqrc"));
}

SIMPLE_TEST_MAIN(KisSolsticePathsTest)

#include "KisSolsticePathsTest.moc"
