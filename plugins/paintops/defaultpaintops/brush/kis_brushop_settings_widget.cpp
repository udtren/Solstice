/*
 *  SPDX-FileCopyrightText: 2002 Patrick Julien <freak@codepimps.org>
 *  SPDX-FileCopyrightText: 2004-2008 Boudewijn Rempt <boud@valdyas.org>
 *  SPDX-FileCopyrightText: 2004 Clarence Dang <dang@kde.org>
 *  SPDX-FileCopyrightText: 2004 Adrian Page <adrian@pagenet.plus.com>
 *  SPDX-FileCopyrightText: 2004 Cyrille Berger <cberger@cberger.net>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_brushop_settings_widget.h"
#include <KisBrushOpSettings.h>

#include <lager/state.hpp>

#include <KisMaskingBrushOption.h>
#include <KisPaintopSettingsIds.h>
#include "kis_brush_option_widget.h"
#include "KisSpacingOptionWidget.h"
#include "KisMirrorOptionWidget.h"
#include "KisSharpnessOptionWidget.h"
#include "KisScatterOptionWidget.h"
#include "KisAirbrushOptionWidget.h"
#include "KisCompositeOpOptionWidget.h"
#include "KisPaintingModeOptionWidget.h"
#include "KisColorSourceOptionWidget.h"
#include "KisLightnessStrengthOptionWidget.h"
#include "KisTextureOptionWidget.h"
#include "KisSizeOptionWidget.h"

#include <KisStandardOptionData.h>
#include <KisPaintOpOptionWidgetUtils.h>
#include <KisBrushBasedOptionStates.h>
#include <KisPaintOpOptionStateUtils.h>
#include <KisPaintOpOptionsModel.h>

namespace
{
const KisBrushOptionWidgetFlags brushOpFlags =
    KisBrushOptionWidgetFlag::SupportsPrecision | KisBrushOptionWidgetFlag::SupportsHSLBrushMode;

KisBrushTipOptionState *addBrushTipOption(KisPaintOpOptionsModel *model)
{
    KisBrushTipOptionState *state = new KisBrushTipOptionState(QStringLiteral("BrushTip"), brushOpFlags);
    model->addOption(state);
    return state;
}
} // namespace

KisBrushOpSettingsWidget::KisBrushOpSettingsWidget(QWidget* parent, KisResourcesInterfaceSP resourcesInterface, KoCanvasResourcesInterfaceSP canvasResourcesInterface)
    : KisBrushOpSettingsWidget(parent, resourcesInterface, canvasResourcesInterface, new KisPaintOpOptionsModel())
{
}

KisBrushOpSettingsWidget::KisBrushOpSettingsWidget(QWidget *parent,
                                                   KisResourcesInterfaceSP resourcesInterface,
                                                   KoCanvasResourcesInterfaceSP canvasResourcesInterface,
                                                   KisPaintOpOptionsModel *model)
    : KisBrushBasedPaintopOptionWidget(brushOpFlags, addBrushTipOption(model)->cursor(), parent)
{
    // TODO: pass into KisPaintOpSettingsWidget!
    Q_UNUSED(canvasResourcesInterface);

    setObjectName("brush option widget");

    namespace kpowu = KisPaintOpOptionWidgetUtils;
    namespace kposu = KisPaintOpOptionStateUtils;
    namespace kbbos = KisBrushBasedOptionStates;
    const QString maskingPrefix = KisPaintOpUtils::MaskingBrushPresetPrefix;

    // Solstice: the option states live in a shared model, so that one option
    // change writes only that option (docs/agent/brush-option-shared-model-plan.md,
    // phase 2b). The brush tip is read first: the masking brush reads with
    // its size.
    model->setParent(this);

    KisBrushTipOptionState *brushTip = static_cast<KisBrushTipOptionState *>(model->option(QStringLiteral("BrushTip")));
    brushTip->setResourcesInterfaceGetter([this]() {
        return this->resourcesInterface();
    });

    KisMaskingBrushOptionState *masking =
        new KisMaskingBrushOptionState(QStringLiteral("MaskingBrush"), brushTip->effectiveBrushSize());
    masking->setResourcesInterfaceGetter([this]() {
        return this->resourcesInterface();
    });
    model->addOption(masking);

    auto *compositeOp = model->addOption(QStringLiteral("CompositeOp"), KisCompositeOpOptionData());
    auto *opacity = model->addOption(QStringLiteral("Opacity"),
                                     KisOpacityOptionData(),
                                     &kposu::bakeCurveOption<KisOpacityOptionData>);
    auto *flow =
        model->addOption(QStringLiteral("Flow"), KisFlowOptionData(), &kposu::bakeCurveOption<KisFlowOptionData>);
    auto *size =
        model->addOption(QStringLiteral("Size"), KisSizeOptionData(), &kposu::bakeCurveOption<KisSizeOptionData>);
    auto *ratio =
        model->addOption(QStringLiteral("Ratio"), KisRatioOptionData(), &kposu::bakeCurveOption<KisRatioOptionData>);
    auto *spacing = model->addOption(QStringLiteral("Spacing"),
                                     KisSpacingOptionData(),
                                     &kposu::bakeCurveOption<KisSpacingOptionData>);
    auto *mirror =
        model->addOption(QStringLiteral("Mirror"), KisMirrorOptionData(), &kposu::bakeCurveOption<KisMirrorOptionData>);
    auto *softness = model->addOption(QStringLiteral("Softness"),
                                      KisSoftnessOptionData(),
                                      &kposu::bakeCurveOption<KisSoftnessOptionData>);
    auto *rotation = model->addOption(QStringLiteral("Rotation"),
                                      KisRotationOptionData(),
                                      &kposu::bakeCurveOption<KisRotationOptionData>);
    auto *sharpness = model->addOption(QStringLiteral("Sharpness"),
                                       KisSharpnessOptionData(),
                                       &kposu::bakeCurveOption<KisSharpnessOptionData>);
    auto *lightnessStrength = model->addOption(
        QStringLiteral("LightnessStrength"),
        KisLightnessStrengthOptionData(),
        [brushTip](const KisLightnessStrengthOptionData &data) {
            return kposu::bakeLinkedCurveOption(data, brushTip->data().lightnessModeEnabled(brushTip->flags()));
        });
    auto *scatter = model->addOption(QStringLiteral("Scatter"),
                                     KisScatterOptionData(),
                                     &kposu::bakeCurveOption<KisScatterOptionData>);

    auto *colorSource = model->addOption(QStringLiteral("ColorSource"), KisColorSourceOptionData());
    auto *darken =
        model->addOption(QStringLiteral("Darken"), KisDarkenOptionData(), &kposu::bakeCurveOption<KisDarkenOptionData>);
    auto *mix = model->addOption(QStringLiteral("Mix"), KisMixOptionData(), &kposu::bakeCurveOption<KisMixOptionData>);
    auto *hue = model->addOption(QStringLiteral("Hue"), KisHueOptionData(), &kposu::bakeCurveOption<KisHueOptionData>);
    auto *saturation = model->addOption(QStringLiteral("Saturation"),
                                        KisSaturationOptionData(),
                                        &kposu::bakeCurveOption<KisSaturationOptionData>);
    auto *value =
        model->addOption(QStringLiteral("Value"), KisValueOptionData(), &kposu::bakeCurveOption<KisValueOptionData>);

    auto *airbrush = model->addOption(QStringLiteral("Airbrush"), KisAirbrushOptionData());
    auto *rate =
        model->addOption(QStringLiteral("Rate"), KisRateOptionData(), &kposu::bakeCurveOption<KisRateOptionData>);

    auto *paintingMode =
        model->addOption(QStringLiteral("PaintingMode"),
                         KisPaintingModeOptionData(),
                         [masking](const KisPaintingModeOptionData &data) {
                             return kbbos::bakePaintingModeOption(data, masking->data().masking.isEnabled);
                         });
    auto *texture = model->addOption(QStringLiteral("Texture"),
                                     KisTextureOptionData(),
                                     [resourcesInterface](const KisTextureOptionData &data) {
                                         return kbbos::bakeTextureOption(data, resourcesInterface);
                                     });
    auto *strength = model->addOption(QStringLiteral("Strength"),
                                      KisStrengthOptionData(),
                                      &kposu::bakeCurveOption<KisStrengthOptionData>);

    auto *maskingOpacity = model->addOption(QStringLiteral("MaskingOpacity"),
                                            KisOpacityOptionData(maskingPrefix),
                                            &kposu::bakeCurveOption<KisOpacityOptionData>);
    auto *maskingFlow = model->addOption(QStringLiteral("MaskingFlow"),
                                         KisFlowOptionData(maskingPrefix),
                                         &kposu::bakeCurveOption<KisFlowOptionData>);
    auto *maskingSize = model->addOption(QStringLiteral("MaskingSize"),
                                         KisSizeOptionData(maskingPrefix),
                                         &kposu::bakeCurveOption<KisSizeOptionData>);
    auto *maskingRatio = model->addOption(QStringLiteral("MaskingRatio"),
                                          KisRatioOptionData(maskingPrefix),
                                          &kposu::bakeCurveOption<KisRatioOptionData>);
    auto *maskingRotation = model->addOption(QStringLiteral("MaskingRotation"),
                                             KisRotationOptionData(maskingPrefix),
                                             &kposu::bakeCurveOption<KisRotationOptionData>);
    auto *maskingMirror = model->addOption(QStringLiteral("MaskingMirror"),
                                           KisMirrorOptionData(maskingPrefix),
                                           &kposu::bakeCurveOption<KisMirrorOptionData>);
    auto *maskingScatter = model->addOption(QStringLiteral("MaskingScatter"),
                                            KisScatterOptionData(maskingPrefix),
                                            &kposu::bakeCurveOption<KisScatterOptionData>);

    // written (baked) data that depends on another option
    model->addDependency(QStringLiteral("MaskingBrush"), QStringLiteral("BrushTip"));
    model->addDependency(QStringLiteral("LightnessStrength"), QStringLiteral("BrushTip"));
    model->addDependency(QStringLiteral("PaintingMode"), QStringLiteral("MaskingBrush"));

    // Brush tip options
    addPaintOpOption(kposu::createOptionWidget<KisCompositeOpOptionWidget>(compositeOp));
    addPaintOpOption(kpowu::createOpacityOptionWidget(kposu::curveCursor(opacity)));
    addPaintOpOption(kpowu::createFlowOptionWidget(kposu::curveCursor(flow)));
    addPaintOpOption(kposu::createOptionWidget<KisSizeOptionWidget>(size));
    addPaintOpOption(kpowu::createRatioOptionWidget(kposu::curveCursor(ratio)));
    addPaintOpOption(kposu::createOptionWidget<KisSpacingOptionWidget>(spacing));
    addPaintOpOption(kposu::createOptionWidget<KisMirrorOptionWidget>(mirror));

    addPaintOpOption(kpowu::createSoftnessOptionWidget(kposu::curveCursor(softness)));
    addPaintOpOption(kpowu::createRotationOptionWidget(kposu::curveCursor(rotation)));

    addPaintOpOption(kposu::createOptionWidget<KisSharpnessOptionWidget>(sharpness));
    addPaintOpOption(
        kposu::createOptionWidget<KisLightnessStrengthOptionWidget>(lightnessStrength,
                                                                    brushOptionWidget()->lightnessModeEnabled()));

    addPaintOpOption(kposu::createOptionWidget<KisScatterOptionWidget>(scatter));

    // Colors options
    addPaintOpOption(kposu::createOptionWidget<KisColorSourceOptionWidget>(colorSource));
    addPaintOpOption(kpowu::createDarkenOptionWidget(kposu::curveCursor(darken)));
    addPaintOpOption(kpowu::createMixOptionWidget(kposu::curveCursor(mix)));
    addPaintOpOption(kpowu::createHueOptionWidget(kposu::curveCursor(hue)));
    addPaintOpOption(kpowu::createSaturationOptionWidget(kposu::curveCursor(saturation)));
    addPaintOpOption(kpowu::createValueOptionWidget(kposu::curveCursor(value)));

    addPaintOpOption(kposu::createOptionWidget<KisAirbrushOptionWidget>(airbrush));
    addPaintOpOption(kpowu::createRateOptionWidget(kposu::curveCursor(rate)));

    KisMaskingBrushOption *maskingOption =
        new KisMaskingBrushOption(masking->cursor(), brushOptionWidget()->effectiveBrushSize());
    addPaintOpOption(
        kposu::createOptionWidget<KisPaintingModeOptionWidget>(paintingMode,
                                                               maskingOption->maskingBrushEnabledReader()));

    addPaintOpOption(kposu::createOptionWidget<KisTextureOptionWidget>(texture,
                                                                       resourcesInterface,
                                                                       SupportsLightnessMode | SupportsGradientMode));
    addPaintOpOption(kpowu::createStrengthOptionWidget(kposu::curveCursor(strength)));

    addPaintOpOption(maskingOption);

    addPaintOpOption(kpowu::createMaskingOpacityOptionWidget(kposu::curveCursor(maskingOpacity)));
    addPaintOpOption(kpowu::createMaskingFlowOptionWidget(kposu::curveCursor(maskingFlow)));
    addPaintOpOption(kposu::createOptionWidget<KisSizeOptionWidget>(maskingSize, KisPaintOpOption::MASKING_BRUSH));
    addPaintOpOption(kpowu::createMaskingRatioOptionWidget(kposu::curveCursor(maskingRatio)));
    addPaintOpOption(kpowu::createMaskingRotationOptionWidget(kposu::curveCursor(maskingRotation)));
    addPaintOpOption(kposu::createOptionWidget<KisMirrorOptionWidget>(maskingMirror, KisPaintOpOption::MASKING_BRUSH));
    addPaintOpOption(
        kposu::createOptionWidget<KisScatterOptionWidget>(maskingScatter, KisPaintOpOption::MASKING_BRUSH));

    setOptionsModel(model);
}

KisBrushOpSettingsWidget::~KisBrushOpSettingsWidget()
{
}

KisPropertiesConfigurationSP KisBrushOpSettingsWidget::configuration() const
{
    KisBrushBasedPaintOpSettingsSP config = new KisBrushOpSettings(resourcesInterface());
    config->setProperty("paintop", "paintbrush"); // XXX: make this a const id string
    writeConfiguration(config);
    return config;
}
