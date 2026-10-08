/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QCheckBox>
#include <QLabel>
#include <QTest>

#include "KisBrushTestMain.h"

#include <KisGlobalResourcesInterface.h>
#include <KisPaintOpOptionsModel.h>
#include <KisSizeOptionData.h>
#include <KisToolOptionsBrushItems.h>
#include <kis_categorized_list_model.h>
#include <kis_config.h>
#include <kis_paintop_option.h>
#include <kis_paintop_options_model.h>
#include <tool/KisToolOptionsBrushSection.h>
#include <widgets/kis_categorized_list_view.h>

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
    for (const QString &id : {QStringLiteral("BrushTip"), QStringLiteral("Opacity"), QStringLiteral("CompositeOp")}) {
        QVERIFY2(!ids.contains(id), qPrintable(id));
    }

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

SOLSTICE_BRUSH_TEST_MAIN(KisToolOptionsBrushTest)

#include "KisToolOptionsBrushTest.moc"
