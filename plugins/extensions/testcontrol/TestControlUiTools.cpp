/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "TestControlUiTools.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QWidget>

#include <KisMainWindow.h>

#include "TestControlSupport.h"

using namespace TestControlSupport;

namespace
{
constexpr int MaxListedWidgets = 400;

QString plainText(QString text)
{
    return text.remove(QLatin1Char('&'));
}

/// Visible widgets in the visible top-level windows, optionally inside the
/// widget named @p within.
QList<QWidget *> visibleWidgets(const QString &within, QString *error)
{
    QList<QWidget *> roots;
    for (QWidget *topLevel : QApplication::topLevelWidgets()) {
        if (topLevel->isVisible()) {
            roots << topLevel;
        }
    }
    if (!within.isEmpty()) {
        QList<QWidget *> containers;
        for (QWidget *root : roots) {
            if (root->objectName() == within) {
                containers << root;
            }
            for (QWidget *child : root->findChildren<QWidget *>(within)) {
                if (child->isVisible()) {
                    containers << child;
                }
            }
        }
        if (containers.isEmpty()) {
            *error = "No visible widget named " + within;
            return {};
        }
        roots = containers;
    }

    QList<QWidget *> widgets;
    for (QWidget *root : roots) {
        widgets << root;
        for (QWidget *child : root->findChildren<QWidget *>()) {
            if (child->isVisible() && !widgets.contains(child)) {
                widgets << child;
            }
        }
    }
    return widgets;
}

QJsonObject describe(QWidget *widget)
{
    QJsonObject result{
        {"objectName", widget->objectName()},
        {"class", QString::fromLatin1(widget->metaObject()->className())},
        {"enabled", widget->isEnabled()},
    };
    if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
        result.insert("text", plainText(button->text()));
        if (button->isCheckable()) {
            result.insert("checked", button->isChecked());
        }
    } else if (auto *combo = qobject_cast<QComboBox *>(widget)) {
        QJsonArray items;
        for (int i = 0; i < combo->count(); ++i) {
            items.append(combo->itemText(i));
        }
        result.insert("currentIndex", combo->currentIndex());
        result.insert("currentText", combo->currentText());
        result.insert("items", items);
    } else if (auto *spin = qobject_cast<QSpinBox *>(widget)) {
        result.insert("value", spin->value());
    } else if (auto *doubleSpin = qobject_cast<QDoubleSpinBox *>(widget)) {
        result.insert("value", doubleSpin->value());
    } else if (auto *edit = qobject_cast<QLineEdit *>(widget)) {
        result.insert("text", edit->text());
    } else if (auto *label = qobject_cast<QLabel *>(widget)) {
        result.insert("text", label->text());
    }
    if (!widget->toolTip().isEmpty()) {
        result.insert("toolTip", widget->toolTip());
    }
    return result;
}

/// The one visible widget of type T named @p objectName (and with button
/// text @p text, if given), or the @p index-th of several.
template<class T>
T *findOne(const QJsonObject &arguments, QString *error)
{
    const QString objectName = arguments.value("objectName").toString();
    const QString text = arguments.value("text").toString();
    if (objectName.isEmpty() && text.isEmpty()) {
        *error = "Give objectName or text";
        return nullptr;
    }
    const QList<QWidget *> widgets = visibleWidgets(arguments.value("within").toString(), error);
    if (!error->isEmpty()) {
        return nullptr;
    }

    QList<T *> matches;
    for (QWidget *widget : widgets) {
        T *candidate = qobject_cast<T *>(widget);
        if (!candidate || (!objectName.isEmpty() && widget->objectName() != objectName)) {
            continue;
        }
        if (!text.isEmpty()) {
            auto *button = qobject_cast<QAbstractButton *>(widget);
            if (!button || plainText(button->text()) != text) {
                continue;
            }
        }
        matches << candidate;
    }

    const int index = arguments.value("index").toInt(-1);
    if (matches.isEmpty()) {
        *error = QStringLiteral("No visible %1 matches objectName \"%2\"%3")
                     .arg(QString::fromLatin1(T::staticMetaObject.className()),
                          objectName,
                          text.isEmpty() ? QString() : " and text \"" + text + "\"");
        return nullptr;
    }
    if (index >= 0) {
        if (index >= matches.size()) {
            *error = QStringLiteral("index %1 is out of range; %2 widgets match").arg(index).arg(matches.size());
            return nullptr;
        }
        return matches[index];
    }
    if (matches.size() > 1) {
        *error = QStringLiteral("%1 widgets match; give index or within").arg(matches.size());
        return nullptr;
    }
    return matches.first();
}

