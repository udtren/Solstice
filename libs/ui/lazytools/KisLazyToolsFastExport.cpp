/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KisLazyToolsFastExport.h"

#include <QDir>
#include <QFileInfo>
#include <kconfiggroup.h>
#include <klocalizedstring.h>
#include "KisDocument.h"
#include "kis_image.h"
#include "kis_properties_configuration.h"

KisLazyToolsFastExport::Settings KisLazyToolsFastExport::Settings::load(KSharedConfigPtr config)
{
    const auto group = config->group(QString());
    Settings settings;
    settings.folder = group.readEntry("Solstice/FastExportFolder", QString());
    settings.pngCompression = group.readEntry("Solstice/FastExportPngCompression", 6);
    settings.pngAlpha = group.readEntry("Solstice/FastExportPngAlpha", true);
    settings.jpegQuality = group.readEntry("Solstice/FastExportJpegQuality", 90);
    return settings;
}

void KisLazyToolsFastExport::Settings::save(KSharedConfigPtr config) const
{
    auto group = config->group(QString());
    group.writeEntry("Solstice/FastExportFolder", folder);
    group.writeEntry("Solstice/FastExportPngCompression", pngCompression);
    group.writeEntry("Solstice/FastExportPngAlpha", pngAlpha);
    group.writeEntry("Solstice/FastExportJpegQuality", jpegQuality);
    group.sync();
}

QString
KisLazyToolsFastExport::outputPath(const QString &sourcePath, const QString &extension, const QString &outputFolder)
{
    const QFileInfo source(sourcePath);
    const QString folder = outputFolder.isEmpty() ? source.absolutePath() : outputFolder;
    return QDir(folder).filePath(source.completeBaseName() + QLatin1Char('.') + extension);
}

KisLazyToolsFastExport::Result KisLazyToolsFastExport::run(const QList<QPointer<KisDocument>> &documents,
                                                           const QString &extension,
                                                           const QString &outputFolder,
                                                           const Settings &settings,
                                                           const WaitForImage &wait)
{
    const QByteArray mimeType = extension == QLatin1String("png") ? QByteArray("image/png") : QByteArray("image/jpeg");
    Result result;
    for (const QPointer<KisDocument> &document : documents) {
        if (!document || document->localFilePath().isEmpty()) {
            result.skipped << (document ? document->caption() : i18n("Unknown document"));
            continue;
        }
        const QString path = outputPath(document->localFilePath(), extension, outputFolder);
        KisPropertiesConfigurationSP options(new KisPropertiesConfiguration);
        if (extension == QLatin1String("png")) {
            options->setProperty("compression", settings.pngCompression);
            options->setProperty("alpha", settings.pngAlpha);
        } else {
            options->setProperty("quality", settings.jpegQuality);
        }
        if (wait(document->image()) && document->exportDocumentSync(path, mimeType, options)) {
            ++result.exported;
        } else {
            result.skipped << document->caption();
        }
    }
    return result;
}
