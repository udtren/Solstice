/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisToolOptionsParameters.h"

#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>

#include <klocalizedstring.h>

#include <KisAngleSelector.h>
#include <KoAspectButton.h>
#include <kis_icon.h>
#include <kis_multipliers_double_slider_spinbox.h>
#include <kis_slider_spin_box.h>
#include <kis_spacing_selection_widget.h>

#include "kis_paintop_option.h"
#include "widgets/kis_cmb_composite.h"

namespace
{
QLayout *layoutHolding(QWidget *widget, int *index)
{
    QWidget *parent = widget->parentWidget();
    if (!parent) {
        return nullptr;
    }
    QList<QLayout *> layouts = parent->findChildren<QLayout *>();
    if (parent->layout()) {
        layouts.prepend(parent->layout());
    }
    Q_FOREACH (QLayout *layout, layouts) {
        const int i = layout->indexOf(widget);
        if (i >= 0) {
            *index = i;
            return layout;
        }
    }
    return nullptr;
}

QWidget *rowLabel(QWidget *control)
{
    int index = -1;
    QLayout *layout = layoutHolding(control, &index);
    if (QGridLayout *grid = qobject_cast<QGridLayout *>(layout)) {
        int row, column, rowSpan, columnSpan;
        grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
        if (column > 0) {
            QLayoutItem *item = grid->itemAtPosition(row, 0);
            if (item && qobject_cast<QLabel *>(item->widget())) {
                return item->widget();
            }
        }
    } else if (QFormLayout *form = qobject_cast<QFormLayout *>(layout)) {
        return form->labelForField(control);
    }
    return nullptr;
}

QIcon eyeIcon(int size)
{
    QIcon icon;
    icon.addPixmap(KisIconUtils::loadIcon("visible").pixmap(size, size), QIcon::Normal, QIcon::On);
    QPixmap empty(size, size);
    empty.fill(Qt::transparent);
    icon.addPixmap(empty, QIcon::Normal, QIcon::Off);
    return icon;
}

/// enabled as shown in the editor, including the page (disabled while its
/// option is unchecked)
bool isEnabledInEditor(QWidget *control, QWidget *page)
{
    if (!page) {
        return control->isEnabled();
    }
    return page->isEnabledTo(page->parentWidget()) && (control == page || control->isEnabledTo(page));
}

template<typename Slider>
void copySliderSettings(const Slider *source, Slider *copy)
{
    if (copy->minimum() != source->minimum() || copy->maximum() != source->maximum()) {
        copy->setRange(source->minimum(), source->maximum());
    }
    if (source->softMinimum() < source->softMaximum()) {
        copy->setSoftRange(source->softMinimum(), source->softMaximum());
    }
    copy->setExponentRatio(source->exponentRatio());
    copy->setPrefix(source->prefix());
    copy->setSuffix(source->suffix());
    copy->setSingleStep(source->singleStep());
}

void copySliderSettings(const KisDoubleSliderSpinBox *source, KisDoubleSliderSpinBox *copy)
{
    if (copy->minimum() != source->minimum() || copy->maximum() != source->maximum()
        || copy->decimals() != source->decimals()) {
        copy->setRange(source->minimum(), source->maximum(), source->decimals());
    }
    if (source->softMinimum() < source->softMaximum()) {
        copy->setSoftRange(source->softMinimum(), source->softMaximum());
    }
    copy->setExponentRatio(source->exponentRatio());
    copy->setPrefix(source->prefix());
    copy->setSuffix(source->suffix());
    copy->setSingleStep(source->singleStep());
}

QList<QRadioButton *> radioButtons(QWidget *control)
{
    return control->findChildren<QRadioButton *>();
}

bool canMirrorSingle(QWidget *control);

/// a group box whose grid holds controls, e.g. the auto tip's Fade
QGridLayout *mirrorableGroupGrid(QWidget *control)
{
    QGroupBox *group = qobject_cast<QGroupBox *>(control);
    if (!group || radioButtons(group).size() >= 2) {
        return nullptr;
    }
    QGridLayout *grid = group->findChild<QGridLayout *>();
    if (!grid) {
        return nullptr;
    }
    for (int i = 0; i < grid->count(); i++) {
        if (grid->itemAt(i)->widget() && canMirrorSingle(grid->itemAt(i)->widget())) {
            return grid;
        }
    }
    return nullptr;
}

bool canMirrorSingle(QWidget *control)
{
    return qobject_cast<KisMultipliersDoubleSliderSpinBox *>(control) || qobject_cast<KisDoubleSliderSpinBox *>(control)
        || qobject_cast<KisSliderSpinBox *>(control) || qobject_cast<QCheckBox *>(control)
        || qobject_cast<KisAngleSelector *>(control) || qobject_cast<KisSpacingSelectionWidget *>(control)
        || qobject_cast<KisCompositeOpListWidget *>(control) || radioButtons(control).size() >= 2;
}
} // namespace

