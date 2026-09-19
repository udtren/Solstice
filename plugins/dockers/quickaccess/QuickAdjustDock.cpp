/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QuickAdjustDock.h"

#include "QuickAdjustKeyController.h"

#include <KisViewManager.h>
#include <KoColorSpace.h>
#include <KoCompositeOpRegistry.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_settings.h>
#include <kconfiggroup.h>
#include <kis_action.h>
#include <kis_action_manager.h>
#include <kis_canvas2.h>
#include <kis_canvas_resource_provider.h>
#include <kis_image.h>
#include <kis_node.h>
#include <kis_node_manager.h>
#include <kis_paint_device.h>
#include <klocalizedstring.h>
#include <ksharedconfig.h>

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>
#include <functional>

namespace
{
bool colorHistoryResetForSession = false;
}

class QuickRotationDial : public QWidget
{
public:
    explicit QuickRotationDial(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(60, 60);
    }

    void setValue(int value)
    {
        value = qBound(0, value, 360);
        if (m_value == value)
            return;
        m_value = value;
        update();
    }

    std::function<void(int)> valueChanged;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPointF center = rect().center();
        const qreal radius = qMin(width(), height()) / 2.0 - 5.0;
        painter.setPen(QPen(palette().mid().color(), 2));
        painter.setBrush(palette().button());
        painter.drawEllipse(center, radius, radius);
        const qreal angle = qDegreesToRadians(qreal(m_value));
        const QPointF endpoint(center.x() + (radius - 10.0) * std::sin(angle),
                               center.y() - (radius - 10.0) * std::cos(angle));
        painter.setPen(QPen(palette().highlight().color(), 3));
        painter.drawLine(center, endpoint);
        painter.setBrush(palette().highlight());
        painter.drawEllipse(center, 3, 3);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            updateFromPosition(event->position());
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging)
            updateFromPosition(event->position());
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_dragging = false;
    }

private:
    void updateFromPosition(const QPointF &position)
    {
        const QPointF center = rect().center();
        qreal angle = qRadiansToDegrees(std::atan2(position.x() - center.x(), center.y() - position.y()));
        if (angle < 0)
            angle += 360.0;
        const int newValue = qRound(angle);
        if (newValue == m_value)
            return;
        m_value = newValue;
        update();
        if (valueChanged)
            valueChanged(m_value);
    }

    int m_value{0};
    bool m_dragging{false};
};

namespace
{
const QStringList DefaultBlendModes{
    COMPOSITE_OVER,
    COMPOSITE_MULT,
    COMPOSITE_SCREEN,
    COMPOSITE_DODGE,
    COMPOSITE_OVERLAY,
    COMPOSITE_SOFT_LIGHT_SVG,
    COMPOSITE_HARD_LIGHT,
    COMPOSITE_DARKEN,
    COMPOSITE_LIGHTEN,
    COMPOSITE_GREATER,
};

void selectBlendMode(QComboBox *combo, const QString &id)
{
    int index = combo->findData(id);
    if (index < 0 && !id.isEmpty()) {
        const KoID op = KoCompositeOpRegistry::instance().getKoID(id);
        combo->addItem(op.name().isEmpty() ? id : op.name(), id);
        index = combo->count() - 1;
    }
    if (index >= 0)
        combo->setCurrentIndex(index);
}
} // namespace

