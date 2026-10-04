/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef KIS_BRUSH_STROKE_PREVIEW_RENDERER_H
#define KIS_BRUSH_STROKE_PREVIEW_RENDERER_H

#include <KoColor.h>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <kis_paint_information.h>
#include <kis_types.h>
#include <kritaui_export.h>
#include <optional>

class KoCanvasResourceProvider;

/** One private RGBA8 image, with cooperative cancellation and no GUI waits. */
class KRITAUI_EXPORT KisBrushStrokePreviewRenderer : public QObject
{
    Q_OBJECT
public:
    explicit KisBrushStrokePreviewRenderer(QObject *parent = nullptr);
    ~KisBrushStrokePreviewRenderer() override;
    bool isRunning() const;
    void start(KisPaintOpPresetSP savedPreset);
    void cancel();
    static bool supported(const QString &engine);
    static bool striped(const QString &engine);
    static void paintBackground(KisPaintDeviceSP device, const QRect &bounds, const QColor &background, bool stripes);
    static KisStrokeId enqueue(KisImageSP image,
                               KisLayerSP layer,
                               KisPaintOpPresetSP preset,
                               KoCanvasResourceProvider *provider,
                               const KoColor &color,
                               const QSize &size,
                               KisPaintInformation &first,
                               KisPaintInformation &last,
                               qreal scale = 1.0,
                               std::optional<int> seed = {});
Q_SIGNALS:
    void finished(const QImage &image, bool cancelled);

private:
    void poll();
    QTimer m_poll;
    QElapsedTimer m_elapsed;
    KisImageSP m_image;
    KisLayerSP m_layer;
    KisStrokeId m_stroke;
    bool m_cancelled = false;
    bool m_timeout = false;
};
#endif