namespace KisToolOptionsParameters
{
QToolButton *createEyeButton(QWidget *parent)
{
    QToolButton *eye = new QToolButton(parent);
    eye->setObjectName(QStringLiteral("ToolOptionsEye"));
    eye->setCheckable(true);
    eye->setFocusPolicy(Qt::NoFocus);
    eye->setToolTip(i18n("Show in Tool Options"));
    const int size = 14;
    eye->setIconSize(QSize(size, size));
    eye->setIcon(eyeIcon(size));
    eye->setFixedSize(size + 6, size + 6);
    eye->hide();
    return eye;
}

bool placeEyeButton(QToolButton *eye, QWidget *control, QWidget *label)
{
    if (!label) {
        label = rowLabel(control);
    }
    QWidget *target = label ? label : control;

    int index = -1;
    QLayout *layout = layoutHolding(target, &index);
    if (!layout) {
        return false;
    }

    // a group box's eye goes next to its title
    const Qt::Alignment eyeAlignment = qobject_cast<QGroupBox *>(target) ? Qt::AlignTop : Qt::AlignVCenter;

    QWidget *row = new QWidget(target->parentWidget());
    row->setObjectName(QStringLiteral("ToolOptionsEyeRow"));
    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(4);

    if (QGridLayout *grid = qobject_cast<QGridLayout *>(layout)) {
        int r, c, rowSpan, columnSpan;
        grid->getItemPosition(index, &r, &c, &rowSpan, &columnSpan);
        QLayoutItem *item = grid->takeAt(index);
        const Qt::Alignment alignment = item->alignment();
        delete item;
        rowLayout->addWidget(eye, 0, eyeAlignment);
        rowLayout->addWidget(target, 1);
        grid->addWidget(row, r, c, rowSpan, columnSpan, alignment);
    } else if (QBoxLayout *box = qobject_cast<QBoxLayout *>(layout)) {
        const int stretch = box->stretch(index);
        QLayoutItem *item = box->takeAt(index);
        delete item;
        rowLayout->addWidget(eye, 0, Qt::AlignTop);
        rowLayout->addWidget(target, 1);
        box->insertWidget(index, row, stretch);
    } else if (QFormLayout *form = qobject_cast<QFormLayout *>(layout)) {
        int r;
        QFormLayout::ItemRole role;
        form->getItemPosition(index, &r, &role);
        QLayoutItem *item = form->takeAt(index);
        delete item;
        rowLayout->addWidget(eye, 0, Qt::AlignVCenter);
        rowLayout->addWidget(target, 1);
        form->setWidget(r, role, row);
    } else {
        delete row;
        return false;
    }

    target->show();
    return true;
}

bool canMirror(QWidget *control)
{
    return canMirrorSingle(control) || mirrorableGroupGrid(control);
}
} // namespace KisToolOptionsParameters

