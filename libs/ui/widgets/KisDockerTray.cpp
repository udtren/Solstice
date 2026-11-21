/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisDockerTray.h"

#include <QCursor>
#include <QDockWidget>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include "dialogs/KisDlgDockerTray.h"
#include "kis_icon_utils.h"

#include <memory>
#include <vector>

namespace
{
constexpr int TrayMargin = 6;
constexpr int TraySpacing = 6;
// Keep the tray this far inside the screen's available geometry.
constexpr int ScreenMargin = 8;

struct Cell {
    QString dockerId;
    QSize wanted;
    QFrame *frame = nullptr;
    QLabel *title = nullptr;
    QVBoxLayout *contentLayout = nullptr;
    QLabel *waiting = nullptr;
    QPointer<QWidget> content;
    QPointer<QWidget> titleArea;
    bool present = false;
};
} // namespace

struct KisDockerTray::Private {
    QPointer<QMainWindow> window;
    KSharedConfigPtr config;
    QPointer<KisDockerBorrowRegistry> registry;
    KisDockerTrayLayout layout;
    bool built = false;

    QFrame *panel = nullptr;
    QWidget *header = nullptr;
    QHBoxLayout *columnsLayout = nullptr;
    QLabel *emptyHint = nullptr;
    QVector<QWidget *> columns;
    std::vector<std::unique_ptr<Cell>> cells;
    QVector<QVector<Cell *>> grid;

    Cell *cell(const QString &dockerId) const
    {
        for (const std::unique_ptr<Cell> &cell : cells) {
            if (cell->dockerId == dockerId) {
                return cell.get();
            }
        }
        return nullptr;
    }
};

KisDockerTray *KisDockerTray::forWindow(QMainWindow *window, KSharedConfigPtr config)
{
    if (!window) {
        return nullptr;
    }
    KisDockerTray *tray = window->findChild<KisDockerTray *>(QString(), Qt::FindDirectChildrenOnly);
    return tray ? tray : new KisDockerTray(window, config);
}

KisDockerTray::KisDockerTray(QMainWindow *window, KSharedConfigPtr config)
    : QWidget(window, Qt::Tool | Qt::FramelessWindowHint)
    , m_d(new Private)
{
    m_d->window = window;
    m_d->config = config;

    setObjectName(QStringLiteral("dockerTray"));
    setAutoFillBackground(true);
    setFocusPolicy(Qt::StrongFocus);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    // The window follows the size of its content.
    outer->setSizeConstraint(QLayout::SetFixedSize);

    m_d->panel = new QFrame(this);
    m_d->panel->setFrameShape(QFrame::StyledPanel);
    m_d->panel->setFrameShadow(QFrame::Raised);
    outer->addWidget(m_d->panel);

    auto *panelLayout = new QVBoxLayout(m_d->panel);
    panelLayout->setContentsMargins(TrayMargin, TrayMargin, TrayMargin, TrayMargin);
    panelLayout->setSpacing(TraySpacing);

    m_d->header = new QWidget(m_d->panel);
    auto *headerLayout = new QHBoxLayout(m_d->header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->addStretch(1);
    auto *configure = new QToolButton(m_d->header);
    configure->setObjectName(QStringLiteral("dockerTrayConfigure"));
    configure->setAutoRaise(true);
    configure->setIcon(KisIconUtils::loadIcon(QStringLiteral("applications-system")));
    configure->setToolTip(i18nc("@info:tooltip", "Configure Docker Tray"));
    connect(configure, &QToolButton::clicked, this, &KisDockerTray::configure);
    headerLayout->addWidget(configure);
    panelLayout->addWidget(m_d->header);

    m_d->columnsLayout = new QHBoxLayout();
    m_d->columnsLayout->setContentsMargins(0, 0, 0, 0);
    m_d->columnsLayout->setSpacing(TraySpacing);
    panelLayout->addLayout(m_d->columnsLayout);

    m_d->emptyHint =
        new QLabel(i18nc("@info", "No dockers are on the Docker Tray. Use the configure button to add some."),
                   m_d->panel);
    m_d->emptyHint->setWordWrap(true);
    m_d->emptyHint->setMinimumWidth(200);
    m_d->emptyHint->hide();
    panelLayout->addWidget(m_d->emptyHint);

    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive && isVisible()) {
            closeTray();
        }
    });
}

KisDockerTray::~KisDockerTray()
{
    // Detach from an input manager that still holds this popup.
    Q_EMIT finished();
    releaseAll();
}

void KisDockerTray::popup(const QPoint &position)
{
    Q_UNUSED(position);
    popupAt(QCursor::pos());
}

