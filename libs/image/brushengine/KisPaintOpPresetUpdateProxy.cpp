/*
 *  SPDX-FileCopyrightText: 2016 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisPaintOpPresetUpdateProxy.h"

#include "kis_signal_compressor.h"

#include <kis_paintop_preset.h>

struct KisPaintOpPresetUpdateProxy::Private
{
    Private()
        : updatesCompressor(100, KisSignalCompressor::FIRST_ACTIVE),
          updatesBlocked(0),
          numUpdatesWhileBlocked(0)
    {
    }

    KisSignalCompressor updatesCompressor;
    int updatesBlocked;
    int numUpdatesWhileBlocked;
    QSet<QString> changedKeys;
    bool allKeysChanged = false;
};

KisPaintOpPresetUpdateProxy::KisPaintOpPresetUpdateProxy()
    : m_d(new Private)
{
    connect(&m_d->updatesCompressor, SIGNAL(timeout()), SLOT(slotDeliverSettingsChanged()));
}

KisPaintOpPresetUpdateProxy::~KisPaintOpPresetUpdateProxy()
{
}

void KisPaintOpPresetUpdateProxy::notifySettingsChanged()
{
    if (m_d->updatesBlocked) {
        m_d->numUpdatesWhileBlocked++;
    } else {
        Q_EMIT sigSettingsChangedUncompressedEarlyWarning();
        emitSettingsKeysChanged();
        Q_EMIT sigSettingsChangedUncompressed();
        m_d->updatesCompressor.start();
    }
}

void KisPaintOpPresetUpdateProxy::recordChangedKey(const QString &key)
{
    m_d->changedKeys.insert(key);
}

void KisPaintOpPresetUpdateProxy::recordAllKeysChanged()
{
    m_d->allKeysChanged = true;
}

void KisPaintOpPresetUpdateProxy::emitSettingsKeysChanged()
{
    if (m_d->changedKeys.isEmpty() && !m_d->allKeysChanged) {
        return;
    }

    const QSet<QString> keys = m_d->changedKeys;
    const bool allKeys = m_d->allKeysChanged;
    m_d->changedKeys.clear();
    m_d->allKeysChanged = false;

    Q_EMIT sigSettingsKeysChanged(keys, allKeys);
}

void KisPaintOpPresetUpdateProxy::notifyUniformPropertiesChanged()
{
    Q_EMIT sigUniformPropertiesChanged();
}

void KisPaintOpPresetUpdateProxy::postponeSettingsChanges()
{
    m_d->updatesBlocked++;
}

void KisPaintOpPresetUpdateProxy::unpostponeSettingsChanges()
{
    m_d->updatesBlocked--;

    if (!m_d->updatesBlocked && m_d->numUpdatesWhileBlocked) {
        m_d->numUpdatesWhileBlocked = 0;
        Q_EMIT sigSettingsChangedUncompressedEarlyWarning();
        emitSettingsKeysChanged();
        Q_EMIT sigSettingsChangedUncompressed();
        Q_EMIT sigSettingsChanged();
    }
}

void KisPaintOpPresetUpdateProxy::slotDeliverSettingsChanged()
{
    if (m_d->updatesBlocked) {
        m_d->numUpdatesWhileBlocked++;
    } else {
        Q_EMIT sigSettingsChanged();
    }
}