KisToolOptionsParameterMirror::KisToolOptionsParameterMirror(QWidget *control,
                                                             KisPaintOpOption *option,
                                                             QWidget *modeWidget,
                                                             QWidget *parent)
    : QObject(parent)
    , m_control(control)
    , m_modeWidget(modeWidget)
    , m_option(option)
{
    QWidget *page = option ? option->configurationPage() : nullptr;

    if (auto *source = qobject_cast<KisMultipliersDoubleSliderSpinBox *>(control)) {
        // the multiplier's slider has the range; the value is the source's
        KisDoubleSliderSpinBox *sourceSlider = source->findChild<KisDoubleSliderSpinBox *>();
        auto *copy = new KisDoubleSliderSpinBox(parent);
        m_widget = copy;
        m_pullValue = [source, sourceSlider, copy]() {
            QSignalBlocker blocker(copy);
            if (sourceSlider) {
                copySliderSettings(sourceSlider, copy);
            }
            copy->setValue(source->value());
        };
        connect(copy, qOverload<double>(&QDoubleSpinBox::valueChanged), source, [source](qreal value) {
            if (!qFuzzyCompare(source->value(), value)) {
                source->setValue(value);
            }
        });
    } else if (auto *source = qobject_cast<KisDoubleSliderSpinBox *>(control)) {
        auto *copy = new KisDoubleSliderSpinBox(parent);
        m_widget = copy;
        m_pullValue = [source, copy]() {
            QSignalBlocker blocker(copy);
            copySliderSettings(source, copy);
            copy->setValue(source->value());
        };
        connect(copy, qOverload<double>(&QDoubleSpinBox::valueChanged), source, [source](qreal value) {
            if (!qFuzzyCompare(source->value(), value)) {
                source->setValue(value);
            }
        });
    } else if (auto *source = qobject_cast<KisSliderSpinBox *>(control)) {
        auto *copy = new KisSliderSpinBox(parent);
        m_widget = copy;
        m_pullValue = [source, copy]() {
            QSignalBlocker blocker(copy);
            copySliderSettings(source, copy);
            copy->setValue(source->value());
        };
        connect(copy, qOverload<int>(&QSpinBox::valueChanged), source, [source](int value) {
            if (source->value() != value) {
                source->setValue(value);
            }
        });
    } else if (auto *source = qobject_cast<QCheckBox *>(control)) {
        auto *copy = new QCheckBox(parent);
        m_widget = copy;
        m_pullValue = [source, copy]() {
            QSignalBlocker blocker(copy);
            copy->setChecked(source->isChecked());
        };
        connect(copy, &QCheckBox::toggled, source, [source](bool checked) {
            if (source->isChecked() != checked) {
                // click(): the control's connection may listen to clicked()
                source->click();
            }
        });
    } else if (auto *source = qobject_cast<KisAngleSelector *>(control)) {
        auto *copy = new KisAngleSelector(parent);
        copy->setFlipOptionsMode(source->flipOptionsMode());
        copy->setIncreasingDirection(source->increasingDirection());
        copy->setWrapping(source->wrapping());
        copy->setResetAngle(source->resetAngle());
        copy->setSnapAngle(source->snapAngle());
        m_widget = copy;
        m_pullValue = [source, copy]() {
            QSignalBlocker blocker(copy);
            copy->setDecimals(source->decimals());
            copy->setRange(source->minimum(), source->maximum());
            copy->setAngle(source->angle());
        };
        connect(copy, &KisAngleSelector::angleChanged, source, [source](qreal angle) {
            if (!qFuzzyCompare(source->angle(), angle)) {
                source->setAngle(angle);
            }
        });
    } else if (auto *source = qobject_cast<KisSpacingSelectionWidget *>(control)) {
        auto *copy = new KisSpacingSelectionWidget(parent);
        m_widget = copy;
        m_pullValue = [source, copy]() {
            const bool isAuto = source->autoSpacingActive();
            copy->setSpacing(isAuto, isAuto ? source->autoSpacingCoeff() : source->spacing());
        };
        connect(copy, &KisSpacingSelectionWidget::sigSpacingChanged, source, [source, copy]() {
            const bool isAuto = copy->autoSpacingActive();
            source->setSpacing(isAuto, isAuto ? copy->autoSpacingCoeff() : copy->spacing());
            // setSpacing() blocks the signal the option listens to
            Q_EMIT source->sigSpacingChanged();
        });
    } else if (auto *source = qobject_cast<KisCompositeOpListWidget *>(control)) {
        auto *copy = new KisCompositeOpComboBox(parent);
        m_widget = copy;
        m_pullValue = [source, copy]() {
            QSignalBlocker blocker(copy);
            copy->selectCompositeOp(source->selectedCompositeOp());
        };
        connect(copy, qOverload<int>(&QComboBox::currentIndexChanged), source, [source, copy]() {
            const KoID op = copy->selectedCompositeOp();
            if (op.id().isEmpty() || op == source->selectedCompositeOp()) {
                return;
            }
            source->setCompositeOp(op);
            // the option listens to clicks in the list
            Q_EMIT source->clicked(source->currentIndex());
        });
    } else if (QGridLayout *sourceGrid = mirrorableGroupGrid(control)) {
        // the group's grid, its labels and controls copied cell by cell; each
        // control has its own mirror, a link button (Fade) follows its source
        QWidget *copy = new QWidget(parent);
        QGridLayout *grid = new QGridLayout(copy);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(2);
        grid->setColumnStretch(1, 1);
        QList<std::function<void()>> pulls;
        for (int i = 0; i < sourceGrid->count(); i++) {
            QWidget *source = sourceGrid->itemAt(i)->widget();
            if (!source) {
                continue;
            }
            int row, column, rowSpan, columnSpan;
            sourceGrid->getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
            if (QLabel *label = qobject_cast<QLabel *>(source)) {
                grid->addWidget(new QLabel(label->text(), copy), row, column, rowSpan, columnSpan);
            } else if (KoAspectButton *sourceButton = qobject_cast<KoAspectButton *>(source)) {
                KoAspectButton *button = new KoAspectButton(copy);
                grid->addWidget(button, row, column, rowSpan, columnSpan);
                pulls << [sourceButton, button]() {
                    QSignalBlocker blocker(button);
                    button->setKeepAspectRatio(sourceButton->keepAspectRatio());
                };
                connect(button, &KoAspectButton::keepAspectRatioChanged, sourceButton, [sourceButton](bool keep) {
                    // the editor's aspect ratio locker listens to its button
                    sourceButton->setKeepAspectRatio(keep);
                });
                connect(sourceButton,
                        &KoAspectButton::keepAspectRatioChanged,
                        this,
                        &KisToolOptionsParameterMirror::schedulePull);
            } else if (canMirrorSingle(source)) {
                auto *mirror = new KisToolOptionsParameterMirror(source, option, nullptr, copy);
                if (mirror->widget()) {
                    grid->addWidget(mirror->widget(), row, column, rowSpan, columnSpan);
                }
            }
        }
        m_widget = copy;
        m_pullValue = [pulls]() {
            Q_FOREACH (const std::function<void()> &pull, pulls) {
                pull();
            }
        };
    } else if (radioButtons(control).size() >= 2) {
        const QList<QRadioButton *> sources = radioButtons(control);
        QWidget *copy = new QWidget(parent);
        QHBoxLayout *layout = new QHBoxLayout(copy);
        layout->setContentsMargins(0, 0, 0, 0);
        QButtonGroup *group = new QButtonGroup(copy);
        QList<QRadioButton *> copies;
        for (int i = 0; i < sources.size(); i++) {
            QRadioButton *button = new QRadioButton(sources[i]->text(), copy);
            group->addButton(button, i);
            layout->addWidget(button);
            copies << button;
        }
        layout->addStretch(1);
        m_widget = copy;
        m_perButtonEnabled = true;
        // the buttons are enabled one by one (e.g. only Wash with a masked
        // brush)
        Q_FOREACH (QRadioButton *source, sources) {
            source->installEventFilter(this);
        }
        m_pullValue = [sources, copies, page]() {
            for (int i = 0; i < sources.size(); i++) {
                QSignalBlocker blocker(copies[i]);
                copies[i]->setChecked(sources[i]->isChecked());
                copies[i]->setEnabled(isEnabledInEditor(sources[i], page));
            }
        };
        connect(group, &QButtonGroup::idClicked, this, [sources](int id) {
            if (id >= 0 && id < sources.size() && !sources[id]->isChecked()) {
                sources[id]->click();
            }
        });
    }

    if (!m_widget) {
        return;
    }

    if (m_option) {
        connect(m_option, &KisPaintOpOption::sigSettingChanged, this, &KisToolOptionsParameterMirror::schedulePull);
        connect(m_option, &KisPaintOpOption::sigCheckedChanged, this, &KisToolOptionsParameterMirror::schedulePull);
        connect(m_option, &KisPaintOpOption::sigEnabledChanged, this, &KisToolOptionsParameterMirror::schedulePull);
    }
    // the enabled state of the control, and the mode (e.g. Auto or
    // Predefined tip) it belongs to
    for (QWidget *widget = control; widget && widget != page; widget = widget->parentWidget()) {
        widget->installEventFilter(this);
    }
    for (QWidget *widget = modeWidget; widget && widget != page; widget = widget->parentWidget()) {
        widget->installEventFilter(this);
    }
    if (page) {
        page->installEventFilter(this);
    }

    pull();
}

