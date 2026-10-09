/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QRadioButton>
#include <QScrollBar>
#include <QTest>
#include <QToolButton>

#include "KisBrushTestMain.h"

#include <KisBrushBasedOptionStates.h>
#include <KisCompositeOpOptionData.h>
#include <KisGlobalResourcesInterface.h>
#include <KisPaintOpOptionsModel.h>
#include <KisPaintingModeOptionData.h>
#include <KisResourceModel.h>
#include <KisResourceModelProvider.h>
#include <KisResourceTypes.h>
#include <KisSizeOptionData.h>
#include <KisStandardOptionData.h>
#include <KisTextureOptionData.h>
#include <KisToolOptionsBrushItems.h>
#include <KoAspectButton.h>
#include <KoCompositeOpRegistry.h>
#include <brushengine/kis_locked_properties_proxy.h>
#include <brushengine/kis_locked_properties_server.h>
#include <brushengine/kis_paintop_preset.h>
#include <kis_categorized_list_model.h>
#include <kis_config.h>
#include <kis_paintop_option.h>
#include <kis_paintop_options_model.h>
#include <kis_slider_spin_box.h>
#include <kis_spacing_selection_widget.h>
#include <tool/KisToolOptionsBrushSection.h>
#include <widgets/kis_categorized_list_view.h>
#include <widgets/kis_cmb_composite.h>

#include "../kis_brushop_settings_widget.h"

/**
 * Phase 3a of the brush option shared model (docs/agent/tool-options-brush.md):
 * the eyes in the Brush Editor's option list and the Brush section of Tool
 * Options.
 */
class KisToolOptionsBrushTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init();
    void testShowableOptions();
    void testEyeIsKeptPerEngine();
    void testEyeClickInList();
    void testSectionFollowsOptions();
    void testParametersRegistered();
    void testDiameterMirror();
    void testTipTypeVisibility();
    void testOtherMirrors();
    void testPresetPreview();
    void testFadeGroup();
    void testOptionListScrollBarColor();
    void testLockWritesModelOption();
};

namespace
{
const QString paintbrush = QStringLiteral("paintbrush");

struct Editor {
    Editor()
        : widget(nullptr, KisGlobalResourcesInterface::instance(), KoCanvasResourcesInterfaceSP())
    {
        widget.setResourcesInterface(KisGlobalResourcesInterface::instance());
        widget.setPaintOpId(paintbrush);
        view = widget.findChild<KisCategorizedListView *>();
        model = view ? dynamic_cast<KisPaintOpOptionListModel *>(view->model()) : nullptr;
    }

    KisPaintOpOption *option(const QString &id) const
    {
        Q_FOREACH (KisPaintOpOption *option, widget.toolOptionsOptions()) {
            if (option->toolOptionsId() == id) {
                return option;
            }
        }
        return nullptr;
    }

    QModelIndex row(const QString &id) const
    {
        for (int row = 0; row < model->rowCount(QModelIndex()); row++) {
            const QModelIndex index = model->index(row);
            KisOptionInfo info;
            if (!index.data(__CategorizedListModelBase::IsHeaderRole).toBool() && model->entryAt(info, index)
                && info.option->toolOptionsId() == id) {
                return index;
            }
        }
        return QModelIndex();
    }

    KisBrushOpSettingsWidget widget;
    KisCategorizedListView *view{nullptr};
    KisPaintOpOptionListModel *model{nullptr};
};

QStringList checkBoxLabels(QWidget *parent)
{
    QStringList labels;
    Q_FOREACH (QCheckBox *checkBox, parent->findChildren<QCheckBox *>()) {
        labels << checkBox->text();
    }
    return labels;
}
} // namespace

void KisToolOptionsBrushTest::init()
{
    KisToolOptionsBrushItems *items = KisToolOptionsBrushItems::instance();
    Q_FOREACH (const QString &id, items->shownItems(paintbrush)) {
        items->setShown(paintbrush, id, false);
    }
    items->setSectionCollapsed(false);
}

