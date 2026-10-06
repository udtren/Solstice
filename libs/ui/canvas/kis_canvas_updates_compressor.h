/*
 *  SPDX-FileCopyrightText: 2015 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef __KIS_CANVAS_UPDATES_COMPRESSOR_H
#define __KIS_CANVAS_UPDATES_COMPRESSOR_H

#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QVector>

#include "kis_update_info.h"

typedef QList<KisUpdateInfoSP> KisUpdateInfoList;

class KisCanvasUpdatesCompressor
{
public:
    bool putUpdateInfo(KisUpdateInfoSP info);
    /**
     * Puts @p infos in order, atomically: takeUpdateInfo() returns all of
     * them or none. True if the list was empty before.
     */
    bool putUpdateInfos(const QVector<KisUpdateInfoSP> &infos);
    void takeUpdateInfo(KisUpdateInfoList &list);

private:
    bool putUpdateInfoLocked(KisUpdateInfoSP info);

    QMutex m_mutex;
    KisUpdateInfoList m_updatesList;
};

#endif /* __KIS_CANVAS_UPDATES_COMPRESSOR_H */
