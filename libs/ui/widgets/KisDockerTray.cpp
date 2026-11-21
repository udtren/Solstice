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
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include "dialogs/KisDlgDockerTray.h"
#include "kis_icon_utils.h"

#include <functional>
#include <memory>
#include <vector>

namespace
{
constexpr int TrayMargin = 6;
constexpr int TraySpacing = 6;
// Keep the tray this far inside the screen's available geometry.
constexpr int ScreenMargin = 8;

/**
 * A thin strip beside a column (Qt::Horizontal: changes its width) or under
 * a cell (Qt::Vertical: changes its height). Dragging reports the distance
 * from the press; release ends the drag.
 */
class ResizeGrip : public QWidget
{
public:
    ResizeGrip(Qt::Orientation orientation,
               QWidget *parent,
               std::function<void()> begin,
               std::function<void(int)> drag,
               std::function<void()> finish)
        : QWidget(parent)
        , m_orientation(orientation)
        , m_begin(std::move(begin))
        , m_drag(std::move(drag))
        , m_finish(std::move(finish))
    {
        if (orientation == Qt::Horizontal) {
            setFixedWidth(TraySpacing);
            setCursor(Qt::SizeHorCursor);
        } else {
            setFixedHeight(TraySpacing);
            setCursor(Qt::SizeVerCursor);
        }
        setAttribute(Qt::WA_Hover);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        m_start = axis(event->globalPosition().toPoint());
        m_dragging = true;
        m_begin();
        update();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging) {
            m_drag(axis(event->globalPosition().toPoint()) - m_start);
            event->accept();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_dragging && event->button() == Qt::LeftButton) {
            m_dragging = false;
            m_finish();
            update();
            event->accept();
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        if (!m_dragging && !underMouse()) {
            return;
        }
        QPainter painter(this);
        const QColor color = palette().color(QPalette::Highlight);
        if (m_orientation == Qt::Horizontal) {
            painter.fillRect(QRect(width() / 2 - 1, 0, 2, height()), color);
        } else {
            painter.fillRect(QRect(0, height() / 2 - 1, width(), 2), color);
        }
    }

private:
    int axis(const QPoint &point) const
    {
        return m_orientation == Qt::Horizontal ? point.x() : point.y();
    }

    Qt::Orientation m_orientation;
    std::function<void()> m_begin;
    std::function<void(int)> m_drag;
    std::function<void()> m_finish;
    int m_start = 0;
    bool m_dragging = false;
};

struct Cell {
    QString dockerId;
    QSize wanted;
    QFrame *frame = nullptr;
    QWidget *heightGrip = nullptr;
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

    // The drag in progress (a resize grip).
    int dragStart = 0;
    int dragMinimum = 0;

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
    // The width grips are the spacing between columns.
    m_d->columnsLayout->setSpacing(0);
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
        cell->heightGrip->setVisible(cell->present);
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
    activateLayouts();
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

    for (int c = 0; c < layout.columns.size(); ++c) {
        const QVector<KisDockerTrayCell> &columnCells = layout.columns[c];
        // A column: its cells, each with a height grip below, then a width
        // grip on the right. The grips are the spacing between cells.
        auto *columnContainer = new QWidget(m_d->panel);
        auto *containerLayout = new QHBoxLayout(columnContainer);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);
        auto *column = new QWidget(columnContainer);
        auto *columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(0);
        containerLayout->addWidget(column, 0, Qt::AlignTop);
        auto *widthGrip = new ResizeGrip(
            Qt::Horizontal,
            columnContainer,
            [this, c] {
                beginWidthDrag(c);
            },
            [this, c](int delta) {
                dragWidth(c, delta);
            },
            [this, c] {
                finishWidthDrag(c);
            });
        widthGrip->setObjectName(QStringLiteral("dockerTrayWidthGrip_%1").arg(c));
        containerLayout->addWidget(widthGrip);
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
            Cell *rawCell = cell.get();
            cell->heightGrip = new ResizeGrip(
                Qt::Vertical,
                column,
                [this, rawCell] {
                    m_d->dragStart = rawCell->frame->height();
                    // Never below what the layout can store.
                    m_d->dragMinimum =
                        qMax(rawCell->frame->minimumSizeHint().height(), int(KisDockerTrayLayout::MinimumExtent));
                },
                [this, rawCell](int delta) {
                    rawCell->frame->setFixedHeight(
                        qBound(m_d->dragMinimum, m_d->dragStart + delta, int(KisDockerTrayLayout::MaximumExtent)));
                    activateLayouts();
                },
                [this, rawCell] {
                    finishHeightDrag(rawCell->dockerId, rawCell->frame->height());
                });
            cell->heightGrip->setObjectName(QStringLiteral("dockerTrayHeightGrip_") + trayCell.dockerId);
            columnLayout->addWidget(cell->heightGrip);
            gridColumn << rawCell;
            m_d->cells.push_back(std::move(cell));
        }
        columnLayout->addStretch(1);

