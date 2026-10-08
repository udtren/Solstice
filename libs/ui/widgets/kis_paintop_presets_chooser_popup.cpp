/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2010 Sven Langkamp <sven.langkamp@gmail.com>
 * SPDX-FileCopyrightText: 2011 Srikanth Tiyyagura <srikanth.tulasiram@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "kis_paintop_presets_chooser_popup.h"

#include <QCompleter>
#include <QMenu>
#include <QWidgetAction>
#include <QSlider>
#include <QActionGroup>

#include <KoResource.h>
#include <KisResourceItemChooser.h>
#include <KisResourceItemListView.h>

#include <ui_wdgpaintoppresets.h>
#include <kis_config.h>
#include <KisResourceServerProvider.h>
#include <brushengine/kis_paintop_preset.h>
#include <kis_icon.h>
#include <brushengine/kis_paintop_settings.h>
#include "KisPopupButton.h"

struct KisPaintOpPresetsChooserPopup::Private
{
public:
    Ui_WdgPaintOpPresets uiWdgPaintOpPresets;
    bool firstShown {true};
    QSlider* iconSizeSlider {nullptr};
    KisPopupButton *viewModeButton {nullptr};
    QMenu *menu{nullptr};
    QList<QAction *> modeActions;
    QAction *displaySection{nullptr};
    QAction *sizeSection{nullptr};
    bool strokePreviewOnly{false};
    QAction *scrollToSelectionAction{nullptr};

    void applyScrollToSelection();
};

namespace
{
const QString scrollToSelectionKey = QStringLiteral("Solstice/BrushPresetScrollToSelection");
}

void KisPaintOpPresetsChooserPopup::Private::applyScrollToSelection()
{
    if (!scrollToSelectionAction) {
        return;
    }
    // read every time: another Brush Presets docker may have changed it
    const bool follow = KisConfig(true).readEntry<bool>(scrollToSelectionKey, false);
    scrollToSelectionAction->setChecked(follow);
    uiWdgPaintOpPresets.wdgPresetChooser->itemChooser()->itemView()->setFollowCurrentItem(follow);
}

KisPaintOpPresetsChooserPopup::KisPaintOpPresetsChooserPopup(QWidget * parent)
    : QWidget(parent)
    , m_d(new Private())
{
    m_d->uiWdgPaintOpPresets.setupUi(this);
    QMenu* menu = new QMenu(this);
    m_d->menu = menu;
    menu->setStyleSheet("margin: 6px");

    m_d->displaySection =
        menu->addSection(i18nc("@title Which elements to display (e.g., thumbnails or details)", "Display"));

    QActionGroup *actionGroup = new QActionGroup(this);

    KisPresetChooser::ViewMode mode = (KisPresetChooser::ViewMode)KisConfig(true).presetChooserViewMode();

    QAction* action = menu->addAction(KisIconUtils::loadIcon("view-preview"), i18n("Thumbnails"), this, SLOT(slotThumbnailMode()));
    action->setCheckable(true);
    action->setChecked(mode == KisPresetChooser::THUMBNAIL);
    action->setActionGroup(actionGroup);
    m_d->modeActions << action;

    action = menu->addAction(KisIconUtils::loadIcon("view-list-details"), i18n("Details"), this, SLOT(slotDetailMode()));
    action->setCheckable(true);
    action->setChecked(mode == KisPresetChooser::DETAIL);
    action->setActionGroup(actionGroup);
    m_d->modeActions << action;

    // add widget slider to control icon size
    QSlider* iconSizeSlider = new QSlider(this);
    iconSizeSlider->setOrientation(Qt::Horizontal);
    iconSizeSlider->setRange(30, 80);
    iconSizeSlider->setValue(m_d->uiWdgPaintOpPresets.wdgPresetChooser->iconSize());
    iconSizeSlider->setMinimumHeight(20);
    iconSizeSlider->setMinimumWidth(40);
    iconSizeSlider->setTickInterval(10);
    m_d->iconSizeSlider = iconSizeSlider;

    QWidgetAction *sliderAction= new QWidgetAction(this);
    sliderAction->setDefaultWidget(iconSizeSlider);

    m_d->sizeSection = menu->addSection(i18n("Icon Size"));
    menu->addAction(sliderAction);



    // setting the view mode
    m_d->uiWdgPaintOpPresets.wdgPresetChooser->setViewMode(mode);
    m_d->uiWdgPaintOpPresets.wdgPresetChooser->showTaggingBar(true);

    m_d->uiWdgPaintOpPresets.wdgPresetChooser->itemChooser()->showViewModeBtn(true);
    m_d->viewModeButton = m_d->uiWdgPaintOpPresets.wdgPresetChooser->itemChooser()->viewModeButton();
    m_d->viewModeButton->setPopupWidget(menu);


    connect(m_d->uiWdgPaintOpPresets.wdgPresetChooser, SIGNAL(resourceSelected(KoResourceSP )),
            this, SIGNAL(resourceSelected(KoResourceSP )));
    connect(m_d->uiWdgPaintOpPresets.wdgPresetChooser, SIGNAL(resourceClicked(KoResourceSP )),
            this, SIGNAL(resourceClicked(KoResourceSP ))) ;


    connect(iconSizeSlider, SIGNAL(valueChanged(int)),
            m_d->uiWdgPaintOpPresets.wdgPresetChooser, SLOT(setIconSize(int)));
    connect(menu, SIGNAL(aboutToHide()),
            m_d->uiWdgPaintOpPresets.wdgPresetChooser, SLOT(saveIconSize()));
    connect(m_d->viewModeButton, SIGNAL(pressed()), this, SLOT(slotUpdateMenu()));

}

