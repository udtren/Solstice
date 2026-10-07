/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QuickAccessStrokePreviews.h"

#include <KisResourceModel.h>
#include <KisResourceServerProvider.h>
#include <kis_paintop_preset.h>

#include <QPainter>

namespace
{
/// The Brush Presets docker draws the transparent previews on this color.
const QColor PreviewBackground(0x30, 0x30, 0x30);
} // namespace

QuickAccessStrokePreviews::QuickAccessStrokePreviews(QObject *parent)
    : QObject(parent)
{
    connect(KisBrushStrokePreviewCache::instance(),
            &KisBrushStrokePreviewCache::previewReady,
            this,
            &QuickAccessStrokePreviews::changed);
}

QuickAccessStrokePreviews::~QuickAccessStrokePreviews()
{
    KisBrushStrokePreviewCache::instance()->removeConsumer(this);
}

KisBrushStrokePreviewCache::Request QuickAccessStrokePreviews::requestFor(const QString &name) const
{
    auto it = m_requests.constFind(name);
    if (it != m_requests.constEnd())
        return it.value();
    KisBrushStrokePreviewCache::Request request;
    auto *model = KisResourceServerProvider::instance()->paintOpPresetServer()->resourceModel();
    const auto resources = model->resourcesForName(name);
    if (!resources.isEmpty())
        request = KisBrushStrokePreviewCache::Request::fromIndex(model->indexForResource(resources.constFirst()));
    m_requests.insert(name, request);
    return request;
}

void QuickAccessStrokePreviews::setNames(const QStringList &names)
{
    // Saved presets get new checksums, so the requests are looked up again.
    m_requests.clear();
    QList<KisBrushStrokePreviewCache::Request> requests;
    for (const QString &name : names) {
        const auto request = requestFor(name);
        if (!request.filename.isEmpty())
            requests << request;
    }
    KisBrushStrokePreviewCache::instance()->setRequests(this, requests);
}

bool QuickAccessStrokePreviews::ready(const QString &name) const
{
    const auto request = requestFor(name);
    return !request.filename.isEmpty() && KisBrushStrokePreviewCache::instance()->ready(request);
}

QPixmap QuickAccessStrokePreviews::pixmap(const QString &name, const QSize &size, qreal devicePixelRatio) const
{
    QPixmap result(size * devicePixelRatio);
    result.setDevicePixelRatio(devicePixelRatio);
    result.fill(PreviewBackground);
    const auto request = requestFor(name);
    if (request.filename.isEmpty())
        return result;
    const QImage image = KisBrushStrokePreviewCache::instance()->preview(request);
    if (image.isNull())
        return result;
    const QSizeF scaled = QSizeF(image.size()).scaled(QSizeF(size), Qt::KeepAspectRatio);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(
        QRectF(QPointF((size.width() - scaled.width()) / 2.0, (size.height() - scaled.height()) / 2.0), scaled),
        image);
    return result;
}
