/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QuickAccessGesture.h"

#include <KisMainWindow.h>
#include <KisPart.h>
#include <KisResourceModel.h>
#include <KisResourceServerProvider.h>
#include <KisViewManager.h>
#include <KoResource.h>
#include <kactioncollection.h>
#include <kconfiggroup.h>
#include <kis_action.h>
#include <kis_action_manager.h>
#include <klocalizedstring.h>
#include <ksharedconfig.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
const QStringList Directions{QStringLiteral("left_up"),
                             QStringLiteral("up"),
                             QStringLiteral("right_up"),
                             QStringLiteral("left"),
                             QStringLiteral("center"),
                             QStringLiteral("right"),
                             QStringLiteral("left_down"),
                             QStringLiteral("down"),
                             QStringLiteral("right_down")};

bool textInputHasFocus()
{
    QWidget *focus = QApplication::focusWidget();
    return qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus) || qobject_cast<QPlainTextEdit *>(focus)
        || qobject_cast<QSpinBox *>(focus) || qobject_cast<QComboBox *>(focus);
}

QString configuredValue(const QJsonObject &config)
{
    const QJsonObject parameters = config.value(QStringLiteral("parameters")).toObject();
    const QString type = config.value(QStringLiteral("gesture_type")).toString();
    if (type == QStringLiteral("action"))
        return parameters.value(QStringLiteral("action_id")).toString();
    if (type == QStringLiteral("brush"))
        return parameters.value(QStringLiteral("brush_name")).toString();
    if (type == QStringLiteral("docker_toggle"))
        return parameters.value(QStringLiteral("docker_name")).toString();
    return QString();
}

QJsonObject aliasFor(const QuickAccess::Document &document, const QString &category, const QString &id)
{
    return document.aliases.value(category).toObject().value(id).toObject();
}

QString resolveIcon(const QString &iconName)
{
    if (QFileInfo(iconName).isAbsolute() && QFileInfo::exists(iconName))
        return iconName;
    const QString fileName = QFileInfo(iconName).fileName();
    if (!fileName.isEmpty()) {
        const QString bundled = QStringLiteral(":/quickaccess/default_icons/%1").arg(fileName);
        if (QFileInfo::exists(bundled))
            return bundled;
    }
    return QString();
}

QIcon gestureIcon(const QuickAccess::Document &document, const QJsonObject &config)
{
    const QJsonObject parameters = config.value(QStringLiteral("parameters")).toObject();
    const QString type = config.value(QStringLiteral("gesture_type")).toString();
    if (type == QStringLiteral("brush")) {
        const QString name = parameters.value(QStringLiteral("brush_name")).toString();
        const auto resources =
            KisResourceServerProvider::instance()->paintOpPresetServer()->resourceModel()->resourcesForName(name);
        if (!resources.isEmpty())
            return QPixmap::fromImage(resources.constFirst()->image());
    }

    QString category;
    QString id;
    if (type == QStringLiteral("action")) {
        category = QStringLiteral("actions");
        id = parameters.value(QStringLiteral("action_id")).toString();
    } else if (type == QStringLiteral("docker_toggle")) {
        category = QStringLiteral("dockers");
        id = parameters.value(QStringLiteral("docker_name")).toString();
    }
    if (id.isEmpty())
        return {};

    const QString iconPath =
        resolveIcon(aliasFor(document, category, id).value(QStringLiteral("icon_name")).toString());
    if (!iconPath.isEmpty())
        return QIcon(iconPath);

    if (type == QStringLiteral("action")) {
        if (KisMainWindow *window = KisPart::instance()->currentMainwindow()) {
            if (KisViewManager *viewManager = window->viewManager()) {
                if (KisAction *action = viewManager->actionManager()->actionByName(id))
                    return action->icon();
            }
        }
    } else {
        for (QWidget *widget : QApplication::allWidgets()) {
            if (auto *docker = qobject_cast<QDockWidget *>(widget); docker && docker->objectName() == id)
                return docker->windowIcon();
        }
    }
    return {};
}
} // namespace

