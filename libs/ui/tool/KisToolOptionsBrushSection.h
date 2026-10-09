/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISTOOLOPTIONSBRUSHSECTION_H
#define KISTOOLOPTIONSBRUSHSECTION_H

#include <QPointer>
#include <QWidget>

#include <kis_types.h>

#include "kritaui_export.h"
#include "widgets/KisBrushStrokePreviewCache.h"

class KisBrushStrokePreviewRenderer;
class KisCanvasResourceProvider;
class KisPaintopBox;
class KisPaintOpSettingsWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;

/**
 * The current brush preset's stroke preview with its name, at the top of the
 * Brush section (as in Clip Studio Paint's tool property palette). The image
 * comes from KisBrushStrokePreviewCache, so it shows the saved preset, as the
 * Brush Presets docker does; a modified preset gets a "*" after its name.
 */
class KRITAUI_EXPORT KisToolOptionsBrushPreview : public QWidget
{
    Q_OBJECT
public:
    explicit KisToolOptionsBrushPreview(QWidget *parent = nullptr);
    ~KisToolOptionsBrushPreview() override;

    void setPreset(KisPaintOpPresetSP preset);

    /// The name shown over the image
    QString text() const;
    /// Whether the preview has an image of the preset: the cache's image of
    /// the saved preset, or the image of the modified preset
    bool hasImage() const;
    /// Whether the image shows the unsaved changes of the modified preset
    bool hasModifiedImage() const;

    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateRequest();
    /// Renders the modified preset after its settings stop changing
    void scheduleModifiedRender();
    void startModifiedRender();
    void cancelModifiedRender();

    KisPaintOpPresetSP m_preset;
    KisBrushStrokePreviewCache::Request m_request;
    bool m_hasRequest{false};
    // the stroke of the modified preset, which the cache does not render
    KisBrushStrokePreviewRenderer *m_renderer{nullptr};
    QTimer *m_renderTimer{nullptr};
    QImage m_modifiedImage;
    bool m_renderAgain{false};
};

/**
 * The "Brush" section of a paint tool's Tool Options: the Brush Editor
 * options of the current brush engine whose eye is on, as checkboxes bound
 * to the editor's options. See docs/agent/tool-options-brush.md.
 */
class KRITAUI_EXPORT KisToolOptionsBrushSection : public QWidget
{
    Q_OBJECT
public:
    /// Follows the current engine of @p paintopBox and the current preset of
    /// @p resourceProvider (both may be null)
    KisToolOptionsBrushSection(KisPaintopBox *paintopBox,
                               KisCanvasResourceProvider *resourceProvider = nullptr,
                               QWidget *parent = nullptr);
    ~KisToolOptionsBrushSection() override;

    /// The Brush Editor settings widget whose options are shown
    void setSettingsWidget(KisPaintOpSettingsWidget *settingsWidget);
    /// The preset whose stroke preview is shown
    void setPreset(KisPaintOpPresetSP preset);

private Q_SLOTS:
    void rebuild();

private:
    QPointer<KisPaintOpSettingsWidget> m_settingsWidget;
    KisToolOptionsBrushPreview *m_preview{nullptr};
    QWidget *m_content{nullptr};
    QVBoxLayout *m_contentLayout{nullptr};
};

#endif // KISTOOLOPTIONSBRUSHSECTION_H
