/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisToolOptionsBrushSection.h"

#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include "KisToolOptionsBrushItems.h"
#include "KisToolOptionsParameters.h"
#include "kis_paintop_box.h"
#include "kis_paintop_option.h"
#include "kis_paintop_options_model.h"
#include "kis_paintop_settings_widget.h"
#include <KisBrushStrokePreviewRenderer.h>
#include <KisPaintOpPresetUpdateProxy.h>
#include <KisResourceModel.h>
#include <KisResourceModelProvider.h>
#include <KisResourceTypes.h>
#include <QPainter>
#include <brushengine/kis_paintop_preset.h>
#include <kis_canvas_resource_provider.h>

namespace
{
/// A thin horizontal line between groups of options
QFrame *createSeparator(QWidget *parent)
{
    QFrame *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("ToolOptionsBrushSeparator"));
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedHeight(1);
    line->setForegroundRole(QPalette::Mid);
    return line;
}
} // namespace

KisToolOptionsBrushPreview::KisToolOptionsBrushPreview(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ToolOptionsBrushPreview"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(KisBrushStrokePreviewCache::instance(),
            &KisBrushStrokePreviewCache::previewReady,
            this,
            QOverload<>::of(&QWidget::update));

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(250);
    connect(m_renderTimer, &QTimer::timeout, this, &KisToolOptionsBrushPreview::startModifiedRender);
}

KisToolOptionsBrushPreview::~KisToolOptionsBrushPreview()
{
    KisBrushStrokePreviewCache::instance()->removeConsumer(this);
    if (m_renderer && m_renderer->isRunning()) {
        // a renderer is deleted only after its stroke has drained
        disconnect(m_renderer, nullptr, this, nullptr);
        m_renderer->setParent(nullptr);
        connect(m_renderer, &KisBrushStrokePreviewRenderer::finished, m_renderer, &QObject::deleteLater);
        m_renderer->cancel();
    }
}

void KisToolOptionsBrushPreview::setPreset(KisPaintOpPresetSP preset)
{
    if (m_preset) {
        disconnect(m_preset->updateProxy(), nullptr, this, nullptr);
    }
    m_preset = preset;
    m_modifiedImage = QImage();
    cancelModifiedRender();
    if (m_preset) {
        // the modified mark and the image of the modified preset
        connect(m_preset->updateProxy(), &KisPaintOpPresetUpdateProxy::sigSettingsChanged, this, [this]() {
            update();
            scheduleModifiedRender();
        });
    }
    updateRequest();
    scheduleModifiedRender();
    update();
}

QString KisToolOptionsBrushPreview::text() const
{
    if (!m_preset) {
        return QString();
    }
    QString name = m_preset->name().replace(QLatin1Char('_'), QLatin1Char(' '));
    if (m_preset->isDirty()) {
        name += QLatin1Char('*');
    }
    return name;
}

bool KisToolOptionsBrushPreview::hasImage() const
{
    return hasModifiedImage() || (m_hasRequest && !KisBrushStrokePreviewCache::instance()->preview(m_request).isNull());
}

bool KisToolOptionsBrushPreview::hasModifiedImage() const
{
    return m_preset && m_preset->isDirty() && !m_modifiedImage.isNull();
}

void KisToolOptionsBrushPreview::scheduleModifiedRender()
{
    if (!m_preset || !m_preset->isDirty()) {
        // the cache shows the saved preset
        m_renderTimer->stop();
        cancelModifiedRender();
        m_modifiedImage = QImage();
        return;
    }
    if (isVisible()) {
        m_renderTimer->start();
    }
}

