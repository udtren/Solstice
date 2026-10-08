/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisBrushTipOptionData.h"

#include <QDebug>

#include "KisAutoBrushModel.h"
#include "KisPredefinedBrushModel.h"

using namespace KisBrushModel;

namespace
{
BrushData bakeBrush(BrushData brush, qreal commonBrushSize, bool supportsHSLBrushTips)
{
    brush.autoBrush = KisAutoBrushModel::bakedOptionData(brush.autoBrush, commonBrushSize);
    brush.predefinedBrush =
        KisPredefinedBrushModel::bakedOptionData(brush.predefinedBrush, commonBrushSize, supportsHSLBrushTips);
    return brush;
}
} // namespace

bool KisBrushTipOptionData::operator==(const KisBrushTipOptionData &rhs) const
{
    return brush == rhs.brush && precision == rhs.precision && qFuzzyCompare(commonBrushSize, rhs.commonBrushSize);
}

bool KisBrushTipOptionData::read(const KisPropertiesConfiguration *settings,
                                 KisResourcesInterfaceSP resourcesInterface,
                                 KisBrushOptionWidgetFlags flags)
{
    std::optional<BrushData> data = BrushData::read(settings, resourcesInterface);
    if (!data) {
        qWarning() << "WARNING: failed to load brush object for the a paintop preset";
        return false;
    }
    brush = *data;
    commonBrushSize = effectiveSizeForBrush(data->type, data->autoBrush, data->predefinedBrush, data->textBrush);

    if (flags & KisBrushOptionWidgetFlag::SupportsPrecision) {
        precision = PrecisionData::read(settings);
    }
    return true;
}

void KisBrushTipOptionData::write(KisPropertiesConfiguration *settings, KisBrushOptionWidgetFlags flags) const
{
    bakedBrushData(flags).write(settings);

    if (flags & KisBrushOptionWidgetFlag::SupportsPrecision) {
        precision.write(settings);
    }
}

BrushData KisBrushTipOptionData::bakedBrushData(KisBrushOptionWidgetFlags flags) const
{
    return bakeBrush(brush, commonBrushSize, flags & KisBrushOptionWidgetFlag::SupportsHSLBrushMode);
}

bool KisBrushTipOptionData::lightnessModeEnabled(KisBrushOptionWidgetFlags flags) const
{
    if (brush.type != Predefined) {
        return false;
    }
    const PredefinedBrushData effective = KisPredefinedBrushModel::effectiveResourceData(brush.predefinedBrush);
    return KisPredefinedBrushModel::effectiveBrushApplication(effective,
                                                              flags & KisBrushOptionWidgetFlag::SupportsHSLBrushMode)
        == LIGHTNESSMAP;
}

bool KisMaskingBrushOptionData::operator==(const KisMaskingBrushOptionData &rhs) const
{
    return masking == rhs.masking && qFuzzyCompare(commonBrushSize, rhs.commonBrushSize)
        && preserveMode == rhs.preserveMode && originalBrush == rhs.originalBrush
        && qFuzzyCompare(originalMasterSize, rhs.originalMasterSize)
        && qFuzzyCompare(originalCommonSize, rhs.originalCommonSize);
}

void KisMaskingBrushOptionData::read(const KisPropertiesConfiguration *settings,
                                     qreal masterBrushSize,
                                     KisResourcesInterfaceSP resourcesInterface)
{
    masking = MaskingBrushData::read(settings, masterBrushSize, resourcesInterface);
    commonBrushSize = effectiveSizeForBrush(masking.brush.type,
                                            masking.brush.autoBrush,
                                            masking.brush.predefinedBrush,
                                            masking.brush.textBrush);
    startPreserveMode(masterBrushSize);
}

void KisMaskingBrushOptionData::startPreserveMode(qreal masterBrushSize)
{
    preserveMode = true;
    originalBrush = masking.brush;
    originalMasterSize = masterBrushSize;
    originalCommonSize = commonBrushSize;
}

bool KisMaskingBrushOptionData::preserveModeHolds(qreal masterBrushSize) const
{
    return preserveMode && originalBrush == masking.brush && qFuzzyCompare(originalMasterSize, masterBrushSize)
        && qFuzzyCompare(originalCommonSize, commonBrushSize);
}

MaskingBrushData KisMaskingBrushOptionData::bakedMaskingData(qreal masterBrushSize) const
{
    MaskingBrushData data = masking;
    // The masking brush never supports the HSL application modes.
    data.brush = bakeBrush(masking.brush, commonBrushSize, false);
    if (masking.useMasterSize && !preserveModeHolds(masterBrushSize)) {
        data.masterSizeCoeff = commonBrushSize / masterBrushSize;
    }
    return data;
}

void KisMaskingBrushOptionData::write(KisPropertiesConfiguration *settings, qreal masterBrushSize) const
{
    bakedMaskingData(masterBrushSize).write(settings);
}
