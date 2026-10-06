/*
 *  SPDX-FileCopyrightText: 2015 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_canvas_updates_compressor.h"
#include "KisPaintTrace.h"

bool KisCanvasUpdatesCompressor::putUpdateInfo(KisUpdateInfoSP info)
{
    QMutexLocker l(&m_mutex);
    return putUpdateInfoLocked(info);
}

bool KisCanvasUpdatesCompressor::putUpdateInfos(const QVector<KisUpdateInfoSP> &infos)
{
    QMutexLocker l(&m_mutex);
    for (const KisUpdateInfoSP &info : infos) {
        putUpdateInfoLocked(info);
    }
    // Like putUpdateInfo(): true if no older update is left in the list.
    return !m_updatesList.isEmpty() && infos.contains(m_updatesList.first());
}

bool KisCanvasUpdatesCompressor::putUpdateInfoLocked(KisUpdateInfoSP info)
{
    const int levelOfDetail = info->levelOfDetail();
    const QRect newUpdateRect = info->dirtyImageRect();
    if (newUpdateRect.isEmpty()) return false;

    if (info->canBeCompressed()) {
        KisUpdateInfoList::iterator it = m_updatesList.begin();
        while (it != m_updatesList.end()) {
            if ((*it)->canBeCompressed() &&
                levelOfDetail == (*it)->levelOfDetail() &&
                newUpdateRect.contains((*it)->dirtyImageRect())) {

                /**
                 * We should always remove the overridden update and put 'info' to the end
                 * of the queue. Otherwise, the updates will become reordered and the canvas
                 * may have tiles artifacts with "outdated" data
                 */
                KisPaintTrace::link("update.superseded", nullptr, (*it)->paintTraceId(), info->paintTraceId());
                it = m_updatesList.erase(it);
            } else {
                ++it;
            }
        }
    }

    m_updatesList.append(info);

    return m_updatesList.size() <= 1;
}

void KisCanvasUpdatesCompressor::takeUpdateInfo(KisUpdateInfoList &list)
{
    KIS_SAFE_ASSERT_RECOVER(list.isEmpty()) { list.clear(); }

    QMutexLocker l(&m_mutex);
    m_updatesList.swap(list);
}
