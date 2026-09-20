/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QuickColorSelectorDock.h"

#include <KisViewManager.h>
#include <KisVisualColorSelector.h>
#include <kconfiggroup.h>
#include <kis_canvas2.h>
#include <kis_canvas_resource_provider.h>
#include <kis_display_color_converter.h>
#include <klocalizedstring.h>
#include <ksharedconfig.h>

#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

QuickColorSelectorWidget::QuickColorSelectorWidget(QWidget *parent)
    : QWidget(parent)
{
    const KConfigGroup config = KSharedConfig::openConfig()->group(QStringLiteral("QuickAccessHueSVC"));
    m_rgbPercentage = config.readEntry("RgbDisplayMode", QStringLiteral("percentage")) != QStringLiteral("value");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    auto *swatches = new QWidget(this);
    swatches->setFixedSize(48, 44);
    m_background = new QToolButton(swatches);
    m_foreground = new QToolButton(swatches);
    m_background->setGeometry(18, 14, 28, 28);
    m_foreground->setGeometry(2, 0, 28, 28);
    m_background->setToolTip(i18nc("@info:tooltip", "Background color; click to swap colors"));
    m_foreground->setToolTip(i18nc("@info:tooltip", "Foreground color; click to swap colors"));
    layout->addWidget(swatches, 0, Qt::AlignLeft);

    m_selector = new KisVisualColorSelector(this);
    m_selector->setMinimumSize(210, 210);
    m_selector->setMinimumSliderWidth(16);
    m_selector->setSliderPosition(Qt::LeftEdge);
    m_selector->setRenderMode(KisVisualColorSelector::StaticBackground);
    layout->addWidget(m_selector, 1);

    auto *channels = new QGridLayout;
    channels->setContentsMargins(0, 0, 0, 0);
    channels->setHorizontalSpacing(4);
    channels->setVerticalSpacing(3);
    const QStringList names{QStringLiteral("H"),
                            QStringLiteral("S"),
                            QStringLiteral("V"),
                            QStringLiteral("R"),
                            QStringLiteral("G"),
                            QStringLiteral("B")};
    QList<QSlider **> sliderTargets{&m_hueSlider,
                                    &m_saturationSlider,
                                    &m_valueSlider,
                                    &m_redSlider,
                                    &m_greenSlider,
                                    &m_blueSlider};
    QList<QSpinBox **> spinTargets{&m_hue, &m_saturation, &m_value, &m_red, &m_green, &m_blue};
    for (int row = 0; row < names.size(); ++row) {
        auto *label = new QLabel(names.at(row), this);
        label->setFixedWidth(fontMetrics().horizontalAdvance(QStringLiteral("W")) + 2);
        auto *slider = new QSlider(Qt::Horizontal, this);
        auto *spin = new QSpinBox(this);
        const bool hue = row == 0;
        const bool rgb = row >= 3;
        const int maximum = hue ? 359 : (rgb && !m_rgbPercentage ? 255 : 100);
        slider->setRange(0, maximum);
        spin->setRange(0, maximum);
        spin->setFixedWidth(72);
        if (hue)
            spin->setSuffix(QStringLiteral("°"));
        else if (!rgb || m_rgbPercentage)
            spin->setSuffix(QStringLiteral("%"));
        *sliderTargets.at(row) = slider;
        *spinTargets.at(row) = spin;
        connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
        channels->addWidget(label, row, 0);
        channels->addWidget(slider, row, 1);
        channels->addWidget(spin, row, 2);
    }
    layout->addLayout(channels);

    connect(m_selector,
            &KisVisualColorSelector::sigNewColor,
            this,
            &QuickColorSelectorWidget::slotSelectorColorChanged);
    connect(m_hue, qOverload<int>(&QSpinBox::valueChanged), this, &QuickColorSelectorWidget::slotHsvChanged);
    connect(m_saturation, qOverload<int>(&QSpinBox::valueChanged), this, &QuickColorSelectorWidget::slotHsvChanged);
    connect(m_value, qOverload<int>(&QSpinBox::valueChanged), this, &QuickColorSelectorWidget::slotHsvChanged);
    connect(m_red, qOverload<int>(&QSpinBox::valueChanged), this, &QuickColorSelectorWidget::slotRgbChanged);
    connect(m_green, qOverload<int>(&QSpinBox::valueChanged), this, &QuickColorSelectorWidget::slotRgbChanged);
    connect(m_blue, qOverload<int>(&QSpinBox::valueChanged), this, &QuickColorSelectorWidget::slotRgbChanged);
    connect(m_foreground, &QToolButton::clicked, this, &QuickColorSelectorWidget::slotSwapColors);
    connect(m_background, &QToolButton::clicked, this, &QuickColorSelectorWidget::slotSwapColors);
    setEnabled(false);
}

