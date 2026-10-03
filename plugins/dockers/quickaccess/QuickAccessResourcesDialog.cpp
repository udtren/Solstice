/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QuickAccessResourcesDialog.h"

#include <KisMainWindow.h>
#include <KisResourceModel.h>
#include <KisResourceServerProvider.h>
#include <KisViewManager.h>
#include <KoResource.h>
#include <kactioncollection.h>
#include <kis_canvas2.h>
#include <klocalizedstring.h>

#include <QAction>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace
{
enum Column {
    NameColumn,
    ShortcutColumn,
    CustomNameColumn,
    BackgroundColumn,
    TextColorColumn,
    FontSizeColumn,
    IconColumn,
    ResetColumn,
    AddColumn,
    ColumnCount,
};

QString stripMnemonic(QString text)
{
    const QString escapedAmpersand = QStringLiteral("\x1f");
    text.replace(QStringLiteral("&&"), escapedAmpersand);
    text.remove(QLatin1Char('&'));
    text.replace(escapedAmpersand, QStringLiteral("&"));
    return text;
}

QString defaultIconsDirectory()
{
    return QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath())
        .filePath(QStringLiteral("resources/default_icons"));
}

QString iconFilter()
{
    return i18nc("@item:inlistbox", "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.svg *.ico);;All Files (*)");
}
} // namespace

QuickAccessResourcesDialog::QuickAccessResourcesDialog(KisCanvas2 *canvas,
                                                       const QuickAccess::Document &document,
                                                       QWidget *parent)
    : QDialog(parent)
    , m_canvas(canvas)
    , m_document(document)
{
    setWindowTitle(i18nc("@title:window", "Resources"));
    resize(1100, 720);

    auto *layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_actions = new QTableWidget(m_tabs);
    m_dockers = new QTableWidget(m_tabs);
    m_brushes = new QListWidget(m_tabs);

    for (QTableWidget *table : {m_actions, m_dockers}) {
        table->setColumnCount(ColumnCount);
        table->setHorizontalHeaderLabels({i18nc("@title:column", "ID"),
                                          i18nc("@title:column", "Shortcut"),
                                          i18nc("@title:column", "Custom Name"),
                                          i18nc("@title:column", "BG Color"),
                                          i18nc("@title:column", "Font Color"),
                                          i18nc("@title:column", "Size"),
                                          i18nc("@title:column", "Icon"),
                                          i18nc("@title:column", "Reset"),
                                          i18nc("@title:column", "Add")});
        table->verticalHeader()->setVisible(true);
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(CustomNameColumn, QHeaderView::Stretch);
        for (int column = ShortcutColumn; column < ColumnCount; ++column) {
            if (column != CustomNameColumn)
                table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
        }
    }

    m_brushes->setViewMode(QListView::IconMode);
    m_brushes->setIconSize(QSize(64, 64));
    m_brushes->setGridSize(QSize(94, 96));
    m_brushes->setResizeMode(QListView::Adjust);
    m_brushes->setMovement(QListView::Static);
    m_brushes->setWordWrap(false);
    m_brushes->setSelectionMode(QAbstractItemView::ExtendedSelection);

    m_tabs->addTab(m_actions, i18nc("@title:tab", "Actions"));
    m_tabs->addTab(m_dockers, i18nc("@title:tab", "Dockers"));
    m_tabs->addTab(m_brushes, i18nc("@title:tab", "Brushes"));
    layout->addWidget(m_tabs, 1);

    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(i18nc("@info:placeholder", "Filter by name or internal ID…"));
    layout->insertWidget(0, m_search);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto *addBrushes =
        buttons->addButton(i18nc("@action:button", "Add Selected Brushes"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, &QuickAccessResourcesDialog::filterItems);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        filterItems(m_search->text());
    });
    connect(addBrushes, &QPushButton::clicked, this, &QuickAccessResourcesDialog::addSelectedBrushes);
    connect(m_tabs, &QTabWidget::currentChanged, addBrushes, [this, addBrushes] {
        addBrushes->setVisible(m_tabs->currentIndex() == BrushesPage);
    });
    connect(m_brushes, &QListWidget::itemDoubleClicked, this, [this] {
        addSelectedBrushes();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        saveAllAliases();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populateActions();
    populateDockers();
    populateBrushes();
    addBrushes->hide();
}

void QuickAccessResourcesDialog::populateBrushes()
{
    auto *model = KisResourceServerProvider::instance()->paintOpPresetServer()->resourceModel();
    QSet<QString> names;
    for (int row = 0; row < model->rowCount(); ++row) {
        const KoResourceSP resource = model->resourceForIndex(model->index(row, KisAbstractResourceModel::Name));
        if (!resource || names.contains(resource->name()))
            continue;
        names.insert(resource->name());
        auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(resource->image())), resource->name(), m_brushes);
        item->setData(IdentifierRole, resource->name());
        item->setToolTip(resource->name());
    }
    m_brushes->sortItems(Qt::AscendingOrder);
}