/// Only checkable options get an eye; the ids are the model's option ids.
void KisToolOptionsBrushTest::testShowableOptions()
{
    Editor editor;
    QVERIFY(editor.view && editor.model);

    QStringList ids;
    Q_FOREACH (KisPaintOpOption *option, editor.widget.toolOptionsOptions()) {
        ids << option->toolOptionsId();
    }
    for (const QString &id : {QStringLiteral("Size"),
                              QStringLiteral("Ratio"),
                              QStringLiteral("Spacing"),
                              QStringLiteral("LightnessStrength"),
                              QStringLiteral("Texture"),
                              QStringLiteral("MaskingBrush"),
                              QStringLiteral("MaskingSize")}) {
        QVERIFY2(ids.contains(id), qPrintable(id));
    }
    // the ids are unique within the engine
    QCOMPARE(QSet<QString>(ids.begin(), ids.end()).size(), ids.size());
    QVERIFY(ids.contains(QStringLiteral("PaintingMode")));

    // Opacity has no checkbox; its Enable Pen Settings is a page parameter
    QVERIFY(ids.contains(QStringLiteral("Opacity")));
    QVERIFY(
        !editor.row(QStringLiteral("Opacity")).data(__CategorizedListModelBase::isShowableInToolOptionsRole).toBool());
    // a checkable curve option is switched by its row's checkbox instead
    Q_FOREACH (const KisPaintOpOption::ToolOptionsParameter &p,
               editor.option(QStringLiteral("Size"))->toolOptionsParameters()) {
        QVERIFY(p.id != QStringLiteral("PenSettings"));
    }
    // page parameters only (phase 3b), no row eye
    QVERIFY(ids.contains(QStringLiteral("BrushTip")));
    QVERIFY(
        !editor.row(QStringLiteral("BrushTip")).data(__CategorizedListModelBase::isShowableInToolOptionsRole).toBool());

    QVERIFY(editor.row(QStringLiteral("Size")).data(__CategorizedListModelBase::isShowableInToolOptionsRole).toBool());

    // a row without a checkbox has no eye
    bool foundUnshowable = false;
    for (int row = 0; row < editor.model->rowCount(QModelIndex()); row++) {
        const QModelIndex index = editor.model->index(row);
        KisOptionInfo info;
        if (!index.data(__CategorizedListModelBase::IsHeaderRole).toBool() && editor.model->entryAt(info, index)
            && !info.option->isCheckable()) {
            QVERIFY(!index.data(__CategorizedListModelBase::isShowableInToolOptionsRole).toBool());
            foundUnshowable = true;
        }
    }
    QVERIFY(foundUnshowable);
}

/// The eye is kept per engine in kritarc and shared by all editors of the
/// engine (e.g. in other windows).
void KisToolOptionsBrushTest::testEyeIsKeptPerEngine()
{
    Editor first;
    Editor second;
    const QModelIndex size = first.row(QStringLiteral("Size"));
    QVERIFY(size.isValid());

    QVERIFY(first.model->setData(size, true, __CategorizedListModelBase::isShownInToolOptionsRole));
    QVERIFY(first.option(QStringLiteral("Size"))->isShownInToolOptions());
    QVERIFY(size.data(__CategorizedListModelBase::isShownInToolOptionsRole).toBool());
    QVERIFY(KisToolOptionsBrushItems::instance()->isShown(paintbrush, QStringLiteral("Size")));
    QVERIFY(second.option(QStringLiteral("Size"))->isShownInToolOptions());
    QVERIFY(KisConfig(true)
                .readEntry<QString>(QStringLiteral("Solstice/ToolOptionsBrushItems/paintbrush"), QString())
                .split(QLatin1Char(','))
                .contains(QStringLiteral("Size")));

    // a new editor starts with the saved items
    Editor third;
    QVERIFY(third.option(QStringLiteral("Size"))->isShownInToolOptions());
    QVERIFY(!third.option(QStringLiteral("Ratio"))->isShownInToolOptions());

    second.option(QStringLiteral("Size"))->setShownInToolOptions(false);
    QVERIFY(!first.option(QStringLiteral("Size"))->isShownInToolOptions());
    QVERIFY(!KisToolOptionsBrushItems::instance()->isShown(paintbrush, QStringLiteral("Size")));
}

