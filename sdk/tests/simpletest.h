#ifndef SIMPLETEST_H
#define SIMPLETEST_H

#include <QTest>
#include <QFile>
#include <QStandardPaths>
#include <QLocale>
// #include <KLocalizedString>
#include <KoTestConfig.h>
#include <KisSynchronizedConnection.h>

/**
 * Solstice: tests run from the build tree, so KoFontRegistry does not find the
 * installed fonts.conf and Fontconfig reports "Cannot load default config
 * file". Point Fontconfig at the installed configuration, unless the
 * environment already chooses one. Call before the QApplication exists.
 */
inline void kisTestSetupFontconfig()
{
    const QString dir = QStringLiteral(KRITA_FONTCONFIG_DIR_FOR_TESTS);
    if (qgetenv("FONTCONFIG_PATH").isEmpty() && QFile::exists(dir + QStringLiteral("/fonts.conf"))) {
        qputenv("FONTCONFIG_PATH", QFile::encodeName(dir));
    }
}

#define SIMPLE_MAIN_IMPL(TestObject) \
    qputenv("LANGUAGE", "en"); \
    kisTestSetupFontconfig(); \
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates)); \
    QStandardPaths::setTestModeEnabled(true); \
    KisSynchronizedConnectionBase::setAutoModeForUnittestsEnabled(true); \
    qputenv("EXTRA_RESOURCE_DIRS", QByteArray(KRITA_RESOURCE_DIRS_FOR_TESTS)); \
    qputenv("KRITA_PLUGIN_PATH", QByteArray(KRITA_PLUGINS_DIR_FOR_TESTS)); \
    QApplication app(argc, argv); \
    app.setAttribute(Qt::AA_Use96Dpi, true); \
    /*QLocale en_US(QLocale::English, QLocale::UnitedStates); \
    KLocalizedString::setLanguages(QStringList() << QStringLiteral("en_US"));*/ \
    QTEST_DISABLE_KEYPAD_NAVIGATION \
    TestObject tc; \
    QTEST_SET_MAIN_SOURCE_PATH \
    return QTest::qExec(&tc, argc, argv);

#define SIMPLE_TEST_MAIN(TestObject) \
int main(int argc, char *argv[]) \
{ \
    QStandardPaths::setTestModeEnabled(true); \
    SIMPLE_MAIN_IMPL(TestObject) \
}

#endif // SIMPLETEST_H
