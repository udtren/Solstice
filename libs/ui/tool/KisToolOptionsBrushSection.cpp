/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisToolOptionsBrushSection.h"

#include <QCheckBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include "KisToolOptionsBrushItems.h"
#include "kis_paintop_box.h"
#include "kis_paintop_option.h"
#include "kis_paintop_options_model.h"
#include "kis_paintop_settings_widget.h"

KisToolOptionsBrushSection::KisToolOptionsBrushSection(KisPaintopBox *paintopBox, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("KisToolOptionsBrushSection"));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(2);

    m_header = new QToolButton(this);
    m_header->setObjectName(QStringLiteral("ToolOptionsBrushHeader"));
    m_header->setText(i18nc("@title Brush options shown in Tool Options", "Brush"));
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setAutoRaise(true);
    m_header->setCheckable(true);
    QFont font = m_header->font();
    font.setBold(true);
    m_header->setFont(font);
    layout->addWidget(m_header);

    m_content = new QWidget(this);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(12, 0, 0, 0);
    m_contentLayout->setSpacing(2);
    layout->addWidget(m_content);

    KisToolOptionsBrushItems *items = KisToolOptionsBrushItems::instance();
    connect(m_header, &QToolButton::toggled, this, [](bool expanded) {
        KisToolOptionsBrushItems::instance()->setSectionCollapsed(!expanded);
    });
    connect(items,
            &KisToolOptionsBrushItems::sigSectionCollapsedChanged,
            this,
            &KisToolOptionsBrushSection::slotCollapsedChanged);
    slotCollapsedChanged(items->isSectionCollapsed());

    connect(items, &KisToolOptionsBrushItems::sigShownItemsChanged, this, &KisToolOptionsBrushSection::rebuild);
    if (paintopBox) {
        QPointer<KisPaintopBox> box(paintopBox);
        connect(paintopBox, &KisPaintopBox::sigCurrentSettingsWidgetChanged, this, [this, box]() {
            setSettingsWidget(box ? box->currentSettingsWidget() : nullptr);
        });
        m_settingsWidget = paintopBox->currentSettingsWidget();
    }
    rebuild();
}

void KisToolOptionsBrushSection::setSettingsWidget(KisPaintOpSettingsWidget *settingsWidget)
{
    m_settingsWidget = settingsWidget;
    rebuild();
}

KisToolOptionsBrushSection::~KisToolOptionsBrushSection()
{
}

void KisToolOptionsBrushSection::slotCollapsedChanged(bool collapsed)
{
    QSignalBlocker blocker(m_header);
    m_header->setChecked(!collapsed);
    m_header->setArrowType(collapsed ? Qt::RightArrow : Qt::DownArrow);
    m_content->setVisible(!collapsed);
}

void KisToolOptionsBrushSection::rebuild()
{
    while (QLayoutItem *item = m_contentLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    KisPaintOpSettingsWidget *settings = m_settingsWidget;
    const QList<KisPaintOpOption *> options = settings ? settings->toolOptionsOptions() : QList<KisPaintOpOption *>();

    auto addHint = [this](const QString &text) {
        QLabel *hint = new QLabel(text, m_content);
        hint->setWordWrap(true);
        hint->setEnabled(false);
        m_contentLayout->addWidget(hint);
    };

    if (options.isEmpty()) {
        addHint(i18n("This brush engine has no options to show here yet."));
        return;
    }

    // the editor's order, grouped by category as in the editor's list
    QList<KisPaintOpOption::PaintopCategory> categories;
    Q_FOREACH (KisPaintOpOption *option, options) {
        if (option->isShownInToolOptions() && !categories.contains(option->category())) {
            categories << option->category();
        }
    }

    if (categories.isEmpty()) {
        addHint(i18n("Click the eye next to an option in the Brush Editor (F5) to show it here."));
        return;
    }

    Q_FOREACH (KisPaintOpOption::PaintopCategory category, categories) {
        if (categories.size() > 1) {
            QLabel *title = new QLabel(KisPaintOpOptionListModel::categoryName(category), m_content);
            QFont font = title->font();
            font.setBold(true);
            title->setFont(font);
            m_contentLayout->addWidget(title);
        }

        Q_FOREACH (KisPaintOpOption *option, options) {
            if (!option->isShownInToolOptions() || option->category() != category) {
                continue;
            }

            QCheckBox *checkBox = new QCheckBox(option->label(), m_content);
            checkBox->setChecked(option->isChecked());
            checkBox->setEnabled(option->isEnabled());
            m_contentLayout->addWidget(checkBox);

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
    }
}