/// Clicking the eye toggles it without toggling the option's checkbox.
void KisToolOptionsBrushTest::testEyeClickInList()
{
    Editor editor;
    editor.widget.resize(900, 700);
    editor.widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor.widget));

    const QModelIndex size = editor.row(QStringLiteral("Size"));
    KisPaintOpOption *option = editor.option(QStringLiteral("Size"));
    const bool checked = option->isChecked();
    const QRect rect = editor.view->visualRect(size);
    QVERIFY(rect.isValid());

    QTest::mouseClick(editor.view->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      QPoint(rect.left() + 5, rect.center().y()));
    QVERIFY(option->isShownInToolOptions());
    QCOMPARE(option->isChecked(), checked);

    QTest::mouseClick(editor.view->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      QPoint(rect.left() + 5, rect.center().y()));
    QVERIFY(!option->isShownInToolOptions());
    QCOMPARE(option->isChecked(), checked);

    // the label area does not touch the eye
    QTest::mouseClick(editor.view->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      QPoint(rect.right() - 30, rect.center().y()));
    QVERIFY(!option->isShownInToolOptions());
}

/// The Brush section shows a checkbox for each shown option and keeps it in
/// sync with the option (and through it with the options model).
void KisToolOptionsBrushTest::testSectionFollowsOptions()
{
    Editor editor;
    KisToolOptionsBrushSection section(nullptr);
    section.setSettingsWidget(&editor.widget);
    QVERIFY(checkBoxLabels(&section).isEmpty());
    QVERIFY(!section.findChildren<QLabel *>().isEmpty()); // the hint

    KisPaintOpOption *size = editor.option(QStringLiteral("Size"));
    size->setShownInToolOptions(true);
    QCOMPARE(checkBoxLabels(&section), QStringList{size->label()});

    QCheckBox *checkBox = section.findChild<QCheckBox *>();
    QCOMPARE(checkBox->isChecked(), size->isChecked());

    // the checkbox edits the option, and the option's state is the model's
    auto *sizeState = dynamic_cast<KisPaintOpOptionState<KisSizeOptionData> *>(
        editor.widget.optionsModel()->option(QStringLiteral("Size")));
    QVERIFY(sizeState);
    const bool before = size->isChecked();
    checkBox->setChecked(!before);
    QCOMPARE(size->isChecked(), !before);
    QCOMPARE(sizeState->data().isChecked, !before);

    // an edit elsewhere (the Brush Editor, a preset switch) updates it
    size->setChecked(before);
    QCOMPARE(checkBox->isChecked(), before);

    // options of another category come under a header
    editor.option(QStringLiteral("MaskingSize"))->setShownInToolOptions(true);
    QCOMPARE(checkBoxLabels(&section).size(), 2);
    QStringList titles;
    Q_FOREACH (QLabel *label, section.findChildren<QLabel *>()) {
        titles << label->text();
    }
    QVERIFY(titles.contains(KisPaintOpOptionListModel::categoryName(KisPaintOpOption::GENERAL)));
    QVERIFY(titles.contains(KisPaintOpOptionListModel::categoryName(KisPaintOpOption::MASKING_BRUSH)));

    // an option that does not apply is disabled, as in the editor
    KisPaintOpOption *lightness = editor.option(QStringLiteral("LightnessStrength"));
    lightness->setShownInToolOptions(true);
    Q_FOREACH (QCheckBox *box, section.findChildren<QCheckBox *>()) {
        if (box->text() == lightness->label()) {
            QCOMPARE(box->isEnabled(), lightness->isEnabled());
        }
    }

    // hidden again: Masked Brush Size and Lightness Strength remain
    size->setShownInToolOptions(false);
    QCOMPARE(checkBoxLabels(&section).size(), 2);
}

