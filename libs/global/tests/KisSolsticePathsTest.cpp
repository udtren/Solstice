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

extern const unsigned char solsticePathsTestDefaultsRcc[];

class KisSolsticePathsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void cleanup();
    void testProfileLayout();
    void testProfileRootOverride();
    void testKConfigNameRelativeToConfigLocation();
    void testMainConfigWithDefaults();
};

void KisSolsticePathsTest::cleanup()
{
    qunsetenv("SOLSTICE_PROFILE_ROOT");
}

void KisSolsticePathsTest::testProfileLayout()
{
    // docs/agent/settings-location.md: %APPDATA%\Solstice\{config,logs,resources,cache}
    const QString root = KisSolsticePaths::profileRoot();
    QVERIFY(root.endsWith(QStringLiteral("qttest/Solstice")));
    QCOMPARE(KisSolsticePaths::configDir(), root + QStringLiteral("/config"));
    QCOMPARE(KisSolsticePaths::configFilePath("kritadisplayrc"), root + QStringLiteral("/config/kritadisplayrc"));
    QCOMPARE(KisSolsticePaths::logDir(), root + QStringLiteral("/logs"));
    QCOMPARE(KisSolsticePaths::logFilePath("krita.log"), root + QStringLiteral("/logs/krita.log"));
    QCOMPARE(KisSolsticePaths::crashLogPath(), root + QStringLiteral("/logs/kritacrash.log"));
    QCOMPARE(KisSolsticePaths::defaultResourceDir(), root + QStringLiteral("/resources"));
    QCOMPARE(KisSolsticePaths::cacheDir(), root + QStringLiteral("/cache"));
    QCOMPARE(KisSolsticePaths::xmlguiDataDir(), root + QStringLiteral("/config"));
    QVERIFY(KisSolsticePaths::legacyResourceDir().endsWith(QStringLiteral("qttest/krita")));

    // KConfig names are relative to GenericConfigLocation.
    const QString name = KisSolsticePaths::kconfigName("kritashortcutsrc");
    QVERIFY2(name.startsWith(QStringLiteral("../")), qPrintable(name));
    const QDir configLocation(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    QCOMPARE(QDir::cleanPath(configLocation.filePath(name)), KisSolsticePaths::configFilePath("kritashortcutsrc"));
    QCOMPARE(KisSolsticePaths::mainConfigName(), KisSolsticePaths::kconfigName("kritarc"));
}

void KisSolsticePathsTest::testProfileRootOverride()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("SOLSTICE_PROFILE_ROOT", QFile::encodeName(QDir::toNativeSeparators(dir.path())));
    QCOMPARE(KisSolsticePaths::profileRoot(), QDir::cleanPath(dir.path()));
    QCOMPARE(KisSolsticePaths::defaultResourceDir(), QDir::cleanPath(dir.path()) + QStringLiteral("/resources"));
}

void KisSolsticePathsTest::testKConfigNameRelativeToConfigLocation()
{
    // KConfig appends names to GenericConfigLocation, so "../" names leave it.
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

void KisSolsticePathsTest::testMainConfigWithDefaults()
{
    // As krita/main.cc does: the main config lives in the profile, and the
    // embedded defaults are mounted where KConfig looks for them.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("SOLSTICE_PROFILE_ROOT", QFile::encodeName(QDir::toNativeSeparators(dir.path())));
    QVERIFY(KisSolsticePaths::registerMainConfigDefaults(solsticePathsTestDefaultsRcc));
    KConfig::setMainConfigName(KisSolsticePaths::mainConfigName());
    QDir().mkpath(KisSolsticePaths::configDir());
    {
        KConfig config(QString(), KConfig::NoGlobals);
        QCOMPARE(config.group("General").readEntry("solsticeDefault", QString()), QStringLiteral("fromqrc"));
        config.group("General").writeEntry("userValue", 7);
        QVERIFY(config.sync());
    }
    QVERIFY(QFile::exists(KisSolsticePaths::configFilePath("kritarc")));
    KConfig reread(QString(), KConfig::NoGlobals);
    QCOMPARE(reread.group("General").readEntry("userValue", 0), 7);
    QCOMPARE(reread.group("General").readEntry("solsticeDefault", QString()), QStringLiteral("fromqrc"));
}

SIMPLE_TEST_MAIN(KisSolsticePathsTest)

#include "KisSolsticePathsTest.moc"