KisToolOptionsParameterMirror::~KisToolOptionsParameterMirror()
{
}

QWidget *KisToolOptionsParameterMirror::widget() const
{
    return m_widget;
}

bool KisToolOptionsParameterMirror::isShownInEditor() const
{
    return m_shownInEditor;
}

bool KisToolOptionsParameterMirror::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    switch (event->type()) {
    case QEvent::ShowToParent:
    case QEvent::HideToParent:
    case QEvent::EnabledChange:
        schedulePull();
        break;
    default:
        break;
    }
    return false;
}

void KisToolOptionsParameterMirror::schedulePull()
{
    if (m_pullScheduled) {
        return;
    }
    m_pullScheduled = true;
    // the control is updated by the same change, possibly after us
    QTimer::singleShot(0, this, [this]() {
        m_pullScheduled = false;
        pull();
    });
}

void KisToolOptionsParameterMirror::pull()
{
    if (!m_control || !m_widget) {
        return;
    }

    QWidget *page = m_option ? m_option->configurationPage() : nullptr;
    m_pullValue();
    if (!m_perButtonEnabled) {
        m_widget->setEnabled(isEnabledInEditor(m_control, page));
    }

    const bool shown = m_modeWidget && page ? m_modeWidget->isVisibleTo(page) : true;
    if (shown != m_shownInEditor) {
        m_shownInEditor = shown;
        Q_EMIT sigShownInEditorChanged(shown);
    }
}