namespace
{
KisPaintOpOption::ToolOptionsParameter parameter(KisPaintOpOption *option, const QString &id)
{
    Q_FOREACH (const KisPaintOpOption::ToolOptionsParameter &parameter, option->toolOptionsParameters()) {
        if (parameter.id == id) {
            return parameter;
        }
    }
    return KisPaintOpOption::ToolOptionsParameter();
}

/// Shows the parameter in Tool Options with its eye in the editor.
void showParameter(Editor &editor, const QString &optionId, const QString &parameterId)
{
    KisPaintOpOption::ToolOptionsParameter p = parameter(editor.option(optionId), parameterId);
    QVERIFY2(p.eye, qPrintable(optionId + "/" + parameterId));
    p.eye->setChecked(true);
}

template<typename Widget>
Widget *mirrorWidget(QWidget *section, const QString &parameterId)
{
    return section->findChild<Widget *>(QStringLiteral("ToolOptionsParameter_") + parameterId);
}

/// the queued updates of the mirrors
void settle()
{
    QTest::qWait(20);
}

template<typename Data>
KisPaintOpOptionState<Data> *typedOption(KisPaintOpOptionsModel *model, const QString &id)
{
    return dynamic_cast<KisPaintOpOptionState<Data> *>(model->option(id));
}

void loadPreset(Editor &editor, const QString &fileName)
{
    KisPaintOpPresetSP preset(new KisPaintOpPreset(QDir(QString(FILES_DATA_DIR)).filePath("brushtip/" + fileName)));
    QVERIFY(preset->load(KisGlobalResourcesInterface::instance()));
    editor.widget.setConfigurationSafe(preset->settings());
}
} // namespace

/// The main page parameters have an eye once the engine supports Tool
/// Options; the eye is kept per engine as "<option>/<parameter>".
void KisToolOptionsBrushTest::testParametersRegistered()
{
    Editor editor;
    const QMap<QString, QStringList> expected{
        {QStringLiteral("BrushTip"),
         {QStringLiteral("Diameter"),
          QStringLiteral("Ratio"),
          QStringLiteral("Fade"),
          QStringLiteral("Angle"),
          QStringLiteral("Density"),
          QStringLiteral("Spacing"),
          QStringLiteral("PredefinedSize"),
          QStringLiteral("PredefinedAngle"),
          QStringLiteral("PredefinedSpacing"),
          QStringLiteral("Precision"),
          QStringLiteral("AutoPrecision")}},
        {QStringLiteral("CompositeOp"), {QStringLiteral("BlendingMode")}},
        {QStringLiteral("Opacity"), {QStringLiteral("Strength"), QStringLiteral("PenSettings")}},
        {QStringLiteral("Flow"), {QStringLiteral("Strength"), QStringLiteral("PenSettings")}},
        {QStringLiteral("MaskingOpacity"), {QStringLiteral("Strength"), QStringLiteral("PenSettings")}},
        {QStringLiteral("PaintingMode"), {QStringLiteral("PaintingMode")}},
        {QStringLiteral("Texture"), {QStringLiteral("Scale")}},
    };
    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
        KisPaintOpOption *option = editor.option(it.key());
        QVERIFY2(option, qPrintable(it.key()));
        for (const QString &id : it.value()) {
            const KisPaintOpOption::ToolOptionsParameter p = parameter(option, id);
            QVERIFY2(p.control && p.eye, qPrintable(it.key() + "/" + id));
            QVERIFY2(!p.eye->isHidden(), qPrintable(it.key() + "/" + id));
        }
    }

    showParameter(editor, QStringLiteral("BrushTip"), QStringLiteral("Diameter"));
    QVERIFY(KisToolOptionsBrushItems::instance()->isShown(paintbrush, QStringLiteral("BrushTip/Diameter")));

    // an editor without Tool Options support keeps the eyes hidden
    KisBrushOpSettingsWidget unsupported(nullptr,
                                         KisGlobalResourcesInterface::instance(),
                                         KoCanvasResourcesInterfaceSP());
    QToolButton *eye = unsupported.findChild<QToolButton *>(QStringLiteral("ToolOptionsEye"));
    QVERIFY(eye);
    QVERIFY(eye->isHidden());
}