void QuickAccessResourcesDialog::populateActions()
{
    if (!m_canvas || !m_canvas->viewManager() || !m_canvas->viewManager()->actionCollection())
        return;
    QList<QAction *> actions = m_canvas->viewManager()->actionCollection()->actions();
    std::sort(actions.begin(), actions.end(), [](QAction *first, QAction *second) {
        return first->objectName().localeAwareCompare(second->objectName()) < 0;
    });
    QSet<QString> ids;
    for (QAction *action : std::as_const(actions)) {
        const QString id = action ? action->objectName() : QString();
        if (id.isEmpty() || ids.contains(id))
            continue;
        ids.insert(id);
        const QString name = stripMnemonic(action->text()).isEmpty() ? id : stripMnemonic(action->text());
        populateTableRow(m_actions, QStringLiteral("actions"), id, name, action->shortcut().toString(), action->icon());
    }
}

void QuickAccessResourcesDialog::populateDockers()
{
    if (!m_canvas || !m_canvas->viewManager() || !m_canvas->viewManager()->mainWindow())
        return;
    QList<QDockWidget *> dockers = m_canvas->viewManager()->mainWindow()->findChildren<QDockWidget *>();
    std::sort(dockers.begin(), dockers.end(), [](QDockWidget *first, QDockWidget *second) {
        return first->windowTitle().localeAwareCompare(second->windowTitle()) < 0;
    });
    QSet<QString> ids;
    for (QDockWidget *docker : std::as_const(dockers)) {
        const QString id = docker ? docker->objectName() : QString();
        if (id.isEmpty() || ids.contains(id))
            continue;
        ids.insert(id);
        const QString name = stripMnemonic(docker->windowTitle()).isEmpty() ? id : stripMnemonic(docker->windowTitle());
        populateTableRow(m_dockers, QStringLiteral("dockers"), id, name, QString(), docker->windowIcon());
    }
}