QuickAdjustDock::QuickAdjustDock(QWidget *parent, bool compactPopup)
    : QDockWidget(parent)
{
    if (!compactPopup)
        new QuickAdjustKeyController(this);
    const KConfigGroup adjustConfig = KSharedConfig::openConfig()->group(QStringLiteral("QuickAccessAdjust"));
    setWindowTitle(i18nc("@title:window", "Quick Brush Adjustments"));
    setMinimumSize(100, 100);

    auto *root = new QWidget(this);
    auto *outerLayout = new QHBoxLayout(root);
    outerLayout->setContentsMargins(5, 5, 5, 5);
    outerLayout->setSpacing(4);
    auto *content = new QWidget(root);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *brushGroup = new QWidget(root);
    auto *brushLayout = new QVBoxLayout(brushGroup);
    brushLayout->setContentsMargins(0, 0, 0, 0);
    brushLayout->setSpacing(4);
    m_brushSize = createSliderRow(i18nc("@label", "Size"), 0, 100, &m_brushSizeValue);
    m_brushOpacity = createSliderRow(i18nc("@label", "Opacity"), 0, 100, &m_brushOpacityValue);
    m_brushFlow = createSliderRow(i18nc("@label", "Flow"), 0, 100, &m_brushFlowValue);
    m_brushRotation = createSliderRow(i18nc("@label", "Rotation"), 0, 360, &m_brushRotationValue);
    m_brushRotationRow = m_brushRotation->parentWidget();
    m_rotationDial = new QuickRotationDial(m_brushRotationRow);
    if (auto *rotationLayout = qobject_cast<QHBoxLayout *>(m_brushRotationRow->layout()))
        rotationLayout->insertWidget(1, m_rotationDial);
    m_rotationDial->valueChanged = [this](int value) {
        m_brushRotation->setValue(value);
    };
    layout->addWidget(m_brushSize->parentWidget());
    brushLayout->addWidget(m_brushOpacity->parentWidget());
    brushLayout->addWidget(m_brushFlow->parentWidget());
    m_brushSize->parentWidget()->setVisible(adjustConfig.readEntry("SizeSliderEnabled", true));
    m_brushOpacity->parentWidget()->setVisible(adjustConfig.readEntry("OpacitySliderEnabled", true));
    m_brushFlow->parentWidget()->setVisible(adjustConfig.readEntry("FlowSliderEnabled", true));

    auto *brushFooter = new QHBoxLayout;
    m_brushBlend = new QComboBox(brushGroup);
    populateBlendModes(m_brushBlend);
    auto *reset = new QPushButton(QIcon(QStringLiteral(":/quickaccess/system_icons/reset.png")), QString(), brushGroup);
    reset->setToolTip(i18nc("@info:tooltip", "Reload the current brush preset"));
    reset->setFixedSize(24, 24);
    brushFooter->addWidget(m_brushBlend, 1);
    brushFooter->addWidget(reset);
    brushLayout->addLayout(brushFooter);
    auto *layerGroup = new QWidget(root);
    auto *layerLayout = new QVBoxLayout(layerGroup);
    layerLayout->setContentsMargins(0, 0, 0, 0);
    layerLayout->setSpacing(4);
    m_layerOpacity = createSliderRow(i18nc("@label", "Opacity"), 0, 100, &m_layerOpacityValue);
    layerLayout->addWidget(m_layerOpacity->parentWidget());
    m_layerOpacity->parentWidget()->setVisible(adjustConfig.readEntry("LayerOpacitySliderEnabled", true));
    m_layerBlend = new QComboBox(layerGroup);
    populateBlendModes(m_layerBlend);
    layerLayout->addWidget(m_layerBlend);
    auto *brushAndLayer = new QHBoxLayout;
    brushAndLayer->setSpacing(8);
    brushAndLayer->addWidget(brushGroup, 1);
    brushAndLayer->addWidget(layerGroup, 1);
    layout->addLayout(brushAndLayer);
    layout->addWidget(m_brushRotationRow);
    m_colorHistoryGroup = createColorHistoryWidget();
    m_brushHistoryGroup = createBrushHistoryWidget();
    m_colorHistoryGroup->setVisible(adjustConfig.readEntry("ColorHistoryEnabled", true));
    m_brushHistoryGroup->setVisible(adjustConfig.readEntry("BrushHistoryEnabled", true));
    layout->addWidget(m_colorHistoryGroup);
    layout->addWidget(m_brushHistoryGroup);
    layout->addStretch();
    outerLayout->addWidget(content, 1);

    auto *statusLayout = new QVBoxLayout;
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(2);
    QToolButton *toolOptions =
        createStatusButton(i18nc("@info:tooltip", "Show Tool Options"), QStringLiteral("tool_options"));
    toolOptions->setVisible(adjustConfig.readEntry("ToolOptionsEnabled", false));
    m_rotationToggle =
        createStatusButton(i18nc("@info:tooltip", "Show brush rotation control"), QStringLiteral("rotate"));
    m_eraseToggle = createStatusButton(i18nc("@info:tooltip", "Toggle eraser mode"), QStringLiteral("erase_mode"));
    m_alphaToggle =
        createStatusButton(i18nc("@info:tooltip", "Toggle preserve alpha"), QStringLiteral("preserve_alpha"));
    m_selectionToggle =
        createStatusButton(i18nc("@info:tooltip", "Toggle selection outline"), QStringLiteral("selection"));
    m_gestureToggle =
        createStatusButton(i18nc("@info:tooltip", "Toggle Quick Access gestures"), QStringLiteral("gesture"));
    for (QToolButton *button :
         {toolOptions, m_rotationToggle, m_eraseToggle, m_alphaToggle, m_selectionToggle, m_gestureToggle}) {
        statusLayout->addWidget(button, 0, Qt::AlignHCenter);
        if (button != m_gestureToggle) {
            auto *line = new QFrame(root);
            line->setFrameShape(QFrame::HLine);
            line->setFixedWidth(14);
            statusLayout->addWidget(line, 0, Qt::AlignHCenter);
        }
        if (compactPopup)
            button->hide();
    }
    statusLayout->addStretch();
    outerLayout->addLayout(statusLayout);
    setWidget(root);

    m_rotationToggle->setChecked(adjustConfig.readEntry("RotationWidgetStartVisible", false));
    m_brushRotationRow->setVisible(compactPopup || m_rotationToggle->isChecked());
    if (compactPopup) {
        m_colorHistoryGroup->hide();
        m_brushHistoryGroup->hide();
        layout->insertWidget(layout->count() - 1, createBrushToggleWidget());
    }
    bool fontOk = false;
    const int configuredFontSize = adjustConfig.readEntry("FontSize", QStringLiteral("12px"))
                                       .remove(QStringLiteral("px"), Qt::CaseInsensitive)
                                       .toInt(&fontOk);
    if (fontOk)
        content->setStyleSheet(QStringLiteral("font-size: %1px;").arg(qBound(6, configuredFontSize, 36)));
    connect(toolOptions, &QToolButton::clicked, this, [this] {
        triggerAction(QStringLiteral("show_tool_options"));
    });
    connect(m_rotationToggle, &QToolButton::toggled, m_brushRotationRow, &QWidget::setVisible);
    connect(m_eraseToggle, &QToolButton::clicked, this, [this] {
        if (m_resourceProvider)
            m_resourceProvider->setEraserMode(!m_resourceProvider->eraserMode());
    });
    connect(m_alphaToggle, &QToolButton::clicked, this, [this] {
        if (m_resourceProvider)
            m_resourceProvider->setGlobalAlphaLock(!m_resourceProvider->globalAlphaLock());
    });
    m_selectionToggle->setCheckable(false);
    connect(m_gestureToggle, &QToolButton::clicked, this, [this] {
        triggerAction(QStringLiteral("toggle_gesture_recognition"));
    });

    connect(m_brushSize, &QSlider::valueChanged, this, &QuickAdjustDock::slotBrushSizeChanged);
    connect(m_brushOpacity, &QSlider::valueChanged, this, &QuickAdjustDock::slotBrushOpacityChanged);
    connect(m_brushFlow, &QSlider::valueChanged, this, &QuickAdjustDock::slotBrushFlowChanged);
    connect(m_brushRotation, &QSlider::valueChanged, this, &QuickAdjustDock::slotBrushRotationChanged);
    connect(m_brushBlend,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &QuickAdjustDock::slotBrushBlendChanged);
    connect(m_layerOpacity, &QSlider::valueChanged, this, &QuickAdjustDock::slotLayerOpacityChanged);
    connect(m_layerBlend,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &QuickAdjustDock::slotLayerBlendChanged);
    connect(reset, &QPushButton::clicked, this, &QuickAdjustDock::slotResetBrush);

    m_syncTimer = new QTimer(this);
    m_syncTimer->setInterval(200);
    connect(m_syncTimer, &QTimer::timeout, this, &QuickAdjustDock::syncFromCanvas);
    m_syncTimer->start();
    setControlsEnabled(false);
}