void KisDockerTray::popupAt(const QPoint &globalPosition)
{
    if (!m_d->window) {
        Q_EMIT finished();
        return;
    }

    QScreen *screen = QGuiApplication::screenAt(globalPosition);
    if (!screen) {
        screen = m_d->window->screen();
    }
    const QRect available = screen
        ? screen->availableGeometry().adjusted(ScreenMargin, ScreenMargin, -ScreenMargin, -ScreenMargin)
        : QRect();

    if (isVisible()) {
        moveCenteredAt(globalPosition, available);
        return;
    }

    rebuild(KisDockerTrayLayout::load(m_d->config));

    m_d->registry = KisDockerBorrowRegistry::forWindow(m_d->window);
    bool any = false;
    for (const std::unique_ptr<Cell> &cell : m_d->cells) {
        QDockWidget *docker = m_d->registry ? m_d->registry->docker(cell->dockerId) : nullptr;
        cell->present = docker && m_d->registry->request(cell->dockerId, this);
        if (docker) {
            cell->title->setText(docker->windowTitle());
        }
        cell->frame->setVisible(cell->present);
        any |= cell->present;
    }
    for (int c = 0; c < m_d->grid.size(); ++c) {
        bool columnPresent = false;
        for (Cell *cell : m_d->grid[c]) {
            columnPresent |= cell->present;
        }
        m_d->columns[c]->setVisible(columnPresent);
    }

    // An empty tray still opens, so that its configure button can be reached.
    m_d->emptyHint->setVisible(!any);

    applySizes(available);
    layout()->activate();
    adjustSize();
    moveCenteredAt(globalPosition, available);
    show();
    raise();
    activateWindow();
    // Windows may place a new window before the move takes effect.
    moveCenteredAt(globalPosition, available);
}

bool KisDockerTray::onScreen()
{
    return isVisible();
}

void KisDockerTray::dismiss()
{
    hide();
}

void KisDockerTray::configure()
{
    if (!m_d->window) {
        return;
    }
    QVector<KisDlgDockerTray::DockerInfo> dockers;
    for (QDockWidget *docker : m_d->window->findChildren<QDockWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!docker->objectName().isEmpty()) {
            dockers.append({docker->objectName(), docker->windowTitle()});
        }
    }

    KisDlgDockerTray dialog(KisDockerTrayLayout::load(m_d->config), dockers, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    dialog.trayLayout().save(m_d->config);
    reload();
}

void KisDockerTray::reload()
{
    if (!isVisible()) {
        return;
    }
    const QPoint center = geometry().center();
    // hide() returns the dockers without emitting finished(): the input
    // manager keeps the tray registered while it reopens.
    hide();
    popupAt(center);
}

void KisDockerTray::closeTray()
{
    hide();
    Q_EMIT finished();
}

QString KisDockerTray::dockerBorrowerName() const
{
    return i18nc("@title:window", "Docker Tray");
}

void KisDockerTray::dockerContentGranted(const QString &dockerId, QWidget *content, QWidget *titleArea)
{
    Cell *cell = m_d->cell(dockerId);
    if (!cell) {
        return;
    }
    cell->waiting->hide();
    if (titleArea) {
        cell->contentLayout->addWidget(titleArea);
    }
    cell->contentLayout->addWidget(content, 1);
    cell->content = content;
    cell->titleArea = titleArea;
}

void KisDockerTray::dockerContentHeldElsewhere(const QString &dockerId, const QString &holderName)
{
    Cell *cell = m_d->cell(dockerId);
    if (!cell) {
        return;
    }
    if (cell->content) {
        cell->contentLayout->removeWidget(cell->content);
    }
    if (cell->titleArea) {
        cell->contentLayout->removeWidget(cell->titleArea);
    }
    cell->content = nullptr;
    cell->titleArea = nullptr;
    cell->waiting->setText(
        i18nc("%1 is the name of the widget", "Docker is open in %1. Close %1 to show here.", holderName));
    cell->waiting->show();
}

KisDockerTrayLayout KisDockerTray::trayLayout() const
{
    return m_d->layout;
}

void KisDockerTray::hideEvent(QHideEvent *event)
{
    // A spontaneous hide (the main window was minimized) keeps the tray
    // open in Qt's view; it comes back with its content.
    if (!event->spontaneous()) {
        releaseAll();
    }
    QWidget::hideEvent(event);
}

void KisDockerTray::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        closeTray();
        return;
    }
    QWidget::keyPressEvent(event);
}