QDialogButtonBox::StandardButton standardButton(const QString &name)
{
    static const QHash<QString, QDialogButtonBox::StandardButton> buttons{
        {"Ok", QDialogButtonBox::Ok},
        {"Cancel", QDialogButtonBox::Cancel},
        {"Apply", QDialogButtonBox::Apply},
        {"Reset", QDialogButtonBox::Reset},
        {"Close", QDialogButtonBox::Close},
        {"Yes", QDialogButtonBox::Yes},
        {"No", QDialogButtonBox::No},
        {"Save", QDialogButtonBox::Save},
        {"Discard", QDialogButtonBox::Discard},
        {"RestoreDefaults", QDialogButtonBox::RestoreDefaults},
    };
    return buttons.value(name, QDialogButtonBox::NoButton);
}

/// Replies with the widget's state once the event loop has run the slots
/// connected to it.
void replyLater(QObject *context, QWidget *widget, const TestControlMcpServer::ToolReply &reply)
{
    QPointer<QWidget> guard(widget);
    QTimer::singleShot(0, context, [guard, reply]() {
        if (!guard) {
            reply(TestControlMcpServer::ToolResult::success({{"widget", QJsonValue::Null}}));
            return;
        }
        reply(TestControlMcpServer::ToolResult::success({{"widget", describe(guard)}}));
    });
}
} // namespace

TestControlUiTools::TestControlUiTools(QObject *parent)
    : QObject(parent)
{
}

TestControlUiTools::~TestControlUiTools() = default;

void TestControlUiTools::registerTools(TestControlMcpServer *server)
{
    using namespace std::placeholders;

    const QJsonObject withinProperty =
        schemaProperty("string", "Search only inside the visible widget with this object name.");
    const QJsonObject indexProperty = schemaProperty("integer", "Which of several matching widgets (from 0).");

    server->addTool(
        "ui_query",
        "List visible widgets with their object name, class, enabled state and value (button text "
        "and checked state, combo items and current index, spin box value, label text).",
        objectSchema({
            {"nameFilter", schemaProperty("string", "Only object names containing this text.")},
            {"within", withinProperty},
            {"includeUnnamed", schemaProperty("boolean", "Also list widgets without an object name (default false).")},
        }),
        std::bind(&TestControlUiTools::uiQuery, this, _1, _2));

    server->addTool(
        "ui_click",
        "Click a visible, enabled button found by object name and/or its text, or a standard "
        "button (Apply, Reset, Ok, ...) of the QDialogButtonBox named objectName. Returns the "
        "button's state afterwards.",
        objectSchema({
            {"objectName", schemaProperty("string", "Object name of the button or button box.")},
            {"text", schemaProperty("string", "Button text without the & mnemonic.")},
            {"standardButton",
             enumProperty(
                 {"Ok", "Cancel", "Apply", "Reset", "Close", "Yes", "No", "Save", "Discard", "RestoreDefaults"},
                 "Standard button of the QDialogButtonBox named objectName.")},
            {"within", withinProperty},
            {"index", indexProperty},
        }),
        std::bind(&TestControlUiTools::uiClick, this, _1, _2));

    server->addTool("ui_set",
                    "Set a visible, enabled widget found by object name: a combo box (index number or item "
                    "text), a spin box (number), a check box or checkable button (true/false) or a line edit "
                    "(text). Spin boxes and line edits then report editing as finished. Returns the new state.",
                    objectSchema(
                        {
                            {"objectName", schemaProperty("string", "Object name of the widget.")},
                            {"value", QJsonObject{{"description", "New value."}}},
                            {"within", withinProperty},
                            {"index", indexProperty},
                        },
                        {"objectName", "value"}),
                    std::bind(&TestControlUiTools::uiSet, this, _1, _2));

    server->addTool("docker_show",
                    "Show or hide a docker of the current window by its ID, e.g. sharedtooldocker (Tool "
                    "Options). An unknown ID fails and lists the IDs.",
                    objectSchema(
                        {
                            {"id", schemaProperty("string", "Docker ID (its object name).")},
                            {"visible", schemaProperty("boolean", "Show (default true) or hide.")},
                        },
                        {"id"}),
                    std::bind(&TestControlUiTools::dockerShow, this, _1, _2));
}