QString QuickAdjustDock::observerName()
{
    return QStringLiteral("QuickAdjustDock");
}

void QuickAdjustDock::setCanvas(KoCanvasBase *canvas)
{
    if (m_resourceProvider)
        disconnect(m_resourceProvider, nullptr, this, nullptr);
    m_canvas = dynamic_cast<KisCanvas2 *>(canvas);
    m_resourceProvider =
        m_canvas && m_canvas->viewManager() ? m_canvas->viewManager()->canvasResourceProvider() : nullptr;
    if (m_resourceProvider) {
        if (!colorHistoryResetForSession) {
            m_resourceProvider->setColorHistory({});
            colorHistoryResetForSession = true;
        }
        connect(m_resourceProvider,
                &KisCanvasResourceProvider::sigFGColorUsed,
                this,
                &QuickAdjustDock::slotForegroundColorChanged);
        connect(m_resourceProvider,
                &KisCanvasResourceProvider::sigPaintOpPresetChanged,
                this,
                &QuickAdjustDock::slotBrushPresetChanged);
    }
    setControlsEnabled(m_canvas);
    loadColorHistory();
    slotBrushPresetChanged(m_resourceProvider ? m_resourceProvider->currentPreset() : KisPaintOpPresetSP());
    syncFromCanvas();
}

