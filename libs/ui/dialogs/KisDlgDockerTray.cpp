/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisDlgDockerTray.h"

#include <QCollator>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include <algorithm>

namespace
{
enum ItemRole {
    DockerIdRole = Qt::UserRole,
    ColumnRole
};

QPushButton *makeButton(const QString &text, const QString &objectName, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(objectName);
    return button;
}
} // namespace

struct KisDlgDockerTray::Private {
    KisDockerTrayLayout layout;
    QVector<DockerInfo> dockers;

    QListWidget *available = nullptr;
    QTreeWidget *tray = nullptr;
    QPushButton *add = nullptr;
    QPushButton *remove = nullptr;
    QPushButton *up = nullptr;
    QPushButton *down = nullptr;
    QPushButton *addColumn = nullptr;
    QPushButton *removeColumn = nullptr;
    QSpinBox *width = nullptr;
    QSpinBox *height = nullptr;
};

KisDlgDockerTray::KisDlgDockerTray(const KisDockerTrayLayout &layout,
                                   const QVector<DockerInfo> &dockers,
                                   QWidget *parent)
    : QDialog(parent)
    , m_d(new Private)
{
    m_d->layout = layout;
    m_d->dockers = dockers;
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(m_d->dockers.begin(), m_d->dockers.end(), [&collator](const DockerInfo &a, const DockerInfo &b) {
        return collator.compare(a.title, b.title) < 0;
    });

    setObjectName(QStringLiteral("dockerTrayDialog"));
    setWindowTitle(i18nc("@title:window", "Configure Docker Tray"));

    auto *main = new QVBoxLayout(this);
    auto *lists = new QHBoxLayout();
    main->addLayout(lists, 1);

    // Available dockers
    auto *availableColumn = new QVBoxLayout();
    availableColumn->addWidget(new QLabel(i18nc("@label", "Available dockers:"), this));
    m_d->available = new QListWidget(this);
    m_d->available->setObjectName(QStringLiteral("availableDockers"));
    availableColumn->addWidget(m_d->available, 1);
    lists->addLayout(availableColumn, 1);

    auto *middle = new QVBoxLayout();
    middle->addStretch(1);
    m_d->add = makeButton(i18nc("@action:button", "Add"), QStringLiteral("addButton"), this);
    m_d->remove = makeButton(i18nc("@action:button", "Remove"), QStringLiteral("removeButton"), this);
    middle->addWidget(m_d->add);
    middle->addWidget(m_d->remove);
    middle->addStretch(1);
    lists->addLayout(middle);

    // The tray's columns
    auto *trayColumn = new QVBoxLayout();
    trayColumn->addWidget(new QLabel(i18nc("@label", "Dockers on the tray:"), this));
    m_d->tray = new QTreeWidget(this);
    m_d->tray->setObjectName(QStringLiteral("trayColumns"));
    m_d->tray->setColumnCount(3);
    m_d->tray->setHeaderLabels(
        {i18nc("@title:column", "Docker"), i18nc("@title:column", "Width"), i18nc("@title:column", "Height")});
    m_d->tray->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_d->tray->header()->setStretchLastSection(false);
    m_d->tray->setRootIsDecorated(false);
    m_d->tray->setItemsExpandable(false);
    trayColumn->addWidget(m_d->tray, 1);

    auto *trayButtons = new QHBoxLayout();
    m_d->up = makeButton(i18nc("@action:button", "Move Up"), QStringLiteral("upButton"), this);
    m_d->down = makeButton(i18nc("@action:button", "Move Down"), QStringLiteral("downButton"), this);
    m_d->addColumn = makeButton(i18nc("@action:button", "Add Column"), QStringLiteral("addColumnButton"), this);
    m_d->removeColumn =
        makeButton(i18nc("@action:button", "Remove Column"), QStringLiteral("removeColumnButton"), this);
    trayButtons->addWidget(m_d->up);
    trayButtons->addWidget(m_d->down);
    trayButtons->addWidget(m_d->addColumn);
    trayButtons->addWidget(m_d->removeColumn);
    trayColumn->addLayout(trayButtons);

    auto *sizeForm = new QFormLayout();
    m_d->width = new QSpinBox(this);
    m_d->width->setObjectName(QStringLiteral("widthSpin"));
    m_d->height = new QSpinBox(this);
    m_d->height->setObjectName(QStringLiteral("heightSpin"));
    for (QSpinBox *spin : {m_d->width, m_d->height}) {
        spin->setRange(KisDockerTrayLayout::MinimumExtent, KisDockerTrayLayout::MaximumExtent);
        spin->setSuffix(i18nc("unit: pixels", " px"));
    }
    sizeForm->addRow(i18nc("@label:spinbox", "Width:"), m_d->width);
    sizeForm->addRow(i18nc("@label:spinbox", "Height:"), m_d->height);
    trayColumn->addLayout(sizeForm);
    lists->addLayout(trayColumn, 2);

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    buttons->setObjectName(QStringLiteral("buttonBox"));
    main->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),
            &QPushButton::clicked,
            this,
            &KisDlgDockerTray::slotRestoreDefaults);
    connect(m_d->add, &QPushButton::clicked, this, &KisDlgDockerTray::slotAdd);
    connect(m_d->available, &QListWidget::itemDoubleClicked, this, &KisDlgDockerTray::slotAdd);
    connect(m_d->remove, &QPushButton::clicked, this, &KisDlgDockerTray::slotRemove);
    connect(m_d->up, &QPushButton::clicked, this, &KisDlgDockerTray::slotMoveUp);
    connect(m_d->down, &QPushButton::clicked, this, &KisDlgDockerTray::slotMoveDown);
    connect(m_d->addColumn, &QPushButton::clicked, this, &KisDlgDockerTray::slotAddColumn);
    connect(m_d->removeColumn, &QPushButton::clicked, this, &KisDlgDockerTray::slotRemoveColumn);
    connect(m_d->width, qOverload<int>(&QSpinBox::valueChanged), this, &KisDlgDockerTray::slotSizeChanged);
    connect(m_d->height, qOverload<int>(&QSpinBox::valueChanged), this, &KisDlgDockerTray::slotSizeChanged);
    connect(m_d->available, &QListWidget::itemSelectionChanged, this, &KisDlgDockerTray::slotSelectionChanged);
    connect(m_d->tray, &QTreeWidget::itemSelectionChanged, this, &KisDlgDockerTray::slotSelectionChanged);

    if (m_d->layout.columns.isEmpty()) {
        m_d->layout.addColumn();
    }
    refresh(QString(), 0);
    resize(640, 420);
}

