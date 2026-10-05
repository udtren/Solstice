/*
 *  SPDX-FileCopyrightText: 2016 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef __KIS_PAINTOP_PRESET_UPDATE_PROXY_H
#define __KIS_PAINTOP_PRESET_UPDATE_PROXY_H

#include <QScopedPointer>
#include <QObject>
#include <QSet>
#include <QString>

#include "kritaimage_export.h"


/**
 * @brief The KisPaintOpPresetUpdateProxy class
 */
class KRITAIMAGE_EXPORT KisPaintOpPresetUpdateProxy : public QObject
{
    Q_OBJECT

public:
    KisPaintOpPresetUpdateProxy();
    ~KisPaintOpPresetUpdateProxy() override;

    void notifySettingsChanged();
    void notifyUniformPropertiesChanged();

    /**
     * Records changed keys for sigSettingsKeysChanged(). Called by the
     * preset's settings update listener.
     */
    void recordChangedKey(const QString &key);
    void recordAllKeysChanged();

    /**
     * Blocks all sigSettingsChanged() signals until unpostponeSettingsChanges()
     * is called. Used to perform "atomic" writing operations.
     *
     * @see unpostponeSettingsChanges()
     */
    void postponeSettingsChanges();

    /**
     * Unblocks sigSettingsChanged() and emits one signal if there were at least one
     * dropped signal while the block was held.
     */
    void unpostponeSettingsChanges();

Q_SIGNALS:
    void sigSettingsChanged();

    /**
     * Uncompressed signals are delivered in two stages. Firstly,
     * the early warning version is emitted, then the normal. The
     * early warning version is needed to let critical code, like
     * KisPresetShadowUpdater, to perform necessary actions before
     * other receivers got the notification. Don't use it unless
     * you know what you are doing. Use normal
     * sigSettingsChangedUncompressed() instead.
     */
    void sigSettingsChangedUncompressedEarlyWarning();
    void sigSettingsChangedUncompressed();

    /**
     * Uncompressed. Emitted after sigSettingsChangedUncompressedEarlyWarning()
     * and before sigSettingsChangedUncompressed() when keys changed since the
     * previous emission. \p allKeys is true when the settings were reset or
     * replaced; \p keys is then incomplete. Removed keys are included.
     */
    void sigSettingsKeysChanged(const QSet<QString> &keys, bool allKeys);
    void sigUniformPropertiesChanged();

private Q_SLOTS:
    void slotDeliverSettingsChanged();

private:
    void emitSettingsKeysChanged();

private:
    struct Private;
    const QScopedPointer<Private> m_d;
};

#endif /* __KIS_PAINTOP_PRESET_UPDATE_PROXY_H */
