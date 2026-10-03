/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuEngineSettings.h"

#include <KoColorModelStandardIds.h>
#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>

#include "kis_image_config.h"

QString KisGpuEngineSettings::enabledKey()
{
    return QStringLiteral("Solstice/GpuEngine");
}

QString KisGpuEngineSettings::convertPolicyKey()
{
    return QStringLiteral("Solstice/GpuEngineConvertDocuments");
}

bool KisGpuEngineSettings::enabledInConfig()
{
    return KisImageConfig(true).readEntry<bool>(enabledKey(), false);
}

void KisGpuEngineSettings::setEnabledInConfig(bool enabled)
{
    KisImageConfig(false).writeEntry<bool>(enabledKey(), enabled);
}

KisGpuEngineSettings::ConvertPolicy KisGpuEngineSettings::convertPolicy()
{
    const int value = KisImageConfig(true).readEntry<int>(convertPolicyKey(), int(Ask));
    return value == int(Convert) || value == int(Keep) ? ConvertPolicy(value) : Ask;
}

void KisGpuEngineSettings::setConvertPolicy(ConvertPolicy policy)
{
    KisImageConfig(false).writeEntry<int>(convertPolicyKey(), int(policy));
}

bool KisGpuEngineSettings::isGpuColorSpace(const KoColorSpace *colorSpace)
{
    return colorSpace && colorSpace->colorModelId() == RGBAColorModelID
        && (colorSpace->colorDepthId() == Float32BitsColorDepthID
            || colorSpace->colorDepthId() == Float16BitsColorDepthID);
}

const KoColorSpace *KisGpuEngineSettings::conversionTarget(const KoColorSpace *colorSpace)
{
    if (!colorSpace || isGpuColorSpace(colorSpace)) {
        return nullptr;
    }
    KoColorSpaceRegistry *registry = KoColorSpaceRegistry::instance();
    const QString model = RGBAColorModelID.id();
    const QString depth = Float32BitsColorDepthID.id();
    if (colorSpace->colorModelId() == RGBAColorModelID && colorSpace->profile()) {
        if (const KoColorSpace *sameProfile = registry->colorSpace(model, depth, colorSpace->profile())) {
            return sameProfile;
        }
    }
    return registry->colorSpace(model, depth, QString());
}
