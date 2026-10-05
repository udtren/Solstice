/*
 *  SPDX-FileCopyrightText: 2008, 2009, 2010 Lukáš Tvrdý <lukast.dev@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "kis_deform_paintop_settings.h"
#include "kis_deform_paintop_settings_widget.h"
#include "KisDeformOptionWidget.h"

#include <kis_paintop_settings_widget.h>
#include "KisBrushSizeOptionWidget.h"

#include <KisStandardOptionData.h>
#include <KisSizeOptionWidget.h>
#include <KisAirbrushOptionWidget.h>
#include <KisPaintOpOptionStateUtils.h>
#include <KisCompositeOpOptionWidget.h>
#include <KisCurveOptionWidget.h>

KisDeformPaintOpSettingsWidget::KisDeformPaintOpSettingsWidget(QWidget* parent)
    : KisPaintOpSettingsWidget(parent)
{
    namespace kpowu = KisPaintOpOptionWidgetUtils;
    namespace kposu = KisPaintOpOptionStateUtils;

    // Option states live in a shared model so that one option change writes
    // only that option (docs/agent/brush-option-shared-model-plan.md).
    KisPaintOpOptionsModel *model = new KisPaintOpOptionsModel(this);

    auto *brushSize = model->addOption(QStringLiteral("BrushSize"), KisBrushSizeOptionData());
    auto *deform = model->addOption(QStringLiteral("Deform"), KisDeformOptionData());
    auto *compositeOp = model->addOption(QStringLiteral("CompositeOp"), KisCompositeOpOptionData());
    auto *opacity = model->addOption(QStringLiteral("Opacity"),
                                     KisOpacityOptionData(),
                                     &kposu::bakeCurveOption<KisOpacityOptionData>);
    auto *size =
        model->addOption(QStringLiteral("Size"), KisSizeOptionData(), &kposu::bakeCurveOption<KisSizeOptionData>);
    auto *rotation = model->addOption(QStringLiteral("Rotation"),
                                      KisRotationOptionData(),
                                      &kposu::bakeCurveOption<KisRotationOptionData>);
    auto *airbrush = model->addOption(QStringLiteral("Airbrush"), KisAirbrushOptionData());
    auto *rate =
        model->addOption(QStringLiteral("Rate"), KisRateOptionData(), &kposu::bakeCurveOption<KisRateOptionData>);

    addPaintOpOption(kposu::createOptionWidget<KisBrushSizeOptionWidget>(brushSize));
    addPaintOpOption(kposu::createOptionWidgetWithLodLimitations<KisDeformOptionWidget>(deform));
    addPaintOpOption(kposu::createOptionWidget<KisCompositeOpOptionWidget>(compositeOp));
    addPaintOpOption(kpowu::createOpacityOptionWidget(kposu::curveCursor(opacity)));
    addPaintOpOption(kposu::createOptionWidget<KisSizeOptionWidget>(size));
    addPaintOpOption(kpowu::createRotationOptionWidget(kposu::curveCursor(rotation)));
    addPaintOpOption(kposu::createOptionWidget<KisAirbrushOptionWidget>(airbrush));
    addPaintOpOption(kpowu::createRateOptionWidget(kposu::curveCursor(rate)));

    setOptionsModel(model);
}

KisDeformPaintOpSettingsWidget::~ KisDeformPaintOpSettingsWidget()
{
}


KisPropertiesConfigurationSP KisDeformPaintOpSettingsWidget::configuration() const
{
    KisDeformPaintOpSettings* config = new KisDeformPaintOpSettings(resourcesInterface());
    config->setProperty("paintop", "deformBrush");
    writeConfiguration(config);
    return config;
}

