/*
 *  SPDX-FileCopyrightText: 2008 Lukas Tvrdy <lukast.dev@gmail.com>
 *  SPDX-FileCopyrightText: 2010 José Luis Vergara <pentalis@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_HATCHING_PAINTOP_SETTINGS_WIDGET_H_
#define KIS_HATCHING_PAINTOP_SETTINGS_WIDGET_H_

#include <kis_paintop_settings_widget.h>
#include <kis_brush_based_paintop_options_widget.h>

#include "ui_wdghatchingoptions.h"
#include "ui_wdghatchingpreferences.h"

class KisPaintOpOptionsModel;

class KisHatchingPaintOpSettingsWidget : public KisBrushBasedPaintopOptionWidget
{
    Q_OBJECT

public:
    KisHatchingPaintOpSettingsWidget(QWidget* parent, KisResourcesInterfaceSP resourcesInterface, KoCanvasResourcesInterfaceSP canvasResourcesInterface);
    ~KisHatchingPaintOpSettingsWidget() override;

    KisPropertiesConfigurationSP configuration() const override;

private:
    // Solstice: the option states live in @p model, which must exist before
    // the base class creates the brush tip option
    KisHatchingPaintOpSettingsWidget(QWidget *parent,
                                     KisResourcesInterfaceSP resourcesInterface,
                                     KoCanvasResourcesInterfaceSP canvasResourcesInterface,
                                     KisPaintOpOptionsModel *model);
};

#endif
