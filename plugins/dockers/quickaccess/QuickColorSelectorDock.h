/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKCOLORSELECTORDOCK_H
#define QUICKCOLORSELECTORDOCK_H

#include <KoCanvasObserverBase.h>
#include <KoColor.h>
#include <KoDockFactoryBase.h>

#include <QDockWidget>
#include <QPointer>
#include <QWidget>

class KisCanvas2;
class KisCanvasResourceProvider;
class KisVisualColorSelector;
class QColor;
class QSlider;
class QSpinBox;
class QToolButton;

class QuickColorSelectorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit QuickColorSelectorWidget(QWidget *parent = nullptr);
    void setCanvas(KisCanvas2 *canvas);

private Q_SLOTS:
    void slotSelectorColorChanged(const KoColor &color);
    void slotCanvasColorChanged(const KoColor &color);
    void slotRgbChanged();
    void slotHsvChanged();
    void slotSwapColors();

private:
    void updateSwatches();
    void updateRgb(const KoColor &color);
    void updateChannelGradients(const QColor &color);

    QPointer<KisCanvas2> m_canvas;
    KisCanvasResourceProvider *m_resourceProvider{nullptr};
    KisVisualColorSelector *m_selector{nullptr};
    QToolButton *m_foreground{nullptr};
    QToolButton *m_background{nullptr};
    QSpinBox *m_red{nullptr};
    QSpinBox *m_green{nullptr};
    QSpinBox *m_blue{nullptr};
    QSpinBox *m_hue{nullptr};
    QSpinBox *m_saturation{nullptr};
    QSpinBox *m_value{nullptr};
    QSlider *m_hueSlider{nullptr};
    QSlider *m_saturationSlider{nullptr};
    QSlider *m_valueSlider{nullptr};
    QSlider *m_redSlider{nullptr};
    QSlider *m_greenSlider{nullptr};
    QSlider *m_blueSlider{nullptr};
    bool m_syncing{false};
    bool m_rgbPercentage{true};
};

class QuickColorSelectorDock : public QDockWidget, public KoCanvasObserverBase
{
    Q_OBJECT
public:
    explicit QuickColorSelectorDock(QWidget *parent = nullptr);

    QString observerName() override;
    void setCanvas(KoCanvasBase *canvas) override;
    void unsetCanvas() override;

private:
    QuickColorSelectorWidget *m_selector{nullptr};
};

class QuickColorSelectorDockFactory : public KoDockFactoryBase
{
public:
    QString id() const override;
    QDockWidget *createDockWidget() override;
    DockPosition defaultDockPosition() const override;
};

#endif