QuickAccessGestureController::QuickAccessGestureController(QuickAccess::Document *document,
                                                           std::function<void(const QuickAccess::Item &)> execute,
                                                           QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_execute(std::move(execute))
{
    reloadSettings();
    qApp->installEventFilter(this);
}

QuickAccessGestureController::~QuickAccessGestureController()
{
    qApp->removeEventFilter(this);
    delete m_preview;
}

void QuickAccessGestureController::reloadSettings()
{
    const KConfigGroup config = KSharedConfig::openConfig()->group(QStringLiteral("QuickAccessGesture"));
    m_enabled = config.readEntry("Enabled", true);
    m_showPreview = config.readEntry("ShowPreview", true);
    m_threshold = qBound(1, config.readEntry("MinimumPixelsToMove", 20), 200);
    if (!m_enabled)
        cancelGesture();
}

void QuickAccessGestureController::cancelGesture()
{
    m_activeKey.clear();
    m_activePage = QJsonObject();
    if (m_preview)
        m_preview->hide();
}

bool QuickAccessGestureController::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)
    if (!m_enabled || !m_document || QApplication::activeModalWidget())
        return false;
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->isAutoRepeat() || textInputHasFocus())
            return false;
        const QString key = eventKey(keyEvent);
        const QJsonObject page = pageForKey(key);
        if (!page.isEmpty()) {
            m_activeKey = key;
            m_activePage = page;
            m_startPosition = QCursor::pos();
            m_lastPosition = m_startPosition;
            if (m_showPreview)
                showPreview(page, m_startPosition);
        }
    } else if (event->type() == QEvent::MouseMove && !m_activeKey.isEmpty()) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        m_lastPosition = mouseEvent->globalPosition().toPoint();
    } else if (event->type() == QEvent::KeyRelease && !m_activeKey.isEmpty()) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->isAutoRepeat() || eventKey(keyEvent) != m_activeKey)
            return false;
        const QPoint delta = m_lastPosition - m_startPosition;
        const qreal distance = std::hypot(qreal(delta.x()), qreal(delta.y()));
        const QString direction = distance < m_threshold ? QStringLiteral("center") : directionForDelta(delta);
        executeConfig(m_activePage.value(direction).toObject());
        cancelGesture();
    }
    return false;
}

QString QuickAccessGestureController::eventKey(QKeyEvent *event) const
{
    QString key = event->text().toUpper();
    if (key.isEmpty() && event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F12)
        key = QStringLiteral("F%1").arg(event->key() - Qt::Key_F1 + 1);
    return key;
}

QJsonObject QuickAccessGestureController::pageForKey(const QString &key) const
{
    if (key.isEmpty())
        return {};
    for (const QJsonValue &value : m_document->gesturePages) {
        const QJsonObject page = value.toObject();
        if (page.value(QStringLiteral("gesture_key")).toString().compare(key, Qt::CaseInsensitive) == 0)
            return page;
    }
    return {};
}

QString QuickAccessGestureController::directionForDelta(const QPoint &delta) const
{
    qreal angle = qRadiansToDegrees(std::atan2(qreal(-delta.y()), qreal(delta.x())));
    if (angle < 0)
        angle += 360;
    if (angle >= 337.5 || angle < 22.5)
        return QStringLiteral("right");
    if (angle < 67.5)
        return QStringLiteral("right_up");
    if (angle < 112.5)
        return QStringLiteral("up");
    if (angle < 157.5)
        return QStringLiteral("left_up");
    if (angle < 202.5)
        return QStringLiteral("left");
    if (angle < 247.5)
        return QStringLiteral("left_down");
    if (angle < 292.5)
        return QStringLiteral("down");
    return QStringLiteral("right_down");
}

