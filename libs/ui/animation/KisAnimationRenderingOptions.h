/*
 *  SPDX-FileCopyrightText: 2019 Dmitry Kazakov <dimula73@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef KISANIMATIONRENDERINGOPTIONS_H
#define KISANIMATIONRENDERINGOPTIONS_H

#include <QString>
#include "kis_properties_configuration.h"

#include "kritaui_export.h"

class KRITAUI_EXPORT KisAnimationRenderingOptions
{
public:
    KisAnimationRenderingOptions();

    QString lastDocumentPath;
    QString videoMimeType = QStringLiteral("video/mp4");
    QString frameMimeType = QStringLiteral("image/png");

    QString basename = QStringLiteral("frame");
    QString directory = QStringLiteral("");
    int firstFrame = 0;
    int lastFrame = 0;
    int sequenceStart = 0;

    // On Android, we only allow either frame or video export, not both. Using
    // a temporary directory instead of scribbling around in external storage is
    // just orders of magnitude faster, doesn't spam the user's recent files
    // with pointless image frames and also makes the export dialog not have to
    // be so screen-escapingly tall. Hence we only have one boolean there.
    bool shouldEncodeVideo = false;
    bool shouldDeleteSequence = false;
    bool includeAudio = false;
    bool wantsOnlyUniqueFrameSequence = false;

    QString ffmpegPath;
    int frameRate = 25;
    int width = 0;
    int height = 0;
    QString scaleFilter;
    QString videoFileName;

    QString customFFMpegOptions;
    KisPropertiesConfigurationSP frameExportConfig;

    QString resolveAbsoluteDocumentFilePath(const QString &documentPath) const;
    QString resolveAbsoluteVideoFilePath(const QString &documentPath) const;
    QString resolveAbsoluteFramesDirectory(const QString &documentPath) const;

    QString resolveAbsoluteVideoFilePath() const;
    QString resolveAbsoluteFramesDirectory() const;


    enum RenderMode {
        RENDER_FRAMES_ONLY,
        RENDER_VIDEO_ONLY,
        RENDER_FRAMES_AND_VIDEO
    };

    RenderMode renderMode() const;


    KisPropertiesConfigurationSP toProperties() const;
    void fromProperties(KisPropertiesConfigurationSP config);

};

#endif // KISANIMATIONRENDERINGOPTIONS_H
