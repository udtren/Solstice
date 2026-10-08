/*
 *  SPDX-FileCopyrightText: 2015 Wolthera van Hövell tot Westerflier <griffinvalley@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TANGENTNORMAL_PAINTOP_SETTINGS_WIDGET_H_
#define KIS_TANGENTNORMAL_PAINTOP_SETTINGS_WIDGET_H_

#include <kis_brush_based_paintop_options_widget.h>

class KisPaintOpOptionsModel;

class KisTangentNormalPaintOpSettingsWidget : public KisBrushBasedPaintopOptionWidget
{
    Q_OBJECT

public:
    KisTangentNormalPaintOpSettingsWidget(QWidget* parent, KisResourcesInterfaceSP resourcesInterface, KoCanvasResourcesInterfaceSP canvasResourcesInterface);
    ~KisTangentNormalPaintOpSettingsWidget() override;

    KisPropertiesConfigurationSP configuration() const override;

private:
    // Solstice: the option states live in @p model, which must exist before
    // the base class creates the brush tip option
    KisTangentNormalPaintOpSettingsWidget(QWidget *parent,
                                          KisResourcesInterfaceSP resourcesInterface,
                                          KoCanvasResourcesInterfaceSP canvasResourcesInterface,
                                          KisPaintOpOptionsModel *model);
};



#endif // KIS_TANGENTNORMAL_PAINTOP_SETTINGS_WIDGET_H_
