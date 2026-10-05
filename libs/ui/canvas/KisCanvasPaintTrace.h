/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KIS_CANVAS_PAINT_TRACE_H
#define KIS_CANVAS_PAINT_TRACE_H

#include <QRegion>
#include <QTransform>
#include <QVector>
#include <utility>

/** GUI-thread-only bookkeeping; allocated only when tracing is enabled.
 * Tracks command coverage, not pixel survival or physical presentation.
 * Call paint only after rendering, and swapped only on this widget's signal.
 */
class KisCanvasPaintTrace
{
public:
    static constexpr int capacity = 4096;
    struct Frame {
        quint64 previous = 0;
        QVector<quint64> uploads;
    };

    // Coordinates below are logical widget pixels, as used by paintGL's rects.
    static bool supportsMapping(const QTransform &transform, bool wrap)
    {
        return !wrap && transform.isAffine() && transform.m12() == 0 && transform.m21() == 0
            && transform.isInvertible();
    }
    static QRect visibleImageRect(const QRect &imageRect, const QTransform &transform, const QRect &viewport)
    {
        return imageRect.isEmpty() ? QRect() : transform.mapRect(QRectF(imageRect)).toAlignedRect() & viewport;
    }
    bool setView(const QTransform &transform, const QRect &viewport, qreal devicePixelRatio, bool wrap)
    {
        const bool changed = m_hasView
            && (m_transform != transform || m_viewport != viewport || m_devicePixelRatio != devicePixelRatio
                || m_wrap != wrap);
        if (changed)
            reset();
        m_hasView = true;
        m_transform = transform;
        m_viewport = viewport;
        m_devicePixelRatio = devicePixelRatio;
        m_wrap = wrap;
        return changed;
    }

    bool addUpdate(quint64 upload, const QRect &visibleRect)
    {
        if (!upload || visibleRect.isEmpty())
            return true;
        if (m_updates.size() >= capacity)
            return false;
        m_updates.append({upload, QRegion(visibleRect), QRegion(visibleRect)});
        return true;
    }

    Frame paint(const QRect &rendered, const QRect &blitted, quint64 frame)
    {
        Frame result;
        result.previous = std::exchange(m_frame, frame);
        for (auto it = m_updates.begin(); it != m_updates.end();) {
            it->renderRemaining -= rendered;
            // Cached image content can be blitted in a later paint pass.
            it->blitRemaining -= QRegion(blitted) - it->renderRemaining;
            if (it->blitRemaining.isEmpty()) {
                result.uploads.append(it->id);
                it = m_updates.erase(it);
            } else {
                ++it;
            }
        }
        return result;
    }

    quint64 swapped()
    {
        return std::exchange(m_frame, quint64(0));
    }
    void reset()
    {
        m_updates.clear();
        m_frame = 0;
    }

private:
    struct Update {
        quint64 id;
        QRegion renderRemaining;
        QRegion blitRemaining;
    };
    QVector<Update> m_updates;
    quint64 m_frame = 0;
    bool m_hasView = false;
    QTransform m_transform;
    QRect m_viewport;
    qreal m_devicePixelRatio = 1;
    bool m_wrap = false;
};
#endif