void QuickColorSelectorWidget::setCanvas(KisCanvas2 *canvas)
{
    if (m_resourceProvider)
        disconnect(m_resourceProvider, nullptr, this, nullptr);
    m_canvas = canvas;
    m_resourceProvider =
        m_canvas && m_canvas->viewManager() ? m_canvas->viewManager()->canvasResourceProvider() : nullptr;
    setEnabled(m_resourceProvider);
    if (!m_resourceProvider) {
        m_selector->setDisplayRenderer(nullptr);
        return;
    }

    m_selector->setDisplayRenderer(m_canvas->displayColorConverter()->displayRendererInterface());
    m_selector->slotSetColorSpace(m_canvas->displayColorConverter()->nodeColorSpace());
    connect(m_resourceProvider,
            &KisCanvasResourceProvider::sigFGColorChanged,
            this,
            &QuickColorSelectorWidget::slotCanvasColorChanged);
    connect(m_resourceProvider, &KisCanvasResourceProvider::sigBGColorChanged, this, [this](const KoColor &) {
        updateSwatches();
    });
    slotCanvasColorChanged(m_resourceProvider->fgColor());
}

void QuickColorSelectorWidget::slotSelectorColorChanged(const KoColor &color)
{
    if (m_syncing || !m_resourceProvider)
        return;
    m_resourceProvider->setFGColor(color);
    updateRgb(color);
    updateSwatches();
}

void QuickColorSelectorWidget::slotCanvasColorChanged(const KoColor &color)
{
    m_syncing = true;
    m_selector->slotSetColor(color);
    updateRgb(color);
    updateSwatches();
    m_syncing = false;
}

void QuickColorSelectorWidget::slotRgbChanged()
{
    if (m_syncing || !m_resourceProvider)
        return;
    const KoColor current = m_resourceProvider->fgColor();
    const auto channel = [this](int value) {
        return m_rgbPercentage ? qRound(value * 255.0 / 100.0) : value;
    };
    const KoColor color(QColor(channel(m_red->value()), channel(m_green->value()), channel(m_blue->value())),
                        current.colorSpace());
    m_resourceProvider->setFGColor(color);
}

void QuickColorSelectorWidget::slotHsvChanged()
{
    if (m_syncing || !m_resourceProvider)
        return;
    const QColor rgb = QColor::fromHsv(m_hue->value(),
                                       qRound(m_saturation->value() * 255.0 / 100.0),
                                       qRound(m_value->value() * 255.0 / 100.0));
    const KoColor current = m_resourceProvider->fgColor();
    m_resourceProvider->setFGColor(KoColor(rgb, current.colorSpace()));
}

void QuickColorSelectorWidget::slotSwapColors()
{
    if (!m_resourceProvider)
        return;
    const KoColor foreground = m_resourceProvider->fgColor();
    const KoColor background = m_resourceProvider->bgColor();
    m_resourceProvider->setFGColor(background);
    m_resourceProvider->setBGColor(foreground);
}

void QuickColorSelectorWidget::updateSwatches()
{
    if (!m_resourceProvider)
        return;
    const QColor foreground = m_resourceProvider->fgColor().toQColor();
    const QColor background = m_resourceProvider->bgColor().toQColor();
    m_foreground->setStyleSheet(QStringLiteral("background-color: %1; border: 2px solid palette(highlight);")
                                    .arg(foreground.name(QColor::HexArgb)));
    m_background->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid palette(mid);").arg(background.name(QColor::HexArgb)));
    m_foreground->setToolTip(i18nc("@info:tooltip", "Foreground color"));
    m_background->setToolTip(i18nc("@info:tooltip", "Background color"));
}

