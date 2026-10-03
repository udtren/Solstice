/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKACCESSPROFILEREPOSITORY_H
#define QUICKACCESSPROFILEREPOSITORY_H

#include "QuickAccessModel.h"

namespace QuickAccess
{

class ProfileRepository
{
public:
    explicit ProfileRepository(QString path);

    QString path() const;
    bool exists() const;
    bool load(Document *document, QString *error = nullptr) const;
    bool save(const Document &document, QString *error = nullptr) const;

private:
    QString m_path;
};

} // namespace QuickAccess

#endif