void KisDockerTray::rebuild(const KisDockerTrayLayout &layout)
{
    if (m_d->built && layout == m_d->layout) {
        return;
    }
    releaseAll();

    m_d->grid.clear();
    m_d->cells.clear();
    qDeleteAll(m_d->columns);
    m_d->columns.clear();

    m_d->layout = layout;
    m_d->built = true;

    for (const QVector<KisDockerTrayCell> &columnCells : layout.columns) {
        auto *column = new QWidget(m_d->panel);
        auto *columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(TraySpacing);
        QVector<Cell *> gridColumn;

        for (const KisDockerTrayCell &trayCell : columnCells) {
            auto cell = std::make_unique<Cell>();
            cell->dockerId = trayCell.dockerId;
            cell->wanted = trayCell.size;
            cell->frame = new QFrame(column);
            cell->frame->setObjectName(QStringLiteral("dockerTrayCell_") + trayCell.dockerId);

            auto *frameLayout = new QVBoxLayout(cell->frame);
            frameLayout->setContentsMargins(0, 0, 0, 0);
            frameLayout->setSpacing(2);

            cell->title = new QLabel(trayCell.dockerId, cell->frame);
            QFont titleFont = cell->title->font();
            titleFont.setBold(true);
            cell->title->setFont(titleFont);
            frameLayout->addWidget(cell->title);

            cell->contentLayout = new QVBoxLayout();
            cell->contentLayout->setContentsMargins(0, 0, 0, 0);
            cell->contentLayout->setSpacing(0);
            frameLayout->addLayout(cell->contentLayout, 1);

            cell->waiting = new QLabel(cell->frame);
            cell->waiting->setWordWrap(true);
            cell->waiting->setAlignment(Qt::AlignCenter);
            cell->waiting->hide();
            cell->contentLayout->addWidget(cell->waiting, 1);

            columnLayout->addWidget(cell->frame);
            gridColumn << cell.get();
            m_d->cells.push_back(std::move(cell));
        }
        columnLayout->addStretch(1);

        m_d->columnsLayout->addWidget(column, 0, Qt::AlignTop);
        m_d->columns << column;
        m_d->grid << gridColumn;
    }
}

void KisDockerTray::applySizes(const QRect &availableGeometry)
{
    QVector<QVector<QSize>> wanted;
    QVector<QVector<QSize>> minimum;
    QVector<QVector<Cell *>> shown;

    for (const QVector<Cell *> &column : m_d->grid) {
        QVector<QSize> columnWanted;
        QVector<QSize> columnMinimum;
        QVector<Cell *> columnShown;
        for (Cell *cell : column) {
            if (!cell->present) {
                continue;
            }
            cell->frame->setMinimumSize(0, 0);
            cell->frame->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            cell->frame->layout()->activate();
            columnWanted << cell->wanted;
            columnMinimum << cell->frame->minimumSizeHint().expandedTo(QSize(0, 0));
            columnShown << cell;
        }
        if (!columnShown.isEmpty()) {
            wanted << columnWanted;
            minimum << columnMinimum;
            shown << columnShown;
        }
    }

    QSize available;
    if (availableGeometry.isValid()) {
        const int frame = 2 * (TrayMargin + m_d->panel->frameWidth());
        const int header = m_d->header->sizeHint().height() + TraySpacing;
        available = (availableGeometry.size() - QSize(frame, frame + header)).expandedTo(QSize(1, 1));
    }

    const QVector<QVector<QSize>> sizes = KisDockerTrayGeometry::fit(wanted, minimum, available, TraySpacing);
    for (int c = 0; c < shown.size(); ++c) {
        for (int r = 0; r < shown[c].size(); ++r) {
            shown[c][r]->frame->setFixedSize(sizes[c][r]);
        }
    }
}

void KisDockerTray::moveCenteredAt(const QPoint &globalPosition, const QRect &availableGeometry)
{
    QRect geometry(QPoint(0, 0), size());
    geometry.moveCenter(globalPosition);
    if (availableGeometry.isValid()) {
        geometry.moveLeft(
            qMax(availableGeometry.left(), qMin(geometry.left(), availableGeometry.right() - geometry.width() + 1)));
        geometry.moveTop(
            qMax(availableGeometry.top(), qMin(geometry.top(), availableGeometry.bottom() - geometry.height() + 1)));
    }
    move(geometry.topLeft());
}

void KisDockerTray::releaseAll()
{
    if (m_d->registry) {
        m_d->registry->releaseAll(this);
    }
    for (const std::unique_ptr<Cell> &cell : m_d->cells) {
        cell->content = nullptr;
        cell->titleArea = nullptr;
        cell->waiting->hide();
    }
}