void QuickAdjustDock::unsetCanvas()
{
    if (m_resourceProvider)
        disconnect(m_resourceProvider, nullptr, this, nullptr);
    m_resourceProvider = nullptr;
    m_canvas.clear();
    m_colorHistory.clear();
    updateColorHistoryButtons();
    updateBrushHistoryButtons();
    setControlsEnabled(false);
}

QSlider *QuickAdjustDock::createSliderRow(const QString &label, int minimum, int maximum, QLabel **valueLabel)
{
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto *name = new QLabel(label, row);
    name->hide();
    auto *slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(minimum, maximum);
    *valueLabel = new QLabel(row);
    (*valueLabel)->setAlignment(Qt::AlignCenter);
    (*valueLabel)->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("1000%")));
    layout->addWidget(name);
    layout->addWidget(slider, 1);
    layout->addWidget(*valueLabel);
    return slider;
}

void QuickAdjustDock::populateBlendModes(QComboBox *combo)
{
    const KConfigGroup config = KSharedConfig::openConfig()->group(QStringLiteral("QuickAccessAdjust"));
    const QStringList configured = config.readEntry("BlendModes", QStringList());
    const QStringList modes = configured.isEmpty() ? DefaultBlendModes : configured;
    for (const QString &id : modes) {
        const KoID op = KoCompositeOpRegistry::instance().getKoID(id);
        combo->addItem(op.name().isEmpty() ? id : op.name(), id);
    }
}

QWidget *QuickAdjustDock::createColorHistoryWidget()
{
    const KConfigGroup config = KSharedConfig::openConfig()->group(QStringLiteral("QuickAccessAdjust"));
    const int colorCount = qBound(2, config.readEntry("ColorHistoryTotal", 14), 40);
    const int columns = (colorCount + 1) / 2;
    const int buttonSize = qBound(16, config.readEntry("ColorHistoryIconSize", 30), 64);

    auto *group = new QWidget(this);
    auto *grid = new QGridLayout(group);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setHorizontalSpacing(1);
    grid->setVerticalSpacing(1);
    for (int i = 0; i < colorCount; ++i) {
        auto *button = new QToolButton(group);
        button->setFixedSize(buttonSize, buttonSize);
        button->setAutoRaise(false);
        connect(button, &QToolButton::clicked, this, [this, i] {
            selectHistoryColor(i);
        });
        grid->addWidget(button, i / columns, i % columns);
        m_colorButtons.append(button);
    }
    grid->setColumnStretch(columns, 1);
    updateColorHistoryButtons();
    return group;
}