/// The copy of a slider writes through the editor to the options model and
/// follows changes of the model.
void KisToolOptionsBrushTest::testDiameterMirror()
{
    Editor editor;
    loadPreset(editor, QStringLiteral("b_Basic-5_Size_Opacity.kpp"));
    showParameter(editor, QStringLiteral("BrushTip"), QStringLiteral("Diameter"));

    KisToolOptionsBrushSection section(nullptr);
    section.setSettingsWidget(&editor.widget);
    settle();

    auto *source = qobject_cast<KisDoubleSliderSpinBox *>(
        parameter(editor.option(QStringLiteral("BrushTip")), QStringLiteral("Diameter")).control.data());
    auto *copy = mirrorWidget<KisDoubleSliderSpinBox>(&section, QStringLiteral("Diameter"));
    QVERIFY(source && copy);
    QCOMPARE(copy->value(), source->value());
    QCOMPARE(copy->maximum(), source->maximum());
    QCOMPARE(copy->exponentRatio(), source->exponentRatio());

    auto *brushTip =
        dynamic_cast<KisBrushTipOptionState *>(editor.widget.optionsModel()->option(QStringLiteral("BrushTip")));
    QVERIFY(brushTip);
    copy->setValue(42.0);
    QCOMPARE(brushTip->data().commonBrushSize, 42.0);
    QCOMPARE(source->value(), 42.0);

    KisBrushTipOptionData data = brushTip->data();
    data.commonBrushSize = 30.0;
    brushTip->cursor().set(data);
    settle();
    QCOMPARE(copy->value(), 30.0);
}

/// Auto tip parameters are shown only for an auto tip, predefined tip
/// parameters only for a predefined tip.
void KisToolOptionsBrushTest::testTipTypeVisibility()
{
    Editor editor;
    showParameter(editor, QStringLiteral("BrushTip"), QStringLiteral("Diameter"));
    showParameter(editor, QStringLiteral("BrushTip"), QStringLiteral("PredefinedSize"));
    showParameter(editor, QStringLiteral("Texture"), QStringLiteral("Scale"));

    KisToolOptionsBrushSection section(nullptr);
    section.setSettingsWidget(&editor.widget);

    loadPreset(editor, QStringLiteral("b_Basic-5_Size_Opacity.kpp"));
    settle();
    QVERIFY(!mirrorWidget<QWidget>(&section, QStringLiteral("Diameter"))->isHidden());
    QVERIFY(mirrorWidget<QWidget>(&section, QStringLiteral("PredefinedSize"))->isHidden());

    loadPreset(editor, QStringLiteral("b_Basic-6_Details.kpp"));
    settle();
    QVERIFY(mirrorWidget<QWidget>(&section, QStringLiteral("Diameter"))->isHidden());
    QVERIFY(!mirrorWidget<QWidget>(&section, QStringLiteral("PredefinedSize"))->isHidden());

    // a page's own tabs do not hide a parameter
    QVERIFY(!mirrorWidget<QWidget>(&section, QStringLiteral("Scale"))->isHidden());
}

