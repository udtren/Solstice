/*
 *  SPDX-FileCopyrightText: 2008 Boudewijn Rempt <boud@valdyas.org>
 *  SPDX-FileCopyrightText: 2008-2010 Lukáš Tvrdý <lukast.dev@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_HAIRYPAINTOP_SETTINGS_WIDGET_H_
#define KIS_HAIRYPAINTOP_SETTINGS_WIDGET_H_

#include <kis_brush_based_paintop_options_widget.h>

class KisPaintOpOptionsModel;

class KisHairyPaintOpSettingsWidget : public KisBrushBasedPaintopOptionWidget
{
    Q_OBJECT

public:
    KisHairyPaintOpSettingsWidget(QWidget* parent = 0);
    ~KisHairyPaintOpSettingsWidget() override;

    KisPropertiesConfigurationSP configuration() const override;

private:
    // Solstice: the option states live in @p model, which must exist before
    // the base class creates the brush tip option
    KisHairyPaintOpSettingsWidget(QWidget *parent, KisPaintOpOptionsModel *model);
};

#endif





