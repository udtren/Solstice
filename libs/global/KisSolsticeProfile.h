/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISSOLSTICEPROFILE_H
#define KISSOLSTICEPROFILE_H

#include <QString>
#include <QStringList>

#include <functional>

#include "kritaglobal_export.h"

/**
 * The Solstice profile folder (KisSolsticePaths::profileRoot()) and the
 * one-time import of a Krita profile (docs/agent/settings-location.md).
 *
 * The state lives in config\SOLSTICE_PROFILE:
 *  - Missing: no profile yet; krita/main.cc asks whether to import;
 *  - Importing: the configuration was imported, the resources not yet;
 *  - Ready: the profile is complete.
 *
 * The Krita files (KisSolsticePaths::legacyConfigDir() and
 * legacyResourceDir()) are only read, never changed.
 */
namespace KisSolsticeProfile
{
enum class State {
    Missing,
    Importing,
    Ready,
};

KRITAGLOBAL_EXPORT State state();
KRITAGLOBAL_EXPORT bool setState(State state);

/// True if a Krita profile exists to import (kritarc, or a resource folder
/// with a resource database).
KRITAGLOBAL_EXPORT bool legacyProfileExists();
/// The language of the Krita profile's language override (e.g. "ja"), or empty.
KRITAGLOBAL_EXPORT QString legacyLanguage();

/// Creates the profile folders for a fresh start (with the language set to
/// English) and marks the profile Ready.
KRITAGLOBAL_EXPORT bool createFreshProfile(QString *error = nullptr);

/**
 * Imports the configuration files, rewriting paths into the Krita resource
 * folder to the Solstice one, and the KXmlGui local files. Marks the profile
 * Importing when resources follow (resourcesToImport()), Ready otherwise.
 */
KRITAGLOBAL_EXPORT bool importConfiguration(QString *error = nullptr);

/// Rewrites references to @p oldDir in configuration text to @p newDir:
/// forward slashes, backslashes, escaped backslashes and $HOME forms.
KRITAGLOBAL_EXPORT QString rewritePaths(const QString &text,
                                        const QString &oldDir,
                                        const QString &newDir,
                                        const QString &homeDir);

/// True if the Krita resource folder is to be copied: it is the default one
/// (no custom ResourceDirectory in the Krita kritarc) and exists.
KRITAGLOBAL_EXPORT bool resourcesToImport();

struct ResourceCopyPlan {
    QString sourceDir;
    QString targetDir;
    QStringList files; ///< relative to sourceDir
    qint64 bytes = 0;
};

/// The files of the Krita resource folder to copy: everything except the
/// resource database backups (resourcecache.sqlite.N~) and kxmlgui5\.
KRITAGLOBAL_EXPORT ResourceCopyPlan planResourceCopy();

/**
 * Copies @p plan. @p progress receives the bytes copied so far and returns
 * false to cancel. On success the profile is marked Ready; on failure or
 * cancel the target folder is removed and @p error says why.
 */
KRITAGLOBAL_EXPORT bool
copyResources(const ResourceCopyPlan &plan, const std::function<bool(qint64)> &progress, QString *error = nullptr);

/// Removes the partly created profile (configuration, resources, marker), so
/// that the next start asks again.
KRITAGLOBAL_EXPORT void abandonImport();
} // namespace KisSolsticeProfile

#endif // KISSOLSTICEPROFILE_H
