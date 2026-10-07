/*
 *  SPDX-FileCopyrightText: 2009 Cyrille Berger <cberger@cberger.net>
 *  SPDX-FileCopyrightText: 2014 Sven Langkamp <sven.langkamp@gmail.com>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */


#ifndef OVERVIEWWIDGET_H
#define OVERVIEWWIDGET_H
#include <QObject>
#include <QWidget>
#include <QPixmap>
#include <QFutureWatcher>
#include <QMutex>

#include "KisWidgetWithIdleTask.h"

#include <kis_canvas2.h>

class KisSignalCompressor;
class KoCanvasBase;

class OverviewWidget : public KisWidgetWithIdleTask<QWidget>
{
    Q_OBJECT

public:
    OverviewWidget(QWidget * parent = 0);

    ~OverviewWidget() override;

    void setCanvas(KisCanvas2 *canvas) override;

    inline bool isDragging() const
    {
        return m_dragging;
    }

public Q_SLOTS:
    void startUpdateCanvasProjection();
    void updateThumbnail(QImage pixmap);
    void slotThemeChanged();

private Q_SLOTS:
    /// Solstice live updates: called from image worker threads.
    void slotImageUpdated(const QRect &rect);
    void startLiveUpdate();
    void finishLiveUpdate();
    void slotConfigChanged();

Q_SIGNALS:
    void signalDraggingStarted();
    void signalDraggingFinished();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void recalculatePreviewDimensions();
    KisIdleTasksManager::TaskGuard registerIdleTask(KisCanvas2 *canvas) override;
    void clearCachedState() override;
    ///
    /// \brief isPixelArt checks if the preview is bigger than the image itself
    ///
    /// We assume that if the preview is bigger than the image itself,
    /// (1) the image is very small,
    ///     (might not be true if the docker is huge and the user is on 4k display...)
    /// (2) the image is pixel art, so they would like to see pixels.
    /// In that case we'd like to use a different algorithm (Nearest Neighbour, called "Box")
    ///    in the OverviewThumbnailStrokeStrategy.
    /// \return
    ///
    bool isPixelArt();

    QPointF calculatePreviewOrigin(QSize previewSize);
    QTransform canvasToPreviewTransform();
    QTransform previewToCanvasTransform();
    QPolygonF previewPolygon();

    qreal m_previewScale {1.0};
    QPixmap m_oldPixmap;
    QPixmap m_pixmap;

    QPointF m_previewOrigin; // in the same coordinates space as m_previewSize
    QSize m_previewSize {QSize(100, 100)};

    bool m_dragging {false};
    QPointF m_lastPos {QPointF(0, 0)};

    QColor m_outlineColor;

    /**
     * Solstice live updates (docs/agent/overview-live-update.md): while the
     * image changes, the changed part of the thumbnail is read from the
     * projection and scaled on a worker thread, at most every 100 ms. The
     * idle task still regenerates the whole thumbnail afterwards.
     */
    struct LiveResult {
        QRect target; ///< in thumbnail pixels
        QImage image;
        int generation{0};
    };
    bool m_liveUpdates{true};
    QMutex m_dirtyLock;
    QRect m_dirtyRect; ///< image coordinates, guarded by m_dirtyLock
    KisSignalCompressor *m_liveCompressor{nullptr};
    QFutureWatcher<LiveResult> m_liveWatcher;
    int m_liveGeneration{0}; ///< changes with the canvas and the thumbnail size
};



#endif /* OVERVIEWWIDGET_H */
