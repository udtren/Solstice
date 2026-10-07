/*
 *  SPDX-FileCopyrightText: 2015 Boudewijn Rempt <boud@valdyas.org>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef KRITAVERSIONWRAPPER_H
#define KRITAVERSIONWRAPPER_H

#include "kritaversion_export.h"
#include <QString>

namespace KritaVersionWrapper {

    /// The Krita compatibility version (e.g. "6.0.5-prealpha"): file
    /// formats, resource updates and scripting compare it. Do not show it as
    /// the application's version.
    KRITAVERSION_EXPORT QString versionString(bool checkGit = false);
    /// Solstice's own version, shown to users (e.g. "0.1.0-alpha"; with
    /// @p checkGit, "0.1.0-alpha (git 2d48c0b)").
    KRITAVERSION_EXPORT QString solsticeVersionString(bool checkGit = false);
    KRITAVERSION_EXPORT bool isDevelopersBuild();
}

#endif // KRITAVERSIONWRAPPER_H
