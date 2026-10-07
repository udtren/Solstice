/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisSolsticePaths.h"

#include <QDir>
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
    // Same parent as GenericConfigLocation elsewhere (~/.config).
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

QString join(const QString &dir, const QString &fileName)
{
    return QDir(dir).filePath(fileName);
}
} // namespace

QString KisSolsticePaths::profileRoot()
{
    QString root = roamingAppData();
    if (QStandardPaths::isTestModeEnabled()) {
        root = join(root, QStringLiteral("qttest"));
    }
    return join(root, QStringLiteral("Solstice"));
}

QString KisSolsticePaths::configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

QString KisSolsticePaths::configFilePath(const QString &fileName)
{
    return join(configDir(), fileName);
}

QString KisSolsticePaths::kconfigName(const QString &fileName)
{
    const QDir base(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    return base.relativeFilePath(configFilePath(fileName));
}

QString KisSolsticePaths::mainConfigName()
{
    return kconfigName(QStringLiteral("kritarc"));
}

QString KisSolsticePaths::logDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

QString KisSolsticePaths::logFilePath(const QString &fileName)
{
    return join(logDir(), fileName);
}

QString KisSolsticePaths::crashLogPath()
{
    return join(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
                QStringLiteral("kritacrash.log"));
}

QString KisSolsticePaths::defaultResourceDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString KisSolsticePaths::cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

QString KisSolsticePaths::xmlguiDataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