KisDlgDockerTray::~KisDlgDockerTray() = default;

KisDockerTrayLayout KisDlgDockerTray::trayLayout() const
{
    KisDockerTrayLayout layout = m_d->layout;
    layout.columns.erase(std::remove_if(layout.columns.begin(),
                                        layout.columns.end(),
                                        [](const QVector<KisDockerTrayCell> &column) {
                                            return column.isEmpty();
                                        }),
                         layout.columns.end());
    return layout;
}

void KisDlgDockerTray::slotAdd()
{
    QListWidgetItem *item = m_d->available->currentItem();
    if (!item || !item->isSelected()) {
        return;
    }
    const QString id = item->data(DockerIdRole).toString();
    int column = selectedColumn();
    if (column < 0) {
        column = qMax(0, int(m_d->layout.columns.size()) - 1);
    }
    if (m_d->layout.addDocker(id, column)) {
        refresh(id);
    }
}

void KisDlgDockerTray::slotRemove()
{
    const QString id = selectedDocker();
    if (id.isEmpty()) {
        return;
    }
    const int column = m_d->layout.find(id).x();
    m_d->layout.removeDocker(id);
    refresh(QString(), column);
}

void KisDlgDockerTray::slotMoveUp()
{
    move(-1);
}

void KisDlgDockerTray::slotMoveDown()
{
    move(1);
}

void KisDlgDockerTray::move(int delta)
{
    const QString id = selectedDocker();
    if (!id.isEmpty()) {
        if (m_d->layout.moveDocker(id, delta)) {
            refresh(id);
        }
        return;
    }
    const int column = selectedColumn();
    if (column >= 0 && m_d->layout.moveColumn(column, delta)) {
        refresh(QString(), column + delta);
    }
}

void KisDlgDockerTray::slotAddColumn()
{
    m_d->layout.addColumn();
    refresh(QString(), m_d->layout.columns.size() - 1);
}

void KisDlgDockerTray::slotRemoveColumn()
{
    const int column = selectedColumn();
    if (column < 0) {
        return;
    }
    m_d->layout.removeColumn(column);
    if (m_d->layout.columns.isEmpty()) {
        m_d->layout.addColumn();
    }
    refresh(QString(), qMin(column, int(m_d->layout.columns.size()) - 1));
}