QWidget *QuickAdjustDock::createBrushHistoryWidget()
{
    const KConfigGroup config = KSharedConfig::openConfig()->group(QStringLiteral("QuickAccessAdjust"));
    const int brushCount = qBound(2, config.readEntry("BrushHistoryTotal", 14), 40);
    const int columns = (brushCount + 1) / 2;
    const int buttonSize = qBound(16, config.readEntry("BrushHistoryIconSize", 34), 64);

    auto *group = new QWidget(this);
    auto *grid = new QGridLayout(group);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setHorizontalSpacing(1);
    grid->setVerticalSpacing(1);
    for (int i = 0; i < brushCount; ++i) {
        auto *button = new QToolButton(group);
        button->setFixedSize(buttonSize, buttonSize);
        button->setIconSize(QSize(buttonSize - 4, buttonSize - 4));
        button->setStyleSheet(
            QStringLiteral("QToolButton { background-color: #b0b0b0; border: 1px solid #888; border-radius: 4px; }"));
        connect(button, &QToolButton::clicked, this, [this, i] {
            selectHistoryBrush(i);
        });
        grid->addWidget(button, i / columns, i % columns);
        m_brushButtons.append(button);
    }
    grid->setColumnStretch(columns, 1);
    updateBrushHistoryButtons();
    return group;
}

