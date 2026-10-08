/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisBrushBasedOptionStates.h"

#include <KisGlobalResourcesInterface.h>

#include "KisFilterOptionModel.h"
#include "KisTextureOptionModel.h"

namespace
{
KisResourcesInterfaceSP resourcesOrGlobal(const KisResourcesInterfaceGetter &getter)
{
    KisResourcesInterfaceSP resources = getter ? getter() : KisResourcesInterfaceSP();
    return resources ? resources : KisGlobalResourcesInterface::instance();
}
} // namespace

KisBrushTipOptionState::KisBrushTipOptionState(const QString &id, KisBrushOptionWidgetFlags flags)
    : KisPaintOpOptionStateBase(id)
    , m_state(lager::make_state(KisBrushTipOptionData(), lager::automatic_tag{}))
    , m_flags(flags)
{
}

KisBrushTipOptionState::~KisBrushTipOptionState()
{
}

void KisBrushTipOptionState::setResourcesInterfaceGetter(KisResourcesInterfaceGetter getter)
{
    m_resources = std::move(getter);
}

lager::cursor<KisBrushTipOptionData> KisBrushTipOptionState::cursor() const
{
    return m_state;
}

KisBrushTipOptionData KisBrushTipOptionState::data() const
{
    return m_state.get();
}

KisBrushOptionWidgetFlags KisBrushTipOptionState::flags() const
{
    return m_flags;
}

lager::reader<qreal> KisBrushTipOptionState::effectiveBrushSize() const
{
    return m_state[&KisBrushTipOptionData::commonBrushSize];
}

lager::reader<bool> KisBrushTipOptionState::lightnessModeEnabled() const
{
    const KisBrushOptionWidgetFlags flags = m_flags;
    return m_state.map([flags](const KisBrushTipOptionData &data) {
        return data.lightnessModeEnabled(flags);
    });
}

void KisBrushTipOptionState::read(const KisPropertiesConfiguration *config)
{
    KisBrushTipOptionData data = m_state.get();
    if (data.read(config, resourcesOrGlobal(m_resources), m_flags)) {
        m_state.set(data);
    }
}

void KisBrushTipOptionState::write(KisPropertiesConfiguration *config) const
{
    m_state->write(config, m_flags);
}

void KisBrushTipOptionState::watch(std::function<void()> callback)
{
    m_state.watch([callback](const KisBrushTipOptionData &) {
        callback();
    });
}

KisMaskingBrushOptionState::KisMaskingBrushOptionState(const QString &id, lager::reader<qreal> masterBrushSize)
    : KisPaintOpOptionStateBase(id)
    , m_state(lager::make_state(KisMaskingBrushOptionData(), lager::automatic_tag{}))
    , m_masterBrushSize(masterBrushSize)
{
}

KisMaskingBrushOptionState::~KisMaskingBrushOptionState()
{
}

void KisMaskingBrushOptionState::setResourcesInterfaceGetter(KisResourcesInterfaceGetter getter)
{
    m_resources = std::move(getter);
}

lager::cursor<KisMaskingBrushOptionData> KisMaskingBrushOptionState::cursor() const
{
    return m_state;
}

KisMaskingBrushOptionData KisMaskingBrushOptionState::data() const
{
    return m_state.get();
}

lager::reader<qreal> KisMaskingBrushOptionState::masterBrushSize() const
{
    return m_masterBrushSize;
}

lager::reader<bool> KisMaskingBrushOptionState::maskingBrushEnabled() const
{
    return m_state[&KisMaskingBrushOptionData::masking][&KisBrushModel::MaskingBrushData::isEnabled];
}

void KisMaskingBrushOptionState::read(const KisPropertiesConfiguration *config)
{
    KisMaskingBrushOptionData data = m_state.get();
    data.read(config, m_masterBrushSize.get(), resourcesOrGlobal(m_resources));
    m_state.set(data);

    // the masking brush's size controls round the size they were given and
    // write it back; start the preserve mode from the settled values
    data = m_state.get();
    data.startPreserveMode(m_masterBrushSize.get());
    m_state.set(data);
}

void KisMaskingBrushOptionState::write(KisPropertiesConfiguration *config) const
{
    m_state->write(config, m_masterBrushSize.get());
}

void KisMaskingBrushOptionState::watch(std::function<void()> callback)
{
    m_state.watch([callback](const KisMaskingBrushOptionData &) {
        callback();
    });
}

namespace KisBrushBasedOptionStates
{
KisTextureOptionData bakeTextureOption(const KisTextureOptionData &data, KisResourcesInterfaceSP resourcesInterface)
{
    return KisTextureOptionModel::bakedOptionData(data, resourcesInterface);
}

KisFilterOptionData bakeFilterOption(const KisFilterOptionData &data)
{
    return KisFilterOptionModel::bakeOptionData(data);
}

KisPaintingModeOptionData bakePaintingModeOption(const KisPaintingModeOptionData &data, bool maskingBrushEnabled)
{
    // as KisPaintingModeOptionModel::bakedOptionData()
    KisPaintingModeOptionData result;
    result.paintingMode = maskingBrushEnabled ? enumPaintingMode::WASH : data.paintingMode;
    return result;
}
} // namespace KisBrushBasedOptionStates
