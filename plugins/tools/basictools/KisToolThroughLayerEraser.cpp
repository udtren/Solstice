/*
 *  SPDX-FileCopyrightText: 2026 Solstice contributors
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisToolThroughLayerEraser.h"

#include <QCheckBox>

#include <KSharedConfig>
#include <KoCanvasBase.h>
#include <KoPointerEvent.h>
#include <klocalizedstring.h>
#include <kundo2magicstring.h>

#include "KisThroughLayerEraserTargets.h"
#include "KisViewManager.h"
#include "kis_canvas2.h"
#include "kis_image.h"
#include "kis_tool_freehand_helper.h"

namespace
{
const char *const SelectedLayersOnlyKey = "selectedLayersOnly";
}

KisToolThroughLayerEraser::KisToolThroughLayerEraser(KoCanvasBase *canvas)
    : KisToolBrush(canvas)
{
    setObjectName("tool_through_layer_eraser");

    m_helper = new KisToolFreehandHelper(paintingInformationBuilder(),
                                         canvas->resourceManager(),
                                         kundo2_i18n("Erase Through Layers"));
    resetHelper(m_helper);
}

KisToolThroughLayerEraser::~KisToolThroughLayerEraser()
{
}

void KisToolThroughLayerEraser::beginPrimaryAction(KoPointerEvent *event)
{
    requestUpdateOutline(event->point, event);

    KisImageSP currentImage = image();
    KisNodeSP isolatedRoot = currentImage->isolationRootNode();
    const KisNodeList targets =
        KisThroughLayerEraserTargets::collect(isolatedRoot ? isolatedRoot : currentImage->root(),
                                              selectedNodes(),
                                              m_selectedLayersOnly);

    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());
    if (targets.isEmpty()) {
        if (canvas2) {
            canvas2->viewManager()->showFloatingMessage(i18n("There are no visible, unlocked paint layers to erase."),
                                                        koIcon("object-locked"));
        }
        event->ignore();
        return;
    }

    KIS_SAFE_ASSERT_RECOVER_RETURN(!m_helper->isRunning());

    setMode(KisTool::PAINT_MODE);

    if (canvas2) {
        canvas2->viewManager()->disableControls();
    }

    m_helper->setThroughLayerTargets(targets);
    initStroke(event);
}

QWidget *KisToolThroughLayerEraser::createOptionWidget()
{
    QWidget *widget = KisToolBrush::createOptionWidget();

    const KConfigGroup config = KSharedConfig::openConfig()->group(toolId());
    m_selectedLayersOnly = config.readEntry(SelectedLayersOnlyKey, false);

    m_chkSelectedLayersOnly = new QCheckBox(i18nc("@option:check", "Selected layers only"), widget);
    m_chkSelectedLayersOnly->setObjectName("chkSelectedLayersOnly");
    m_chkSelectedLayersOnly->setToolTip(
        i18n("Erase only the selected layers and the layers in the selected groups. When unchecked, every visible "
             "layer is erased."));
    m_chkSelectedLayersOnly->setChecked(m_selectedLayersOnly);
    connect(m_chkSelectedLayersOnly, SIGNAL(toggled(bool)), this, SLOT(setSelectedLayersOnly(bool)));
    addOptionWidgetOption(m_chkSelectedLayersOnly);

    return widget;
}

void KisToolThroughLayerEraser::setSelectedLayersOnly(bool value)
{
    m_selectedLayersOnly = value;

    KConfigGroup config = KSharedConfig::openConfig()->group(toolId());
    config.writeEntry(SelectedLayersOnlyKey, value);
}