/// Spacing, blending mode, painting mode and texture scale write through to
/// the options model.
void KisToolOptionsBrushTest::testOtherMirrors()
{
    Editor editor;
    loadPreset(editor, QStringLiteral("h_Charcoal_Pencil_Medium.kpp"));
    showParameter(editor, QStringLiteral("BrushTip"), QStringLiteral("Spacing"));
    showParameter(editor, QStringLiteral("CompositeOp"), QStringLiteral("BlendingMode"));
    showParameter(editor, QStringLiteral("PaintingMode"), QStringLiteral("PaintingMode"));
    showParameter(editor, QStringLiteral("Texture"), QStringLiteral("Scale"));

    KisToolOptionsBrushSection section(nullptr);
    section.setSettingsWidget(&editor.widget);
    settle();
    KisPaintOpOptionsModel *model = editor.widget.optionsModel();

    // spacing: the spacing widget's signal is emitted for the editor
    auto *brushTip = dynamic_cast<KisBrushTipOptionState *>(model->option(QStringLiteral("BrushTip")));
    auto *spacing = mirrorWidget<KisSpacingSelectionWidget>(&section, QStringLiteral("Spacing"));
    QVERIFY(spacing);
    spacing->setSpacing(false, 0.7);
    Q_EMIT spacing->sigSpacingChanged();
    QCOMPARE(brushTip->data().brush.common.useAutoSpacing, false);
    QCOMPARE(brushTip->data().brush.common.spacing, 0.7);

    // blending mode: the list's click is emitted for the editor
    auto *blending = mirrorWidget<KisCompositeOpComboBox>(&section, QStringLiteral("BlendingMode"));
    QVERIFY(blending);
    blending->selectCompositeOp(KoCompositeOpRegistry::instance().getKoID(COMPOSITE_MULT));
    auto *compositeOp = typedOption<KisCompositeOpOptionData>(model, QStringLiteral("CompositeOp"));
    QCOMPARE(compositeOp->data().compositeOpId, QString(COMPOSITE_MULT));

    // painting mode: disabled while the masked brush is enabled, as in the
    // editor
    QWidget *paintingMode = mirrorWidget<QWidget>(&section, QStringLiteral("PaintingMode"));
    QVERIFY(paintingMode);
    QList<QRadioButton *> radios = paintingMode->findChildren<QRadioButton *>();
    QCOMPARE(radios.size(), 2);
    QVERIFY(!radios[0]->isEnabled());

    auto *masking = dynamic_cast<KisMaskingBrushOptionState *>(model->option(QStringLiteral("MaskingBrush")));
    KisMaskingBrushOptionData maskingData = masking->data();
    maskingData.masking.isEnabled = false;
    masking->cursor().set(maskingData);
    settle();
    QVERIFY(radios[0]->isEnabled());
    auto *paintingModeState = typedOption<KisPaintingModeOptionData>(model, QStringLiteral("PaintingMode"));
    radios[0]->click();
    QCOMPARE(paintingModeState->data().paintingMode, enumPaintingMode::BUILDUP);
    radios[1]->click();
    QCOMPARE(paintingModeState->data().paintingMode, enumPaintingMode::WASH);

    // Opacity's Enable Pen Settings
    showParameter(editor, QStringLiteral("Opacity"), QStringLiteral("PenSettings"));
    settle();
    auto *penSettings = mirrorWidget<QCheckBox>(&section, QStringLiteral("PenSettings"));
    QVERIFY(penSettings);
    auto *opacity = typedOption<KisOpacityOptionData>(model, QStringLiteral("Opacity"));
    const bool useCurve = opacity->data().useCurve;
    QCOMPARE(penSettings->isChecked(), useCurve);
    penSettings->setChecked(!useCurve);
    QCOMPARE(opacity->data().useCurve, !useCurve);
    KisOpacityOptionData opacityData = opacity->data();
    opacityData.useCurve = useCurve;
    opacity->cursor().set(opacityData);
    settle();
    QCOMPARE(penSettings->isChecked(), useCurve);

    // Opacity's strength value (the bar at the top of its page)
    showParameter(editor, QStringLiteral("Opacity"), QStringLiteral("Strength"));
    settle();
    auto *strength = mirrorWidget<KisDoubleSliderSpinBox>(&section, QStringLiteral("Strength"));
    QVERIFY(strength);
    strength->setValue(40);
    QCOMPARE(opacity->data().strengthValue, 0.4);
    opacityData = opacity->data();
    opacityData.strengthValue = 0.75;
    opacity->cursor().set(opacityData);
    settle();
    QCOMPARE(strength->value(), 75.0);

    // texture scale
    auto *scale = mirrorWidget<KisDoubleSliderSpinBox>(&section, QStringLiteral("Scale"));
    QVERIFY(scale);
    scale->setValue(0.5);
    auto *texture = typedOption<KisTextureOptionData>(model, QStringLiteral("Texture"));
    QCOMPARE(texture->data().scale, 0.5);
}