void TestControlUiTools::dockerShow(const Arguments &arguments, const Reply &reply)
{
    KisMainWindow *window = currentWindow();
    if (!window) {
        reply(TestControlMcpServer::ToolResult::failure("No main window"));
        return;
    }
    const QString id = arguments.value("id").toString();
    QDockWidget *dock = window->dockWidget(id);
    if (!dock) {
        QStringList ids;
        for (QDockWidget *candidate : window->dockWidgets()) {
            ids << candidate->objectName();
        }
        ids.sort();
        reply(TestControlMcpServer::ToolResult::failure("Unknown docker: " + id,
                                                        {{"dockers", QJsonArray::fromStringList(ids)}}));
        return;
    }
    const bool visible = arguments.value("visible").toBool(true);
    dock->setVisible(visible);
    if (visible) {
        dock->raise();
    }
    QPointer<QDockWidget> guard(dock);
    QTimer::singleShot(0, this, [guard, reply, id]() {
        reply(TestControlMcpServer::ToolResult::success({
            {"id", id},
            {"visible", guard && guard->isVisible()},
            {"floating", guard && guard->isFloating()},
        }));
    });
}

void TestControlUiTools::uiQuery(const Arguments &arguments, const Reply &reply)
{
    QString error;
    const QList<QWidget *> widgets = visibleWidgets(arguments.value("within").toString(), &error);
    if (!error.isEmpty()) {
        reply(TestControlMcpServer::ToolResult::failure(error));
        return;
    }
    const QString filter = arguments.value("nameFilter").toString();
    const bool includeUnnamed = arguments.value("includeUnnamed").toBool(false);

    QJsonArray list;
    int matching = 0;
    for (QWidget *widget : widgets) {
        if (widget->objectName().isEmpty() ? !includeUnnamed || !filter.isEmpty()
                                           : !widget->objectName().contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        matching++;
        if (list.size() < MaxListedWidgets) {
            list.append(describe(widget));
        }
    }
    reply(TestControlMcpServer::ToolResult::success({{"widgets", list}, {"matching", matching}}));
}

void TestControlUiTools::uiClick(const Arguments &arguments, const Reply &reply)
{
    QString error;
    QAbstractButton *button = nullptr;
    if (arguments.contains("standardButton")) {
        QDialogButtonBox *box = findOne<QDialogButtonBox>(arguments, &error);
        const QDialogButtonBox::StandardButton which = standardButton(arguments.value("standardButton").toString());
        if (box) {
            button = box->button(which);
            if (!button) {
                error = "The button box has no " + arguments.value("standardButton").toString() + " button";
            }
        }
    } else {
        button = findOne<QAbstractButton>(arguments, &error);
    }
    if (!button) {
        reply(TestControlMcpServer::ToolResult::failure(error));
        return;
    }
    if (!button->isEnabled()) {
        reply(TestControlMcpServer::ToolResult::failure("The button is disabled", {{"widget", describe(button)}}));
        return;
    }

    button->click();
    replyLater(this, button, reply);
}

void TestControlUiTools::uiSet(const Arguments &arguments, const Reply &reply)
{
    QString error;
    QWidget *widget = findOne<QWidget>(arguments, &error);
    if (!widget) {
        reply(TestControlMcpServer::ToolResult::failure(error));
        return;
    }
    if (!widget->isEnabled()) {
        reply(TestControlMcpServer::ToolResult::failure("The widget is disabled", {{"widget", describe(widget)}}));
        return;
    }

    const QJsonValue value = arguments.value("value");
    if (auto *combo = qobject_cast<QComboBox *>(widget)) {
        const int index = value.isDouble() ? value.toInt() : combo->findText(value.toString());
        if (index < 0 || index >= combo->count()) {
            reply(TestControlMcpServer::ToolResult::failure("No such item", {{"widget", describe(combo)}}));
            return;
        }
        combo->setCurrentIndex(index);
    } else if (auto *spin = qobject_cast<QSpinBox *>(widget)) {
        if (!value.isDouble()) {
            reply(TestControlMcpServer::ToolResult::failure("A spin box needs a number"));
            return;
        }
        spin->setValue(value.toInt());
        Q_EMIT spin->editingFinished();
    } else if (auto *doubleSpin = qobject_cast<QDoubleSpinBox *>(widget)) {
        if (!value.isDouble()) {
            reply(TestControlMcpServer::ToolResult::failure("A spin box needs a number"));
            return;
        }
        doubleSpin->setValue(value.toDouble());
        Q_EMIT doubleSpin->editingFinished();
    } else if (auto *button = qobject_cast<QAbstractButton *>(widget); button && button->isCheckable()) {
        if (!value.isBool()) {
            reply(TestControlMcpServer::ToolResult::failure("A checkable button needs true or false"));
            return;
        }
        if (button->isChecked() != value.toBool()) {
            button->click();
        }
    } else if (auto *edit = qobject_cast<QLineEdit *>(widget)) {
        edit->setText(value.toVariant().toString());
        Q_EMIT edit->editingFinished();
    } else {
        reply(TestControlMcpServer::ToolResult::failure(
            QStringLiteral("Cannot set a %1").arg(QString::fromLatin1(widget->metaObject()->className()))));
        return;
    }
    replyLater(this, widget, reply);
}
