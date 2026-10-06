/*
 *  SPDX-FileCopyrightText: 2017 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISDABRENDERINGQUEUE_H
#define KISDABRENDERINGQUEUE_H

#include <QScopedPointer>

#include "kritadefaultpaintops_export.h"

#include <QList>
class KisDabRenderingJob;
struct KisRenderedDab;

#include "KisDabCacheUtils.h"

class KRITADEFAULTPAINTOPS_EXPORT KisDabRenderingQueue
{
public:
    struct CacheInterface {
        virtual ~CacheInterface() {}
        virtual void getDabType(bool hasDabInCache,
                                KisDabCacheUtils::DabRenderingResources *resources,
                                const KisDabCacheUtils::DabRequestInfo &request,
                                /* out */
                                KisDabCacheUtils::DabGenerationInfo *di,
                                bool *shouldUseCache) = 0;

        virtual bool hasSeparateOriginal(KisDabCacheUtils::DabRenderingResources *resources) const = 0;
    };


public:
    KisDabRenderingQueue(const KoColorSpace *cs, KisDabCacheUtils::ResourcesFactory resourcesFactory);
    ~KisDabRenderingQueue();

    KisDabRenderingJobSP addDab(const KisDabCacheUtils::DabRequestInfo &request,
                               qreal opacity, qreal flow);

    QList<KisDabRenderingJobSP> notifyJobFinished(int seqNo, int usecsTime = -1);

    /// Optional byte budget counts each returned dab conservatively, even when
    /// pixels are shared (described dabs, KisRenderedDab::procedural, count one
    /// byte per pixel). Always return at least one ready dab to make progress.
    /// @p stoppedByByteLimit: set to true if a ready dab was left only because
    /// of @p maxDabBytes.
    QList<KisRenderedDab> takeReadyDabs(bool returnMutableDabs = false,
                                        int oneTimeLimit = -1,
                                        bool *someDabsLeft = 0,
                                        quint64 maxDabBytes = ~quint64(0),
                                        quint64 paintTraceBatch = 0,
                                        bool *stoppedByByteLimit = nullptr);

    bool hasPreparedDabs() const;

    void setCacheInterface(CacheInterface *interface);

    KisFixedPaintDeviceSP fetchCachedPaintDevice();

    void putResourcesToCache(KisDabCacheUtils::DabRenderingResources *resources);
    KisDabCacheUtils::DabRenderingResources* fetchResourcesFromCache();

    qreal averageExecutionTime() const;
    int averageDabSize() const;

    int testingGetQueueSize() const;

private:
    struct Private;
    const QScopedPointer<Private> m_d;
};

#endif // KISDABRENDERINGQUEUE_H
