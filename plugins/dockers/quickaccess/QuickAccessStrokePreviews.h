/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKACCESSSTROKEPREVIEWS_H
#define QUICKACCESSSTROKEPREVIEWS_H

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QStringList>

#include <widgets/KisBrushStrokePreviewCache.h>

/**
 * Stroke previews of brush presets by name for Quick Access (palette brush
 * items and the Resources dialog), from the shared Brush Presets docker
 * cache (docs/agent/brush-stroke-preview.md). Each instance is one consumer
 * of the cache: setNames() replaces its requests, and changed() is emitted
 * when previews become available.
 */
class QuickAccessStrokePreviews : public QObject
{
    Q_OBJECT
public:
    explicit QuickAccessStrokePreviews(QObject *parent = nullptr);
    ~QuickAccessStrokePreviews() override;

    /// Requests the previews of these presets (the first preset with each name).
    void setNames(const QStringList &names);
    /// Whether the preview of @p name is available (it may be empty).
    bool ready(const QString &name) const;
    /**
     * The preview of @p name on the docker's dark preview background, scaled
     * to fit @p size (device-independent pixels) and centered.
     */
    QPixmap pixmap(const QString &name, const QSize &size, qreal devicePixelRatio) const;

    /// Width to height ratio of the rendered previews.
    static constexpr qreal AspectRatio = 3.0;

Q_SIGNALS:
    void changed();

private:
    KisBrushStrokePreviewCache::Request requestFor(const QString &name) const;

    mutable QHash<QString, KisBrushStrokePreviewCache::Request> m_requests;
};

#endif
