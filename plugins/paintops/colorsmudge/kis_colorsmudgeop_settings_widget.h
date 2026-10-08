/*
 *  SPDX-FileCopyrightText: 2011 Silvio Heinrich <plassy@web.de>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_COLORSMUDGEOP_SETTINGS_WIDGET_H_
#define KIS_COLORSMUDGEOP_SETTINGS_WIDGET_H_

#include <kis_brush_based_paintop_options_widget.h>

class KisPaintOpOptionsModel;

class KisColorSmudgeOpSettingsWidget : public KisBrushBasedPaintopOptionWidget
{
    Q_OBJECT

public:
    KisColorSmudgeOpSettingsWidget(QWidget* parent, KisResourcesInterfaceSP resourcesInterface, KoCanvasResourcesInterfaceSP canvasResourcesInterface);
    ~KisColorSmudgeOpSettingsWidget() override;

    KisPropertiesConfigurationSP configuration() const override;

private:
    // Solstice: the option states live in @p model, which must exist before
    // the base class creates the brush tip option
    KisColorSmudgeOpSettingsWidget(QWidget *parent,
                                   KisResourcesInterfaceSP resourcesInterface,
                                   KoCanvasResourcesInterfaceSP canvasResourcesInterface,
                                   KisPaintOpOptionsModel *model);

    struct Private;
    const QScopedPointer<Private> m_d;
};



#endif // KIS_COLORSMUDGEOP_SETTINGS_WIDGET_H_
