/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisCanvasUpdateBatcher.h"

#include <QMutexLocker>

#include "KisPaintTrace.h"

namespace
{
thread_local const KisCanvasUpdateBatcher *t_building = nullptr;
}

KisCanvasUpdateBatcher::KisCanvasUpdateBatcher(const void *owner, int maxRequests, qint64 maxPixels)
    : m_owner(owner)
    , m_maxRequests(qMax(1, maxRequests))
    , m_maxPixels(maxPixels)
{
}

void KisCanvasUpdateBatcher::process(const Request &request, const BuildFunction &build)
{
    if (t_building == this) {
        build({request});
        return;
    }
    QMutexLocker locker(&m_mutex);
    const quint64 ticket = m_nextTicket++;
    m_pending.append({request, ticket});

    while (m_completedTicket < ticket) {
        if (m_building) {
            KisPaintTrace::Scope trace("canvas.batch_wait", m_owner);
            m_finished.wait(&m_mutex);
            continue;
        }
        // The oldest pending requests, possibly of other threads. Batches
        // are taken from the front, so they complete in ticket order.
        QVector<Request> requests;
        quint64 lastTicket = 0;
        qint64 pixels = 0;
        while (!m_pending.isEmpty() && requests.size() < m_maxRequests) {
            const QRect &next = m_pending.first().request.rect;
            const qint64 area = qint64(next.width()) * next.height();
            if (!requests.isEmpty() && pixels + area > m_maxPixels) {
                break;
            }
            pixels += area;
            const Entry entry = m_pending.takeFirst();
            requests << entry.request;
            lastTicket = entry.ticket;
        }
        m_building = true;
        locker.unlock();
        t_building = this;
        build(requests);
        t_building = nullptr;
        locker.relock();
        m_completedTicket = lastTicket;
        m_building = false;
        m_finished.wakeAll();
    }
}

int KisCanvasUpdateBatcher::pendingCountForTesting()
{
    QMutexLocker locker(&m_mutex);
    return m_pending.size();
}
