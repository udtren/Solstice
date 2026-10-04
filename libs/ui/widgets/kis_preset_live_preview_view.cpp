
/*
 *  SPDX-FileCopyrightText: 2017 Scott Petrovic <scottpetrovic@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisBrushStrokePreviewRenderer.h"
#include <QEvent>

#include <kis_preset_live_preview_view.h>
#include <QDebug>
#include <QGraphicsPixmapItem>
#include "kis_paintop_settings.h"
#include <strokes/freehand_stroke.h>
#include <strokes/KisFreehandStrokeInfo.h>
#include "KisAsynchronousStrokeUpdateHelper.h"
#include <kis_brush.h>
#include <KisGlobalResourcesInterface.h>
#include "kis_transaction.h"
#include <KoCanvasResourceProvider.h>

KisPresetLivePreviewView::KisPresetLivePreviewView(QWidget *parent)
    : QGraphicsView(parent),
      m_updateCompressor(100, KisSignalCompressor::FIRST_ACTIVE)
{
    connect(&m_updateCompressor, SIGNAL(timeout()), SLOT(updateStroke()));
}

KisPresetLivePreviewView::~KisPresetLivePreviewView()
{
    delete m_noPreviewText;
    delete m_brushPreviewScene;
}

void KisPresetLivePreviewView::setup(KoCanvasResourceProvider* resourceManager)
{
    m_resourceManager = resourceManager;

    // initializing to 0 helps check later if they actually have something in them
    m_noPreviewText = 0;
    m_sceneImageItem = 0;

    setHorizontalScrollBarPolicy ( Qt::ScrollBarAlwaysOff );
    setVerticalScrollBarPolicy ( Qt::ScrollBarAlwaysOff );


    // layer image needs to be big enough to get an entire stroke of data
    m_canvasSize.setWidth(this->width());
    m_canvasSize.setHeight(this->height());

    m_canvasCenterPoint.setX(m_canvasSize.width()*0.5);
    m_canvasCenterPoint.setY(m_canvasSize.height()*0.5);

    m_colorSpace = KoColorSpaceRegistry::instance()->rgb8();
    m_image = new KisImage(0, m_canvasSize.width(), m_canvasSize.height(), m_colorSpace, "stroke sample image");


    m_layer = new KisPaintLayer(m_image, "livePreviewStrokeSample", OPACITY_OPAQUE_U8, m_colorSpace);

    // set scene for the view
    m_brushPreviewScene = new QGraphicsScene();
    setScene(m_brushPreviewScene);

}

void KisPresetLivePreviewView::setCurrentPreset(KisPaintOpPresetSP preset)
{
    m_currentPreset = preset;
}

void KisPresetLivePreviewView::requestUpdateStroke()
{
    m_updateCompressor.start();
}

void KisPresetLivePreviewView::updateStroke()
{
    // do not paint a stroke if we are any of these engines (they have some issue currently)
    if (m_currentPreset->paintOp().id() == "roundmarker" ||
            m_currentPreset->paintOp().id() == "experimentbrush" ||
            m_currentPreset->paintOp().id() == "duplicate") {

        paintBackground();
        slotPreviewGenerationCompleted();
        return;
    }

    if (!m_previewGenerationInProgress) {
        paintBackground();
        setupAndPaintStroke();
    } else {
        m_updateCompressor.start();
    }
}

void KisPresetLivePreviewView::slotPreviewGenerationCompleted()
{
    m_previewGenerationInProgress = false;

    QImage m_temp_image;
    m_temp_image = m_layer->paintDevice()->convertToQImage(0, m_image->bounds());

    // only add the object once...then just update the pixmap so we can move the preview around
    if (!m_sceneImageItem) {
        m_sceneImageItem = m_brushPreviewScene->addPixmap(QPixmap::fromImage(m_temp_image));
    } else {
        m_sceneImageItem->setPixmap(QPixmap::fromImage(m_temp_image));
    }
}

void KisPresetLivePreviewView::paintBackground()
{
    // clean up "no preview" text object if it exists. we will add it later if we need it
    if (m_noPreviewText) {
        this->scene()->removeItem(m_noPreviewText);
        m_noPreviewText = 0;
    }


    if (m_currentPreset->paintOp().id() == "colorsmudge" ||
            m_currentPreset->paintOp().id() == "deformbrush" ||
            m_currentPreset->paintOp().id() == "filter") {

        // easier to see deformations and smudging with alternating stripes in the background
        // paint the whole background with alternating stripes
        // filter engine may or may not show things depending on the filter...but it is better than nothing

        KisBrushStrokePreviewRenderer::paintBackground(m_layer->paintDevice(),
                                                       m_image->bounds(),
                                                       palette().color(QPalette::Window),
                                                       true);

        m_paintColor = KoColor(Qt::white, m_colorSpace);

    }
    else if (m_currentPreset->paintOp().id() == "roundmarker" ||
             m_currentPreset->paintOp().id() == "experimentbrush" ||
             m_currentPreset->paintOp().id() == "duplicate" ) {

        // cases where we will not show a preview for now
        // roundbrush (quick) -- this isn't showing anything, disable showing preview
        // experimentbrush -- this creates artifacts that carry over to other previews and messes up their display
        // duplicate (clone) brush doesn't have a preview as it doesn't show anything)

        // fill with gray first to clear out what existed from previous preview
        KisBrushStrokePreviewRenderer::paintBackground(m_layer->paintDevice(),
                                                       m_image->bounds(),
                                                       palette().color(QPalette::Window),
                                                       false);

        m_paintColor = KoColor(palette().color(QPalette::Text), m_colorSpace);

        QFont font;
        font.setPixelSize(14);
        font.setBold(false);

        m_noPreviewText = this->scene()->addText(i18n("No Preview for this engine"),font);
        m_noPreviewText->setPos(50, this->height()/4);

        return;

    }
    else {

        // fill with gray first to clear out what existed from previous preview
        KisBrushStrokePreviewRenderer::paintBackground(m_layer->paintDevice(),
                                                       m_image->bounds(),
                                                       palette().color(QPalette::Window),
                                                       false);

        m_paintColor = KoColor(palette().color(QPalette::Text), m_colorSpace);
    }
}

class NotificationStroke : public QObject, public KisSimpleStrokeStrategy
{
  Q_OBJECT
public:
    NotificationStroke()
        : KisSimpleStrokeStrategy(QLatin1String("NotificationStroke"))
    {
        setClearsRedoOnStart(false);
        this->enableJob(JOB_INIT, true, KisStrokeJobData::BARRIER);
        this->enableJob(JOB_CANCEL, true, KisStrokeJobData::BARRIER);
    }

    void initStrokeCallback() override {
        Q_EMIT timeout();
    }

    void cancelStrokeCallback() override {
        Q_EMIT cancelled();
    }

Q_SIGNALS:
    void timeout();
    void cancelled();
};

void KisPresetLivePreviewView::setupAndPaintStroke()
{
    KisBrushStrokePreviewRenderer::enqueue(m_image,
                                           m_layer,
                                           m_currentPreset,
                                           m_resourceManager,
                                           m_paintColor,
                                           size(),
                                           m_curvePointPI1,
                                           m_curvePointPI2);

    m_previewGenerationInProgress = true;

    NotificationStroke *notificationStroke = new NotificationStroke();
    connect(notificationStroke, SIGNAL(timeout()), SLOT(slotPreviewGenerationCompleted()));
    KisStrokeId notificationId = m_image->startStroke(notificationStroke);
    m_image->endStroke(notificationId);


    // TODO: if we don't have any regressions because of it until 4.2.8, then
    //       just remove this code.
    // even though the brush is cloned, the proxy_preset still has some connection to the original preset which will mess brush sizing
    // we need to return brush size to normal.The normal brush sends out a lot of extra signals, so keeping the proxy for now
    //proxy_preset->settings()->setPaintOpSize(originalPresetSize);

}

void KisPresetLivePreviewView::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        if (m_currentPreset) {
            requestUpdateStroke();
        }
    }
}

#include "kis_preset_live_preview_view.moc"