QWidget *QuickAdjustDock::createBrushToggleWidget()
{
    auto *widget = new QWidget(this);
    auto *grid = new QGridLayout(widget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(4);
    const QList<QPair<QString, QString>> properties{
        {QStringLiteral("PressureSize"), QStringLiteral("SizeUseCurve")},
        {QStringLiteral("OpacityUseCurve"), QString()},
        {QStringLiteral("FlowUseCurve"), QString()},
        {QStringLiteral("PressureRotation"), QStringLiteral("RotationUseCurve")},
    };
    const QStringList labels{i18nc("@action:button", "Size"),
                             i18nc("@action:button", "Opacity"),
                             i18nc("@action:button", "Flow"),
                             i18nc("@action:button", "Rotation")};
    for (int i = 0; i < properties.size(); ++i) {
        const QString name = properties.at(i).first;
        const QString pairedName = properties.at(i).second;
        auto *button = new QToolButton(widget);
        button->setText(labels.at(i));
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(button, &QToolButton::toggled, this, [this, name, pairedName](bool checked) {
            if (m_syncing || !m_resourceProvider || !m_resourceProvider->currentPreset())
                return;
            KisPaintOpSettingsSP settings = m_resourceProvider->currentPreset()->settings();
            if (!settings || !settings->hasProperty(name))
                return;
            settings->setProperty(name, checked);
            if (!pairedName.isEmpty() && settings->hasProperty(pairedName))
                settings->setProperty(pairedName, checked);
            m_resourceProvider->setPaintOpPreset(m_resourceProvider->currentPreset());
        });
        grid->addWidget(button, i / 2, i % 2);
        m_brushToggleButtons.insert(name, button);
    }
    return widget;
}

void QuickAdjustDock::loadColorHistory()
{
    m_colorHistory = m_resourceProvider ? m_resourceProvider->colorHistory() : QList<KoColor>();
    while (m_colorHistory.size() > m_colorButtons.size())
        m_colorHistory.removeLast();
    updateColorHistoryButtons();
}

void QuickAdjustDock::updateColorHistoryButtons()
{
    for (int i = 0; i < m_colorButtons.size(); ++i) {
        QToolButton *button = m_colorButtons.at(i);
        if (i < m_colorHistory.size()) {
            const QColor color = m_colorHistory.at(i).toQColor();
            button->setEnabled(true);
            button->setToolTip(i18nc("@info:tooltip", "RGB: %1, %2, %3", color.red(), color.green(), color.blue()));
            button->setStyleSheet(
                QStringLiteral("QToolButton { background-color: %1; border: 1px solid #888; border-radius: 4px; }")
                    .arg(color.name(QColor::HexArgb)));
        } else {
            button->setEnabled(false);
            button->setToolTip(QString());
            button->setStyleSheet(QStringLiteral(
                "QToolButton { background-color: #b0b0b0; border: 1px solid #888; border-radius: 4px; }"));
        }
    }
}

void QuickAdjustDock::selectHistoryColor(int index)
{
    if (m_resourceProvider && index >= 0 && index < m_colorHistory.size())
        m_resourceProvider->setFGColor(m_colorHistory.at(index));
}

void QuickAdjustDock::updateBrushHistoryButtons()
{
    for (int i = 0; i < m_brushButtons.size(); ++i) {
        QToolButton *button = m_brushButtons.at(i);
        if (i < m_brushHistory.size() && m_brushHistory.at(i)) {
            const KisPaintOpPresetSP preset = m_brushHistory.at(i);
            button->setEnabled(true);
            button->setIcon(QIcon(QPixmap::fromImage(preset->image())));
            button->setToolTip(i18nc("@info:tooltip", "Brush: %1", preset->name()));
        } else {
            button->setEnabled(false);
            button->setIcon(QIcon());
            button->setToolTip(QString());
        }
    }
}

void QuickAdjustDock::selectHistoryBrush(int index)
{
    if (m_resourceProvider && index >= 0 && index < m_brushHistory.size())
        m_resourceProvider->setPaintOpPreset(m_brushHistory.at(index));
}

QToolButton *QuickAdjustDock::createStatusButton(const QString &toolTip, const QString &iconName)
{
    auto *button = new QToolButton(this);
    button->setCheckable(true);
    button->setAutoRaise(false);
    button->setFixedSize(18, 18);
    button->setIconSize(QSize(16, 16));
    button->setProperty("quickAccessIconName", iconName);
    button->setToolTip(toolTip);
    return button;
}

void QuickAdjustDock::updateStatusButtons()
{
    auto setStateIcon = [](QToolButton *button, bool active) {
        const QString name = button->property("quickAccessIconName").toString();
        const QString separator = name == QStringLiteral("rotate") ? QStringLiteral("-") : QStringLiteral("_");
        button->setIcon(QIcon(QStringLiteral(":/quickaccess/quick_adjust/%1%2%3.png")
                                  .arg(name, separator, active ? QStringLiteral("on") : QStringLiteral("off"))));
        const QSignalBlocker blocker(button);
        button->setChecked(active);
    };

    setStateIcon(m_rotationToggle, m_brushRotationRow && m_brushRotationRow->isVisible());
    setStateIcon(m_eraseToggle, m_resourceProvider && m_resourceProvider->eraserMode());
    setStateIcon(m_alphaToggle, m_resourceProvider && m_resourceProvider->globalAlphaLock());

    bool selectionVisible = false;
    if (m_canvas && m_canvas->viewManager() && m_canvas->viewManager()->actionManager()) {
        if (KisAction *action =
                m_canvas->viewManager()->actionManager()->actionByName(QStringLiteral("toggle_display_selection"))) {
            selectionVisible = action->isChecked();
        }
    }
    setStateIcon(m_selectionToggle, selectionVisible);
    const KConfigGroup gestures = KSharedConfig::openConfig()->group(QStringLiteral("QuickAccessGesture"));
    setStateIcon(m_gestureToggle, gestures.readEntry("Enabled", true));
}

void QuickAdjustDock::triggerAction(const QString &name)
{
    if (!m_canvas || !m_canvas->viewManager() || !m_canvas->viewManager()->actionManager())
        return;
    if (KisAction *action = m_canvas->viewManager()->actionManager()->actionByName(name))
        action->trigger();
}

void QuickAdjustDock::setControlsEnabled(bool enabled)
{
    if (widget())
        widget()->setEnabled(enabled);
}

int QuickAdjustDock::brushSizeToSlider(qreal size)
{
    size = qBound<qreal>(1.0, size, 1000.0);
    return size <= 100.0 ? qRound((size - 1.0) * 70.0 / 100.0) : qRound(70.0 + (size - 100.0) * 30.0 / 900.0);
}

qreal QuickAdjustDock::sliderToBrushSize(int value)
{
    return value <= 70 ? qreal(1 + value * 100 / 70) : qreal(100 + (value - 70) * 900 / 30);
}

void QuickAdjustDock::syncFromCanvas()
{
    if (!m_canvas || !m_canvas->viewManager())
        return;
    KisCanvasResourceProvider *provider = m_canvas->viewManager()->canvasResourceProvider();
    if (!provider)
        return;

    m_syncing = true;
    {
        const QSignalBlocker sizeBlocker(m_brushSize);
        const QSignalBlocker opacityBlocker(m_brushOpacity);
        const QSignalBlocker flowBlocker(m_brushFlow);
        const QSignalBlocker rotationBlocker(m_brushRotation);
        const QSignalBlocker brushBlendBlocker(m_brushBlend);
        m_brushSize->setValue(brushSizeToSlider(provider->size()));
        m_brushOpacity->setValue(qRound(provider->opacity() * 100.0));
        m_brushFlow->setValue(qRound(provider->flow() * 100.0));
        m_brushRotation->setValue(qRound(provider->brushRotation()));
        m_rotationDial->setValue(m_brushRotation->value());
        selectBlendMode(m_brushBlend, provider->currentCompositeOp());
        m_brushSizeValue->setText(QString::number(qRound(provider->size())));
        m_brushOpacityValue->setText(QStringLiteral("%1%").arg(m_brushOpacity->value()));
        m_brushFlowValue->setText(QStringLiteral("%1%").arg(m_brushFlow->value()));
        m_brushRotationValue->setText(QStringLiteral("%1°").arg(m_brushRotation->value()));
    }

    const KisNodeSP node = m_canvas->viewManager()->activeNode();
    m_layerOpacity->setEnabled(bool(node));
    m_layerBlend->setEnabled(bool(node));
    if (node) {
        const QSignalBlocker opacityBlocker(m_layerOpacity);
        const QSignalBlocker blendBlocker(m_layerBlend);
        m_layerOpacity->setValue(qRound(node->opacity() * 100.0 / 255.0));
        m_layerOpacityValue->setText(QStringLiteral("%1%").arg(m_layerOpacity->value()));
        selectBlendMode(m_layerBlend, node->compositeOpId());
    }
    updateStatusButtons();
    m_syncing = false;
}

void QuickAdjustDock::slotBrushSizeChanged(int value)
{
    const qreal size = sliderToBrushSize(value);
    m_brushSizeValue->setText(QString::number(qRound(size)));
    if (!m_syncing && m_canvas)
        m_canvas->viewManager()->canvasResourceProvider()->setSize(size);
}

void QuickAdjustDock::slotBrushOpacityChanged(int value)
{
    m_brushOpacityValue->setText(QStringLiteral("%1%").arg(value));
    if (!m_syncing && m_canvas)
        m_canvas->viewManager()->canvasResourceProvider()->setOpacity(value / 100.0);
}

void QuickAdjustDock::slotBrushFlowChanged(int value)
{
    m_brushFlowValue->setText(QStringLiteral("%1%").arg(value));
    if (!m_syncing && m_canvas)
        m_canvas->viewManager()->canvasResourceProvider()->setFlow(value / 100.0);
}

void QuickAdjustDock::slotBrushRotationChanged(int value)
{
    m_brushRotationValue->setText(QStringLiteral("%1°").arg(value));
    if (!m_syncing && m_canvas)
        m_canvas->viewManager()->canvasResourceProvider()->setBrushRotation(value);
}

void QuickAdjustDock::slotBrushBlendChanged(int index)
{
    if (!m_syncing && m_canvas && index >= 0)
        m_canvas->viewManager()->canvasResourceProvider()->setCurrentCompositeOp(
            m_brushBlend->itemData(index).toString());
}

void QuickAdjustDock::slotLayerOpacityChanged(int value)
{
    m_layerOpacityValue->setText(QStringLiteral("%1%").arg(value));
    if (!m_syncing && m_canvas && m_canvas->viewManager()->activeNode())
        m_canvas->viewManager()->nodeManager()->setNodeOpacity(m_canvas->viewManager()->activeNode(),
                                                               qRound(value * 255.0 / 100.0));
}

void QuickAdjustDock::slotLayerBlendChanged(int index)
{
    if (m_syncing || !m_canvas || index < 0)
        return;
    const KisNodeSP node = m_canvas->viewManager()->activeNode();
    if (!node || !node->paintDevice())
        return;
    const QString id = m_layerBlend->itemData(index).toString();
    const KoColorSpace *colorSpace = node->paintDevice()->compositionSourceColorSpace();
    const KoCompositeOp *op = colorSpace ? colorSpace->compositeOp(id) : nullptr;
    if (op)
        m_canvas->viewManager()->nodeManager()->setNodeCompositeOp(node, op);
}

void QuickAdjustDock::slotResetBrush()
{
    if (!m_canvas || !m_canvas->viewManager()->actionManager())
        return;
    if (KisAction *action =
            m_canvas->viewManager()->actionManager()->actionByName(QStringLiteral("reload_preset_action"))) {
        action->trigger();
        QTimer::singleShot(150, this, &QuickAdjustDock::syncFromCanvas);
    }
}

void QuickAdjustDock::slotForegroundColorChanged(const KoColor &color)
{
    if (!m_resourceProvider || m_resourceProvider->eraserMode())
        return;
    m_colorHistory.removeAll(color);
    m_colorHistory.prepend(color);
    while (m_colorHistory.size() > m_colorButtons.size())
        m_colorHistory.removeLast();
    m_resourceProvider->setColorHistory(m_colorHistory);
    updateColorHistoryButtons();
}

void QuickAdjustDock::slotBrushPresetChanged(const KisPaintOpPresetSP preset)
{
    if (!preset)
        return;
    if (!m_brushToggleButtons.isEmpty()) {
        const KisPaintOpSettingsSP settings = preset->settings();
        for (auto it = m_brushToggleButtons.begin(); it != m_brushToggleButtons.end(); ++it) {
            const QSignalBlocker blocker(it.value());
            const bool supported = settings && settings->hasProperty(it.key());
            it.value()->setEnabled(supported);
            it.value()->setChecked(supported && settings->getBool(it.key()));
        }
    }
    for (auto it = m_brushHistory.begin(); it != m_brushHistory.end();) {
        if (*it && (*it)->name() == preset->name())
            it = m_brushHistory.erase(it);
        else
            ++it;
    }
    m_brushHistory.prepend(preset);
    while (m_brushHistory.size() > m_brushButtons.size())
        m_brushHistory.removeLast();
    updateBrushHistoryButtons();
}

QString QuickAdjustDockFactory::id() const
{
    return QStringLiteral("brush_adjust_docker");
}

QDockWidget *QuickAdjustDockFactory::createDockWidget()
{
    auto *dock = new QuickAdjustDock;
    dock->setObjectName(id());
    return dock;
}

KoDockFactoryBase::DockPosition QuickAdjustDockFactory::defaultDockPosition() const
{
    return DockRight;
}
