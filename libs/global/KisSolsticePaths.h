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
 * (docs/agent/settings-location.md). Every such path is built here.
 *
 * The profile is %APPDATA%\Solstice with the subfolders config\, logs\,
 * resources\ and cache\. KConfig resolves configuration names against
 * GenericConfigLocation (%LOCALAPPDATA%), so kconfigName() returns names
 * relative to it ("../Roaming/Solstice/config/kritarc"). When no relative path
 * exists (the folders are on different drives), the configuration falls back
 * to %LOCALAPPDATA%\Solstice\config.
 *
 * The functions for the previous locations (shared with a stock Krita
 * install) are the import sources of KisSolsticeProfile.
 *
 * Environment overrides, for trials and tests: SOLSTICE_PROFILE_ROOT (the
 * profile folder), SOLSTICE_LEGACY_CONFIG_DIR and SOLSTICE_LEGACY_RESOURCE_DIR
 * (the Krita profile to import). In test mode
 * (QStandardPaths::setTestModeEnabled) the default profile is
 * %APPDATA%\qttest\Solstice.
 *
 * Usable before any QCoreApplication exists.
 */
namespace KisSolsticePaths
{
/// The Solstice profile folder.
KRITAGLOBAL_EXPORT QString profileRoot();

/// Folder of the configuration files (kritarc, kritadisplayrc, ...).
KRITAGLOBAL_EXPORT QString configDir();
/// Absolute path of configuration file @p fileName, for QSettings and files.
KRITAGLOBAL_EXPORT QString configFilePath(const QString &fileName);
/**
 * Name to pass to KConfig / KSharedConfig::openConfig() for configuration
 * file @p fileName: a path relative to GenericConfigLocation, because KConfig
 * does not handle absolute main config names.
 */
KRITAGLOBAL_EXPORT QString kconfigName(const QString &fileName);
/// Name of the main configuration (kritarc), for KConfig::setMainConfigName().
KRITAGLOBAL_EXPORT QString mainConfigName();
/**
 * Makes the embedded kritarc defaults visible where KConfig looks for them
 * (":/kconfig/" + mainConfigName(), which Qt cleans into another resource
 * path). @p rccData is a binary resource (rcc --binary) holding "kritarc"
 * at its root; it must stay valid for the lifetime of the process.
 * Returns false if the defaults could not be mounted.
 */
KRITAGLOBAL_EXPORT bool registerMainConfigDefaults(const uchar *rccData);

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

/// Previous configuration folder, shared with stock Krita (%LOCALAPPDATA%).
KRITAGLOBAL_EXPORT QString legacyConfigDir();
/// Previous resource folder, shared with stock Krita (%APPDATA%\krita).
KRITAGLOBAL_EXPORT QString legacyResourceDir();
} // namespace KisSolsticePaths

#endif // KISSOLSTICEPATHS_H