void QuickAccessResourcesDialog::populateTableRow(QTableWidget *table,
                                                  const QString &category,
                                                  const QString &id,
                                                  const QString &displayName,
                                                  const QString &shortcut,
                                                  const QIcon &nativeIcon)
{
    const int row = table->rowCount();
    table->insertRow(row);
    const QJsonObject alias = aliasFor(category, id);

    auto *name = new QTableWidgetItem(QStringLiteral("%1 (%2)").arg(displayName, id));
    name->setData(IdentifierRole, id);
    name->setFlags(name->flags() & ~Qt::ItemIsEditable);
    name->setIcon(nativeIcon);
    table->setItem(row, NameColumn, name);
    auto *shortcutItem = new QTableWidgetItem(shortcut);
    shortcutItem->setFlags(shortcutItem->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, ShortcutColumn, shortcutItem);
    table->setItem(row, CustomNameColumn, new QTableWidgetItem(alias.value(QStringLiteral("custom_name")).toString()));

    auto *background = new QPushButton(table);
    auto *foreground = new QPushButton(table);
    setColorButton(background, alias.value(QStringLiteral("background_color")).toString());
    setColorButton(foreground, alias.value(QStringLiteral("font_color")).toString());
    connect(background, &QPushButton::clicked, this, [this, background] {
        const QColor initial(background->property("quickAccessColor").toString());
        const QColor color = QColorDialog::getColor(initial.isValid() ? initial : QColor(QStringLiteral("#3a263f")),
                                                    this,
                                                    i18nc("@title:window", "Choose Background Color"));
        if (color.isValid())
            setColorButton(background, color.name());
    });
    connect(foreground, &QPushButton::clicked, this, [this, foreground] {
        const QColor initial(foreground->property("quickAccessColor").toString());
        const QColor color = QColorDialog::getColor(initial.isValid() ? initial : QColor(Qt::white),
                                                    this,
                                                    i18nc("@title:window", "Choose Text Color"));
        if (color.isValid())
            setColorButton(foreground, color.name());
    });
    table->setCellWidget(row, BackgroundColumn, background);
    table->setCellWidget(row, TextColorColumn, foreground);

    auto *fontSize = new QSpinBox(table);
    fontSize->setRange(0, 96);
    fontSize->setSpecialValueText(i18nc("@item:inlistbox", "Default"));
    fontSize->setValue(alias.value(QStringLiteral("font_size")).toString().toInt());
    table->setCellWidget(row, FontSizeColumn, fontSize);

    auto *icon = new QPushButton(i18nc("@action:button", "Icon"), table);
    icon->setProperty("quickAccessIcon", alias.value(QStringLiteral("icon_name")).toString());
    const QString iconPath = resolveIcon(icon->property("quickAccessIcon").toString());
    icon->setIcon(iconPath.isEmpty() ? nativeIcon : QIcon(iconPath));
    connect(icon, &QPushButton::clicked, this, [this, table, row] {
        chooseRowIcon(table, row);
    });
    table->setCellWidget(row, IconColumn, icon);

    auto *reset = new QPushButton(i18nc("@action:button", "Reset"), table);
    connect(reset, &QPushButton::clicked, this, [this, table, row] {
        resetTableRow(table, row);
    });
    table->setCellWidget(row, ResetColumn, reset);

    auto *add = new QPushButton(i18nc("@action:button", "Add"), table);
    connect(add, &QPushButton::clicked, this, [this, table, category, row, id] {
        saveTableRow(table, category, row);
        if (category == QStringLiteral("actions"))
            Q_EMIT actionRequested(id);
        else
            Q_EMIT dockerRequested(id);
    });
    table->setCellWidget(row, AddColumn, add);
}

void QuickAccessResourcesDialog::filterItems(const QString &text)
{
    const QString query = text.trimmed();
    if (m_tabs->currentIndex() == BrushesPage) {
        for (int row = 0; row < m_brushes->count(); ++row) {
            QListWidgetItem *item = m_brushes->item(row);
            item->setHidden(!query.isEmpty() && !item->text().contains(query, Qt::CaseInsensitive));
        }
        return;
    }
    QTableWidget *table = m_tabs->currentIndex() == ActionsPage ? m_actions : m_dockers;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem *item = table->item(row, NameColumn);
        const bool matches = query.isEmpty() || item->text().contains(query, Qt::CaseInsensitive)
            || item->data(IdentifierRole).toString().contains(query, Qt::CaseInsensitive);
        table->setRowHidden(row, !matches);
    }
}

void QuickAccessResourcesDialog::addSelectedBrushes()
{
    if (m_tabs->currentIndex() != BrushesPage)
        return;
    for (QListWidgetItem *item : m_brushes->selectedItems()) {
        const QString name = item->data(IdentifierRole).toString();
        if (!name.isEmpty())
            Q_EMIT brushRequested(name);
    }
}

void QuickAccessResourcesDialog::saveAllAliases()
{
    for (int row = 0; row < m_actions->rowCount(); ++row)
        saveTableRow(m_actions, QStringLiteral("actions"), row);
    for (int row = 0; row < m_dockers->rowCount(); ++row)
        saveTableRow(m_dockers, QStringLiteral("dockers"), row);
}

