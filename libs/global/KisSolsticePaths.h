/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISSOLSTICEPATHS_H
#define KISSOLSTICEPATHS_H

#include <QString>

#include "kritaglobal_export.h"

/**
 * Where Solstice keeps its configuration, logs, resources and cache
 * (docs/agent/settings-location.md). Every such path is built here, so that
 * the profile location is decided in one place.
 *
 * Phase 1: every function returns the location used so far (shared with a
 * stock Krita install): %LOCALAPPDATA% for configuration and logs,
 * %APPDATA%\krita for resources, %LOCALAPPDATA%\krita\cache for the cache.
 * Phase 2 switches to %APPDATA%\Solstice\{config,logs,resources,cache}.
 *
 * Usable before any QCoreApplication exists, except defaultResourceDir(),
 * cacheDir() and xmlguiDataDir(), which depend on the application name.
 * Test mode (QStandardPaths::setTestModeEnabled) is respected.
 */
namespace KisSolsticePaths
{
/// The Solstice profile folder, %APPDATA%\Solstice (a "qttest" subfolder of
/// %APPDATA% in test mode). Not used for any file yet in phase 1.
KRITAGLOBAL_EXPORT QString profileRoot();

/// Folder of the configuration files (kritarc, kritadisplayrc, ...).
KRITAGLOBAL_EXPORT QString configDir();
/// Absolute path of configuration file @p fileName, for QSettings and files.
KRITAGLOBAL_EXPORT QString configFilePath(const QString &fileName);
/**
 * Name to pass to KConfig / KSharedConfig::openConfig() for configuration
 * file @p fileName. KConfig resolves names against GenericConfigLocation and
 * does not handle absolute main config names, so this is a path relative to
 * that location (just @p fileName while the folder is the same).
 */
KRITAGLOBAL_EXPORT QString kconfigName(const QString &fileName);
/// Name of the main configuration (kritarc), for KConfig::setMainConfigName().
KRITAGLOBAL_EXPORT QString mainConfigName();

/// Folder of the usage and system information logs.
KRITAGLOBAL_EXPORT QString logDir();
/// Absolute path of log file @p fileName (krita.log, krita-sysinfo.log).
KRITAGLOBAL_EXPORT QString logFilePath(const QString &fileName);
/// Absolute path of the crash log written by DrMingw.
KRITAGLOBAL_EXPORT QString crashLogPath();

/// Resource folder used when neither --resource-location nor the
/// ResourceDirectory setting chooses one.
KRITAGLOBAL_EXPORT QString defaultResourceDir();
/// Folder of regenerable caches (brush stroke previews).
KRITAGLOBAL_EXPORT QString cacheDir();
/// Folder that holds KXmlGui's local "kxmlgui5" directory.
KRITAGLOBAL_EXPORT QString xmlguiDataDir();
} // namespace KisSolsticePaths

#endif // KISSOLSTICEPATHS_H
