/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKADJUSTDOCK_H
#define QUICKADJUSTDOCK_H

#include <KoCanvasObserverBase.h>
#include <KoColor.h>
#include <KoDockFactoryBase.h>
#include <kis_types.h>

#include <QDockWidget>
#include <QHash>
#include <QList>
#include <QPointer>

class KisCanvas2;
class KisCanvasResourceProvider;
class QComboBox;
class QLabel;
class QSlider;
class QTimer;
class QToolButton;
class QuickRotationDial;

class QuickAdjustDock : public QDockWidget, public KoCanvasObserverBase
{
    Q_OBJECT
public:
    explicit QuickAdjustDock(QWidget *parent = nullptr, bool compactPopup = false);
    ~QuickAdjustDock() override;

    QString observerName() override;
    void setCanvas(KoCanvasBase *canvas) override;
    void unsetCanvas() override;

private Q_SLOTS:
    void syncFromCanvas();
    void slotBrushSizeChanged(int value);
    void slotBrushOpacityChanged(int value);
    void slotBrushFlowChanged(int value);
    void slotBrushRotationChanged(int value);
    void slotBrushBlendChanged(int index);
    void slotLayerOpacityChanged(int value);
    void slotLayerBlendChanged(int index);
    void slotResetBrush();
    void slotForegroundColorChanged(const KoColor &color);
    void slotBrushPresetChanged(const KisPaintOpPresetSP preset);

private:
    QSlider *createSliderRow(const QString &label, int minimum, int maximum, QLabel **valueLabel);
    void populateBlendModes(QComboBox *combo);
    QWidget *createColorHistoryWidget();
    QWidget *createBrushHistoryWidget();
    QWidget *createBrushToggleWidget();
    void loadColorHistory();
    void updateColorHistoryButtons();
    void selectHistoryColor(int index);
    void updateBrushHistoryButtons();
    void selectHistoryBrush(int index);
    QToolButton *createStatusButton(const QString &toolTip, const QString &iconName);
    void updateStatusButtons();
    void triggerAction(const QString &name);
    void setControlsEnabled(bool enabled);
    void ensureToolOptionsPad();
    void setToolOptionsPadVisible(bool visible);
    void positionToolOptionsPad();
    void returnToolOptionsDocker();
    static int brushSizeToSlider(qreal size);
    static qreal sliderToBrushSize(int value);

protected:
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QPointer<KisCanvas2> m_canvas;
    KisCanvasResourceProvider *m_resourceProvider{nullptr};
    QTimer *m_syncTimer{nullptr};
    QSlider *m_brushSize{nullptr};
    QSlider *m_brushOpacity{nullptr};
    QSlider *m_brushFlow{nullptr};
    QSlider *m_brushRotation{nullptr};
    QWidget *m_brushRotationRow{nullptr};
    QuickRotationDial *m_rotationDial{nullptr};
    QSlider *m_layerOpacity{nullptr};
    QLabel *m_brushSizeValue{nullptr};
    QLabel *m_brushOpacityValue{nullptr};
    QLabel *m_brushFlowValue{nullptr};
    QLabel *m_brushRotationValue{nullptr};
    QLabel *m_layerOpacityValue{nullptr};
    QComboBox *m_brushBlend{nullptr};
    QComboBox *m_layerBlend{nullptr};
    QWidget *m_colorHistoryGroup{nullptr};
    QWidget *m_brushHistoryGroup{nullptr};
    QList<QToolButton *> m_colorButtons;
    QList<KoColor> m_colorHistory;
    QList<QToolButton *> m_brushButtons;
    QList<KisPaintOpPresetSP> m_brushHistory;
    QHash<QString, QToolButton *> m_brushToggleButtons;
    QToolButton *m_toolOptionsToggle{nullptr};
    QToolButton *m_eraseToggle{nullptr};
    QToolButton *m_alphaToggle{nullptr};
    QToolButton *m_selectionToggle{nullptr};
    QToolButton *m_gestureToggle{nullptr};
    QPointer<QDockWidget> m_toolOptionsDocker;
    QPointer<QWidget> m_toolOptionsPad;
    QPointer<QWidget> m_borrowedToolOptions;
    QPointer<QWidget> m_toolOptionsPlaceholder;
    bool m_toolOptionsDockerWasVisible{false};
    bool m_compactPopup{false};
    bool m_syncing{false};
};

class QuickAdjustDockFactory : public KoDockFactoryBase
{
public:
    QString id() const override;
    QDockWidget *createDockWidget() override;
    DockPosition defaultDockPosition() const override;
};

#endif