void KisToolOptionsBrushPreview::startModifiedRender()
{
    if (!m_preset || !m_preset->isDirty() || !isVisible()) {
        return;
    }
    if (!m_renderer) {
        m_renderer = new KisBrushStrokePreviewRenderer(this);
        connect(m_renderer,
                &KisBrushStrokePreviewRenderer::finished,
                this,
                [this](const QImage &image, bool cancelled) {
                    if (m_renderAgain) {
                        m_renderAgain = false;
                        startModifiedRender();
                        return;
                    }
                    if (!cancelled && m_preset && m_preset->isDirty()) {
                        m_modifiedImage = image;
                        update();
                    }
                });
    }
    if (m_renderer->isRunning()) {
        // render the latest settings once the running stroke is cancelled
        m_renderAgain = true;
        m_renderer->cancel();
        return;
    }
    // a copy: the settings may change while the stroke is rendered
    m_renderer->start(m_preset->clone().dynamicCast<KisPaintOpPreset>());
}

void KisToolOptionsBrushPreview::cancelModifiedRender()
{
    m_renderAgain = false;
    if (m_renderer && m_renderer->isRunning()) {
        m_renderer->cancel();
    }
}

bool KisToolOptionsBrushPreview::hasHeightForWidth() const
{
    return true;
}

int KisToolOptionsBrushPreview::heightForWidth(int width) const
{
    // the cache's previews are three times as wide as high
    return qBound(36, width / 3, 110);
}

QSize KisToolOptionsBrushPreview::sizeHint() const
{
    return QSize(240, heightForWidth(240));
}

void KisToolOptionsBrushPreview::updateRequest()
{
    m_hasRequest = false;
    if (m_preset && m_preset->resourceId() >= 0) {
        KisAllResourcesModel *model = KisResourceModelProvider::resourceModel(ResourceType::PaintOpPresets);
        const QModelIndex index = model->indexForResourceId(m_preset->resourceId());
        if (index.isValid()) {
            m_request = KisBrushStrokePreviewCache::Request::fromIndex(index);
            m_hasRequest = true;
        }
    }

    if (m_hasRequest && isVisible()) {
        KisBrushStrokePreviewCache::instance()->setRequests(this, {m_request});
    } else {
        KisBrushStrokePreviewCache::instance()->removeConsumer(this);
    }
}

void KisToolOptionsBrushPreview::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateRequest();
    scheduleModifiedRender();
}

void KisToolOptionsBrushPreview::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    KisBrushStrokePreviewCache::instance()->removeConsumer(this);
    m_renderTimer->stop();
    cancelModifiedRender();
}

void KisToolOptionsBrushPreview::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    const QRect rect = contentsRect();
    // the background of the previews in the Brush Presets docker
    painter.fillRect(rect, QColor("#303030"));

    // the modified preset's own stroke while it has one, else the saved one
    QImage image = m_modifiedImage;
    if (!hasModifiedImage()) {
        image = m_hasRequest ? KisBrushStrokePreviewCache::instance()->preview(m_request) : QImage();
    }
    if (!image.isNull()) {
        QSize size = image.size().scaled(rect.size(), Qt::KeepAspectRatio);
        QRect imageRect(QPoint(), size);
        imageRect.moveCenter(rect.center());
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(imageRect, image);
    }

    const QString name = text();
    if (!name.isEmpty()) {
        QFont font = painter.font();
        painter.setFont(font);
        const QFontMetrics metrics(font);
        const QString elided = metrics.elidedText(name, Qt::ElideMiddle, rect.width() - 8);
        const QRect textRect(rect.left(), rect.top(), metrics.horizontalAdvance(elided) + 8, metrics.height() + 2);
        painter.fillRect(textRect, QColor(0, 0, 0, 150));
        painter.setPen(QColor("#e0e0e0"));
        painter.drawText(textRect, Qt::AlignCenter, elided);
    }
}