/// The current preset's stroke preview and name are at the top of the
/// section, from the stroke preview cache.
void KisToolOptionsBrushTest::testPresetPreview()
{
    // a preset of the resource database (the cache keys its previews by it)
    KisAllResourcesModel *presets = KisResourceModelProvider::resourceModel(ResourceType::PaintOpPresets);
    KisPaintOpPresetSP preset;
    for (int row = 0; row < presets->rowCount() && !preset; row++) {
        KisPaintOpPresetSP candidate =
            presets->resourceForIndex(presets->index(row, 0)).dynamicCast<KisPaintOpPreset>();
        if (candidate && candidate->paintOp().id() == paintbrush) {
            preset = candidate;
        }
    }
    QVERIFY(preset);

    KisToolOptionsBrushSection section(nullptr);
    section.resize(300, 400);
    section.setPreset(preset);
    section.show();
    QVERIFY(QTest::qWaitForWindowExposed(&section));

    auto *preview = section.findChild<KisToolOptionsBrushPreview *>();
    QVERIFY(preview);
    QVERIFY(!preview->isHidden());
    QCOMPARE(preview->text(), preset->name().replace(QLatin1Char('_'), QLatin1Char(' ')));
    QTRY_VERIFY_WITH_TIMEOUT(preview->hasImage(), 20000);

    // the modified mark, as in the Brush Presets docker
    preset->setDirty(true);
    QVERIFY(preview->text().endsWith(QLatin1Char('*')));
    preset->setDirty(false);

    // a change of the settings renders the modified preset's own stroke; the
    // saved preset again shows the cache's image
    QVERIFY(!preview->hasModifiedImage());
    const qreal size = preset->settings()->paintOpSize();
    preset->settings()->setPaintOpSize(size * 2);
    QVERIFY(preset->isDirty());
    QTRY_VERIFY_WITH_TIMEOUT(preview->hasModifiedImage(), 20000);
    preset->settings()->setPaintOpSize(size);
    preset->setDirty(false);
    QVERIFY(!preview->hasModifiedImage());
    QVERIFY(preview->hasImage());

    // collapsing the section hides it
    KisToolOptionsBrushItems::instance()->setSectionCollapsed(true);
    QVERIFY(preview->isHidden());
    KisToolOptionsBrushItems::instance()->setSectionCollapsed(false);
    QVERIFY(!preview->isHidden());
}

/// The auto tip's Fade is one parameter: both values and their link, shown
/// only while the mask type has a Fade page (not Soft).
void KisToolOptionsBrushTest::testFadeGroup()
{
    Editor editor;
    loadPreset(editor, QStringLiteral("b_Basic-5_Size_Opacity.kpp"));
    showParameter(editor, QStringLiteral("BrushTip"), QStringLiteral("Fade"));

    KisToolOptionsBrushSection section(nullptr);
    section.setSettingsWidget(&editor.widget);
    settle();

    QWidget *fade = mirrorWidget<QWidget>(&section, QStringLiteral("Fade"));
    QVERIFY(fade);
    QVERIFY(!fade->isHidden());
    const QList<KisDoubleSliderSpinBox *> sliders = fade->findChildren<KisDoubleSliderSpinBox *>();
    KoAspectButton *link = fade->findChild<KoAspectButton *>();
    QCOMPARE(sliders.size(), 2);
    QVERIFY(link);

    auto *brushTip =
        dynamic_cast<KisBrushTipOptionState *>(editor.widget.optionsModel()->option(QStringLiteral("BrushTip")));
    KisBrushTipOptionData data = brushTip->data();
    data.brush.autoBrush.generator.horizontalFade = 1.0;
    data.brush.autoBrush.generator.verticalFade = 1.0;
    data.brush.autoBrush.generator.type = KisBrushModel::Default;
    brushTip->cursor().set(data);
    settle();

    // linked: the editor's locker keeps the ratio
    link->setKeepAspectRatio(true);
    settle();
    sliders[0]->setValue(0.5);
    settle();
    QCOMPARE(brushTip->data().brush.autoBrush.generator.horizontalFade, 0.5);
    QCOMPARE(brushTip->data().brush.autoBrush.generator.verticalFade, 0.5);
    QCOMPARE(sliders[1]->value(), 0.5);

    // unlinked: one value only
    link->setKeepAspectRatio(false);
    settle();
    sliders[1]->setValue(0.8);
    QCOMPARE(brushTip->data().brush.autoBrush.generator.verticalFade, 0.8);
    QCOMPARE(brushTip->data().brush.autoBrush.generator.horizontalFade, 0.5);

    // the Soft mask type has a curve instead of Fade
    data = brushTip->data();
    data.brush.autoBrush.generator.type = KisBrushModel::Soft;
    brushTip->cursor().set(data);
    settle();
    QVERIFY(fade->isHidden());
    data.brush.autoBrush.generator.type = KisBrushModel::Gaussian;
    brushTip->cursor().set(data);
    settle();
    QVERIFY(!fade->isHidden());
}