void QuickAccessResourcesDialog::saveTableRow(QTableWidget *table, const QString &category, int row)
{
    const QString id = table->item(row, NameColumn)->data(IdentifierRole).toString();
    auto *background = qobject_cast<QPushButton *>(table->cellWidget(row, BackgroundColumn));
    auto *foreground = qobject_cast<QPushButton *>(table->cellWidget(row, TextColorColumn));
    auto *fontSize = qobject_cast<QSpinBox *>(table->cellWidget(row, FontSizeColumn));
    auto *icon = qobject_cast<QPushButton *>(table->cellWidget(row, IconColumn));
    const QJsonObject alias{
        {QStringLiteral("custom_name"), table->item(row, CustomNameColumn)->text().trimmed()},
        {QStringLiteral("background_color"), background->property("quickAccessColor").toString()},
        {QStringLiteral("font_color"), foreground->property("quickAccessColor").toString()},
        {QStringLiteral("font_size"), fontSize->value() > 0 ? QString::number(fontSize->value()) : QString()},
        {QStringLiteral("icon_name"), icon->property("quickAccessIcon").toString()}};
    const QJsonObject existing = aliasFor(category, id);
    const bool empty = std::all_of(alias.begin(), alias.end(), [](const QJsonValue &value) {
        return value.toString().isEmpty();
    });
    if (alias == existing || (existing.isEmpty() && empty))
        return;
    QJsonObject aliases = m_document.aliases;
    QJsonObject categoryAliases = aliases.value(category).toObject();
    categoryAliases.insert(id, alias);
    aliases.insert(category, categoryAliases);
    m_document.aliases = aliases;
    Q_EMIT aliasChanged(category, id, alias);
}

void QuickAccessResourcesDialog::resetTableRow(QTableWidget *table, int row)
{
    table->item(row, CustomNameColumn)->setText(QString());
    setColorButton(qobject_cast<QPushButton *>(table->cellWidget(row, BackgroundColumn)), QString());
    setColorButton(qobject_cast<QPushButton *>(table->cellWidget(row, TextColorColumn)), QString());
    qobject_cast<QSpinBox *>(table->cellWidget(row, FontSizeColumn))->setValue(0);
    auto *icon = qobject_cast<QPushButton *>(table->cellWidget(row, IconColumn));
    icon->setProperty("quickAccessIcon", QString());
    icon->setIcon(table->item(row, NameColumn)->icon());
}

void QuickAccessResourcesDialog::chooseRowIcon(QTableWidget *table, int row)
{
    auto *button = qobject_cast<QPushButton *>(table->cellWidget(row, IconColumn));
    const QString current = button->property("quickAccessIcon").toString();
    const QString start = QFileInfo(current).isAbsolute() ? QFileInfo(current).absolutePath() : defaultIconsDirectory();
    const QString selected =
        QFileDialog::getOpenFileName(this, i18nc("@title:window", "Choose Icon"), start, iconFilter());
    if (selected.isEmpty())
        return;
    button->setProperty("quickAccessIcon", QDir::cleanPath(selected));
    button->setIcon(QIcon(selected));
    button->setToolTip(selected);
}

QJsonObject QuickAccessResourcesDialog::aliasFor(const QString &category, const QString &id) const
{
    return m_document.aliases.value(category).toObject().value(id).toObject();
}

QString QuickAccessResourcesDialog::resolveIcon(const QString &iconName) const
{
    if (QFileInfo(iconName).isAbsolute() && QFileInfo::exists(iconName))
        return iconName;
    const QString fileName = QFileInfo(iconName).fileName();
    const QString bundled = QStringLiteral(":/quickaccess/default_icons/%1").arg(fileName);
    return !fileName.isEmpty() && QFileInfo::exists(bundled) ? bundled : QString();
}

void QuickAccessResourcesDialog::setColorButton(QPushButton *button, const QString &color)
{
    const QColor parsed(color);
    if (!parsed.isValid()) {
        button->setProperty("quickAccessColor", QString());
        button->setText(i18nc("@item:inlistbox", "Default"));
        button->setStyleSheet(QString());
        return;
    }
    button->setProperty("quickAccessColor", parsed.name());
    button->setText(parsed.name());
    button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;").arg(parsed.name()));
}