        m_d->columnsLayout->addWidget(columnContainer, 0, Qt::AlignTop);
        m_d->columns << columnContainer;
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
        // fit() counts a spacing between cells; the grips also follow the
        // last column and the last cell of each column.
        available = (availableGeometry.size() - QSize(frame + TraySpacing, frame + header + TraySpacing))
                        .expandedTo(QSize(1, 1));
    }

    const QVector<QVector<QSize>> sizes = KisDockerTrayGeometry::fit(wanted, minimum, available, TraySpacing);
    for (int c = 0; c < shown.size(); ++c) {
        for (int r = 0; r < shown[c].size(); ++r) {
            shown[c][r]->frame->setFixedSize(sizes[c][r]);
        }
    }
}

void KisDockerTray::beginWidthDrag(int column)
{
    m_d->dragStart = 0;
    // Never below what the layout can store.
    m_d->dragMinimum = KisDockerTrayLayout::MinimumExtent;
    for (Cell *cell : m_d->grid.value(column)) {
        if (cell->present) {
            m_d->dragStart = qMax(m_d->dragStart, cell->frame->width());
            m_d->dragMinimum = qMax(m_d->dragMinimum, cell->frame->minimumSizeHint().width());
        }
    }
}

void KisDockerTray::dragWidth(int column, int delta)
{
    const int width = qBound(m_d->dragMinimum, m_d->dragStart + delta, int(KisDockerTrayLayout::MaximumExtent));
    for (Cell *cell : m_d->grid.value(column)) {
        if (cell->present) {
            cell->frame->setFixedWidth(width);
        }
    }
    activateLayouts();
}

void KisDockerTray::activateLayouts()
{
    // Nested widgets update their layouts through posted events, one level
    // per pass of the event loop; update them now, innermost first, so the
    // window takes its new size at once.
    for (const std::unique_ptr<Cell> &cell : m_d->cells) {
        cell->frame->layout()->activate();
    }
    for (QWidget *container : m_d->columns) {
        for (QWidget *child : container->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (child->layout()) {
                child->layout()->activate();
            }
        }
        container->layout()->activate();
    }
    m_d->panel->layout()->activate();
    layout()->activate();
}

void KisDockerTray::finishWidthDrag(int column)
{
    // Every shown docker of the column takes the new width.
    for (Cell *cell : m_d->grid.value(column)) {
        if (!cell->present) {
            continue;
        }
        const QPoint position = m_d->layout.find(cell->dockerId);
        if (position.x() < 0) {
            continue;
        }
        const QSize stored = m_d->layout.columns[position.x()][position.y()].size;
        m_d->layout.setSize(cell->dockerId, QSize(cell->frame->width(), stored.height()));
        cell->wanted = m_d->layout.columns[position.x()][position.y()].size;
    }
    m_d->layout.save(m_d->config);
    keepOnScreen();
}

void KisDockerTray::finishHeightDrag(const QString &dockerId, int height)
{
    const QPoint position = m_d->layout.find(dockerId);
    if (position.x() >= 0) {
        const QSize stored = m_d->layout.columns[position.x()][position.y()].size;
        m_d->layout.setSize(dockerId, QSize(stored.width(), height));
        if (Cell *cell = m_d->cell(dockerId)) {
            cell->wanted = m_d->layout.columns[position.x()][position.y()].size;
        }
        m_d->layout.save(m_d->config);
    }
    keepOnScreen();
}

void KisDockerTray::keepOnScreen()
{
    activateLayouts();
    QScreen *screen = QGuiApplication::screenAt(geometry().center());
    if (!screen) {
        screen = this->screen();
    }
    if (!screen) {
        return;
    }
    const QRect available =
        screen->availableGeometry().adjusted(ScreenMargin, ScreenMargin, -ScreenMargin, -ScreenMargin);
    QRect rect = geometry();
    rect.moveLeft(qMax(available.left(), qMin(rect.left(), available.right() - rect.width() + 1)));
    rect.moveTop(qMax(available.top(), qMin(rect.top(), available.bottom() - rect.height() + 1)));
    move(rect.topLeft());
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