void QuickColorSelectorWidget::updateRgb(const KoColor &color)
{
    const QColor rgb = color.toQColor();
    const QSignalBlocker hueBlocker(m_hue);
    const QSignalBlocker saturationBlocker(m_saturation);
    const QSignalBlocker valueBlocker(m_value);
    const QSignalBlocker hueSliderBlocker(m_hueSlider);
    const QSignalBlocker saturationSliderBlocker(m_saturationSlider);
    const QSignalBlocker valueSliderBlocker(m_valueSlider);
    const QSignalBlocker redBlocker(m_red);
    const QSignalBlocker greenBlocker(m_green);
    const QSignalBlocker blueBlocker(m_blue);
    const QSignalBlocker redSliderBlocker(m_redSlider);
    const QSignalBlocker greenSliderBlocker(m_greenSlider);
    const QSignalBlocker blueSliderBlocker(m_blueSlider);
    const auto channel = [this](int value) {
        return m_rgbPercentage ? qRound(value * 100.0 / 255.0) : value;
    };
    const int hue = qMax(0, rgb.hsvHue());
    const int saturation = qRound(rgb.hsvSaturation() * 100.0 / 255.0);
    const int value = qRound(rgb.value() * 100.0 / 255.0);
    const int red = channel(rgb.red());
    const int green = channel(rgb.green());
    const int blue = channel(rgb.blue());
    m_hue->setValue(hue);
    m_hueSlider->setValue(hue);
    m_saturation->setValue(saturation);
    m_saturationSlider->setValue(saturation);
    m_value->setValue(value);
    m_valueSlider->setValue(value);
    m_red->setValue(red);
    m_redSlider->setValue(red);
    m_green->setValue(green);
    m_greenSlider->setValue(green);
    m_blue->setValue(blue);
    m_blueSlider->setValue(blue);
    updateChannelGradients(rgb);
}

void QuickColorSelectorWidget::updateChannelGradients(const QColor &color)
{
    const QString handle = QStringLiteral(
        " QSlider::handle:horizontal { width: 9px; margin: -3px 0; background: palette(button-text); "
        "border: 1px solid palette(base); }");
    const auto style = [&handle](const QString &gradient) {
        return QStringLiteral(
                   "QSlider::groove:horizontal { height: 9px; border: 1px solid palette(mid); "
                   "background: %1; }")
                   .arg(gradient)
            + handle;
    };
    m_hueSlider->setStyleSheet(style(
        QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #ff0000, stop:.166 #ffff00, stop:.333 #00ff00, "
                       "stop:.5 #00ffff, stop:.666 #0000ff, stop:.833 #ff00ff, stop:1 #ff0000)")));
    const int hue = qMax(0, color.hsvHue());
    const QColor saturationStart = QColor::fromHsv(hue, 0, color.value());
    const QColor saturationEnd = QColor::fromHsv(hue, 255, color.value());
    const QColor valueEnd = QColor::fromHsv(hue, color.hsvSaturation(), 255);
    m_saturationSlider->setStyleSheet(style(QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 %1, stop:1 %2)")
                                                .arg(saturationStart.name(), saturationEnd.name())));
    m_valueSlider->setStyleSheet(
        style(QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #000000, stop:1 %1)").arg(valueEnd.name())));
    m_redSlider->setStyleSheet(style(
        QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 %1, stop:1 %2)")
            .arg(QColor(0, color.green(), color.blue()).name(), QColor(255, color.green(), color.blue()).name())));
    m_greenSlider->setStyleSheet(
        style(QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 %1, stop:1 %2)")
                  .arg(QColor(color.red(), 0, color.blue()).name(), QColor(color.red(), 255, color.blue()).name())));
    m_blueSlider->setStyleSheet(
        style(QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 %1, stop:1 %2)")
                  .arg(QColor(color.red(), color.green(), 0).name(), QColor(color.red(), color.green(), 255).name())));
}

QuickColorSelectorDock::QuickColorSelectorDock(QWidget *parent)
    : QDockWidget(parent)
    , m_selector(new QuickColorSelectorWidget(this))
{
    setWindowTitle(i18nc("@title:window", "HueSVC"));
    setWidget(m_selector);
}

QString QuickColorSelectorDock::observerName()
{
    return QStringLiteral("QuickColorSelectorDock");
}

void QuickColorSelectorDock::setCanvas(KoCanvasBase *canvas)
{
    m_selector->setCanvas(dynamic_cast<KisCanvas2 *>(canvas));
}

void QuickColorSelectorDock::unsetCanvas()
{
    m_selector->setCanvas(nullptr);
}

QString QuickColorSelectorDockFactory::id() const
{
    return QStringLiteral("HueSVC");
}

QDockWidget *QuickColorSelectorDockFactory::createDockWidget()
{
    auto *dock = new QuickColorSelectorDock;
    dock->setObjectName(id());
    return dock;
}

KoDockFactoryBase::DockPosition QuickColorSelectorDockFactory::defaultDockPosition() const
{
    return DockRight;
}