KisPaintOpPresetsChooserPopup::~KisPaintOpPresetsChooserPopup()
{
    delete m_d;
}

void KisPaintOpPresetsChooserPopup::slotThumbnailMode()
{
    KisConfig(false).setPresetChooserViewMode(KisPresetChooser::THUMBNAIL);
    m_d->uiWdgPaintOpPresets.wdgPresetChooser->setViewMode(KisPresetChooser::THUMBNAIL);
}

void KisPaintOpPresetsChooserPopup::slotDetailMode()
{
    KisConfig(false).setPresetChooserViewMode(KisPresetChooser::DETAIL);
    m_d->uiWdgPaintOpPresets.wdgPresetChooser->setViewMode(KisPresetChooser::DETAIL);
}

void KisPaintOpPresetsChooserPopup::slotUpdateMenu()
{
    QSignalBlocker b(m_d->iconSizeSlider);
    m_d->iconSizeSlider->setValue(m_d->uiWdgPaintOpPresets.wdgPresetChooser->iconSize());
    m_d->applyScrollToSelection();
}

void KisPaintOpPresetsChooserPopup::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    //Workaround to get the column and row size right
    if(m_d->firstShown) {
        m_d->uiWdgPaintOpPresets.wdgPresetChooser->updateViewSettings();
        m_d->firstShown = false;
    }
}

void KisPaintOpPresetsChooserPopup::canvasResourceChanged(KisPaintOpPresetSP  preset)
{
    if (preset) {
        m_d->applyScrollToSelection();
        blockSignals(true);
        m_d->uiWdgPaintOpPresets.wdgPresetChooser->setCurrentResource(preset);
        blockSignals(false);
    }
    m_d->uiWdgPaintOpPresets.wdgPresetChooser->updateViewSettings();
}

void KisPaintOpPresetsChooserPopup::slotThemeChanged()
{
   m_d->viewModeButton->setIcon(KisIconUtils::loadIcon("view-choose"));
   m_d->uiWdgPaintOpPresets.wdgPresetChooser->itemChooser()->updateView(); // updates other icons
}

void KisPaintOpPresetsChooserPopup::updateViewSettings()
{
   m_d->uiWdgPaintOpPresets.wdgPresetChooser->updateViewSettings();
}

void KisPaintOpPresetsChooserPopup::setResponsiveness(bool value)
{
    m_d->uiWdgPaintOpPresets.wdgPresetChooser->itemChooser()->setResponsiveness(value);
}

void KisPaintOpPresetsChooserPopup::enableStrokePreviewSetting()
{
    if (m_d->strokePreviewOnly)
        return;
    m_d->strokePreviewOnly = true;
    m_d->uiWdgPaintOpPresets.wdgPresetChooser->enableDockerFilters();
    // Solstice: the Brush Presets docker always shows stroke previews. The
    // icon view settings (Display: Thumbnails/Details) do not apply there
    // and are hidden; the size slider sets the preview width.
    m_d->uiWdgPaintOpPresets.wdgPresetChooser->setStrokePreviewMode(true);
    for (auto *action : m_d->modeActions)
        action->setVisible(false);
    m_d->displaySection->setVisible(false);
    m_d->sizeSection->setText(i18n("Preview Size"));
    slotUpdateMenu();
}

void KisPaintOpPresetsChooserPopup::enableScrollToSelectionSetting()
{
    if (m_d->scrollToSelectionAction) {
        return;
    }
    m_d->menu->addSection(i18nc("@title Brush Presets docker display menu", "Selection"));
    m_d->scrollToSelectionAction = m_d->menu->addAction(i18n("Scroll to Selected Preset"));
    m_d->scrollToSelectionAction->setCheckable(true);
    m_d->scrollToSelectionAction->setToolTip(
        i18n("When a brush is selected elsewhere, scroll the list to show it. "
             "Otherwise the list keeps its position and only the highlight moves."));
    connect(m_d->scrollToSelectionAction, &QAction::toggled, this, [this](bool checked) {
        KisConfig(false).writeEntry<bool>(scrollToSelectionKey, checked);
        m_d->uiWdgPaintOpPresets.wdgPresetChooser->itemChooser()->itemView()->setFollowCurrentItem(checked);
    });
    m_d->applyScrollToSelection();
}