/// The option list replaces its active window color with the text color for
/// its checkboxes; its scroll bars keep the window color (they turned white
/// while the Brush Editor had focus).
void KisToolOptionsBrushTest::testOptionListScrollBarColor()
{
    Editor editor;
    QScrollBar *bar = editor.view->verticalScrollBar();
    const QColor window = editor.view->palette().color(QPalette::Inactive, QPalette::Window);
    QCOMPARE(bar->palette().color(QPalette::Active, QPalette::Window), window);
    QVERIFY(bar->palette().color(QPalette::Active, QPalette::Window)
            != editor.view->palette().color(QPalette::Active, QPalette::Text));

    // a theme change (palette change) keeps it
    QPalette palette = editor.view->palette();
    palette.setColor(QPalette::Inactive, QPalette::Window, QColor(10, 20, 30));
    editor.view->setPalette(palette);
    QCOMPARE(bar->palette().color(QPalette::Active, QPalette::Window), QColor(10, 20, 30));
}

/// Locking an option keeps what the options model writes for it (phase 5 of
/// the shared model plan); unlocking removes it again.
void KisToolOptionsBrushTest::testLockWritesModelOption()
{
    Editor editor;
    loadPreset(editor, QStringLiteral("b_Basic-5_Size_Opacity.kpp"));
    KisPaintOpOptionsModel *model = editor.widget.optionsModel();
    QVERIFY(model);
    KisPaintOpOption *size = editor.option(QStringLiteral("Size"));
    QVERIFY(size);
    const QModelIndex row = editor.row(QStringLiteral("Size"));
    QVERIFY(row.isValid());

    KisPropertiesConfigurationSP expected = new KisPropertiesConfiguration();
    model->option(QStringLiteral("Size"))->write(expected.data());
    QVERIFY(!expected->getProperties().isEmpty());

    QVERIFY(QMetaObject::invokeMethod(&editor.widget, "lockProperties", Q_ARG(QModelIndex, row)));
    QVERIFY(size->isLocked());
    // a preset read through the locked properties gets the locked values
    KisPaintOpPresetSP other(
        new KisPaintOpPreset(QDir(QString(FILES_DATA_DIR)).filePath("brushtip/b_Basic-5_Size_Opacity.kpp")));
    QVERIFY(other->load(KisGlobalResourcesInterface::instance()));
    other->updateProxy();
    KisLockedPropertiesProxySP proxy =
        KisLockedPropertiesServer::instance()->createLockedPropertiesProxy(other->settings());
    const QMap<QString, QVariant> values = expected->getProperties();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        QVERIFY2(KisLockedPropertiesServer::instance()->hasProperty(it.key()), qPrintable(it.key()));
        QCOMPARE(proxy->getProperty(it.key()), it.value());
    }

    QVERIFY(QMetaObject::invokeMethod(&editor.widget, "lockProperties", Q_ARG(QModelIndex, row)));
    QVERIFY(!size->isLocked());
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        QVERIFY2(!KisLockedPropertiesServer::instance()->hasProperty(it.key()), qPrintable(it.key()));
    }
}

SOLSTICE_BRUSH_TEST_MAIN(KisToolOptionsBrushTest)

#include "KisToolOptionsBrushTest.moc"
