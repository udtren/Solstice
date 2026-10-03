/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKACCESSLEGACYIMPORTER_H
#define QUICKACCESSLEGACYIMPORTER_H

#include "QuickAccessModel.h"

#include <QStringList>

namespace QuickAccess
{

struct LegacyImportResult {
    Document document;
    QJsonObject settings;
    QStringList warnings;
};

class LegacyImporter
{
public:
    static bool importDirectory(const QString &remasterDirectory, LegacyImportResult *result, QString *error = nullptr);

private:
    static bool readObject(const QString &path, QJsonObject *object, QString *error);
};

} // namespace QuickAccess

#endif
