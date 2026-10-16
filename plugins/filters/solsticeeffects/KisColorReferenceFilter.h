/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KIS_COLOR_REFERENCE_FILTER_H
#define KIS_COLOR_REFERENCE_FILTER_H
#include <filter/kis_filter.h>
class KisColorReferenceFilter : public KisFilter
{
public:
    KisColorReferenceFilter();
    KisFilterConfigurationSP defaultConfiguration(KisResourcesInterfaceSP resources) const override;
    KisConfigWidget *
    createConfigurationWidget(QWidget *parent, const KisPaintDeviceSP device, bool useForMasks) const override;
    bool supportsLevelOfDetail(const KisFilterConfigurationSP, int) const override
    {
        return true;
    }
    QRect neededRect(const QRect &rect, const KisFilterConfigurationSP, int) const override
    {
        return rect;
    }
    QRect changedRect(const QRect &rect, const KisFilterConfigurationSP, int) const override
    {
        return rect;
    }
    void processImpl(KisPaintDeviceSP device,
                     const QRect &rect,
                     const KisFilterConfigurationSP config,
                     KoUpdater *progress) const override;
};
#endif