void QuickAccessGestureController::showPreview(const QJsonObject &page, const QPoint &position)
{
    if (!m_preview) {
        m_preview = new QFrame(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        m_preview->setAttribute(Qt::WA_ShowWithoutActivating);
        m_preview->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_preview->setStyleSheet(
            QStringLiteral("QFrame { background: rgba(35,35,35,225); border: 1px solid #666; "
                           "border-radius: 5px; } QToolButton { color: white; padding: 3px; }"));
    }
    delete m_preview->layout();
    const auto cells = m_preview->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    qDeleteAll(cells);
    auto *layout = new QGridLayout(m_preview);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(2);
    for (int index = 0; index < Directions.size(); ++index) {
        const QJsonObject config = page.value(Directions.at(index)).toObject();
        const QString value = configuredValue(config);
        const QIcon icon = gestureIcon(*m_document, config);
        auto *cell = new QToolButton(m_preview);
        cell->setFocusPolicy(Qt::NoFocus);
        cell->setAttribute(Qt::WA_TransparentForMouseEvents);
        cell->setMinimumSize(72, 42);
        cell->setMaximumWidth(110);
        cell->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        cell->setToolTip(value);
        if (icon.isNull()) {
            cell->setText(value.isEmpty() ? QStringLiteral("·") : value);
            cell->setToolButtonStyle(Qt::ToolButtonTextOnly);
        } else {
            cell->setIcon(icon);
            cell->setIconSize(QSize(36, 36));
            cell->setToolButtonStyle(Qt::ToolButtonIconOnly);
        }
        layout->addWidget(cell, index / 3, index % 3);
    }
    layout->activate();
    const QSize previewSize = layout->sizeHint();
    const QPoint topLeft = position - QPoint(previewSize.width() / 2, previewSize.height() / 2);
    m_preview->setGeometry(QRect(topLeft, previewSize));
    m_preview->show();
    m_preview->move(topLeft);
    m_preview->raise();
    QTimer::singleShot(0, m_preview, [this, topLeft] {
        if (m_preview && m_preview->isVisible())
            m_preview->move(topLeft);
    });
}

void QuickAccessGestureController::executeConfig(const QJsonObject &config)
{
    if (config.isEmpty())
        return;
    QuickAccess::Item item;
    item.id = QStringLiteral("gesture");
    const QString type = config.value(QStringLiteral("gesture_type")).toString();
    const QJsonObject parameters = config.value(QStringLiteral("parameters")).toObject();
    if (type == QStringLiteral("action")) {
        item.type = QuickAccess::ItemType::Action;
        item.payload.insert(QStringLiteral("action_id"), parameters.value(QStringLiteral("action_id")));
    } else if (type == QStringLiteral("brush")) {
        item.type = QuickAccess::ItemType::Brush;
        item.payload.insert(QStringLiteral("brush_name"), parameters.value(QStringLiteral("brush_name")));
    } else if (type == QStringLiteral("docker_toggle")) {
        item.type = QuickAccess::ItemType::DockerToggle;
        item.payload.insert(QStringLiteral("docker_id"), parameters.value(QStringLiteral("docker_name")));
    } else {
        return;
    }
    m_execute(item);
}

QuickAccessGestureDialog::QuickAccessGestureDialog(const QuickAccess::Document &document, QWidget *parent)
    : QDialog(parent)
    , m_document(&document)
{
    setWindowTitle(i18nc("@title:window", "Gesture Configuration"));
    resize(650, 560);
    for (const QJsonValue &value : document.gesturePages)
        m_pages.append(value.toObject());

    const KConfigGroup config = KSharedConfig::openConfig()->group(QStringLiteral("QuickAccessGesture"));
    auto *root = new QVBoxLayout(this);
    auto *settings = new QFormLayout();
    m_enabled = new QCheckBox(this);
    m_enabled->setChecked(config.readEntry("Enabled", true));
    m_threshold = new QSpinBox(this);
    m_threshold->setRange(1, 200);
    m_threshold->setSuffix(i18nc("@item:valuesuffix", " px"));
    m_threshold->setValue(config.readEntry("MinimumPixelsToMove", 20));
    m_preview = new QCheckBox(this);
    m_preview->setChecked(config.readEntry("ShowPreview", true));
    settings->addRow(i18nc("@label", "Enable gesture system:"), m_enabled);
    settings->addRow(i18nc("@label:spinbox", "Minimum movement:"), m_threshold);
    settings->addRow(i18nc("@label", "Show gesture preview:"), m_preview);
    root->addLayout(settings);

    auto *toolbar = new QHBoxLayout();
    auto *addButton = new QToolButton(this);
    addButton->setText(QStringLiteral("+"));
    addButton->setToolTip(i18nc("@info:tooltip", "Add gesture page"));
    auto *removeButton = new QToolButton(this);
    removeButton->setText(QStringLiteral("−"));
    removeButton->setToolTip(i18nc("@info:tooltip", "Remove current gesture page"));
    toolbar->addStretch();
    toolbar->addWidget(addButton);
    toolbar->addWidget(removeButton);
    root->addLayout(toolbar);
    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs);
    if (m_pages.isEmpty())
        m_pages.append(QJsonObject());
    for (int index = 0; index < m_pages.size(); ++index)
        m_tabs->addTab(createPage(index), QString::number(index + 1));
    connect(addButton, &QToolButton::clicked, this, [this]() {
        addPage();
    });
    connect(removeButton, &QToolButton::clicked, this, [this]() {
        const int index = m_tabs->currentIndex();
        if (index < 0 || m_pages.size() <= 1)
            return;
        m_pages.removeAt(index);
        while (m_tabs->count()) {
            QWidget *page = m_tabs->widget(0);
            m_tabs->removeTab(0);
            page->deleteLater();
        }
        for (int tab = 0; tab < m_pages.size(); ++tab)
            m_tabs->addTab(createPage(tab), QString::number(tab + 1));
        m_tabs->setCurrentIndex(qMin(index, m_tabs->count() - 1));
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QJsonArray QuickAccessGestureDialog::pages() const
{
    QJsonArray result;
    for (const QJsonObject &page : m_pages)
        result.append(page);
    return result;
}

bool QuickAccessGestureDialog::gesturesEnabled() const
{
    return m_enabled->isChecked();
}

bool QuickAccessGestureDialog::showPreview() const
{
    return m_preview->isChecked();
}

int QuickAccessGestureDialog::movementThreshold() const
{
    return m_threshold->value();
}

void QuickAccessGestureDialog::addPage(const QJsonObject &page)
{
    const int index = m_pages.size();
    m_pages.append(page);
    m_tabs->addTab(createPage(index), QString::number(index + 1));
    m_tabs->setCurrentIndex(index);
}

QWidget *QuickAccessGestureDialog::createPage(int pageIndex)
{
    auto *page = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(page);
    auto *key = new QLineEdit(m_pages.at(pageIndex).value(QStringLiteral("gesture_key")).toString(), page);
    key->setMaxLength(12);
    auto *form = new QFormLayout();
    form->addRow(i18nc("@label:textbox", "Gesture key:"), key);
    layout->addLayout(form);
    connect(key, &QLineEdit::textChanged, this, [this, pageIndex](const QString &text) {
        m_pages[pageIndex].insert(QStringLiteral("gesture_key"), text.trimmed().toUpper());
    });

    auto *grid = new QGridLayout();
    grid->setSpacing(10);
    grid->setAlignment(Qt::AlignCenter);
    const QList<QPoint> resourcePositions{QPoint(0, 0),
                                          QPoint(0, 2),
                                          QPoint(0, 4),
                                          QPoint(2, 0),
                                          QPoint(2, 2),
                                          QPoint(2, 4),
                                          QPoint(4, 0),
                                          QPoint(4, 2),
                                          QPoint(4, 4)};
    const QList<QPoint> arrowPositions{QPoint(1, 1),
                                       QPoint(1, 2),
                                       QPoint(1, 3),
                                       QPoint(2, 1),
                                       QPoint(-1, -1),
                                       QPoint(2, 3),
                                       QPoint(3, 1),
                                       QPoint(3, 2),
                                       QPoint(3, 3)};
    for (int index = 0; index < Directions.size(); ++index) {
        const QString direction = Directions.at(index);
        const QJsonObject config = m_pages.at(pageIndex).value(direction).toObject();
        auto *resourceButton = new QPushButton(page);
        resourceButton->setFixedSize(100, 82);
        resourceButton->setToolTip(directionLabel(config));
        updateResourceButton(resourceButton, config);
        connect(resourceButton, &QPushButton::clicked, this, [this, pageIndex, direction, resourceButton]() {
            editDirection(pageIndex, direction, resourceButton);
        });
        const QPoint resourcePosition = resourcePositions.at(index);
        grid->addWidget(resourceButton, resourcePosition.x(), resourcePosition.y(), Qt::AlignCenter);

        const QPoint arrowPosition = arrowPositions.at(index);
        if (arrowPosition.x() >= 0) {
            auto *arrowButton = new QPushButton(page);
            arrowButton->setFixedSize(64, 64);
            arrowButton->setIcon(QIcon(QStringLiteral(":/quickaccess/gesture/%1.png").arg(direction)));
            arrowButton->setIconSize(QSize(48, 48));
            arrowButton->setToolTip(direction);
            connect(arrowButton, &QPushButton::clicked, this, [this, pageIndex, direction, resourceButton]() {
                editDirection(pageIndex, direction, resourceButton);
            });
            grid->addWidget(arrowButton, arrowPosition.x(), arrowPosition.y(), Qt::AlignCenter);
        }
    }
    layout->addLayout(grid);
    layout->addStretch();
    return page;
}

void QuickAccessGestureDialog::editDirection(int pageIndex, const QString &direction, QPushButton *button)
{
    const QJsonObject existing = m_pages.at(pageIndex).value(direction).toObject();
    QDialog dialog(this);
    dialog.setWindowTitle(i18nc("@title:window", "Configure %1 Gesture", direction));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    auto *type = new QComboBox(&dialog);
    type->addItem(i18nc("@item:inlistbox", "Empty"), QString());
    type->addItem(i18nc("@item:inlistbox", "Action"), QStringLiteral("action"));
    type->addItem(i18nc("@item:inlistbox", "Brush"), QStringLiteral("brush"));
    type->addItem(i18nc("@item:inlistbox", "Docker"), QStringLiteral("docker_toggle"));
    const int typeIndex = type->findData(existing.value(QStringLiteral("gesture_type")).toString());
    type->setCurrentIndex(qMax(0, typeIndex));
    auto *value = new QComboBox(&dialog);
    value->setEditable(true);
    value->setInsertPolicy(QComboBox::NoInsert);
    form->addRow(i18nc("@label:listbox", "Type:"), type);
    form->addRow(i18nc("@label:listbox", "Resource:"), value);
    layout->addLayout(form);
    const auto populate = [type, value, existing]() {
        const QString selectedType = type->currentData().toString();
        const QString previous =
            value->currentData().toString().isEmpty() ? configuredValue(existing) : value->currentData().toString();
        value->clear();
        QSet<QString> identifiers;
        if (selectedType == QStringLiteral("action")) {
            if (KisMainWindow *window = KisPart::instance()->currentMainwindow()) {
                QList<QAction *> actions = window->viewManager()->actionCollection()->actions();
                std::sort(actions.begin(), actions.end(), [](QAction *a, QAction *b) {
                    return a->text().localeAwareCompare(b->text()) < 0;
                });
                for (QAction *action : std::as_const(actions)) {
                    const QString id = action->objectName();
                    if (id.isEmpty() || identifiers.contains(id))
                        continue;
                    identifiers.insert(id);
                    QString label = action->text();
                    label.remove(QLatin1Char('&'));
                    value->addItem(label.isEmpty() ? id : label, id);
                }
            }
        } else if (selectedType == QStringLiteral("brush")) {
            auto *model = KisResourceServerProvider::instance()->paintOpPresetServer()->resourceModel();
            for (int row = 0; row < model->rowCount(); ++row) {
                const KoResourceSP resource =
                    model->resourceForIndex(model->index(row, KisAbstractResourceModel::Name));
                if (!resource || identifiers.contains(resource->name()))
                    continue;
                identifiers.insert(resource->name());
                value->addItem(QIcon(QPixmap::fromImage(resource->image())), resource->name(), resource->name());
            }
            value->model()->sort(0);
        } else if (selectedType == QStringLiteral("docker_toggle")) {
            if (KisMainWindow *window = KisPart::instance()->currentMainwindow()) {
                QList<QDockWidget *> dockers = window->findChildren<QDockWidget *>();
                std::sort(dockers.begin(), dockers.end(), [](QDockWidget *a, QDockWidget *b) {
                    return a->windowTitle().localeAwareCompare(b->windowTitle()) < 0;
                });
                for (QDockWidget *docker : std::as_const(dockers)) {
                    const QString id = docker->objectName();
                    if (id.isEmpty() || identifiers.contains(id))
                        continue;
                    identifiers.insert(id);
                    value->addItem(docker->windowTitle().isEmpty() ? id : docker->windowTitle(), id);
                }
            }
        }
        const int index = value->findData(previous);
        if (index >= 0)
            value->setCurrentIndex(index);
        else
            value->setEditText(previous);
    };
    connect(type, &QComboBox::currentIndexChanged, &dialog, populate);
    populate();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString selectedType = type->currentData().toString();
    const QString selectedValue =
        value->currentData().toString().isEmpty() ? value->currentText().trimmed() : value->currentData().toString();
    if (selectedType.isEmpty() || selectedValue.isEmpty()) {
        m_pages[pageIndex].remove(direction);
    } else {
        QString parameterName = QStringLiteral("action_id");
        if (selectedType == QStringLiteral("brush"))
            parameterName = QStringLiteral("brush_name");
        else if (selectedType == QStringLiteral("docker_toggle"))
            parameterName = QStringLiteral("docker_name");
        m_pages[pageIndex].insert(
            direction,
            QJsonObject{{QStringLiteral("gesture_type"), selectedType},
                        {QStringLiteral("parameters"), QJsonObject{{parameterName, selectedValue}}}});
    }
    updateResourceButton(button, m_pages.at(pageIndex).value(direction).toObject());
}

void QuickAccessGestureDialog::updateResourceButton(QPushButton *button, const QJsonObject &config) const
{
    const QIcon icon = gestureIcon(*m_document, config);
    button->setIcon(icon);
    if (icon.isNull()) {
        button->setText(directionLabel(config));
    } else {
        button->setText(QString());
        button->setIconSize(QSize(64, 64));
    }
    button->setToolTip(directionLabel(config));
}

QString QuickAccessGestureDialog::directionLabel(const QJsonObject &config) const
{
    if (config.isEmpty())
        return i18nc("@label", "[Empty]");
    const QString type = config.value(QStringLiteral("gesture_type")).toString();
    const QString value = configuredValue(config);
    if (type == QStringLiteral("action"))
        return i18n("Action:\n%1", value);
    if (type == QStringLiteral("brush"))
        return i18n("Brush:\n%1", value);
    if (type == QStringLiteral("docker_toggle"))
        return i18n("Docker:\n%1", value);
    return value;
}
