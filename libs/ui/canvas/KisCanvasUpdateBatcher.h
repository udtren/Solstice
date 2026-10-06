/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISCANVASUPDATEBATCHER_H
#define KISCANVASUPDATEBATCHER_H

#include <QMutex>
#include <QRect>
#include <QVector>
#include <QWaitCondition>

#include <functional>

#include "kritaui_export.h"

/**
 * GPU engine (Solstice): combines canvas projection updates that arrive
 * concurrently (one per projection walker thread) into batches, so that one
 * thread builds them together, e.g. with one shared GPU upload, instead of
 * every thread submitting its own work (docs/agent/gpu-engine.md).
 *
 * process() enqueues a request and returns only after a build that contains
 * it has finished. The calling thread builds the oldest pending requests
 * itself when no build is running, otherwise it waits for the running one.
 * Builds never overlap and handle the requests in arrival order, so a caller
 * can rely on its update being complete when process() returns, exactly as
 * if it had built the update itself.
 */
class KRITAUI_EXPORT KisCanvasUpdateBatcher
{
public:
    struct Request {
        QRect rect;
        /// KisPaintTrace flow of the requesting thread.
        quint64 flow = 0;
    };
    using BuildFunction = std::function<void(const QVector<Request> &)>;

    /**
     * @param owner paint trace owner of the wait intervals
     * @param maxRequests, maxPixels bounds of one batch (the first request
     *        is always taken), so a large refresh does not hold back the
     *        threads waiting for it or need a large shared upload buffer
     */
    explicit KisCanvasUpdateBatcher(const void *owner = nullptr,
                                    int maxRequests = 32,
                                    qint64 maxPixels = qint64(1) << 20);

    KisCanvasUpdateBatcher(const KisCanvasUpdateBatcher &) = delete;
    KisCanvasUpdateBatcher &operator=(const KisCanvasUpdateBatcher &) = delete;

    /// @p build: called without internal locks held; the callers of one
    /// batcher must pass equivalent functions. A call from inside a build
    /// (same thread) builds its request alone instead of waiting for itself.
    void process(const Request &request, const BuildFunction &build);

    /// Tests: requests waiting for a build.
    int pendingCountForTesting();

private:
    struct Entry {
        Request request;
        quint64 ticket = 0;
    };

    const void *const m_owner;
    const int m_maxRequests;
    const qint64 m_maxPixels;

    QMutex m_mutex;
    QWaitCondition m_finished;
    QVector<Entry> m_pending;
    quint64 m_nextTicket = 1;
    quint64 m_completedTicket = 0;
    bool m_building = false;
};

#endif // KISCANVASUPDATEBATCHER_H