KisToolOptionsBrushSection::KisToolOptionsBrushSection(KisPaintopBox *paintopBox,
                                                       KisCanvasResourceProvider *resourceProvider,
                                                       QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("KisToolOptionsBrushSection"));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(2);

    // a line, not a heading, between the tool's options and the brush's
    // (user request 2026-10-09: headings took too much room)
    layout->addWidget(createSeparator(this));

    // at the top of the section, above the options rebuild() replaces
    m_preview = new KisToolOptionsBrushPreview(this);
    layout->addWidget(m_preview);

    m_content = new QWidget(this);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(2);
    layout->addWidget(m_content);

    KisToolOptionsBrushItems *items = KisToolOptionsBrushItems::instance();
    connect(items, &KisToolOptionsBrushItems::sigShownItemsChanged, this, &KisToolOptionsBrushSection::rebuild);
    if (paintopBox) {
        QPointer<KisPaintopBox> box(paintopBox);
        connect(paintopBox, &KisPaintopBox::sigCurrentSettingsWidgetChanged, this, [this, box]() {
            setSettingsWidget(box ? box->currentSettingsWidget() : nullptr);
        });
        m_settingsWidget = paintopBox->currentSettingsWidget();
    }
    if (resourceProvider) {
        connect(resourceProvider,
                &KisCanvasResourceProvider::sigPaintOpPresetChanged,
                this,
                &KisToolOptionsBrushSection::setPreset);
        setPreset(resourceProvider->currentPreset());
    }
    rebuild();
}

void KisToolOptionsBrushSection::setPreset(KisPaintOpPresetSP preset)
{
    m_preview->setPreset(preset);
}

void KisToolOptionsBrushSection::setSettingsWidget(KisPaintOpSettingsWidget *settingsWidget)
{
    m_settingsWidget = settingsWidget;
    rebuild();
}

KisToolOptionsBrushSection::~KisToolOptionsBrushSection()
{
}