void KisDlgDockerTray::slotRestoreDefaults()
{
    m_d->layout = KisDockerTrayLayout::defaultLayout();
    refresh(QString(), 0);
}

void KisDlgDockerTray::slotSizeChanged()
{
    const QString id = selectedDocker();
    if (id.isEmpty()) {
        return;
    }
    m_d->layout.setSize(id, QSize(m_d->width->value(), m_d->height->value()));
    const QPoint position = m_d->layout.find(id);
    const QSize size = m_d->layout.columns[position.x()][position.y()].size;
    QTreeWidgetItem *item = m_d->tray->currentItem();
    if (item) {
        item->setText(1, QString::number(size.width()));
        item->setText(2, QString::number(size.height()));
    }
}

void KisDlgDockerTray::slotSelectionChanged()
{
    const QString docker = selectedDocker();
    const int column = selectedColumn();
    QListWidgetItem *availableItem = m_d->available->currentItem();

    m_d->add->setEnabled(availableItem && availableItem->isSelected());
    m_d->remove->setEnabled(!docker.isEmpty());
    m_d->up->setEnabled(column >= 0);
    m_d->down->setEnabled(column >= 0);
    m_d->removeColumn->setEnabled(column >= 0 && docker.isEmpty());
    m_d->width->setEnabled(!docker.isEmpty());
    m_d->height->setEnabled(!docker.isEmpty());

    if (!docker.isEmpty()) {
        const QPoint position = m_d->layout.find(docker);
        const QSize size = m_d->layout.columns[position.x()][position.y()].size;
        const QSignalBlocker blockWidth(m_d->width);
        const QSignalBlocker blockHeight(m_d->height);
        m_d->width->setValue(size.width());
        m_d->height->setValue(size.height());
    }
}

void KisDlgDockerTray::refresh(const QString &selectDocker, int selectColumn)
{
    const QSignalBlocker blockAvailable(m_d->available);
    const QSignalBlocker blockTray(m_d->tray);

    const QStringList used = m_d->layout.dockerIds();
    m_d->available->clear();
    for (const DockerInfo &docker : m_d->dockers) {
        if (!used.contains(docker.id)) {
            auto *item = new QListWidgetItem(docker.title, m_d->available);
            item->setData(DockerIdRole, docker.id);
        }
    }

    m_d->tray->clear();
    QTreeWidgetItem *selected = nullptr;
    for (int c = 0; c < m_d->layout.columns.size(); ++c) {
        auto *columnItem = new QTreeWidgetItem(m_d->tray, {i18nc("@item", "Column %1", c + 1)});
        columnItem->setData(0, ColumnRole, c);
        QFont font = columnItem->font(0);
        font.setBold(true);
        columnItem->setFont(0, font);
        if (c == selectColumn && selectDocker.isEmpty()) {
            selected = columnItem;
        }
        for (const KisDockerTrayCell &cell : m_d->layout.columns[c]) {
            auto *item = new QTreeWidgetItem(
                columnItem,
                {title(cell.dockerId), QString::number(cell.size.width()), QString::number(cell.size.height())});
            item->setData(0, DockerIdRole, cell.dockerId);
            item->setData(0, ColumnRole, c);
            if (cell.dockerId == selectDocker) {
                selected = item;
            }
        }
    }
    m_d->tray->expandAll();
    if (selected) {
        m_d->tray->setCurrentItem(selected);
    }
    m_d->available->setCurrentRow(-1);

    slotSelectionChanged();
}

QString KisDlgDockerTray::title(const QString &dockerId) const
{
    for (const DockerInfo &docker : m_d->dockers) {
        if (docker.id == dockerId) {
            return docker.title;
        }
    }
    return i18nc("@item %1 is a docker id", "%1 (not available)", dockerId);
}

QString KisDlgDockerTray::selectedDocker() const
{
    QTreeWidgetItem *item = m_d->tray->currentItem();
    return item && item->isSelected() ? item->data(0, DockerIdRole).toString() : QString();
}

int KisDlgDockerTray::selectedColumn() const
{
    QTreeWidgetItem *item = m_d->tray->currentItem();
    return item && item->isSelected() ? item->data(0, ColumnRole).toInt() : -1;
}
