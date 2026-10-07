/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisSolsticePaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QResource>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <shlobj.h>
#include <windows.h>
#endif

namespace
{
QString roamingAppData()
{
#ifdef Q_OS_WIN
    PWSTR path = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path) == S_OK && path) {
        const QString result = QDir::fromNativeSeparators(QString::fromWCharArray(path));
        CoTaskMemFree(path);
        return result;
    }
    if (path) {
        CoTaskMemFree(path);
    }
#endif
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

QString join(const QString &dir, const QString &fileName)
{
    return QDir(dir).filePath(fileName);
}

QString environmentPath(const char *name)
{
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(value));
}

QString genericConfigLocation()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

bool isRelativeTo(const QString &base, const QString &target)
{
    return QDir::isRelativePath(QDir(base).relativeFilePath(target));
}
} // namespace

QString KisSolsticePaths::profileRoot()
{
    const QString overridden = environmentPath("SOLSTICE_PROFILE_ROOT");
    if (!overridden.isEmpty()) {
        return overridden;
    }
    QString root = roamingAppData();
    if (QStandardPaths::isTestModeEnabled()) {
        root = join(root, QStringLiteral("qttest"));
    }
    return join(root, QStringLiteral("Solstice"));
}

QString KisSolsticePaths::configDir()
{
    const QString preferred = join(profileRoot(), QStringLiteral("config"));
    if (isRelativeTo(genericConfigLocation(), preferred)) {
        return preferred;
    }
    // KConfig needs a name relative to GenericConfigLocation; on another
    // drive the configuration stays under it.
    return join(genericConfigLocation(), QStringLiteral("Solstice/config"));
}

QString KisSolsticePaths::configFilePath(const QString &fileName)
{
    return join(configDir(), fileName);
}

QString KisSolsticePaths::kconfigName(const QString &fileName)
{
    return QDir(genericConfigLocation()).relativeFilePath(configFilePath(fileName));
}

QString KisSolsticePaths::mainConfigName()
{
    return kconfigName(QStringLiteral("kritarc"));
}

bool KisSolsticePaths::registerMainConfigDefaults(const uchar *rccData)
{
    // KConfig reads defaults from ":/kconfig/" + name; Qt cleans the "../"
    // parts, so the defaults have to be mounted at the cleaned folder.
    const QString lookup = QStringLiteral(":/kconfig/") + mainConfigName();
    if (QFile::exists(lookup)) {
        return true;
    }
    const QString cleaned = QDir::cleanPath(lookup);
    if (!cleaned.startsWith(QStringLiteral(":/"))) {
        return false;
    }
    const QString mapRoot = QFileInfo(cleaned.mid(1)).path();
    if (!QResource::registerResource(rccData, mapRoot)) {
        return false;
    }
    return QFile::exists(lookup);
}

QString KisSolsticePaths::logDir()
{
    return join(profileRoot(), QStringLiteral("logs"));
}

QString KisSolsticePaths::logFilePath(const QString &fileName)
{
    return join(logDir(), fileName);
}

QString KisSolsticePaths::crashLogPath()
{
    return logFilePath(QStringLiteral("kritacrash.log"));
}

QString KisSolsticePaths::defaultResourceDir()
{
    return join(profileRoot(), QStringLiteral("resources"));
}

QString KisSolsticePaths::cacheDir()
{
    return join(profileRoot(), QStringLiteral("cache"));
}

QString KisSolsticePaths::xmlguiDataDir()
{
    return configDir();
}

QString KisSolsticePaths::legacyConfigDir()
{
    const QString overridden = environmentPath("SOLSTICE_LEGACY_CONFIG_DIR");
    return overridden.isEmpty() ? genericConfigLocation() : overridden;
}

QString KisSolsticePaths::legacyResourceDir()
{
    const QString overridden = environmentPath("SOLSTICE_LEGACY_RESOURCE_DIR");
    if (!overridden.isEmpty()) {
        return overridden;
    }
    QString root = roamingAppData();
    if (QStandardPaths::isTestModeEnabled()) {
        root = join(root, QStringLiteral("qttest"));
    }
    return join(root, QStringLiteral("krita"));
}
