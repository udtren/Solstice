/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KIS_LAZY_TOOLS_FAST_EXPORT_H
#define KIS_LAZY_TOOLS_FAST_EXPORT_H

#include <QList>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <ksharedconfig.h>
#include <kis_types.h>
#include <kritaui_export.h>

class KisDocument;

namespace KisLazyToolsFastExport
{
struct KRITAUI_EXPORT Settings {
    QString folder;
    int pngCompression{6};
    bool pngAlpha{true};
    int jpegQuality{90};

    static Settings load(KSharedConfigPtr config = KSharedConfig::openConfig());
    void save(KSharedConfigPtr config = KSharedConfig::openConfig()) const;
};

struct Result {
    int exported{0};
    QStringList skipped;
};

using WaitForImage = std::function<bool(KisImageSP)>;
// An empty output folder selects the source directory. Unnamed documents are
// skipped before path generation; exportDocumentSync keeps document identity.
KRITAUI_EXPORT QString outputPath(const QString &source, const QString &extension, const QString &outputFolder);
KRITAUI_EXPORT Result run(const QList<QPointer<KisDocument>> &documents,
                          const QString &extension,
                          const QString &outputFolder,
                          const Settings &settings,
                          const WaitForImage &wait);
} // namespace KisLazyToolsFastExport

#endif