void KisToolOptionsBrushSection::rebuild()
{
    // a new body each time; the mirrors are its children
    while (QLayoutItem *item = m_contentLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    QWidget *body = new QWidget(m_content);
    QGridLayout *grid = new QGridLayout(body);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(5);
    grid->setVerticalSpacing(2);
    grid->setColumnStretch(1, 1);
    m_contentLayout->addWidget(body);
    int row = 0;

    KisPaintOpSettingsWidget *settings = m_settingsWidget;
    const QList<KisPaintOpOption *> options = settings ? settings->toolOptionsOptions() : QList<KisPaintOpOption *>();
    const QSet<QString> shownItems =
        settings ? KisToolOptionsBrushItems::instance()->shownItems(settings->paintOpId()) : QSet<QString>();

    auto addHint = [grid, body](const QString &text) {
        QLabel *hint = new QLabel(text, body);
        hint->setWordWrap(true);
        hint->setEnabled(false);
        grid->addWidget(hint, 0, 0, 1, 2);
    };

    if (options.isEmpty()) {
        addHint(i18n("This brush engine has no options to show here yet."));
        return;
    }

    auto shownParameters = [settings, shownItems](KisPaintOpOption *option) {
        QList<KisPaintOpOption::ToolOptionsParameter> result;
        Q_FOREACH (const KisPaintOpOption::ToolOptionsParameter &parameter, option->toolOptionsParameters()) {
            if (parameter.control && shownItems.contains(settings->toolOptionsParameterId(option, parameter))) {
                result << parameter;
            }
        }
        return result;
    };
    auto isShown = [shownParameters](KisPaintOpOption *option) {
        return (option->isCheckable() && option->isShownInToolOptions()) || !shownParameters(option).isEmpty();
    };

    // the editor's order, grouped by category as in the editor's list
    QList<KisPaintOpOption::PaintopCategory> categories;
    Q_FOREACH (KisPaintOpOption *option, options) {
        if (isShown(option) && !categories.contains(option->category())) {
            categories << option->category();
        }
    }

    if (categories.isEmpty()) {
        addHint(i18n("Click the eye next to an option in the Brush Editor (F5) to show it here."));
        return;
    }

    Q_FOREACH (KisPaintOpOption::PaintopCategory category, categories) {
        // a line between categories (the Pixel Brush has "Size" in General
        // and in Masked Brush)
        if (category != categories.first()) {
            grid->addWidget(createSeparator(body), row++, 0, 1, 2);
        }

        Q_FOREACH (KisPaintOpOption *option, options) {
            if (option->category() != category || !isShown(option)) {
                continue;
            }

            if (option->isCheckable() && option->isShownInToolOptions()) {
                QCheckBox *checkBox = new QCheckBox(option->label(), body);
                checkBox->setChecked(option->isChecked());
                checkBox->setEnabled(option->isEnabled());
                grid->addWidget(checkBox, row++, 0, 1, 2);

                QPointer<KisPaintOpOption> guarded(option);
                connect(checkBox, &QCheckBox::toggled, option, [guarded](bool checked) {
                    if (guarded && guarded->isChecked() != checked) {
                        guarded->setChecked(checked);
                    }
                });
                connect(option, &KisPaintOpOption::sigCheckedChanged, checkBox, [checkBox](bool checked) {
                    QSignalBlocker blocker(checkBox);
                    checkBox->setChecked(checked);
                });
                connect(option, &KisPaintOpOption::sigEnabledChanged, checkBox, &QWidget::setEnabled);
            }

            const QList<KisPaintOpOption::ToolOptionsParameter> parameters = shownParameters(option);
            // a curve option's Enable Pen Settings sits beside its strength
            // bar when both are shown
            const QString strengthId = QStringLiteral("Strength");
            const QString penSettingsId = QStringLiteral("PenSettings");
            bool hasStrength = false;
            bool hasPenSettings = false;
            for (const KisPaintOpOption::ToolOptionsParameter &parameter : parameters) {
                hasStrength |= parameter.id == strengthId;
                hasPenSettings |= parameter.id == penSettingsId;
            }
            const bool penSettingsBesideStrength = hasStrength && hasPenSettings;

            QHBoxLayout *strengthRow = nullptr;
            Q_FOREACH (const KisPaintOpOption::ToolOptionsParameter &parameter, parameters) {
                if (penSettingsBesideStrength && parameter.id == penSettingsId) {
                    continue;
                }
                auto *mirror = new KisToolOptionsParameterMirror(parameter.control, option, parameter.modeWidget, body);
                if (!mirror->widget()) {
                    delete mirror;
                    continue;
                }
                QLabel *label = new QLabel(parameter.label, body);
                label->setObjectName(QStringLiteral("ToolOptionsParameterLabel"));
                mirror->widget()->setObjectName(QStringLiteral("ToolOptionsParameter_") + parameter.id);
                grid->addWidget(label, row, 0);

                QWidget *rowWidget = mirror->widget();
                if (penSettingsBesideStrength && parameter.id == strengthId) {
                    QWidget *cell = new QWidget(body);
                    strengthRow = new QHBoxLayout(cell);
                    strengthRow->setContentsMargins(0, 0, 0, 0);
                    strengthRow->setSpacing(4);
                    strengthRow->addWidget(mirror->widget(), 1);
                    grid->addWidget(cell, row, 1);
                    rowWidget = cell;

                    Q_FOREACH (const KisPaintOpOption::ToolOptionsParameter &penSettings, parameters) {
                        if (penSettings.id != penSettingsId) {
                            continue;
                        }
                        auto *penMirror = new KisToolOptionsParameterMirror(penSettings.control,
                                                                            option,
                                                                            penSettings.modeWidget,
                                                                            body);
                        if (!penMirror->widget()) {
                            delete penMirror;
                            continue;
                        }
                        penMirror->widget()->setObjectName(QStringLiteral("ToolOptionsParameter_") + penSettings.id);
                        penMirror->widget()->setToolTip(i18n("Enable Pen Settings"));
                        strengthRow->addWidget(penMirror->widget());
                    }
                } else {
                    grid->addWidget(mirror->widget(), row, 1);
                }
                row++;

                // e.g. the Auto tip's Diameter only while the tip is Auto
                auto setShown = [label, rowWidget](bool shown) {
                    label->setVisible(shown);
                    rowWidget->setVisible(shown);
                };
                setShown(mirror->isShownInEditor());
                connect(mirror, &KisToolOptionsParameterMirror::sigShownInEditorChanged, body, setShown);
            }
        }
    }
}
