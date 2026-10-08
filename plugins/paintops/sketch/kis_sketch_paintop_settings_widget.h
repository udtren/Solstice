/*
 *  SPDX-FileCopyrightText: 2010 Lukáš Tvrdý <lukast.dev@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_SKETCHPAINTOP_SETTINGS_WIDGET_H_
#define KIS_SKETCHPAINTOP_SETTINGS_WIDGET_H_

#include <kis_paintop_settings_widget.h>
#include <kis_brush_based_paintop_options_widget.h>

#include "ui_wdgsketchoptions.h"

class KisPaintActionTypeOption;
class KisSketchOpOption;
class KisPaintOpOptionsModel;

class KisSketchPaintOpSettingsWidget : public KisBrushBasedPaintopOptionWidget
{
    Q_OBJECT

public:
    KisSketchPaintOpSettingsWidget(QWidget* parent = 0);
    ~KisSketchPaintOpSettingsWidget() override;

    KisPropertiesConfigurationSP configuration() const override;

private:
    // Solstice: the option states live in @p model, which must exist before
    // the base class creates the brush tip option
    KisSketchPaintOpSettingsWidget(QWidget *parent, KisPaintOpOptionsModel *model);
};

#endif
